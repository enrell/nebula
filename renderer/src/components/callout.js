export default {
  name: 'callout',
  summary: 'Highlighted note: info, success, warning, danger or note. text is Markdown.',
  shorthand: { scalar: 'text' },
  schema: {
    type: 'object',
    additionalProperties: false,
    required: ['text'],
    properties: {
      tone: { enum: ['info', 'success', 'warning', 'danger', 'note'], default: 'info' },
      title: { type: 'string' },
      text: { type: 'string', description: 'Markdown' },
    },
  },
  example: 'tone: warning\ntitle: Migration needed\ntext: Run `just migrate` before deploying.',
};
