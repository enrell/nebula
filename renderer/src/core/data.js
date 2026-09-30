// Tabular input shared by table, chart and chart3d: either `data:` (a .csv/.tsv/.json file, read through the host)
// or inline `rows:` (objects, or lists named by `columns:`). Produces records keyed by column name.
import { parseDelimited } from './csv.js';
import { extname } from './paths.js';
import { closest } from './suggest.js';

export const MAX_ROWS = 5000;
const NUMERIC = /^[-+]?(\d+\.?\d*|\.\d+)([eE][-+]?\d+)?$/;

export const didYouMean = (word, keys) => {
  const g = closest(word, keys);
  return (g ? `did you mean "${g}"? ` : '') + `available: ${keys.join(', ')}`;
};

export const isNumeric = (v) => typeof v === 'number' || (typeof v === 'string' && NUMERIC.test(v.trim()));

// JSON Schema properties every data-driven component accepts.
export const dataProps = {
  data: { type: 'string', description: 'relative path to a .csv, .tsv or .json (array of objects) file' },
  rows: { type: 'array', items: { type: ['array', 'object'] }, description: 'inline rows: objects, or lists named by columns' },
};

function readFile(path, ctx) {
  const ext = extname(path);
  if (!['csv', 'tsv', 'json'].includes(ext)) return ctx.error('/data', `unsupported data file ".${ext}"`, 'use a .csv, .tsv or .json file');
  const f = ctx.readText(path);
  if (!f.ok) return ctx.error('/data', f.error);
  if (ext === 'json') {
    let v;
    try { v = JSON.parse(f.text); } catch (e) { return ctx.error('/data', `${path} is not valid JSON: ${e.message}`); }
    if (!Array.isArray(v) || v.some((r) => r === null || typeof r !== 'object' || Array.isArray(r)))
      return ctx.error('/data', `${path} must hold an array of objects`);
    return { keys: [...new Set(v.flatMap((r) => Object.keys(r)))], records: v };
  }
  const rows = parseDelimited(f.text, ext === 'tsv' ? '\t' : ',');
  if (!rows.length) return ctx.error('/data', `${path} is empty`);
  const [header, ...body] = rows;
  return { keys: header, records: body.map((r) => Object.fromEntries(header.map((k, i) => [k, r[i] ?? '']))) };
}

// -> { keys, records } or undefined (an error was reported). `columns` names list rows (strings or {key}).
export function loadRecords(props, ctx, columns = props.columns) {
  if ((props.rows === undefined) === (props.data === undefined)) return ctx.error('', 'give exactly one of "rows" or "data"');
  const names = (columns ?? []).map((c) => (typeof c === 'string' ? c : c.key));
  if (props.data !== undefined) return readFile(props.data, ctx);
  if (props.rows.every(Array.isArray)) {
    if (!names.length) return ctx.error('/columns', '"columns" is required when rows are lists');
    const bad = props.rows.findIndex((r) => r.length !== names.length);
    if (bad >= 0) return ctx.error(`/rows/${bad}`, `row has ${props.rows[bad].length} cells but there are ${names.length} columns`);
    return { keys: names, records: props.rows.map((r) => Object.fromEntries(names.map((k, i) => [k, r[i]]))) };
  }
  if (props.rows.some(Array.isArray)) return ctx.error('/rows', 'rows must be all lists or all objects');
  return { keys: [...new Set(props.rows.flatMap((r) => Object.keys(r)))], records: props.rows };
}

// Checks that `key` is a column; `path` is where the column was named in the document.
export function requireColumn(key, d, ctx, path, source) {
  if (d.keys.includes(key)) return true;
  ctx.error(path, `"${key}" is not a column${source ? ` of ${source}` : ''}`, didYouMean(key, d.keys));
  return false;
}

// Numbers of one column; reports the first non-numeric cell.
export function numericColumn(key, d, ctx, path) {
  const out = [];
  for (let i = 0; i < d.records.length; i++) {
    const v = d.records[i][key];
    if (v === '' || v === null || v === undefined) { out.push(null); continue; }
    if (!isNumeric(v)) return ctx.error(path, `column "${key}" must be numeric, but row ${i + 1} has ${JSON.stringify(v)}`);
    out.push(Number(v));
  }
  return out;
}
