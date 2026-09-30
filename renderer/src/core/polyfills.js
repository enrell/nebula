// QJSEngine (Qt's V4) is an ES2016-level engine; esbuild lowers newer syntax, this adds the newer built-ins the
// checker and its libraries use. Loaded before anything else in dist/core.js; no-ops where they already exist.
/* eslint-disable no-extend-native */
const global = typeof globalThis !== 'undefined' ? globalThis : Function('return this')();
const define = (obj, name, value) => { if (!(name in obj)) Object.defineProperty(obj, name, { value, writable: true, configurable: true }); };

define(global, 'globalThis', global);
define(Object, 'fromEntries', (entries) => { const o = {}; for (const [k, v] of entries) o[k] = v; return o; });
define(Object, 'entries', (o) => Object.keys(o).map((k) => [k, o[k]]));
define(Object, 'values', (o) => Object.keys(o).map((k) => o[k]));
define(Array.prototype, 'flat', function flat(depth = 1) {
  return depth < 1 ? Array.prototype.slice.call(this) : Array.prototype.reduce.call(this, (a, v) => a.concat(Array.isArray(v) ? v.flat(depth - 1) : v), []);
});
define(Array.prototype, 'flatMap', function flatMap(fn, self) { return Array.prototype.map.call(this, fn, self).flat(1); });
define(String.prototype, 'trimStart', function trimStart() { return this.replace(/^\s+/, ''); });
define(String.prototype, 'trimEnd', function trimEnd() { return this.replace(/\s+$/, ''); });
define(String.prototype, 'padStart', function padStart(n, s = ' ') { let r = String(this); while (r.length < n) r = s + r; return r.slice(r.length - Math.max(n, this.length)); });
define(String.prototype, 'padEnd', function padEnd(n, s = ' ') { let r = String(this); while (r.length < n) r += s; return r.slice(0, Math.max(n, this.length)); });

// dotAll (the `s` flag) is missing in older V4. esbuild turns /…/s literals into `new RegExp(…, 's')` for this target,
// so wrapping the constructor covers every use: `.` outside a character class becomes [\s\S], which is what `s` means.
(() => {
  try { new RegExp('.', 's'); return; } catch { /* unsupported: install the shim */ }
  const Native = RegExp;
  const dotAll = (src) => {
    let out = '', cls = false;
    for (let i = 0; i < src.length; i++) {
      const c = src[i];
      if (c === '\\') { out += c + (src[++i] ?? ''); continue; }
      if (cls) { if (c === ']') cls = false; out += c; continue; }
      if (c === '[') { cls = true; out += c; continue; }
      out += c === '.' ? '[\\s\\S]' : c;
    }
    return out;
  };
  function RegExpShim(pattern, flags) {
    if (flags === undefined && pattern instanceof Native) return new Native(pattern);
    const f = flags === undefined ? '' : String(flags);
    if (!f.includes('s')) return new Native(pattern, flags);
    return new Native(dotAll(pattern instanceof Native ? pattern.source : String(pattern)), f.replace('s', ''));
  }
  RegExpShim.prototype = Native.prototype;
  Object.setPrototypeOf(RegExpShim, Native);
  global.RegExp = RegExpShim;
})();
