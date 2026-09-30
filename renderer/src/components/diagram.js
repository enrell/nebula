// Mermaid diagrams. The checker validates the diagram type (the full grammar lives in mermaid, which runs in the
// page; syntax errors come back as render issues with mermaid's own line numbers). A plain ```mermaid fence in the
// Markdown is the same component with the fence body as its source.
import { closest } from '../core/suggest.js';

// the first word of a diagram, without -beta / -v2 / -elk suffixes (mermaid 12)
const TYPES = ['flowchart', 'graph', 'sequenceDiagram', 'classDiagram', 'stateDiagram', 'erDiagram', 'journey', 'gantt', 'pie', 'quadrantChart',
  'requirementDiagram', 'requirement', 'gitGraph', 'C4Context', 'C4Container', 'C4Component', 'C4Dynamic', 'C4Deployment', 'mindmap', 'timeline',
  'sankey', 'xychart', 'block', 'packet', 'kanban', 'architecture', 'radar', 'treemap', 'venn', 'ishikawa', 'cynefin', 'eventmodeling', 'railroad',
  'railroad-abnf', 'railroad-ebnf', 'railroad-peg', 'swimlane', 'treeView', 'usecase', 'wardley', 'agentflow'];

// the line (0-based, within source) and first word of the diagram, skipping front matter, %% comments and directives
export function diagramType(source) {
  const lines = source.replace(/\r/g, '').split('\n');
  let i = 0;
  if (lines[0]?.trim() === '---') { const end = lines.indexOf('---', 1); i = end < 0 ? lines.length : end + 1; }
  for (; i < lines.length; i++) {
    const l = lines[i].trim();
    if (!l || l.startsWith('%%')) continue;
    return { line: i, word: l.split(/[\s:;{]/)[0] };
  }
  return null;
}

export default {
  name: 'diagram',
  summary: 'Mermaid diagrams: flowchart, sequence, class, state, ER, gantt, pie, mindmap, timeline, gitGraph, quadrant, xychart, sankey, block, architecture, kanban… A ```mermaid fence in Markdown is drawn the same way.',
  shorthand: { scalar: 'source' },
  schema: {
    type: 'object',
    additionalProperties: false,
    required: ['source'],
    properties: {
      title: { type: 'string' },
      source: { type: 'string', description: 'the mermaid text, e.g. "flowchart LR\\n  A --> B"' },
      caption: { type: 'string', description: 'Markdown (inline) under the diagram' },
    },
  },
  example: 'source: |\n  flowchart LR\n    data[(raw data)] --> clean[clean] --> fit{fit ok?}\n    fit -- yes --> report\n    fit -- no --> clean',
  resolve(props, ctx) {
    const found = diagramType(props.source);
    if (!found) return ctx.error('/source', 'the diagram is empty');
    const base = found.word.replace(/-(beta|v2|elk)$/, '');
    if (!TYPES.includes(base)) {
      const guess = closest(base, TYPES);
      return ctx.error('/source', `"${found.word}" is not a mermaid diagram type`, (guess ? `did you mean "${guess}"? ` : '') + 'start with e.g. flowchart TD, sequenceDiagram, classDiagram, gantt, pie, mindmap');
    }
    if (props.source.length > 100000) return ctx.error('/source', 'diagram source is too long (max 100,000 characters)');
    if (props.caption) ctx.markdown('/caption', props.caption);
    return { title: props.title, source: props.source, caption: props.caption, type: base };
  },
};
