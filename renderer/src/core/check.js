// check(source, host) -> { title, blocks, diagnostics, refs }
// Parses a view document, validates every component against its schema, runs the component's semantic checks
// and loads referenced files through `host`. Nothing here touches a DOM: the same code runs in nebula (QJSEngine)
// and in node (tests). The renderer only ever draws the returned, already validated blocks.
import MarkdownIt from 'markdown-it';
import { LineCounter, parseDocument } from 'yaml';
import { byName, components } from '../components/index.js';
import * as validators from './validators.generated.js';
import { splitDocument } from './document.js';
import { offsetOf, pointerSegments } from './locate.js';
import { isRemote } from './paths.js';
import { mathPlugin } from './mdmath.js';
import { closest } from './suggest.js';
import { checkTex } from './tex.js';

export const LIMITS = { sourceBytes: 1024 * 1024, blocks: 200 };

let md;
function init() {
  md ??= new MarkdownIt({ html: false }).use(mathPlugin);
}

function describeType(v) {
  if (v === null) return 'null';
  if (Array.isArray(v)) return 'a list';
  if (typeof v === 'object') return 'a mapping';
  return `${typeof v} ${JSON.stringify(v)}`;
}

// One readable message per ajv error, phrased for a model to act on.
function explain(err, data) {
  const { keyword: k, params: p } = err;
  const allowed = err.parentSchema?.properties ? Object.keys(err.parentSchema.properties) : [];
  switch (k) {
    case 'required':
      return { key: p.missingProperty, message: `missing required field "${p.missingProperty}"` };
    case 'additionalProperties': {
      const guess = closest(p.additionalProperty, allowed);
      return { key: p.additionalProperty, message: `unknown field "${p.additionalProperty}"`, hint: (guess ? `did you mean "${guess}"? ` : '') + `allowed: ${allowed.join(', ')}` };
    }
    case 'enum': {
      const guess = closest(valueAt(data, err.instancePath), p.allowedValues);
      return { message: `must be one of: ${p.allowedValues.join(', ')} (got ${describeType(valueAt(data, err.instancePath))})`, hint: guess ? `did you mean "${guess}"?` : undefined };
    }
    case 'type': {
      const want = [].concat(p.type).map((t) => ({ object: 'a mapping', array: 'a list' })[t] ?? `a ${t}`).join(' or ');
      return { message: `must be ${want}, got ${describeType(valueAt(data, err.instancePath))}` };
    }
    default:
      return { message: err.message };
  }
}

function valueAt(data, pointer) {
  let v = data;
  for (const s of pointerSegments(pointer)) v = v?.[s];
  return v;
}

export function check(source, host) {
  init();
  const diagnostics = [];
  const refs = new Set();
  const out = { title: undefined, blocks: [], diagnostics, refs: [] };
  if (typeof source !== 'string') source = String(source ?? '');
  if (source.length > LIMITS.sourceBytes) {
    diagnostics.push({ severity: 'error', line: 1, message: `document is too large (${source.length} bytes, max ${LIMITS.sourceBytes})`, hint: 'reference big data from a file (e.g. table data:) instead of inlining it' });
    return out;
  }

  const { front, blocks, problems } = splitDocument(source);
  for (const p of problems) diagnostics.push({ severity: p.severity ?? 'error', line: p.line, message: p.message, hint: p.hint });

  if (front) {
    const parsed = parseYaml(front.body, front.line, '(front matter)', diagnostics);
    if (parsed) {
      const data = parsed.value ?? {};
      if (validators.frontMatter(data)) out.title = data.title;
      else for (const e of validators.frontMatter.errors) pushSchemaError(e, data, parsed, diagnostics, { component: '(front matter)' });
    }
  }

  if (blocks.length > LIMITS.blocks) {
    diagnostics.push({ severity: 'error', line: blocks[LIMITS.blocks].line, message: `too many blocks (${blocks.length}, max ${LIMITS.blocks})` });
    blocks.length = LIMITS.blocks;
  }

  const ids = new Map();
  blocks.forEach((b, index) => {
    const before = diagnostics.length;
    const block = { index, type: b.type, line: b.line, endLine: b.endLine };
    if (b.type === 'markdown') {
      block.text = b.text;
      checkMarkdown(b, host, refs, diagnostics, index);
    } else {
      block.component = b.component;
      checkComponent(b, block, host, refs, diagnostics, index, ids);
    }
    const errors = diagnostics.slice(before).filter((d) => d.severity === 'error');
    if (errors.length) {
      block.ok = false;
      block.errors = errors.map((d) => ({ line: d.line, message: d.message, hint: d.hint }));
      delete block.props;
    } else block.ok = true;
    out.blocks.push(block);
  });
  // equation numbers follow document order
  let n = 0;
  for (const b of out.blocks) if (b.ok && b.component === 'math' && b.props.number) b.props.n = ++n;
  out.refs = [...refs];
  diagnostics.sort((a, b) => a.line - b.line);
  return out;
}

