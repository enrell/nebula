// BED (3–12 columns) and bedGraph, tab or space separated; `track`/`browser`/# lines are skipped.
export function parseBed(text, { graph = false } = {}) {
  const out = [];
  const lines = text.replace(/\r/g, '').split('\n');
  for (let n = 0; n < lines.length; n++) {
    const line = lines[n];
    if (!line.trim() || /^(#|track|browser)/.test(line)) continue;
    const f = line.split(/\t| +/);
    if (f.length < (graph ? 4 : 3)) return { error: `line ${n + 1}: expected at least ${graph ? 4 : 3} columns (chrom start end${graph ? ' value' : ''})` };
    const start = Number(f[1]), end = Number(f[2]);
    if (!Number.isInteger(start) || !Number.isInteger(end) || start < 0 || end < start) return { error: `line ${n + 1}: start/end must be integers with 0 ≤ start ≤ end` };
    if (graph) {
      const value = Number(f[3]);
      if (!Number.isFinite(value)) return { error: `line ${n + 1}: "${f[3]}" is not a number` };
      out.push({ chrom: f[0], start, end, value });
      continue;
    }
    const feat = { chrom: f[0], start, end, name: f[3], strand: f[5] === '+' || f[5] === '-' ? f[5] : undefined };
    if (f.length >= 12) {
      const sizes = f[10].split(',').filter(Boolean).map(Number), starts = f[11].split(',').filter(Boolean).map(Number);
      if (sizes.length !== starts.length || sizes.some((v) => !Number.isFinite(v))) return { error: `line ${n + 1}: blockSizes and blockStarts do not match` };
      feat.exons = starts.map((s, k) => [start + s, start + s + sizes[k]]);
      const ts = Number(f[6]), te = Number(f[7]);
      if (Number.isInteger(ts) && Number.isInteger(te) && te > ts) feat.thick = [ts, te];
    }
    out.push(feat);
  }
  return { features: out };
}

// "chr1:1,000-5,000" -> { chrom, start, end }
export function parseRegion(s) {
  const m = /^\s*([^:\s]+):([\d,]+)-([\d,]+)\s*$/.exec(String(s));
  if (!m) return undefined;
  const start = Number(m[2].replace(/,/g, '')), end = Number(m[3].replace(/,/g, ''));
  return end > start ? { chrom: m[1], start, end } : undefined;
}
