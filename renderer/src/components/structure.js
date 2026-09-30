import { extname } from '../core/paths.js';
import { checkMolecule, FORMATS } from '../core/formats/molecule.js';

const STYLES = ['auto', 'cartoon', 'stick', 'sphere', 'line', 'ballstick', 'surface'];

export default {
  name: 'structure',
  summary: '3D molecular structure (WebGL, rotatable) from a PDB, mmCIF, SDF/MOL, MOL2, XYZ or GRO file: proteins as cartoon, small molecules as sticks; highlight residues or chains.',
  schema: {
    type: 'object',
    additionalProperties: false,
    required: ['data'],
    properties: {
      title: { type: 'string' },
      data: { type: 'string', description: 'relative path to a .pdb .cif .sdf .mol .mol2 .xyz .gro .pqr file' },
      style: { enum: STYLES, default: 'auto' },
      color: { enum: ['element', 'chain', 'spectrum', 'secondary'], description: 'default: spectrum for proteins, element otherwise' },
      highlight: {
        type: 'array',
        items: { type: 'object', additionalProperties: false, properties: {
          chain: { type: 'string' }, residues: { type: 'string', description: 'e.g. "45-60" or "12,15,19"' }, resn: { type: 'string', description: 'residue name, e.g. HEM' },
          style: { enum: STYLES.slice(1) }, color: { type: 'string', description: 'a colour name or #hex' }, label: { type: 'string' } } },
      },
      spin: { type: 'boolean', default: false },
      height: { type: 'integer', minimum: 200, maximum: 1200, default: 460 },
    },
  },
  example: 'data: 1a8o.pdb\nhighlight:\n  - {resn: MSE, style: stick, color: orange, label: selenomethionine}',
  resolve(props, ctx) {
    const format = FORMATS[extname(props.data)];
    if (!format) return ctx.error('/data', `unsupported structure file ".${extname(props.data)}"`, `use one of: ${Object.keys(FORMATS).join(', ')}`);
    const f = ctx.readText(props.data);
    if (!f.ok) return ctx.error('/data', f.error);
    const info = checkMolecule(f.text, format);
    if (info.error) return ctx.error('/data', `${props.data}: ${info.error}`);
    for (const [i, h] of (props.highlight ?? []).entries())
      if (h.residues !== undefined && !/^\s*\d+(\s*-\s*\d+)?(\s*,\s*\d+(\s*-\s*\d+)?)*\s*$/.test(h.residues)) return ctx.error(`/highlight/${i}/residues`, `"${h.residues}" is not a residue list`, 'write e.g. "45-60" or "12,15,19"');
    ctx.ref(props.data);
    const style = props.style === 'auto' ? (info.protein ? 'cartoon' : 'ballstick') : props.style;
    return { title: props.title, file: props.data, format, style, color: props.color ?? (info.protein ? 'spectrum' : 'element'),
      highlight: props.highlight ?? [], spin: props.spin, height: props.height, atoms: info.atoms, chains: info.chains };
  },
};
