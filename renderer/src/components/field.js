// Vector fields: formulas u(x, y), v(x, y) or a sampled grid from a .npy (ny × nx × 2), drawn as streamlines and/or
// arrows coloured by magnitude over an optional scalar background. Formula fields take slider params.
import { parse } from '../core/expr.js';
import { base64ToBytes, parseNpy } from '../core/npy.js';
import { PARAMS_SCHEMA, resolveParams } from '../core/params.js';
import { extname } from '../core/paths.js';

const range = (d) => ({ type: 'array', items: { type: 'number' }, minItems: 2, maxItems: 2, description: d });
const MAX_CELLS = 250 * 250;

export default {
  name: 'field',
  summary: 'Vector fields (flows, forces, phase portraits): u and v of x, y as formulas, or a .npy grid (ny × nx × 2); streamlines and/or arrows coloured by magnitude, optional scalar background; params can be sliders.',
  schema: {
    type: 'object',
    additionalProperties: false,
    properties: {
      title: { type: 'string' },
      u: { type: ['string', 'number'], description: 'x component, e.g. "y"' },
      v: { type: ['string', 'number'], description: 'y component, e.g. "-sin(x) - b y"' },
      data: { type: 'string', description: 'relative path to a .npy array of shape (ny, nx, 2), rows from y min to y max' },
      x: range('x range (default [-5, 5])'),
      y: range('y range (default [-5, 5])'),
      background: { type: 'string', description: 'scalar formula of x, y drawn as a colour map (potential, density), or "magnitude"' },
      style: { enum: ['streamlines', 'arrows', 'both'], default: 'streamlines' },
      density: { type: 'integer', minimum: 6, maximum: 60, default: 22, description: 'arrows / streamline seeds per axis' },
      params: PARAMS_SCHEMA,
      xlabel: { type: 'string' },
      ylabel: { type: 'string' },
      height: { type: 'integer', minimum: 200, maximum: 1200, default: 440 },
    },
  },
  example: 'title: Damped pendulum\nu: y\nv: "-sin(x) - b y"\nx: [-7, 7]\ny: [-4, 4]\nparams:\n  b: {value: 0.25, min: 0, max: 1.5}\nxlabel: θ\nylabel: ω',
  resolve(props, ctx) {
    for (const axis of ['x', 'y']) if (props[axis] && !(props[axis][0] < props[axis][1])) return ctx.error(`/${axis}`, `${axis} range must be [min, max] with min < max`);
    const base = { title: props.title, x: props.x ?? [-5, 5], y: props.y ?? [-5, 5], style: props.style, density: props.density,
      xlabel: props.xlabel ?? 'x', ylabel: props.ylabel ?? 'y', height: props.height };
    const formulas = props.u !== undefined || props.v !== undefined;
    if (formulas === (props.data !== undefined)) return ctx.error('', 'give either formulas "u" and "v", or "data" (a .npy grid)');
    if (props.data !== undefined) {
      if (props.params) return ctx.error('/params', 'params apply to formula fields only');
      if (props.background && props.background !== 'magnitude') return ctx.error('/background', 'a data field takes only background: magnitude');
      if (extname(props.data) !== 'npy') return ctx.error('/data', 'expected a .npy file of shape (ny, nx, 2)');
      const f = ctx.readBase64(props.data);
      if (!f.ok) return ctx.error('/data', f.error);
      const a = parseNpy(base64ToBytes(f.base64), { dims: [3] });
      if (a.error) return ctx.error('/data', `${props.data}: ${a.error}`);
      const [ny, nx, k] = a.shape;
      if (k !== 2) return ctx.error('/data', `${props.data}: last dimension is ${k}, expected 2 (u, v)`);
      if (nx < 2 || ny < 2 || nx * ny > MAX_CELLS) return ctx.error('/data', `${props.data}: ${ny}×${nx} grid; expected at least 2×2 and at most ${MAX_CELLS.toLocaleString()} cells`, 'subsample the grid');
      return { ...base, grid: { nx, ny, uv: a.data.map((v) => (Number.isFinite(v) ? v : 0)) }, background: props.background, params: {}, sliders: [] };
    }
    if (props.u === undefined || props.v === undefined) return ctx.error('', `missing "${props.u === undefined ? 'u' : 'v'}"`, 'a formula field needs both u and v');
    const resolved = resolveParams(props.params, ['x', 'y'], ctx);
    if (!resolved) return undefined;
    const allowed = ['x', 'y', ...Object.keys(resolved.values)];
    for (const k of ['u', 'v', 'background']) {
      if (props[k] === undefined || (k === 'background' && props[k] === 'magnitude')) continue;
      const r = parse(String(props[k]), allowed);
      if (!r.ok) return ctx.error(`/${k}`, `${k} = ${props[k]}: ${r.message}${r.col ? ` (column ${r.col})` : ''}`);
    }
    return { ...base, u: String(props.u), v: String(props.v), background: props.background, params: resolved.values, sliders: resolved.sliders };
  },
};
