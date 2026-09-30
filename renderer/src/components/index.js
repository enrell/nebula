// The component registry: the single source of truth for what a document may contain.
// A component is { name, summary, schema (JSON Schema for its YAML body), example, shorthand?, resolve?(props, ctx) }.
// resolve() runs after schema validation: semantic checks, file loading, normalisation into what the renderer draws.
import callout from './callout.js';
import checklist from './checklist.js';
import code from './code.js';
import image from './image.js';
import stats from './stats.js';
import table from './table.js';

export const components = [callout, stats, table, checklist, code, image];
export const byName = Object.fromEntries(components.map((c) => [c.name, c]));
