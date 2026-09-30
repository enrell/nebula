// The component registry: the single source of truth for what a document may contain.
// A component is { name, summary, schema (JSON Schema for its YAML body), example, shorthand?, resolve?(props, ctx) }.
// resolve() runs after schema validation: semantic checks, file loading, normalisation into what the renderer draws.
import callout from './callout.js';
import chart from './chart.js';
import chart3d from './chart3d.js';
import checklist from './checklist.js';
import code from './code.js';
import html from './html.js';
import image from './image.js';
import math from './math.js';
import matrix from './matrix.js';
import plot from './plot.js';
import stats from './stats.js';
import table from './table.js';

export const components = [callout, stats, table, chart, chart3d, plot, matrix, math, checklist, code, image, html];
export const byName = Object.fromEntries(components.map((c) => [c.name, c]));
