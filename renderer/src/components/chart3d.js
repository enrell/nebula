import { dataProps, loadRecords, MAX_ROWS, numericColumn, requireColumn } from '../core/data.js';

export default {
  name: 'chart3d',
  summary: '3D chart drawn with WebGL (scatter, bar, line or surface), rotatable with the mouse: x, y, z name numeric columns; color an optional numeric column.',
  schema: {
    type: 'object',
    additionalProperties: false,
    required: ['type', 'x', 'y', 'z'],
    properties: {
      type: { enum: ['scatter', 'bar', 'line', 'surface'] },
      title: { type: 'string' },
      ...dataProps,
      columns: { type: 'array', items: { type: 'string' }, minItems: 1, description: 'names for list rows' },
      x: { type: 'string' },
      y: { type: 'string' },
      z: { type: 'string' },
      color: { type: 'string', description: 'numeric column mapped to a colour scale (default: z)' },
      height: { type: 'integer', minimum: 200, maximum: 1200, default: 440 },
      rotate: { type: 'boolean', default: false, description: 'turn slowly until the user drags it' },
    },
  },
  example: 'type: surface\ndata: grid.csv\nx: lr\ny: batch\nz: loss',
  resolve(props, ctx) {
    const d = loadRecords(props, ctx);
    if (!d) return undefined;
    const axes = ['x', 'y', 'z'].concat(props.color ? ['color'] : []);
    const cols = {};
    for (const a of axes) {
      if (!requireColumn(props[a], d, ctx, `/${a}`, props.data)) return undefined;
      cols[a] = numericColumn(props[a], d, ctx, `/${a}`);
      if (!cols[a]) return undefined;
    }
    if (d.records.length > MAX_ROWS * 4) return ctx.error(props.data ? '/data' : '/rows', `${d.records.length} points is too many (max ${MAX_ROWS * 4})`);
    const points = d.records.map((_, i) => [cols.x[i], cols.y[i], cols.z[i], (cols.color ?? cols.z)[i]]).filter((p) => p.every((v) => v !== null));
    if (points.length < d.records.length) ctx.warn('', `${d.records.length - points.length} rows with empty cells were left out`);
    if (props.type === 'surface' && props.color && props.color !== props.z) ctx.warn('/color', 'a surface is coloured by z; color is ignored');
    if (props.type === 'surface') {
      // a surface needs a full grid: every x paired with every y exactly once
      const xs = new Set(points.map((p) => p[0])), ys = new Set(points.map((p) => p[1]));
      const cells = new Set(points.map((p) => `${p[0]}|${p[1]}`));
      if (cells.size !== points.length) return ctx.error('/type', 'a surface needs one z per (x, y) pair, but some pairs repeat');
      if (xs.size * ys.size !== points.length)
        return ctx.error('/type', `a surface needs a full grid: ${xs.size} x values × ${ys.size} y values = ${xs.size * ys.size} points, got ${points.length}`, 'use type: scatter for irregular points');
      points.sort((a, b) => a[1] - b[1] || a[0] - b[0]);
    }
    const range = (i) => points.reduce(([lo, hi], p) => [Math.min(lo, p[i]), Math.max(hi, p[i])], [Infinity, -Infinity]);
    return { type: props.type, title: props.title, names: { x: props.x, y: props.y, z: props.z, color: props.color ?? props.z }, points, colorRange: points.length ? range(3) : [0, 1], height: props.height, rotate: props.rotate };
  },
};
