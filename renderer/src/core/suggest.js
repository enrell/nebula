function distance(a, b) {
  const d = Array.from({ length: a.length + 1 }, (_, i) => [i, ...Array(b.length).fill(0)]);
  for (let j = 1; j <= b.length; j++) d[0][j] = j;
  for (let i = 1; i <= a.length; i++)
    for (let j = 1; j <= b.length; j++)
      d[i][j] = Math.min(d[i - 1][j] + 1, d[i][j - 1] + 1, d[i - 1][j - 1] + (a[i - 1] === b[j - 1] ? 0 : 1));
  return d[a.length][b.length];
}

// The closest candidate, if it is plausibly a typo.
export function closest(word, candidates) {
  const w = String(word).toLowerCase();
  let best, bestD = Infinity;
  for (const c of candidates) {
    const d = distance(w, c.toLowerCase());
    if (d < bestD) { best = c; bestD = d; }
  }
  return best !== undefined && (bestD <= 2 || (bestD <= 3 && w.length > 6)) ? best : undefined;
}