function parseYaml(body, firstLine, what, diagnostics) {
  const lineCounter = new LineCounter();
  const doc = parseDocument(body, { lineCounter, uniqueKeys: true, prettyErrors: false });
  const lineOf = (offset) => firstLine + lineCounter.linePos(offset).line - 1;
  if (doc.errors.length) {
    for (const e of doc.errors) {
      diagnostics.push({ severity: 'error', line: lineOf(e.pos[0]), component: what, message: `YAML: ${e.message.split('\n')[0]}`,
        hint: 'quote strings that contain ": " or start with [, {, *, &, ! or #' });
    }
    return null;
  }
  return { doc, value: doc.toJS(), lineOf };
}

function pushSchemaError(err, data, parsed, diagnostics, where) {
  const e = explain(err, data);
  const segments = pointerSegments(err.instancePath).slice(where.strip ?? 0);
  const path = err.instancePath + (e.key !== undefined ? `/${e.key}` : '');
  const d = { severity: 'error', line: parsed.lineOf(offsetOf(parsed.doc, segments, e.key)), component: where.component, path: path || '/', message: e.message, hint: e.hint, block: where.block };
  if (!diagnostics.some((x) => x.line === d.line && x.path === d.path && x.message === d.message)) diagnostics.push(d);
}

function checkComponent(b, block, host, refs, diagnostics, index, ids) {
  const tag = `nebula:${b.component}`;
  const def = byName[b.component];
  if (b.unclosed) {
    diagnostics.push({ severity: 'error', line: b.line, block: index, component: tag, message: `${tag} block is never closed`, hint: `close it with ${b.fence} on its own line` });
    return;
  }
  for (const a of b.attrs) {
    if (/^#[A-Za-z][\w-]*$/.test(a)) {
      const id = a.slice(1);
      if (ids.has(id)) diagnostics.push({ severity: 'error', line: b.line, block: index, component: tag, message: `duplicate block id "#${id}" (also on line ${ids.get(id)})` });
      else { ids.set(id, b.line); block.id = id; }
    } else diagnostics.push({ severity: 'warning', line: b.line, block: index, component: tag, message: `ignored "${a}" after ${tag}`, hint: 'only a block id like #plan may follow the component name' });
  }
  if (!def) {
    const guess = closest(b.component, components.map((c) => c.name));
    diagnostics.push({ severity: 'error', line: b.line, block: index, component: tag, message: `unknown component "${tag}"`,
      hint: (guess ? `did you mean "nebula:${guess}"? ` : '') + `available: ${components.map((c) => c.name).join(', ')}` });
    return;
  }
  const parsed = parseYaml(b.body, b.bodyLine, tag, diagnostics);
  if (!parsed) return;
  let data = parsed.value;
  let strip = 0;
  if (data === null || data === undefined) data = {};
  else if (Array.isArray(data) && def.shorthand?.array) { data = { [def.shorthand.array]: data }; strip = 1; }
  else if (typeof data !== 'object' && def.shorthand?.scalar) { data = { [def.shorthand.scalar]: String(data) }; strip = 1; }
  if (typeof data !== 'object' || Array.isArray(data)) {
    diagnostics.push({ severity: 'error', line: b.bodyLine, block: index, component: tag, message: `body must be a YAML mapping (key: value), got ${describeType(data)}`, hint: `example:\n${def.example}` });
    return;
  }
  // `text: a: b` style mistakes: an unquoted ": " turned the short form into a mapping with an unknown key.
  // Checked before validation, which fills in defaults.
  const known = Object.keys(def.schema.properties);
  if (strip === 0 && def.shorthand?.scalar && Object.keys(data).length && !Object.keys(data).some((k) => known.includes(k))) {
    diagnostics.push({ severity: 'error', line: b.bodyLine, block: index, component: tag,
      message: 'the body was read as a YAML mapping because it contains ": "',
      hint: `write it as ${def.shorthand.scalar}: "…" (quoted), or ${def.shorthand.scalar}: | followed by indented lines` });
    return;
  }
  const validate = validators[def.name];
  if (!validate(data)) {
    for (const err of validate.errors) pushSchemaError(err, data, parsed, diagnostics, { component: tag, block: index, strip });
    return;
  }
  if (!def.resolve) { block.props = data; return; }
  const at = (path) => parsed.lineOf(offsetOf(parsed.doc, pointerSegments(path).slice(strip)));
  const ctx = {
    error(path, message, hint) { diagnostics.push({ severity: 'error', line: at(path), block: index, component: tag, path: path || '/', message, hint }); return undefined; },
    warn(path, message, hint) { diagnostics.push({ severity: 'warning', line: at(path), block: index, component: tag, path: path || '/', message, hint }); },
    readText: (p) => host.readText(p),
    // Markdown in a component field: checks its math (and images) like a Markdown block
    markdown: (path, text) => checkMarkdown({ text, line: at(path) }, host, refs, diagnostics, index, tag),
    stat: (p) => host.stat(p),
    readBase64: (p) => host.readBase64(p),
    ref: (p) => refs.add(p),
  };
  block.props = def.resolve(data, ctx);
}

