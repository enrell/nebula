// 3D scalar fields: isosurfaces (WebGL, 3Dmol.js) next to a slice viewer. From a .npy array a[i, j, k] over x, y, z
// (numpy indexing='ij') or a Gaussian .cube file (densities, orbitals; its atoms are drawn too).
import { base64ToBytes, parseNpy } from '../core/npy.js';
import { extname } from '../core/paths.js';

const MAX_VOXELS = 256 * 256 * 256;

// Gaussian cube: two comment lines, natoms + origin, three axis lines (count + step), atoms, then the values.
export function checkCube(text) {
  const lines = text.replace(/\r/g, '').split('\n');
  const nums = (l) => l.trim().split(/\s+/).map(Number);
  const head = nums(lines[2] ?? '');
  if (head.length < 4 || head.some((v) => !Number.isFinite(v))) return { error: 'line 3: expected the atom count and the origin (x y z)' };
  const natoms = Math.abs(head[0]);
  const axes = [3, 4, 5].map((i) => nums(lines[i] ?? ''));
  const bad = axes.findIndex((a) => a.length < 4 || a.some((v) => !Number.isFinite(v)) || !Number.isInteger(a[0]) || a[0] === 0);
  if (bad >= 0) return { error: `line ${bad + 4}: expected a voxel count and a step vector` };
  const size = axes.map((a) => Math.abs(a[0]));
  const first = 6 + natoms + (head[0] < 0 ? 1 : 0);   // a negative atom count adds a line of orbital ids
  for (let i = 6; i < 6 + natoms; i++) if (nums(lines[i] ?? '').length < 5) return { error: `line ${i + 1}: expected an atom (Z, charge, x, y, z)` };
  const want = size[0] * size[1] * size[2];
  if (want > MAX_VOXELS) return { error: `${size.join('×')} voxels is too many (max ${MAX_VOXELS.toLocaleString()})` };
  let count = 0;
  for (let i = first; i < lines.length; i++) {
    for (const t of lines[i].trim().split(/\s+/)) {
      if (!t) continue;
      if (!Number.isFinite(Number(t))) return { error: `line ${i + 1}: "${t}" is not a number` };
      count++;
    }
  }
  if (count !== want) return { error: `${count.toLocaleString()} values for a ${size.join('×')} grid (${want.toLocaleString()} expected)` };
  return { size, atoms: natoms };
}

export default {
  name: 'volume',
  summary: '3D scalar fields (densities, orbitals, CT/MRI stacks, simulation grids): isosurfaces in WebGL plus a slice viewer. From a .npy a[i, j, k] over x, y, z or a Gaussian .cube file.',
  shorthand: { scalar: 'data' },
  schema: {
    type: 'object',
    additionalProperties: false,
    required: ['data'],
    properties: {
      title: { type: 'string' },
      data: { type: 'string', description: 'relative path to a .npy (shape (nx, ny, nz)) or a .cube file' },
      levels: {
        type: 'array', minItems: 1, maxItems: 6,
        items: { type: ['number', 'object'], additionalProperties: false, required: ['value'],
          properties: { value: { type: 'number' }, color: { type: 'string' }, opacity: { type: 'number', minimum: 0.05, maximum: 1 } } },
        description: 'isosurface values (default: ± a third of the largest |value| for signed data, else half the maximum)',
      },
      spacing: { type: 'array', items: { type: 'number', exclusiveMinimum: 0 }, minItems: 3, maxItems: 3, description: '.npy voxel size [dx, dy, dz] (default [1, 1, 1])' },
      view: { enum: ['both', 'isosurface', 'slices'], default: 'both' },
      height: { type: 'integer', minimum: 240, maximum: 1200, default: 420 },
    },
  },
  example: 'title: Electron density\ndata: density.npy\nlevels: [0.2, {value: 0.6, color: orange, opacity: 0.9}]',
  resolve(props, ctx) {
    const ext = extname(props.data);
    const out = { title: props.title, file: props.data, levels: (props.levels ?? []).map((l) => (typeof l === 'number' ? { value: l } : l)), view: props.view, height: props.height };
    if (ext === 'npy') {
      const f = ctx.readBase64(props.data);
      if (!f.ok) return ctx.error('/data', f.error);
      const a = parseNpy(base64ToBytes(f.base64), { dims: [3], headerOnly: true });
      if (a.error) return ctx.error('/data', `${props.data}: ${a.error}`);
      const n = a.shape.reduce((x, y) => x * y, 1);
      if (a.shape.some((d) => d < 2)) return ctx.error('/data', `${props.data}: shape ${a.shape.join('×')}; every axis needs at least 2 samples`);
      if (n > MAX_VOXELS) return ctx.error('/data', `${props.data}: ${a.shape.join('×')} voxels is too many (max ${MAX_VOXELS.toLocaleString()})`, 'downsample the grid');
      ctx.ref(props.data);
      return { ...out, format: 'npy', size: a.shape, spacing: props.spacing ?? [1, 1, 1] };
    }
    if (ext === 'cube') {
      if (props.spacing) ctx.warn('/spacing', 'a .cube file carries its own voxel size; spacing is ignored');
      const f = ctx.readText(props.data);
      if (!f.ok) return ctx.error('/data', f.error);
      const c = checkCube(f.text);
      if (c.error) return ctx.error('/data', `${props.data}: ${c.error}`);
      ctx.ref(props.data);
      return { ...out, format: 'cube', size: c.size, atoms: c.atoms };
    }
    return ctx.error('/data', `unsupported volume file ".${ext}"`, 'use a .npy (3-D array) or a .cube file');
  },
};
