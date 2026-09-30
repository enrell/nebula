import { checkSmiles, splitReaction } from '../core/formats/smiles.js';

export default {
  name: 'reaction',
  summary: 'A chemical reaction drawn from reaction SMILES (reactants>agents>products), with conditions over the arrow.',
  shorthand: { scalar: 'smiles' },
  schema: {
    type: 'object',
    additionalProperties: false,
    required: ['smiles'],
    properties: {
      title: { type: 'string' },
      smiles: { type: 'string', description: 'e.g. "CCO.CC(=O)O>[H+]>CCOC(C)=O.O"' },
      conditions: { type: 'string', description: 'text over the arrow, e.g. "H2SO4, reflux, 2 h"' },
      caption: { type: 'string', description: 'Markdown (inline) under the scheme' },
    },
  },
  example: 'smiles: "CCO.CC(=O)O>[H+]>CCOC(C)=O.O"\nconditions: H₂SO₄, reflux\ncaption: Fischer esterification',
  resolve(props, ctx) {
    const r = splitReaction(props.smiles);
    if (r.error) return ctx.error('/smiles', r.error);
    for (const part of [...r.reactants, ...r.agents, ...r.products]) {
      const bad = checkSmiles(part);
      if (bad) return ctx.error('/smiles', `${part}: ${bad.message}`);
    }
    if (props.caption) ctx.markdown('/caption', props.caption);
    return { ...props, smiles: props.smiles.trim() };
  },
};
