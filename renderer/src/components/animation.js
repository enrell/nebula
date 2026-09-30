// Animations over time t: curves y(x, t), moving points (x(t), y(t)) with trails, or frames of a simulation saved as
// .npy — (frames, n) animates a curve, (frames, ny, nx) a colour map. Play/pause, scrub and speed in the page.
import { parse } from '../core/expr.js';
import { base64ToBytes, parseNpy } from '../core/npy.js';
import { PARAMS_SCHEMA, resolveParams } from '../core/params.js';
import { extname } from '../core/paths.js';

const range = (d) => ({ type: 'array', items: { type: 'number' }, minItems: 2, maxItems: 2, description: d });
const MAX_VALUES = 4000000;

export default {
  name: 'animation',
  summary: 'Animate over time t: curves y(x, t) (waves), moving points x(t), y(t) with trails (orbits, pendulums), or simulation frames from a .npy — (frames, n) as a curve, (frames, ny, nx) as a colour map. Play/pause and scrub; params can be sliders.',
  schema: {
    type: 'object',
    additionalProperties: false,
    properties: {
      title: { type: 'string' },
      functions: { type: 'array', minItems: 1, maxItems: 8, items: { type: ['string', 'object'], additionalProperties: false, required: ['y'],
        properties: { y: { type: 'string' }, label: { type: 'string' } } }, description: 'curves y(x, t)' },
      points: { type: 'array', minItems: 1, maxItems: 12, items: { type: 'object', additionalProperties: false, required: ['x', 'y'],
        properties: { x: { type: ['string', 'number'] }, y: { type: ['string', 'number'] }, label: { type: 'string' }, trail: { type: 'boolean', default: true } } },
        description: 'moving points x(t), y(t)' },
      data: { type: 'string', description: 'relative path to a .npy of shape (frames, n) or (frames, ny, nx)' },
      t: range('time range (default [0, 10]; for data: the time of the first and last frame)'),
      duration: { type: 'number', minimum: 1, maximum: 300, default: 8, description: 'seconds to play the whole range' },
      loop: { type: 'boolean', default: true },
      x: range('x range (curves: the domain, default [-10, 10]; points: the view, default automatic)'),
      y: range('y range of the view (default automatic)'),
      params: PARAMS_SCHEMA,
      xlabel: { type: 'string' },
      ylabel: { type: 'string' },
      height: { type: 'integer', minimum: 200, maximum: 1200, default: 380 },
    },
  },
  example: 'title: Standing wave\nt: [0, 6.283]\nx: [0, 10]\nparams:\n  k: {value: 1, min: 0.5, max: 3}\nfunctions:\n  - {y: "sin(k x - t)", label: right}\n  - {y: "sin(k x + t)", label: left}\n  - {y: "sin(k x - t) + sin(k x + t)", label: sum}',
  resolve(props, ctx) {
    const sources = ['functions', 'points', 'data'].filter((k) => props[k] !== undefined);
    if (sources.length === 0) return ctx.error('', 'give "functions" (curves y(x, t)), "points" (x(t), y(t)) or "data" (a .npy of frames)');
    if (props.data !== undefined && sources.length > 1) return ctx.error('/data', 'data animations cannot be mixed with functions or points');
    for (const axis of ['x', 'y', 't']) if (props[axis] && !(props[axis][0] < props[axis][1])) return ctx.error(`/${axis}`, `${axis} range must be [min, max] with min < max`);
    const base = { title: props.title, t: props.t ?? [0, 10], duration: props.duration, loop: props.loop, y: props.y, xlabel: props.xlabel ?? 'x', ylabel: props.ylabel, height: props.height };
    if (props.data !== undefined) {
      if (props.params) return ctx.error('/params', 'params apply to formula animations only');
      if (extname(props.data) !== 'npy') return ctx.error('/data', 'expected a .npy file of shape (frames, n) or (frames, ny, nx)');
      const f = ctx.readBase64(props.data);
      if (!f.ok) return ctx.error('/data', f.error);
      const a = parseNpy(base64ToBytes(f.base64), { dims: [2, 3], headerOnly: true });
      if (a.error) return ctx.error('/data', `${props.data}: ${a.error}`);
      const count = a.shape.reduce((x, y) => x * y, 1);
      if (a.shape[0] < 2) return ctx.error('/data', `${props.data}: only ${a.shape[0]} frame`);
      if (count > MAX_VALUES) return ctx.error('/data', `${props.data}: ${count.toLocaleString()} values is too many (max ${MAX_VALUES.toLocaleString()})`, 'keep fewer frames or subsample the grid');
      ctx.ref(props.data);
      return { ...base, kind: a.shape.length === 2 ? 'curve' : 'map', file: props.data, shape: a.shape, x: props.x ?? (a.shape.length === 2 ? [0, a.shape[1] - 1] : [0, a.shape[2] - 1]),
        y: props.y ?? (a.shape.length === 3 ? [0, a.shape[1] - 1] : undefined), params: {}, sliders: [] };
    }
    const resolved = resolveParams(props.params, ['x', 't'], ctx);
    if (!resolved) return undefined;
    const names = Object.keys(resolved.values);
    const check = (src, vars, path, what) => {
      const r = parse(String(src), [...vars, ...names]);
      if (!r.ok) { ctx.error(path, `${what} = ${src}: ${r.message}${r.col ? ` (column ${r.col})` : ''}`); return false; }
      return true;
    };
    const functions = [];
    for (const [i, item] of (props.functions ?? []).entries()) {
      const f = typeof item === 'string' ? { y: item } : item;
      if (!check(f.y, ['x', 't'], typeof item === 'string' ? `/functions/${i}` : `/functions/${i}/y`, 'y')) return undefined;
      functions.push({ y: f.y, label: f.label ?? f.y });
    }
    const points = [];
    for (const [i, pt] of (props.points ?? []).entries()) {
      for (const k of ['x', 'y']) if (!check(pt[k], ['t'], `/points/${i}/${k}`, k)) return undefined;
      points.push({ x: String(pt.x), y: String(pt.y), label: pt.label ?? `(${pt.x}, ${pt.y})`, trail: pt.trail });
    }
    if (functions.length && points.length && !props.x) return ctx.error('/x', 'give an x range when animating both curves and points');
    return { ...base, kind: 'formula', functions, points, x: props.x ?? (functions.length ? [-10, 10] : undefined), params: resolved.values, sliders: resolved.sliders };
  },
};
