// FASTA (and plain sequences) with alphabet checks.
export const ALPHABETS = {
  dna: 'ACGTNRYSWKMBDHV',
  rna: 'ACGUNRYSWKMBDHV',
  protein: 'ACDEFGHIKLMNPQRSTVWYBJOUXZ*',
};

export function parseFasta(text) {
  const seqs = [];
  let cur = null;
  const lines = text.replace(/\r/g, '').split('\n');
  for (let n = 0; n < lines.length; n++) {
    const line = lines[n].trim();
    if (!line || line.startsWith(';')) continue;
    if (line.startsWith('>')) { cur = { name: line.slice(1).trim() || `seq${seqs.length + 1}`, seq: '', line: n + 1 }; seqs.push(cur); continue; }
    if (!cur) return { error: `line ${n + 1}: sequence data before the first ">name" header` };
    cur.seq += line.replace(/\s+/g, '');
  }
  if (!seqs.length) return { error: 'no sequences (FASTA records start with ">name")' };
  return { seqs };
}

// guess dna / rna / protein from the letters
export function guessType(seqs) {
  const letters = seqs.map((s) => s.seq.toUpperCase().replace(/[-.]/g, '')).join('');
  if (!letters) return 'dna';
  const nuc = [...letters].filter((c) => 'ACGTUN'.includes(c)).length / letters.length;
  if (nuc > 0.9) return letters.includes('U') && !letters.includes('T') ? 'rna' : 'dna';
  return 'protein';
}

// first invalid letter: { name, pos (1-based), char } or undefined
export function checkAlphabet(seqs, type) {
  const ok = ALPHABETS[type] + '-.';
  for (const s of seqs) {
    const up = s.seq.toUpperCase();
    for (let i = 0; i < up.length; i++) if (!ok.includes(up[i])) return { name: s.name, pos: i + 1, char: s.seq[i] };
  }
  return undefined;
}
