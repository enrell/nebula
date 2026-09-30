import { extname } from '../core/paths.js';
import { leaves, parseNewick } from '../core/formats/newick.js';

export default {
  name: 'tree',
  summary: 'Phylogenetic or hierarchical tree from Newick (file or inline), with branch lengths, support values and highlighted clades.',
  shorthand: { scalar: 'newick' },
  schema: {
    type: 'object',
    additionalProperties: false,
    properties: {
      title: { type: 'string' },
      data: { type: 'string', description: 'relative path to a Newick file (.nwk .newick .tre .tree)' },
      newick: { type: 'string', description: 'e.g. ((A:0.1,B:0.2):0.3,C:0.4);' },
      lengths: { type: 'boolean', description: 'scale branches by length (default: when lengths are given)' },
      support: { type: 'boolean', default: true, description: 'show support values' },
      highlight: { type: 'array', items: { type: 'string' }, description: 'leaf names to highlight' },
    },
  },
  example: 'newick: "((human:0.1,chimp:0.12)98:0.3,(mouse:0.4,rat:0.38)100:0.2,chicken:0.9);"\nhighlight: [human]',
  resolve(props, ctx) {
    if ((props.data === undefined) === (props.newick === undefined)) return ctx.error('', 'give exactly one of "data" or "newick"');
    let text = props.newick;
    if (props.data !== undefined) {
      if (!['nwk', 'newick', 'tre', 'tree', 'txt'].includes(extname(props.data))) return ctx.error('/data', 'expected a Newick file (.nwk, .newick, .tre)');
      const f = ctx.readText(props.data);
      if (!f.ok) return ctx.error('/data', f.error);
      text = f.text;
    }
    const r = parseNewick(text);
    if (r.error) return ctx.error(props.data ? '/data' : '/newick', `Newick: ${r.error}${props.data ? '' : ` (column ${r.col})`}`);
    const n = leaves(r.tree);
    if (n > 2000) return ctx.error(props.data ? '/data' : '/newick', `${n} leaves is too many to draw (max 2000)`);
    const hasLengths = JSON.stringify(r.tree).includes('"length"');
    const names = new Set();
    (function walk(t) { if (!t.children.length) names.add(t.name); t.children.forEach(walk); })(r.tree);
    // Unquoted Newick labels read "_" as a space, so accept a highlight written either way.
    const highlight = (props.highlight ?? []).map((h) => (names.has(h) ? h : h.replace(/_/g, ' ')));
    const missing = (props.highlight ?? []).filter((h, i) => !names.has(highlight[i]));
    if (missing.length) ctx.warn('/highlight', `not leaves of the tree: ${missing.join(', ')}`);
    return { title: props.title, tree: r.tree, leaves: n, lengths: props.lengths ?? hasLengths, support: props.support, highlight };
  },
};
