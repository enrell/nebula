// Numbers as scientists write them: a value with its uncertainty rounded to the uncertainty's precision
// (2 significant figures of the uncertainty), or a value rounded to N significant figures.
export function sig(v, digits) {
  if (!Number.isFinite(v) || v === 0) return String(v);
  const s = Number(v.toPrecision(digits));
  return Math.abs(s) >= 1e6 || Math.abs(s) < 1e-4 ? s.toExponential(Math.max(0, digits - 1)) : String(s);
}

export function withError(v, e) {
  if (!Number.isFinite(v)) return String(v);
  if (!Number.isFinite(e) || e <= 0) return String(v);
  const exp = Math.floor(Math.log10(e)) - 1;            // keep 2 significant figures of the error
  const decimals = Math.max(0, -exp);
  const scale = 10 ** exp;
  const r = (x) => (exp < 0 ? x.toFixed(decimals) : String(Math.round(x / scale) * scale));
  return `${r(v)} ± ${r(e)}`;
}

export function formatValue(v, { error, unit, digits } = {}) {
  let s;
  if (typeof v !== 'number') s = String(v);
  else if (error !== undefined) s = withError(v, error);
  else if (digits) s = sig(v, digits);
  else s = String(v);
  return unit ? `${s} ${unit}` : s;
}
