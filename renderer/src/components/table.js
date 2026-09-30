import { parseDelimited } from '../core/csv.js';
import { extname } from '../core/paths.js';
import { closest } from '../core/suggest.js';

const MAX_ROWS = 5000;
const didYouMean = (word, keys) => { const g = closest(word, keys); return (g ? `did you mean "${g}"? ` : '') + `available: ${keys.join(', ')}`; };
const NUMERIC = /^[-+]?(\d+\.?\d*|\.\d+)([eE][-+]?\d+)?$/;

function loadData(path, ctx) {
  const ext = extname(path);
  if (!['csv', 'tsv', 'json'].includes(ext)) return ctx.error('/data', `unsupported data file ".${ext}"`, 'use a .csv, .tsv or .json file');
  const f = ctx.readText(path);
  if (!f.ok) return ctx.error('/data', f.error);
  if (ext === 'json') {
    let v;
    try { v = JSON.parse(f.text); } catch (e) { return ctx.error('/data', `${path} is not valid JSON: ${e.message}`); }
    if (!Array.isArray(v) || v.some((r) => r === null || typeof r !== 'object' || Array.isArray(r)))
      return ctx.error('/data', `${path} must hold an array of objects`);
    return { keys: [...new Set(v.flatMap((r) => Object.keys(r)))], rows: v };
  }
  const rows = parseDelimited(f.text, ext === 'tsv' ? '\t' : ',');
  if (!rows.length) return ctx.error('/data', `${path} is empty`);
  const [header, ...body] = rows;
  return { keys: header, rows: body.map((r) => Object.fromEntries(header.map((k, i) => [k, r[i] ?? '']))) };
}

export default {
  name: 'table',
  summary: 'Sortable table from inline rows or a data file (csv, tsv, json). Numbers are right-aligned automatically.',
  schema: {
    type: 'object',
    additionalProperties: false,
    properties: {
      title: { type: 'string' },
      columns: {
        type: 'array',
        minItems: 1,
        items: {
          type: ['string', 'object'],
          additionalProperties: false,
          required: ['key'],
          properties: {
            key: { type: 'string', description: 'field name (object rows / data files) or header (list rows)' },
            label: { type: 'string' },
            align: { enum: ['left', 'right', 'center'] },
          },
        },
        description: 'columns to show, in order; default: every field',
      },
      rows: { type: 'array', items: { type: ['array', 'object'] }, description: 'lists (need columns) or objects' },
      data: { type: 'string', description: 'relative path to a .csv, .tsv or .json (array of objects) file' },
      sort: {
        type: 'object',
        additionalProperties: false,
        required: ['by'],
        properties: { by: { type: 'string' }, desc: { type: 'boolean', default: false } },
      },
      limit: { type: 'integer', minimum: 1, maximum: MAX_ROWS, default: 1000 },
    },
  },
  example: 'columns: [suite, passed, failed, {key: ms, label: time (ms)}]\nrows:\n  - [auth, 42, 0, 812]\n  - [billing, 17, 2, 1290]\nsort: {by: failed, desc: true}',
  resolve(props, ctx) {
    if ((props.rows === undefined) === (props.data === undefined)) return ctx.error('', 'give exactly one of "rows" or "data"');
    let cols = (props.columns ?? []).map((c) => (typeof c === 'string' ? { key: c } : c));
    let records;
    if (props.data !== undefined) {
      const d = loadData(props.data, ctx);
      if (!d) return undefined;
      if (!cols.length) cols = d.keys.map((key) => ({ key }));
      const missing = cols.filter((c) => !d.keys.includes(c.key));
      if (missing.length)
        return ctx.error('/columns', `column${missing.length > 1 ? 's' : ''} ${missing.map((c) => `"${c.key}"`).join(', ')} not found in ${props.data}`, didYouMean(missing[0].key, d.keys));
      records = d.rows;
    } else if (props.rows.every(Array.isArray)) {
      if (!cols.length) return ctx.error('/columns', '"columns" is required when rows are lists');
      const bad = props.rows.findIndex((r) => r.length !== cols.length);
      if (bad >= 0) return ctx.error(`/rows/${bad}`, `row has ${props.rows[bad].length} cells but there are ${cols.length} columns`);
      records = props.rows.map((r) => Object.fromEntries(cols.map((c, i) => [c.key, r[i]])));
    } else if (props.rows.some(Array.isArray)) {
      return ctx.error('/rows', 'rows must be all lists or all objects');
    } else {
      const keys = [...new Set(props.rows.flatMap((r) => Object.keys(r)))];
      if (!cols.length) cols = keys.map((key) => ({ key }));
      const missing = cols.filter((c) => !keys.includes(c.key));
      if (missing.length) return ctx.error('/columns', `column "${missing[0].key}" is not a field of any row`, didYouMean(missing[0].key, keys));
      records = props.rows;
    }
    const cell = (v) => (v === null || v === undefined ? '' : typeof v === 'object' ? JSON.stringify(v) : v);
    let rows = records.map((r) => cols.map((c) => cell(r[c.key])));
    const numeric = cols.map((_, i) => rows.length > 0 && rows.every((r) => r[i] === '' || typeof r[i] === 'number' || NUMERIC.test(String(r[i]).trim())));
    let sort;
    if (props.sort) {
      const i = cols.findIndex((c) => c.key === props.sort.by);
      if (i < 0) return ctx.error('/sort/by', `"${props.sort.by}" is not a column`, didYouMean(props.sort.by, cols.map((c) => c.key)));
      sort = { column: i, desc: props.sort.desc };
    }
    if (rows.length > props.limit) {
      ctx.warn('/limit', `showing ${props.limit} of ${rows.length} rows`);
      rows = rows.slice(0, props.limit);
    }
    return {
      title: props.title,
      columns: cols.map((c, i) => ({ key: c.key, label: c.label ?? c.key, align: c.align ?? (numeric[i] ? 'right' : 'left'), numeric: numeric[i] })),
      rows,
      sort,
    };
  },
};
