// NumPy .npy (format 1.0 – 3.0) numeric arrays, from base64 (the host hands binary files over as base64) or from the
// bytes the page fetched. Supports little/big endian ints, unsigned ints, floats and bools; C and Fortran order.
const B64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
const LOOKUP = (() => { const t = new Int16Array(128).fill(-1); for (let i = 0; i < 64; i++) t[B64.charCodeAt(i)] = i; return t; })();

export function base64ToBytes(s) {
  const clean = s.replace(/[^A-Za-z0-9+/]/g, '');
  const out = new Uint8Array(Math.floor((clean.length * 3) / 4));
  let o = 0;
  for (let i = 0; i + 1 < clean.length; i += 4) {
    const n = (LOOKUP[clean.charCodeAt(i)] << 18) | (LOOKUP[clean.charCodeAt(i + 1)] << 12)
      | ((LOOKUP[clean.charCodeAt(i + 2)] & 63) << 6) | (LOOKUP[clean.charCodeAt(i + 3)] & 63);
    out[o++] = (n >> 16) & 255;
    if (i + 2 < clean.length) out[o++] = (n >> 8) & 255;
    if (i + 3 < clean.length) out[o++] = n & 255;
  }
  return out.subarray(0, o);
}

// -> { shape, data: number[] (row-major) } or { error }. `dims` lists the accepted dimensionalities; with
// headerOnly the data is checked for length but not decoded (the checker of a large volume needs only the shape).
export function parseNpy(bytes, { dims = [1, 2], headerOnly = false } = {}) {
  const magic = [0x93, 0x4e, 0x55, 0x4d, 0x50, 0x59];
  if (bytes.length < 10 || magic.some((b, i) => bytes[i] !== b)) return { error: 'not a .npy file (bad magic)' };
  const major = bytes[6];
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const headerLen = major === 1 ? view.getUint16(8, true) : view.getUint32(8, true);
  const start = major === 1 ? 10 : 12;
  let header = '';
  for (let i = start; i < start + headerLen; i++) header += String.fromCharCode(bytes[i]);
  const descr = /'descr':\s*'([^']+)'/.exec(header)?.[1];
  const fortran = /'fortran_order':\s*True/.test(header);
  const shapeText = /'shape':\s*\(([^)]*)\)/.exec(header)?.[1];
  if (!descr || shapeText === undefined) return { error: 'unreadable .npy header' };
  const shape = shapeText.split(',').map((s) => s.trim()).filter(Boolean).map(Number);
  if (!dims.includes(shape.length)) return { error: `.npy array has ${shape.length} dimension${shape.length === 1 ? '' : 's'} (shape ${shape.join('×') || 'scalar'}); expected ${dims.join(' or ')}` };
  const m = /^([<>|=])([fiub])(\d+)$/.exec(descr);
  if (!m) return { error: `.npy dtype ${descr} is not supported (numeric arrays only)` };
  const little = m[1] !== '>';
  const kind = m[2], size = Number(m[3]);
  const readers = {
    f4: (o) => view.getFloat32(o, little), f8: (o) => view.getFloat64(o, little),
    i1: (o) => view.getInt8(o), i2: (o) => view.getInt16(o, little), i4: (o) => view.getInt32(o, little),
    u1: (o) => view.getUint8(o), u2: (o) => view.getUint16(o, little), u4: (o) => view.getUint32(o, little),
    b1: (o) => view.getUint8(o),
    i8: (o) => { const lo = view.getUint32(o + (little ? 0 : 4), little), hi = view.getInt32(o + (little ? 4 : 0), little); return hi * 4294967296 + lo; },
    u8: (o) => { const lo = view.getUint32(o + (little ? 0 : 4), little), hi = view.getUint32(o + (little ? 4 : 0), little); return hi * 4294967296 + lo; },
  };
  const read = readers[`${kind}${size}`];
  if (!read) return { error: `.npy dtype ${descr} is not supported` };
  const count = shape.reduce((a, b) => a * b, 1);
  const offset = start + headerLen;
  if (offset + count * size > bytes.length) return { error: '.npy file is truncated' };
  if (headerOnly) return { shape };
  const flat = new Array(count);
  for (let i = 0; i < count; i++) flat[i] = read(offset + i * size);
  if (!fortran || shape.length < 2) return { shape, data: flat };
  // Fortran order: the first index varies fastest; reorder to row-major (last index fastest)
  const out = new Array(count), idx = new Array(shape.length).fill(0);
  const fstride = shape.map((_, k) => shape.slice(0, k).reduce((a, b) => a * b, 1));
  for (let i = 0; i < count; i++) {
    let f = 0;
    for (let k = 0; k < shape.length; k++) f += idx[k] * fstride[k];
    out[i] = flat[f];
    for (let k = shape.length - 1; k >= 0 && ++idx[k] === shape[k]; k--) idx[k] = 0;
  }
  return { shape, data: out };
}
