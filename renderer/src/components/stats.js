export default {
  name: 'stats',
  summary: 'A row of key numbers (KPIs) with optional change and tone. A bare list is the items.',
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
        maxItems: 12,
        items: {
          type: 'object',
          additionalProperties: false,
          required: ['label', 'value'],
          properties: {
            label: { type: 'string' },
            value: { type: ['string', 'number'] },
            delta: { type: ['string', 'number'], description: 'change, e.g. +12% or -3' },
            tone: { enum: ['good', 'bad', 'neutral'], default: 'neutral' },
            hint: { type: 'string', description: 'small text under the value' },
          },
        },
      },
    },
  },
  // YAML reads `delta: +18` as the number 18; keep the sign that the author clearly meant to show.
  resolve(props) {
    const signed = (d) => (typeof d === 'number' && d > 0 ? `+${d}` : d === undefined ? undefined : String(d));
    return { ...props, items: props.items.map((it) => ({ ...it, value: String(it.value), delta: signed(it.delta) })) };
  },
  example: '- {label: Tests, value: 142, delta: +18, tone: good}\n- {label: p95 latency, value: 38ms, delta: -41%, tone: good}\n- {label: Open issues, value: 3}',
};
