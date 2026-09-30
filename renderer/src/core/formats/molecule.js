// Light structural checks of molecule files before 3Dmol parses them in the page: the right format for the
// extension, and a line number when a file is malformed.
export const FORMATS = { pdb: 'pdb', ent: 'pdb', pqr: 'pqr', cif: 'cif', mmcif: 'cif', sdf: 'sdf', mol: 'sdf', mol2: 'mol2', xyz: 'xyz', gro: 'gro' };

export function checkMolecule(text, format) {
  const lines = text.replace(/\r/g, '').split('\n');
  if (format === 'pdb' || format === 'pqr') {
    const atoms = lines.filter((l) => /^(ATOM  |HETATM)/.test(l));
    if (!atoms.length) return { error: 'no ATOM or HETATM records' };
    const bad = lines.findIndex((l) => /^(ATOM  |HETATM)/.test(l) && (l.length < 54 || [30, 38, 46].some((c) => !Number.isFinite(Number(l.slice(c, c + 8))))));
    if (bad >= 0) return { error: `line ${bad + 1}: malformed coordinates (x, y, z are columns 31–54)` };
    const protein = atoms.some((l) => l.startsWith('ATOM  ') && l.slice(12, 16).trim() === 'CA');
    return { atoms: atoms.length, protein, chains: [...new Set(atoms.map((l) => l[21]).filter((c) => c && c.trim()))] };
  }
  if (format === 'xyz') {
    const n = Number(lines[0]?.trim());
    if (!Number.isInteger(n) || n <= 0) return { error: 'line 1: expected the number of atoms' };
    for (let k = 0; k < n; k++) {
      const f = (lines[k + 2] ?? '').trim().split(/\s+/);
      if (f.length < 4 || f.slice(1, 4).some((v) => !Number.isFinite(Number(v)))) return { error: `line ${k + 3}: expected "element x y z" (atom ${k + 1} of ${n})` };
    }
    return { atoms: n };
  }
  if (format === 'sdf') {
    const counts = lines.findIndex((l) => /V[23]000\s*$/.test(l));
    if (counts < 0) return { error: 'no counts line (V2000/V3000) in the molfile header' };
    const n = Number(lines[counts].slice(0, 3));
    return { atoms: Number.isFinite(n) ? n : undefined };
  }
  if (format === 'mol2') {
    if (!lines.some((l) => l.startsWith('@<TRIPOS>ATOM'))) return { error: 'no @<TRIPOS>ATOM section' };
    return {};
  }
  if (format === 'cif') {
    if (!lines.some((l) => l.startsWith('_atom_site.'))) return { error: 'no _atom_site loop' };
    return { protein: lines.some((l) => /\bCA\b/.test(l)) };
  }
  return {};
}
