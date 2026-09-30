import { dataProps, loadRecords, MAX_ROWS, numericColumn, requireColumn } from '../core/data.js';

const TYPES = ['line', 'bar', 'area', 'scatter', 'pie'];

export default {
  name: 'chart',
  summary: 'Line, bar, area, scatter or pie chart from a data file or inline rows: x names the category/x column, y one or more numeric columns (one series each).',
  schema: {
    type: 'object',
    additionalProperties: false,
    required: ['type', 'x', 'y'],
    properties: {
      type: { enum: TYPES },
      title: { type: 'string' },
      ...dataProps,
      columns: { type: 'array', items: { type: 'string' }, minItems: 1, description: 'names for list rows' },
      x: { type: 'string', description: 'column for the x axis (pie: slice labels)' },
      y: { type: ['string', 'array'], items: { type: 'string' }, minItems: 1, description: 'numeric column(s), one series each' },
      stack: { type: 'boolean', default: false, description: 'stack the series (bar, area)' },
      horizontal: { type: 'boolean', default: false, description: 'horizontal bars' },
      unit: { type: 'string', description: 'suffix for values, e.g. ms or %' },
      height: { type: 'integer', minimum: 160, maximum: 900, default: 300 },
    },
  },
  example: 'type: line\ntitle: Latency per run\ndata: bench.csv\nx: run\ny: [before_ms, after_ms]\nunit: ms',
  resolve(props, ctx) {
    const d = loadRecords(props, ctx);
    if (!d) return undefined;
    const ys = [].concat(props.y);
    if (!requireColumn(props.x, d, ctx, '/x', props.data)) return undefined;
    for (let i = 0; i < ys.length; i++) if (!requireColumn(ys[i], d, ctx, Array.isArray(props.y) ? `/y/${i}` : '/y', props.data)) return undefined;
    if (props.type === 'pie' && ys.length !== 1) return ctx.error('/y', 'a pie chart takes exactly one y column');
    if (d.records.length > MAX_ROWS) return ctx.error(props.data ? '/data' : '/rows', `${d.records.length} rows is too many to chart (max ${MAX_ROWS})`, 'aggregate the data first');
    if (!d.records.length) ctx.warn(props.data ? '/data' : '/rows', 'no rows: the chart is empty');
    const series = [];
    for (let i = 0; i < ys.length; i++) {
      const values = numericColumn(ys[i], d, ctx, Array.isArray(props.y) ? `/y/${i}` : '/y');
      if (!values) return undefined;
      series.push({ name: ys[i], values });
    }
    let xs = d.records.map((r) => r[props.x]);
    const xNumeric = props.type === 'scatter';
    if (xNumeric) { xs = numericColumn(props.x, d, ctx, '/x'); if (!xs) return undefined; }
    else xs = xs.map((v) => (v === null || v === undefined ? '' : String(v)));
    if ((props.stack || props.horizontal) && !['bar', 'area', 'line'].includes(props.type)) ctx.warn(props.stack ? '/stack' : '/horizontal', `ignored for ${props.type} charts`);
    return { type: props.type, title: props.title, xName: props.x, xs, xNumeric, series, stack: props.stack, horizontal: props.horizontal, unit: props.unit ?? '', height: props.height };
  },
};
