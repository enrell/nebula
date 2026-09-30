import { checkSmiles } from '../core/formats/smiles.js';

export default {
  name: 'molecule',
  summary: '2D structure drawings from SMILES, one or a grid of several with names.',
  shorthand: { scalar: 'smiles', array: 'items' },
  schema: {
    type: 'object',
    additionalProperties: false,
    properties: {
      title: { type: 'string' },
      smiles: { type: 'string' },
      name: { type: 'string' },
      items: { type: 'array', minItems: 1, maxItems: 48, items: { type: ['string', 'object'], additionalProperties: false, required: ['smiles'],
        properties: { smiles: { type: 'string' }, name: { type: 'string' } } } },
      size: { type: 'integer', minimum: 120, maximum: 800, default: 240, description: 'drawing size in px' },
      hydrogens: { type: 'boolean', default: false, description: 'draw explicit hydrogens' },
    },
  },
  example: 'items:\n  - {smiles: "CC(=O)Oc1ccccc1C(=O)O", name: aspirin}\n  - {smiles: "CN1C=NC2=C1C(=O)N(C(=O)N2C)C", name: caffeine}',
  resolve(props, ctx) {
    if ((props.smiles === undefined) === (props.items === undefined)) return ctx.error('', 'give "smiles" (one molecule) or "items" (several)');
    const items = props.items ? props.items.map((it) => (typeof it === 'string' ? { smiles: it } : it)) : [{ smiles: props.smiles, name: props.name }];
    for (let i = 0; i < items.length; i++) {
      const bad = checkSmiles(items[i].smiles);
      const path = props.items ? (typeof props.items[i] === 'string' ? `/items/${i}` : `/items/${i}/smiles`) : '/smiles';
      if (bad) return ctx.error(path, `SMILES ${items[i].smiles}: ${bad.message}${bad.col ? ` (column ${bad.col})` : ''}`);
    }
    return { title: props.title, items: items.map((it) => ({ smiles: it.smiles.trim(), name: it.name })), size: props.size, hydrogens: props.hydrogens };
  },
};
