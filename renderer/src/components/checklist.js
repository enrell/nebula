// A plan or task list with per-item status. Items may be written compactly as "[x] text".
const MARKS = { 'x': 'done', ' ': 'todo', '~': 'running', '!': 'failed', '-': 'skipped' };

export default {
  name: 'checklist',
  summary: 'Plan / task list with status per item. Items: "[x] done", "[ ] todo", "[~] running", "[!] failed", "[-] skipped", or {text, status, note}.',
  shorthand: { array: 'items' },
  schema: {
    type: 'object',
    additionalProperties: false,
    required: ['items'],
    properties: {
      title: { type: 'string' },
      items: {
        type: 'array',
        minItems: 1,
        items: {
          type: ['string', 'object'],
          additionalProperties: false,
          required: ['text'],
          properties: {
            text: { type: 'string', description: 'Markdown (inline)' },
            status: { enum: ['done', 'todo', 'running', 'failed', 'skipped'], default: 'todo' },
            note: { type: 'string' },
          },
        },
      },
    },
  },
  example: '- "[x] Extract the session store"\n- "[~] Port callers to the new API"\n- text: Remove the old cache\n  status: todo\n  note: after the release, once callers are ported',
  resolve(props, ctx) {
    const items = props.items.map((it) => {
      if (typeof it !== 'string') return it;
      const m = /^\[(.)\]\s+(.*)$/s.exec(it);
      return m && MARKS[m[1]] ? { text: m[2], status: MARKS[m[1]] } : { text: it, status: 'todo' };
    });
    items.forEach((it, i) => ctx.markdown(Array.isArray(props.items) ? `/items/${i}` : '/items', it.text));
    const count = (s) => items.filter((i) => i.status === s).length;
    return { ...props, items, done: count('done'), total: items.length - count('skipped') };
  },
};
