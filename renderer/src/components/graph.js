// Networks: nodes and edges from inline lists, a CSV/TSV edge list or a JSON {nodes, edges} file; drawn with a force
// or circular layout, arrows when directed, edge width by weight, node colour by group and size by degree or value.
import { parseDelimited } from '../core/csv.js';
import { extname } from '../core/paths.js';
import { closest } from '../core/suggest.js';

const MAX_NODES = 3000, MAX_EDGES = 12000;
const id = { type: ['string', 'number'] };

export default {
  name: 'graph',
  summary: 'Networks and graphs: edges (inline [a, b, weight] or a CSV edge list source,target[,weight]) and optional nodes with label, group and value; force or circular layout, directed arrows, colour by group, size by degree or value.',
  shorthand: { array: 'edges' },
  schema: {
    type: 'object',
    additionalProperties: false,
    properties: {
      title: { type: 'string' },
      data: { type: 'string', description: 'relative path to a .csv/.tsv edge list (source, target[, weight]) or a .json {nodes, edges}' },
      nodes: { type: 'array', items: { type: ['string', 'number', 'object'], additionalProperties: false, required: ['id'],
        properties: { id, label: { type: 'string' }, group: { type: ['string', 'number'] }, value: { type: 'number' } } } },
      edges: { type: 'array', items: { type: ['array', 'object'], minItems: 2, maxItems: 3, items: { type: ['string', 'number'] }, additionalProperties: false, required: ['source', 'target'],
        properties: { source: id, target: id, weight: { type: 'number' }, label: { type: 'string' } } }, description: '[a, b], [a, b, weight] or {source, target, weight, label}' },
      directed: { type: 'boolean', default: false },
      layout: { enum: ['force', 'circular'], default: 'force' },
      size: { enum: ['degree', 'value', 'uniform'], default: 'degree', description: 'node size' },
      labels: { enum: ['auto', 'all', 'none'], default: 'auto', description: 'auto: labels when there are at most 60 nodes, else on hover' },
      height: { type: 'integer', minimum: 200, maximum: 1400, default: 460 },
    },
  },
  example: 'directed: true\nnodes:\n  - {id: gene A, group: regulator}\n  - {id: gene B, group: target}\n  - {id: gene C, group: target}\nedges:\n  - [gene A, gene B, 2]\n  - [gene A, gene C]\n  - [gene B, gene C, 0.5]',
  resolve(props, ctx) {
    let nodes = props.nodes, edges = props.edges;
    const at = (k, i) => (props.data !== undefined ? '/data' : `/${k}/${i}`);
    if (props.data !== undefined) {
      if (edges || nodes) return ctx.error('/data', 'give either "data" or inline nodes/edges, not both');
      const ext = extname(props.data);
      const f = ctx.readText(props.data);
      if (!f.ok) return ctx.error('/data', f.error);
      if (ext === 'json') {
        let doc;
        try { doc = JSON.parse(f.text); } catch (e) { return ctx.error('/data', `${props.data}: invalid JSON (${e.message})`); }
        if (!doc || !Array.isArray(doc.edges)) return ctx.error('/data', `${props.data}: expected {"nodes": [...], "edges": [...]}`);
        nodes = doc.nodes;
        edges = doc.edges;
        const bad = edges.findIndex((e) => !(Array.isArray(e) ? e.length >= 2 : e && e.source !== undefined && e.target !== undefined));
        if (bad >= 0) return ctx.error('/data', `${props.data}: edge ${bad + 1} needs a source and a target`);
      } else if (ext === 'csv' || ext === 'tsv') {
        const rows = parseDelimited(f.text, ext === 'tsv' ? '\t' : ',').filter((r) => r.some((c) => c.trim()));
        if (!rows.length) return ctx.error('/data', `${props.data} is empty`);
        const head = rows[0].map((c) => c.trim().toLowerCase());
        const hasHeader = head.includes('source') || head.includes('target') || head.includes('from');
        const col = (names, dflt) => { const i = head.findIndex((c) => names.includes(c)); return hasHeader ? i : dflt; };
        const [si, ti, wi, li] = [col(['source', 'from'], 0), col(['target', 'to'], 1), col(['weight', 'value', 'w'], 2), col(['label'], -1)];
        if (si < 0 || ti < 0) return ctx.error('/data', `${props.data}: needs source and target columns`);
        edges = [];
        for (const [n, r] of rows.slice(hasHeader ? 1 : 0).entries()) {
          if (r.length < 2) return ctx.error('/data', `${props.data}: row ${n + (hasHeader ? 2 : 1)} has fewer than 2 columns`);
          const w = wi >= 0 && r[wi] !== undefined && r[wi].trim() !== '' ? Number(r[wi]) : undefined;
          if (w !== undefined && !Number.isFinite(w)) return ctx.error('/data', `${props.data}: row ${n + (hasHeader ? 2 : 1)}: weight "${r[wi]}" is not a number`);
          edges.push({ source: r[si].trim(), target: r[ti].trim(), weight: w, label: li >= 0 ? r[li] : undefined });
        }
      } else return ctx.error('/data', `unsupported graph file ".${ext}"`, 'use a .csv/.tsv edge list or a .json {nodes, edges}');
    }
    if (!edges && !nodes) return ctx.error('', 'give "edges" (and optionally "nodes"), or "data"');
    edges ??= [];
    const out = new Map();
    for (const [i, n] of (nodes ?? []).entries()) {
      const node = typeof n === 'object' ? n : { id: n };
      const key = String(node.id);
      if (out.has(key)) return ctx.error(at('nodes', i), `duplicate node "${key}"`);
      out.set(key, { id: key, label: node.label ?? key, group: node.group !== undefined ? String(node.group) : undefined, value: node.value, degree: 0 });
    }
    const declared = out.size > 0;
    const list = [];
    for (const [i, e] of edges.entries()) {
      const [s, t, w, label] = Array.isArray(e) ? e : [e.source, e.target, e.weight, e.label];
      if (w !== undefined && typeof w !== 'number') return ctx.error(at('edges', i), `weight ${JSON.stringify(w)} is not a number`);
      for (const end of [s, t]) {
        const key = String(end);
        if (!out.has(key)) {
          if (declared) {
            const guess = closest(key, [...out.keys()]);
            return ctx.error(at('edges', i), `node "${key}" is not in nodes`, guess ? `did you mean "${guess}"?` : 'add it to nodes, or leave nodes out to take them from the edges');
          }
          out.set(key, { id: key, label: key, degree: 0 });
        }
      }
      out.get(String(s)).degree++;
      out.get(String(t)).degree++;
      list.push({ s: String(s), t: String(t), w, label });
    }
    if (out.size > MAX_NODES) return ctx.error(props.data ? '/data' : '/nodes', `${out.size} nodes is too many to draw (max ${MAX_NODES})`);
    if (list.length > MAX_EDGES) return ctx.error(props.data ? '/data' : '/edges', `${list.length} edges is too many to draw (max ${MAX_EDGES})`);
    if (props.size === 'value' && ![...out.values()].some((n) => n.value !== undefined)) ctx.warn('/size', 'no node has a value; sizes are uniform');
    const all = [...out.values()];
    return { title: props.title, nodes: all, edges: list, groups: [...new Set(all.map((n) => n.group).filter((g) => g !== undefined))],
      directed: props.directed, layout: props.layout, size: props.size, labels: props.labels === 'auto' ? (all.length <= 60 ? 'all' : 'none') : props.labels, height: props.height };
  },
};
