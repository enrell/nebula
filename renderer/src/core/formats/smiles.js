// SMILES validation with smiles-drawer's own parser (the page draws with the same library).
// by path: the package's export map only exposes its bundled build, not the parser module
import Parser from '../../../node_modules/smiles-drawer/src/Parser.js';

// -> undefined, or { message, col }
export function checkSmiles(smiles) {
  if (!smiles.trim()) return { message: 'empty SMILES' };
  if (/\s/.test(smiles.trim())) return { message: 'SMILES cannot contain spaces', col: smiles.trim().search(/\s/) + 1 };
  try {
    Parser.parse(smiles.trim(), {});
    return undefined;
  } catch (e) {
    const col = e.location?.start?.column;
    return { message: e.message.replace(/^Expected .* but "(.)" found\.$/, 'unexpected "$1"'), col };
  }
}

// "CCO.O=O>catalyst>CC=O": reactants > agents > products, each a "."-separated list of SMILES
export function splitReaction(s) {
  const parts = s.trim().split('>');
  if (parts.length !== 3) return { error: 'a reaction SMILES has the form reactants>agents>products (agents may be empty: A.B>>C)' };
  const [reactants, agents, products] = parts.map((p) => (p ? p.split('.') : []));
  if (!reactants.length || !products.length) return { error: 'a reaction needs at least one reactant and one product' };
  return { reactants, agents, products };
}
