// Plots of formulas: y = f(x), parametric (x(t), y(t)), polar r(t) and surfaces z = f(x, y).
// Expressions use the safe language in core/expr.js; the checker validates them, the page samples them (so a later
// slider can change a parameter without asking the agent again).
import { constantNames, functionNames, parse } from '../core/expr.js';

const VARS = { function: ['x'], parametric: ['t'], polar: ['t', 'theta'], surface: ['x', 'y'] };
const FIELDS = { function: ['y'], parametric: ['x', 'y'], polar: ['r'], surface: ['z'] };
const range = (d) => ({ type: 'array', items: { type: 'number' }, minItems: 2, maxItems: 2, description: d });

export default {
  name: 'plot',
  summary: 'Plot formulas: type function (y: "sin(x)/x"), parametric (x, y of t), polar (r of t) or surface (z of x, y, WebGL). Functions: sin cos tan exp ln log sqrt abs gamma erf …; constants pi e; params for named constants.',
  shorthand: { array: 'functions', scalar: 'functions' },
  schema: {
    type: 'object',
    additionalProperties: false,
    required: ['functions'],
    properties: {
      type: { enum: ['function', 'parametric', 'polar', 'surface'], default: 'function' },
      title: { type: 'string' },
      functions: {
        type: ['array', 'string'],
        minItems: 1,
        maxItems: 12,
        items: {
          type: ['string', 'object'],
          additionalProperties: false,
          properties: { y: { type: ['string', 'number'] }, x: { type: ['string', 'number'] }, r: { type: ['string', 'number'] }, z: { type: ['string', 'number'] }, label: { type: 'string' } },
        },
        description: 'expressions: strings, or {y|x,y|r|z, label} by type',
      },
      x: range('x range (function, surface; default [-10, 10])'),
      y: range('y range: the view for functions (default: automatic), the domain for surfaces'),
      t: range('parameter range for parametric and polar (default [0, 2pi])'),
      params: { type: 'object', additionalProperties: { type: 'number' }, description: 'named constants, e.g. {a: 2, k: 0.5}' },
      samples: { type: 'integer', minimum: 10, maximum: 5000, description: 'points per curve (default 600) or grid size per axis for surfaces (default 60, max 150)' },
      xlabel: { type: 'string' },
      ylabel: { type: 'string' },
      height: { type: 'integer', minimum: 160, maximum: 1200 },
      rotate: { type: 'boolean', default: false, description: 'surfaces: turn slowly until the user drags' },
    },
  },
  example: 'title: Damped oscillation\nx: [0, 20]\nparams: {a: 0.15, w: 2}\nfunctions:\n  - {y: "exp(-a x) cos(w x)", label: signal}\n  - {y: "exp(-a x)", label: envelope}',
  resolve(props, ctx) {
    const type = props.type;
    const params = props.params ?? {};
    for (const name of Object.keys(params)) {
      if (!/^[A-Za-z_]\w*$/.test(name)) return ctx.error(`/params/${name}`, `"${name}" is not a valid name`);
      if (functionNames.includes(name) || constantNames.includes(name) || VARS[type].includes(name))
        return ctx.error(`/params/${name}`, `"${name}" is already a ${functionNames.includes(name) ? 'function' : constantNames.includes(name) ? 'constant' : 'variable'} name`);
    }
    const allowed = [...VARS[type], ...Object.keys(params)];
    const list = [].concat(props.functions);
    if (type === 'surface' && list.length !== 1) return ctx.error('/functions', 'a surface plot takes exactly one function');
    const functions = [];
    for (let i = 0; i < list.length; i++) {
      const item = list[i];
      const path = Array.isArray(props.functions) ? `/functions/${i}` : '/functions';
      const f = typeof item === 'string' ? { [FIELDS[type][0]]: item } : { ...item };
      if (typeof item === 'string' && type === 'parametric') return ctx.error(path, 'parametric curves need {x: "…", y: "…"}');
      const extra = Object.keys(f).filter((k) => k !== 'label' && !FIELDS[type].includes(k));
      if (extra.length) return ctx.error(`${path}/${extra[0]}`, `"${extra[0]}" does not apply to ${type} plots`, `use ${FIELDS[type].join(' and ')}`);
      for (const k of FIELDS[type]) {
        if (f[k] === undefined) return ctx.error(path, `missing "${k}"`, `a ${type} plot needs ${FIELDS[type].join(' and ')}`);
        const r = parse(f[k], allowed);
        if (!r.ok) return ctx.error(typeof item === 'string' ? path : `${path}/${k}`, `${k} = ${f[k]}: ${r.message}${r.col ? ` (column ${r.col})` : ''}`);
        f[k] = String(f[k]);
      }
      f.label ??= type === 'parametric' ? `(${f.x}, ${f.y})` : f[FIELDS[type][0]];
      functions.push(f);
    }
    for (const axis of ['x', 'y', 't'])
      if (props[axis] && !(props[axis][0] < props[axis][1])) return ctx.error(`/${axis}`, `${axis} range must be [min, max] with min < max`);
    if (type === 'surface' && props.samples > 150) return ctx.error('/samples', 'surfaces take at most 150 samples per axis');
    const surfaceY = props.y ?? [-10, 10];
    return {
      type, title: props.title, functions, params,
      x: props.x ?? [-10, 10], y: type === 'surface' ? surfaceY : props.y, t: props.t ?? [0, 2 * Math.PI],
      samples: props.samples ?? (type === 'surface' ? 60 : 600),
      xlabel: props.xlabel ?? (type === 'polar' ? undefined : 'x'), ylabel: props.ylabel ?? (type === 'surface' ? 'y' : undefined),
      height: props.height ?? (type === 'surface' ? 440 : 320), rotate: props.rotate,
    };
  },
};
