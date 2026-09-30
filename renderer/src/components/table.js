import { dataProps, didYouMean, isNumeric, loadRecords, MAX_ROWS } from '../core/data.js';
import { formatValue } from '../core/numfmt.js';

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
            unit: { type: 'string', description: 'shown in the header' },
            error: { type: 'string', description: 'column holding the ± uncertainty of this one' },
            digits: { type: 'integer', minimum: 1, maximum: 12, description: 'significant figures' },
          },
        },
        description: 'columns to show, in order; default: every field',
      },
      ...dataProps,
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
    const d = loadRecords(props, ctx);
    if (!d) return undefined;
    const cols = props.columns?.length ? props.columns.map((c) => (typeof c === 'string' ? { key: c } : c)) : d.keys.map((key) => ({ key }));
    const missing = cols.filter((c) => !d.keys.includes(c.key));
    if (missing.length)
      return ctx.error('/columns', `column${missing.length > 1 ? 's' : ''} ${missing.map((c) => `"${c.key}"`).join(', ')} not found${props.data ? ` in ${props.data}` : ''}`, didYouMean(missing[0].key, d.keys));
    const records = d.records;
    for (const [i, c] of cols.entries())
      if (c.error !== undefined && !d.keys.includes(c.error)) return ctx.error(`/columns/${i}/error`, `"${c.error}" is not a column`, didYouMean(c.error, d.keys));
    const cell = (v) => (v === null || v === undefined ? '' : typeof v === 'object' ? JSON.stringify(v) : v);
    let rows = records.map((r) => cols.map((c) => {
      const v = cell(r[c.key]);
      if ((c.error === undefined && c.digits === undefined) || v === '' || !isNumeric(v)) return v;
      const e = c.error !== undefined && isNumeric(r[c.error]) ? Number(r[c.error]) : undefined;
      return formatValue(Number(v), { error: e, digits: c.digits });
    }));
    const numeric = cols.map((c, i) => rows.length > 0 && rows.every((r) => r[i] === '' || isNumeric(r[i]) || (c.error !== undefined && / ± /.test(r[i]))));
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
      columns: cols.map((c, i) => ({ key: c.key, label: (c.label ?? c.key) + (c.unit ? ` (${c.unit})` : ''), align: c.align ?? (numeric[i] ? 'right' : 'left'), numeric: numeric[i] })),
      rows,
      sort,
    };
  },
};
