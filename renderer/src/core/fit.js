// Least-squares fits for chart `fit:`: returns { predict(x), label, r2 } or { error }.
function solve(A, b) {   // Gaussian elimination with partial pivoting, small systems
  const n = b.length, M = A.map((row, i) => [...row, b[i]]);
  for (let c = 0; c < n; c++) {
    let p = c;
    for (let r = c + 1; r < n; r++) if (Math.abs(M[r][c]) > Math.abs(M[p][c])) p = r;
    if (Math.abs(M[p][c]) < 1e-12) return null;
    [M[c], M[p]] = [M[p], M[c]];
    for (let r = 0; r < n; r++) if (r !== c) { const f = M[r][c] / M[c][c]; for (let k = c; k <= n; k++) M[r][k] -= f * M[c][k]; }
  }
  return M.map((row, i) => row[n] / row[i]);
}

function polyfit(xs, ys, degree) {
  const n = degree + 1;
  const A = Array.from({ length: n }, (_, i) => Array.from({ length: n }, (_, j) => xs.reduce((s, x) => s + x ** (i + j), 0)));
  const b = Array.from({ length: n }, (_, i) => xs.reduce((s, x, k) => s + ys[k] * x ** i, 0));
  return solve(A, b);
}

const fmt = (v) => Number(v.toPrecision(4));

export const FITS = ['linear', 'quadratic', 'cubic', 'exponential', 'log', 'power'];

export function fit(kind, xs, ys) {
  const pts = xs.map((x, i) => [x, ys[i]]).filter(([x, y]) => Number.isFinite(x) && Number.isFinite(y));
  const need = { linear: 2, quadratic: 3, cubic: 4 }[kind] ?? 2;
  if (pts.length < need + 1) return { error: `needs at least ${need + 1} points` };
  let predict, label;
  if (kind === 'linear' || kind === 'quadratic' || kind === 'cubic') {
    const c = polyfit(pts.map((p) => p[0]), pts.map((p) => p[1]), need - 1);
    if (!c) return { error: 'the points do not determine a fit' };
    predict = (x) => c.reduce((s, a, i) => s + a * x ** i, 0);
    const terms = c.map((a, i) => [fmt(a), i]).filter(([a]) => Math.abs(a) > 1e-12).reverse()
      .map(([a, i]) => (i === 0 ? `${a}` : `${a === 1 ? '' : a === -1 ? '-' : a}x${i > 1 ? `^${i}` : ''}`));
    label = `y = ${terms.join(' + ').replace(/\+ -/g, '− ') || '0'}`;
  } else {
    const tx = kind === 'log' || kind === 'power' ? (x) => Math.log(x) : (x) => x;
    const ty = kind === 'exponential' || kind === 'power' ? (y) => Math.log(y) : (y) => y;
    const usable = pts.filter(([x, y]) => (tx === Math.log ? x > 0 : true) && (ty === Math.log ? y > 0 : true));
    if (usable.length < 3) return { error: `${kind} fits need positive ${kind === 'log' ? 'x' : kind === 'power' ? 'x and y' : 'y'} values` };
    const c = polyfit(usable.map((p) => tx(p[0])), usable.map((p) => ty(p[1])), 1);
    if (!c) return { error: 'the points do not determine a fit' };
    const [a, b] = c;
    if (kind === 'exponential') { predict = (x) => Math.exp(a + b * x); label = `y = ${fmt(Math.exp(a))}·e^(${fmt(b)}x)`; }
    else if (kind === 'log') { predict = (x) => a + b * Math.log(x); label = `y = ${fmt(a)} + ${fmt(b)}·ln x`; }
    else { predict = (x) => Math.exp(a) * x ** b; label = `y = ${fmt(Math.exp(a))}·x^${fmt(b)}`; }
  }
  const mean = pts.reduce((s, p) => s + p[1], 0) / pts.length;
  const ssTot = pts.reduce((s, p) => s + (p[1] - mean) ** 2, 0);
  const ssRes = pts.reduce((s, p) => s + (p[1] - predict(p[0])) ** 2, 0);
  return { predict, label, r2: ssTot > 0 ? 1 - ssRes / ssTot : 1 };
}
