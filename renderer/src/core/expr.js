// A small, safe math expression language for plots (no eval): numbers, variables, + - * / % ^, unary minus,
// parentheses, implicit multiplication (2x, 3(x + 1), x y), constants and the usual functions.
// parse() returns { ok, fn, vars } or { ok: false, message, col } with a 1-based column for error reporting.
import { closest } from './suggest.js';

const FUNCS = {
  sin: Math.sin, cos: Math.cos, tan: Math.tan, asin: Math.asin, acos: Math.acos, atan: Math.atan,
  sinh: Math.sinh, cosh: Math.cosh, tanh: Math.tanh, asinh: Math.asinh, acosh: Math.acosh, atanh: Math.atanh,
  exp: Math.exp, ln: Math.log, log: Math.log10, log10: Math.log10, log2: Math.log2, sqrt: Math.sqrt, cbrt: Math.cbrt,
  abs: Math.abs, floor: Math.floor, ceil: Math.ceil, round: Math.round, sign: Math.sign,
  atan2: Math.atan2, pow: Math.pow, min: Math.min, max: Math.max, hypot: Math.hypot,
  sec: (v) => 1 / Math.cos(v), csc: (v) => 1 / Math.sin(v), cot: (v) => 1 / Math.tan(v),
  gamma: (v) => gamma(v), erf: (v) => erf(v), sinc: (v) => (v === 0 ? 1 : Math.sin(v) / v),
  heaviside: (v) => (v < 0 ? 0 : 1), clamp: (v, lo, hi) => Math.min(Math.max(v, lo), hi),
};
const ARITY = { atan2: 2, pow: 2, min: [1, 16], max: [1, 16], hypot: [1, 16], clamp: 3 };
const CONSTS = { pi: Math.PI, e: Math.E, tau: 2 * Math.PI, phi: (1 + Math.sqrt(5)) / 2, inf: Infinity };

export const functionNames = Object.keys(FUNCS);
export const constantNames = Object.keys(CONSTS);

function gamma(z) {   // Lanczos approximation
  if (z < 0.5) return Math.PI / (Math.sin(Math.PI * z) * gamma(1 - z));
  const g = 7, c = [0.99999999999980993, 676.5203681218851, -1259.1392167224028, 771.32342877765313, -176.61502916214059,
    12.507343278686905, -0.13857109526572012, 9.9843695780195716e-6, 1.5056327351493116e-7];
  z -= 1;
  let x = c[0];
  for (let i = 1; i < g + 2; i++) x += c[i] / (z + i);
  const t = z + g + 0.5;
  return Math.sqrt(2 * Math.PI) * t ** (z + 0.5) * Math.exp(-t) * x;
}

function erf(x) {   // Abramowitz-Stegun 7.1.26
  const s = Math.sign(x), a = Math.abs(x), t = 1 / (1 + 0.3275911 * a);
  return s * (1 - (((((1.061405429 * t - 1.453152027) * t) + 1.421413741) * t - 0.284496736) * t + 0.254829592) * t * Math.exp(-a * a));
}

class ExprError extends Error { constructor(message, col) { super(message); this.col = col; } }

function tokenize(src) {
  const out = [];
  const re = /\s*(?:(\d+\.?\d*(?:[eE][-+]?\d+)?|\.\d+(?:[eE][-+]?\d+)?)|([A-Za-z_][A-Za-z_0-9]*)|(\*\*|[-+*/%^(),]))/y;
  let i = 0;
  while (i < src.length) {
    if (/^\s*$/.test(src.slice(i))) break;
    re.lastIndex = i;
    const m = re.exec(src);
    if (!m) {
      const at = i + (src.slice(i).length - src.slice(i).trimStart().length);
      throw new ExprError(`unexpected "${src[at]}"`, at + 1);
    }
    const col = m.index + m[0].length - (m[1] ?? m[2] ?? m[3]).length + 1;
    if (m[1] !== undefined) out.push({ t: 'num', v: Number(m[1]), col });
    else if (m[2] !== undefined) out.push({ t: 'id', v: m[2], col });
    else out.push({ t: 'op', v: m[3] === '**' ? '^' : m[3], col });
    i = re.lastIndex;
  }
  out.push({ t: 'end', col: src.length + 1 });
  return out;
}

