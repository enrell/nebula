// claygl (under echarts-gl) sizes its render targets with strings like "expr([width * dpr / 4, height * dpr / 4])"
// and compiles them with `new Function`, which the view page's CSP (no 'unsafe-eval') forbids. build.mjs swaps
// that call for this evaluator: numbers, width/height/dpr, + - * /, parentheses and [a, b] lists.
export function compileSizeExpr(src) {
  const tokens = src.match(/\d+(\.\d+)?|[A-Za-z_]\w*|[-+*/()[\],]|\S/g) ?? [];
  let i = 0;
  const peek = () => tokens[i];
  const take = (t) => { if (tokens[i] !== t) throw new Error(`size expression: expected ${t} in ${src}`); i++; };
  function primary() {
    const t = tokens[i++];
    if (t === undefined) throw new Error(`size expression ended early: ${src}`);
    if (t === '(') { const e = sum(); take(')'); return e; }
    if (t === '[') { const items = [sum()]; while (peek() === ',') { i++; items.push(sum()); } take(']'); return (v) => items.map((f) => f(v)); }
    if (t === '-') { const e = primary(); return (v) => -e(v); }
    if (/^\d/.test(t)) { const n = Number(t); return () => n; }
    if (t === 'width' || t === 'height' || t === 'dpr') return (v) => v[t];
    throw new Error(`size expression: unexpected ${t} in ${src}`);
  }
  function product() {
    let e = primary();
    while (peek() === '*' || peek() === '/') {
      const op = tokens[i++], l = e, r = primary();
      e = op === '*' ? (v) => l(v) * r(v) : (v) => l(v) / r(v);
    }
    return e;
  }
  function sum() {
    let e = product();
    while (peek() === '+' || peek() === '-') {
      const op = tokens[i++], l = e, r = product();
      e = op === '+' ? (v) => l(v) + r(v) : (v) => l(v) - r(v);
    }
    return e;
  }
  const e = sum();
  if (i !== tokens.length) throw new Error(`size expression: unexpected ${tokens[i]} in ${src}`);
  return (width, height, dpr) => e({ width, height, dpr });
}
