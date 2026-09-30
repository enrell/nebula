// Named constants for formula components (plot, field, animation). A parameter is a number, or a slider:
// {value, min, max, step?, label?}, which the page draws as a control that re-samples the formulas live.
import { constantNames, functionNames } from './expr.js';

export const PARAMS_SCHEMA = {
  type: 'object',
  additionalProperties: {
    type: ['number', 'object'], additionalProperties: false, required: ['value', 'min', 'max'],
    properties: { value: { type: 'number' }, min: { type: 'number' }, max: { type: 'number' }, step: { type: 'number', exclusiveMinimum: 0 }, label: { type: 'string' } },
  },
  description: 'named constants: a number (a: 2) or a slider (a: {value: 2, min: 0, max: 5, step: 0.1})',
};

// -> { values: {name: number}, sliders: [{name, value, min, max, step, label}] }, or undefined after ctx.error
export function resolveParams(params = {}, variables, ctx) {
  const values = {}, sliders = [];
  for (const [name, spec] of Object.entries(params)) {
    const path = `/params/${name}`;
    if (!/^[A-Za-z_]\w*$/.test(name)) return ctx.error(path, `"${name}" is not a valid name`);
    const clash = functionNames.includes(name) ? 'function' : constantNames.includes(name) ? 'constant' : variables.includes(name) ? 'variable' : null;
    if (clash) return ctx.error(path, `"${name}" is already a ${clash} name`);
    if (typeof spec === 'number') { values[name] = spec; continue; }
    if (!(spec.min < spec.max)) return ctx.error(path, `slider range must have min < max (got ${spec.min}…${spec.max})`);
    if (spec.value < spec.min || spec.value > spec.max) return ctx.error(`${path}/value`, `${spec.value} is outside ${spec.min}…${spec.max}`);
    values[name] = spec.value;
    sliders.push({ name, value: spec.value, min: spec.min, max: spec.max, step: spec.step ?? niceStep((spec.max - spec.min) / 100), label: spec.label ?? name });
  }
  return { values, sliders };
}

const niceStep = (raw) => { const p = 10 ** Math.floor(Math.log10(raw)); return [1, 2, 5, 10].map((m) => m * p).find((s) => s >= raw); };