// Pratt parser producing closures over a variable record.
function build(tokens, allowed) {
  let i = 0;
  const vars = new Set();
  const peek = () => tokens[i];
  const next = () => tokens[i++];
  const expect = (v) => { const t = next(); if (t.v !== v) throw new ExprError(t.t === 'end' ? `missing "${v}"` : `expected "${v}"`, t.col); };
  const startsPrimary = (t) => t.t === 'num' || t.t === 'id' || t.v === '(';

  function primary() {
    const t = next();
    if (t.t === 'num') return () => t.v;
    if (t.v === '(') { const e = expr(0); expect(')'); return e; }
    if (t.v === '-' || t.v === '+') { const e = expr(3); return t.v === '-' ? (v) => -e(v) : e; }
    if (t.t === 'id') {
      if (peek().v === '(' && FUNCS[t.v]) {
        next();
        const args = [];
        if (peek().v !== ')') { args.push(expr(0)); while (peek().v === ',') { next(); args.push(expr(0)); } }
        expect(')');
        const want = ARITY[t.v] ?? 1;
        const [lo, hi] = Array.isArray(want) ? want : [want, want];
        if (args.length < lo || args.length > hi) throw new ExprError(`${t.v}() takes ${lo === hi ? lo : `${lo} to ${hi}`} argument${hi === 1 ? '' : 's'}, got ${args.length}`, t.col);
        const f = FUNCS[t.v];
        if (args.length === 1) { const [a] = args; return (v) => f(a(v)); }
        return (v) => f(...args.map((a) => a(v)));
      }
      if (FUNCS[t.v]) throw new ExprError(`${t.v} is a function: write ${t.v}(…)`, t.col);
      if (t.v in CONSTS && !allowed.includes(t.v)) { const c = CONSTS[t.v]; return () => c; }
      if (allowed.includes(t.v)) { vars.add(t.v); const name = t.v; return (v) => v[name]; }
      const guess = closest(t.v, [...allowed, ...constantNames, ...functionNames]);
      throw new ExprError(`unknown name "${t.v}"${guess ? ` (did you mean "${guess}"?)` : ''}; variables here: ${allowed.join(', ') || 'none'}`, t.col);
    }
    throw new ExprError(t.t === 'end' ? 'expression ends too early' : `unexpected "${t.v}"`, t.col);
  }

  // binding powers: + - 1, * / % 2 (implicit multiplication too), unary 3, ^ 4 (right associative)
  function expr(minBp) {
    let left = primary();
    for (;;) {
      const t = peek();
      let op = t.t === 'op' ? t.v : null;
      if (!op && startsPrimary(t)) op = 'implicit';
      if (op === '(' ) op = 'implicit';
      const bp = { '+': 1, '-': 1, '*': 2, '/': 2, '%': 2, implicit: 2, '^': 4 }[op];
      if (bp === undefined || bp < minBp) break;   // the right operand is parsed at bp + 1, which makes + - * / left associative
      if (op !== 'implicit') next();
      const right = expr(op === '^' ? bp : bp + 1);
      const l = left;
      left = { '+': (v) => l(v) + right(v), '-': (v) => l(v) - right(v), '*': (v) => l(v) * right(v), implicit: (v) => l(v) * right(v),
        '/': (v) => l(v) / right(v), '%': (v) => l(v) % right(v), '^': (v) => l(v) ** right(v) }[op];
    }
    return left;
  }

  const fn = expr(0);
  const end = peek();
  if (end.t !== 'end') throw new ExprError(`unexpected "${end.v}"`, end.col);
  return { fn, vars };
}

export function parse(src, allowed = ['x']) {
  if (typeof src === 'number') return { ok: true, fn: () => src, vars: new Set() };
  try {
    const { fn, vars } = build(tokenize(String(src)), allowed);
    return { ok: true, fn, vars };
  } catch (e) {
    if (e instanceof ExprError) return { ok: false, message: e.message, col: e.col };
    throw e;
  }
}