function checkMarkdown(b, host, refs, diagnostics, index, component) {
  const visit = (tokens, line) => {
    for (const t of tokens) {
      const l = t.map ? b.line + t.map[0] : line;
      if (t.type === 'math_inline' || t.type === 'math_block') {
        const bad = checkTex(t.content, t.type === 'math_block');
        if (bad) diagnostics.push({ severity: 'error', line: l, block: index, component, message: `${bad.message} in ${t.markup}${t.content.length > 40 ? `${t.content.slice(0, 40)}…` : t.content}${t.markup}`,
          hint: 'KaTeX supports most of LaTeX math; a literal dollar sign is written \\$' });
      }
      if (t.type === 'image') {
        const src = t.attrGet('src');
        if (isRemote(src)) diagnostics.push({ severity: 'warning', line: l, block: index, message: `remote image ${src} is not loaded (views have no network access)`, hint: 'download it into the project and use a relative path' });
        else {
          const st = host.stat(decodeURI(src));
          // an image inside prose is not worth dropping the whole block: the page shows a placeholder instead
          if (!st.ok) diagnostics.push({ severity: 'warning', line: l, block: index, message: `image ${src}: ${st.error}` });
          else refs.add(decodeURI(src));
        }
      }
      if (t.children) visit(t.children, l);
    }
  };
  visit(md.parse(b.text, {}), b.line);
}

export function listComponents() {
  return components.map((c) => ({ name: c.name, summary: c.summary }));
}

export function describeComponent(name) {
  const c = byName[name];
  if (!c) return undefined;
  return { name: c.name, summary: c.summary, schema: c.schema, shorthand: c.shorthand, example: `\`\`\`nebula:${c.name}\n${c.example}\n\`\`\`` };
}
