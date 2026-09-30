import { dataProps, loadRecords, MAX_ROWS, numericColumn, requireColumn } from '../core/data.js';
import { FITS } from '../core/fit.js';

const TYPES = ['line', 'bar', 'area', 'scatter', 'pie', 'histogram', 'box', 'heatmap'];
const cols = (v) => [].concat(v ?? []);

export default {
  name: 'chart',
  summary: 'Charts from a data file or inline rows: line, bar, area, scatter, pie, histogram, box, heatmap. x names the category/x column, y the numeric column(s). Extras: error bars (error), confidence band (band), fitted curve with R² (fit), log axes.',
  schema: {
    type: 'object',
    additionalProperties: false,
    required: ['type'],
    properties: {
      type: { enum: TYPES },
      title: { type: 'string' },
      ...dataProps,
      columns: { type: 'array', items: { type: 'string' }, minItems: 1, description: 'names for list rows' },
      x: { type: 'string', description: 'x / category column (pie: labels; box: optional group column; heatmap: columns of the grid)' },
      y: { type: ['string', 'array'], items: { type: 'string' }, minItems: 1, description: 'numeric column(s), one series each (heatmap: rows of the grid)' },
      value: { type: 'string', description: 'heatmap: the numeric column coloured in each cell' },
      error: { type: ['string', 'array'], items: { type: 'string' }, description: '± error column per y column: error bars (line, scatter, bar)' },
      band: {
        type: 'object', additionalProperties: false, required: ['low', 'high'],
        properties: { low: { type: 'string' }, high: { type: 'string' }, label: { type: 'string' } },
        description: 'shaded band between two columns, e.g. a 95% confidence interval',
      },
      fit: { enum: FITS, description: 'least-squares curve through each y series, with its equation and R² (numeric x)' },
      bins: { type: 'integer', minimum: 2, maximum: 200, description: 'histogram bins (default: Freedman–Diaconis)' },
      logx: { type: 'boolean', default: false },
      logy: { type: 'boolean', default: false },
      stack: { type: 'boolean', default: false, description: 'stack the series (bar, area)' },
      horizontal: { type: 'boolean', default: false, description: 'horizontal bars' },
      unit: { type: 'string', description: 'suffix for values, e.g. ms or %' },
      height: { type: 'integer', minimum: 160, maximum: 900, default: 300 },
    },
  },
  example: 'type: scatter\ntitle: Calibration\ndata: calibration.csv\nx: concentration\ny: absorbance\nerror: sd\nfit: linear',
  resolve(props, ctx) {
    const d = loadRecords(props, ctx);
    if (!d) return undefined;
    const t = props.type;
    const src = props.data;
    const where = (field, i) => (Array.isArray(props[field]) ? `/${field}/${i}` : `/${field}`);
    const need = (field) => props[field] === undefined && ctx.error('', `a ${t} chart needs "${field}"`);
    if (t === 'histogram' || t === 'box') { if (need('y')) return undefined; }
    else if (t === 'heatmap') { if (need('x') || need('y') || need('value')) return undefined; }
    else if (need('x') || need('y')) return undefined;
    const ys = cols(props.y);
    if (props.x !== undefined && !requireColumn(props.x, d, ctx, '/x', src)) return undefined;
    for (let i = 0; i < ys.length; i++) if (!requireColumn(ys[i], d, ctx, where('y', i), src)) return undefined;
    if (d.records.length > MAX_ROWS * 4) return ctx.error(src ? '/data' : '/rows', `${d.records.length} rows is too many to chart (max ${MAX_ROWS * 4})`, 'aggregate the data first');
    if (!d.records.length) ctx.warn(src ? '/data' : '/rows', 'no rows: the chart is empty');
    const num = (col, path) => numericColumn(col, d, ctx, path);
    const text = (col) => d.records.map((r) => (r[col] === null || r[col] === undefined ? '' : String(r[col])));
    const base = { type: t, title: props.title, unit: props.unit ?? '', height: props.height, logx: props.logx, logy: props.logy };
    const positive = (vals, path, axis) => (vals.some((v) => v !== null && v <= 0) ? ctx.error(path, `a log ${axis} axis needs positive values`) : true);

    if (t === 'pie') {
      if (ys.length !== 1) return ctx.error('/y', 'a pie chart takes exactly one y column');
      const values = num(ys[0], '/y');
      return values && { ...base, xs: text(props.x), series: [{ name: ys[0], values }] };
    }
    if (t === 'histogram') {
      const series = [];
      for (let i = 0; i < ys.length; i++) { const v = num(ys[i], where('y', i)); if (!v) return undefined; series.push({ name: ys[i], values: v.filter((x) => x !== null) }); }
      return { ...base, series, bins: props.bins };
    }
    if (t === 'box') {
      if (props.x !== undefined) {
        if (ys.length !== 1) return ctx.error('/y', 'with x as the group column, give one y column');
        const v = num(ys[0], '/y');
        if (!v) return undefined;
        const groups = new Map();
        text(props.x).forEach((g, i) => { if (v[i] !== null) (groups.get(g) ?? groups.set(g, []).get(g)).push(v[i]); });
        return { ...base, groups: [...groups].map(([name, values]) => ({ name, values })), yName: ys[0] };
      }
      const groups = [];
      for (let i = 0; i < ys.length; i++) { const v = num(ys[i], where('y', i)); if (!v) return undefined; groups.push({ name: ys[i], values: v.filter((x) => x !== null) }); }
      return { ...base, groups };
    }
    if (t === 'heatmap') {
      if (ys.length !== 1) return ctx.error('/y', 'a heatmap takes one y (row) column');
      if (!requireColumn(props.value, d, ctx, '/value', src)) return undefined;
      const v = num(props.value, '/value');
      if (!v) return undefined;
      const xs = text(props.x), rows = text(ys[0]);
      return { ...base, xs: [...new Set(xs)], ysCat: [...new Set(rows)], cells: v.map((z, i) => [xs[i], rows[i], z]), xName: props.x, yName: ys[0], valueName: props.value };
    }

    // line, bar, area, scatter
    const errs = cols(props.error);
    if (errs.length && errs.length !== ys.length) return ctx.error('/error', `give one error column per y column (${ys.length})`);
    if (errs.length && t === 'area') ctx.warn('/error', 'error bars are not drawn on area charts');
    const numericX = t === 'scatter' || props.fit !== undefined || props.logx || props.band !== undefined;
    let xs = numericX ? num(props.x, '/x') : text(props.x);
    if (!xs) return undefined;
    if (props.logx && positive(xs, '/x', 'x') !== true) return undefined;
    const series = [];
    for (let i = 0; i < ys.length; i++) {
      const values = num(ys[i], where('y', i));
      if (!values) return undefined;
      if (props.logy && positive(values, where('y', i), 'y') !== true) return undefined;
      const s = { name: ys[i], values };
      if (errs[i]) {
        if (!requireColumn(errs[i], d, ctx, where('error', i), src)) return undefined;
        s.errors = num(errs[i], where('error', i));
        if (!s.errors) return undefined;
        if (s.errors.some((e) => e !== null && e < 0)) return ctx.error(where('error', i), `column "${errs[i]}" has negative errors`);
      }
      series.push(s);
    }
    let band;
    if (props.band) {
      for (const k of ['low', 'high']) if (!requireColumn(props.band[k], d, ctx, `/band/${k}`, src)) return undefined;
      const low = num(props.band.low, '/band/low'), high = num(props.band.high, '/band/high');
      if (!low || !high) return undefined;
      band = { low, high, label: props.band.label ?? `${props.band.low} – ${props.band.high}` };
    }
    if ((props.stack || props.horizontal) && !['bar', 'area', 'line'].includes(t)) ctx.warn(props.stack ? '/stack' : '/horizontal', `ignored for ${t} charts`);
    return { ...base, xName: props.x, xs, xNumeric: numericX, series, band, fit: props.fit, stack: props.stack, horizontal: props.horizontal };
  },
};
