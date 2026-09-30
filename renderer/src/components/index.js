// The component registry: the single source of truth for what a document may contain.
// A component is { name, summary, schema (JSON Schema for its YAML body), example, shorthand?, resolve?(props, ctx) }.
// resolve() runs after schema validation: semantic checks, file loading, normalisation into what the renderer draws.
import animation from './animation.js';
import callout from './callout.js';
import chart from './chart.js';
import chart3d from './chart3d.js';
import checklist from './checklist.js';
import code from './code.js';
import diagram from './diagram.js';
import field from './field.js';
import graph from './graph.js';
import html from './html.js';
import image from './image.js';
import map from './map.js';
import math from './math.js';
import matrix from './matrix.js';
import molecule from './molecule.js';
import plot from './plot.js';
import provenance from './provenance.js';
import reaction from './reaction.js';
import references from './references.js';
import sequence from './sequence.js';
import stats from './stats.js';
import structure from './structure.js';
import table from './table.js';
import tracks from './tracks.js';
import tree from './tree.js';
import volume from './volume.js';

export const components = [
  callout, stats, table, checklist, code, image, html,                          // general
  chart, chart3d, plot, matrix, math,                                          // data and math
  molecule, reaction, structure, sequence, tree, tracks,                       // chemistry and biology
  field, animation, volume,                                                    // physics
  graph, diagram, map,                                                         // networks, diagrams and maps
  references, provenance,                                                      // research record
];
export const byName = Object.fromEntries(components.map((c) => [c.name, c]));
