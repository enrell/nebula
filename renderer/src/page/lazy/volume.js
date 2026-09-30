// Volumes: isosurfaces with 3Dmol.js (WebGL) and a slice viewer on a canvas, over the same grid. Grids are x-major
// (index ((x · ny) + y) · nz + z), which is both 3Dmol's layout and a C-order numpy array a[i, j, k] over x, y, z.
import * as $3Dmol from '3dmol/build/3Dmol.es6.js';
import { parseNpy } from '../../core/npy.js';
import { css, ramp, tick } from '../canvasplot.js';
import { card, h, readFile } from '../dom.js';

const webglAvailable = () => { try { return !!document.createElement('canvas').getContext('webgl'); } catch { return false; } };

async function load(p, ctx) {
  if (p.format === 'cube') {
    const text = await readFile(ctx.fileUrl(p.file));
    const vol = new $3Dmol.VolumeData(text, 'cube');
    return { vol, text, size: [vol.size.x, vol.size.y, vol.size.z], data: vol.data };
  }
  const a = parseNpy(await readFile(ctx.fileUrl(p.file), 'arraybuffer'), { dims: [3] });
  if (a.error) throw new Error(`${p.file}: ${a.error}`);
  const vol = new $3Dmol.VolumeData('', 'none');
  const [nx, ny, nz] = a.shape;
  vol.size = { x: nx, y: ny, z: nz };
  vol.unit = { x: p.spacing[0], y: p.spacing[1], z: p.spacing[2] };
  vol.origin = { x: 0, y: 0, z: 0 };
  vol.data = Float32Array.from(a.data, (v) => (Number.isFinite(v) ? v : 0));
  return { vol, size: a.shape, data: vol.data };
}

function statsOf(data) {
  let lo = Infinity, hi = -Infinity;
  for (const v of data) { if (v < lo) lo = v; if (v > hi) hi = v; }
  return { lo, hi, amax: Math.max(Math.abs(lo), Math.abs(hi)) };
}

// the default levels: ± a third of the largest |value| for signed data (orbitals, differences), else half the maximum
function levelsFor(p, st) {
  const colors = [css('accent'), css('blue'), css('green'), css('yellow'), css('red')];
  if (p.levels.length) {
    // nested surfaces: the innermost (highest) level is opaque, the outer ones more and more transparent. 3Dmol draws
    // opaque surfaces first, so a translucent inner surface would be lost behind the outer one's depth.
    const order = [...p.levels].map((l) => Math.abs(l.value)).sort((a, b) => b - a);
    return p.levels.map((l, i) => ({ value: l.value, color: l.color ?? colors[i % colors.length],
      opacity: l.opacity ?? (p.levels.length > 1 ? 1 - (0.7 * order.indexOf(Math.abs(l.value))) / (p.levels.length - 1) : 0.9) }));
  }
  if (st.lo < 0 && st.hi > 0 && Math.min(-st.lo, st.hi) > 0.1 * st.amax)
    return [{ value: st.amax / 3, color: css('blue'), opacity: 0.8 }, { value: -st.amax / 3, color: css('red'), opacity: 0.8 }];
  return [{ value: st.lo + (st.hi - st.lo) / 2, color: css('accent'), opacity: 0.85 }];
}

function isoView(box, grid, levels) {
  const viewer = $3Dmol.createViewer(box, { backgroundColor: css('panel'), antialias: true });
  if (grid.text) viewer.addModel(grid.text, 'cube').setStyle({}, { stick: { radius: 0.15 }, sphere: { scale: 0.3 } });
  for (const l of levels) {
    // 3Dmol draws the region above a positive level and below a negative one
    viewer.addIsosurface(grid.vol, { isoval: l.value, color: l.color, opacity: l.opacity, smoothness: 1 });
  }
  // an outline of the grid box, so the surfaces have a frame of reference
  const { vol } = grid;
  const [x0, y0, z0] = [vol.origin.x, vol.origin.y, vol.origin.z];
  const [x1, y1, z1] = [x0 + (vol.size.x - 1) * vol.unit.x, y0 + (vol.size.y - 1) * vol.unit.y, z0 + (vol.size.z - 1) * vol.unit.z];
  if (!vol.matrix) {
    const c = [[x0, y0, z0], [x1, y0, z0], [x1, y1, z0], [x0, y1, z0], [x0, y0, z1], [x1, y0, z1], [x1, y1, z1], [x0, y1, z1]];
    for (const [a, b] of [[0, 1], [1, 2], [2, 3], [3, 0], [4, 5], [5, 6], [6, 7], [7, 4], [0, 4], [1, 5], [2, 6], [3, 7]])
      viewer.addLine({ start: { x: c[a][0], y: c[a][1], z: c[a][2] }, end: { x: c[b][0], y: c[b][1], z: c[b][2] }, color: css('border'), dashed: false });
  }
  viewer.zoomTo();
  // a three-quarter view: looking straight down an axis hides the shape of lobes and layers
  viewer.rotate(-55, 'x');
  viewer.rotate(25, 'y');
  viewer.render();
  const ro = new ResizeObserver(() => { viewer.resize(); viewer.render(); });
  ro.observe(box);
  return () => { ro.disconnect(); viewer.clear(); };
}

function sliceView(wrap, p, grid, st, levels) {
  const [nx, ny, nz] = grid.size;
  const dims = { x: nx, y: ny, z: nz };
  const at = (x, y, z) => grid.data[(x * ny + y) * nz + z];
  // start on the axis whose middle slice shows the most (a p_z orbital is flat through z = 0, busy through y = 0)
  const spread = (a) => {
    const vals = [];
    const m = Math.floor(dims[a] / 2);
    for (let i = 0; i < 24; i++) for (let j = 0; j < 24; j++) {
      const c = { x: 0, y: 0, z: 0, [a]: m };
      const [u, v] = { z: ['x', 'y'], y: ['x', 'z'], x: ['y', 'z'] }[a];
      c[u] = Math.floor(((i + 0.5) / 24) * dims[u]);
      c[v] = Math.floor(((j + 0.5) / 24) * dims[v]);
      vals.push(at(c.x, c.y, c.z));
    }
    const mean = vals.reduce((x, y) => x + y, 0) / vals.length;
    return vals.reduce((x, y) => x + (y - mean) ** 2, 0);
  };
  let axis = ['z', 'y', 'x'].reduce((best, a) => (spread(a) > spread(best) * 1.5 ? a : best), 'z');
  let index = Math.floor(dims[axis] / 2);
  const canvas = h('canvas.slice-canvas');
  const slider = h('input.anim-scrub', { type: 'range', min: 0, max: dims[axis] - 1, step: 1, value: index, 'aria-label': 'slice' });
  const label = h('output.anim-time');
  const buttons = ['x', 'y', 'z'].map((a) => h('button.slice-axis', { type: 'button', onclick: () => { axis = a; index = Math.floor(dims[a] / 2); slider.max = dims[a] - 1; slider.value = index; draw(); } }, a));
  slider.addEventListener('input', () => { index = Number(slider.value); draw(); });
  const signed = st.lo < 0 && st.hi > 0;
  const color = ramp();
  const norm = signed ? (v) => 0.5 + v / (2 * st.amax) : (v) => (v - st.lo) / (st.hi - st.lo || 1);
  function draw() {
    // the slice's two in-plane axes, in order: z -> (x, y), y -> (x, z), x -> (y, z)
    const [u, v] = { z: ['x', 'y'], y: ['x', 'z'], x: ['y', 'z'] }[axis];
    const W = dims[u], H = dims[v];
    const img = new ImageData(W, H);
    for (let j = 0; j < H; j++) for (let i = 0; i < W; i++) {
      const c = { [u]: i, [v]: H - 1 - j, [axis]: index };
      const [r, g, b] = color(norm(at(c.x, c.y, c.z)));
      img.data.set([r, g, b, 255], (j * W + i) * 4);
    }
    const off = new OffscreenCanvas(W, H);
    off.getContext('2d').putImageData(img, 0, 0);
    const box = canvas.parentElement.getBoundingClientRect();
    const side = Math.max(120, Math.min(box.width, box.height));
    const scale = Math.min(side / W, side / H);
    const dpr = window.devicePixelRatio || 1;
    canvas.width = Math.round(W * scale * dpr);
    canvas.height = Math.round(H * scale * dpr);
    canvas.style.width = `${Math.round(W * scale)}px`;
    canvas.style.height = `${Math.round(H * scale)}px`;
    const g = canvas.getContext('2d');
    g.imageSmoothingEnabled = scale < 3;   // coarse grids stay crisp voxels when enlarged
    g.drawImage(off, 0, 0, canvas.width, canvas.height);
    // contour of each isosurface level on this slice (marching squares, segment form)
    g.lineWidth = 1.2 * dpr;
    for (const l of levels) {
      g.strokeStyle = l.color;
      g.beginPath();
      const val = (i, j) => { const c = { [u]: i, [v]: j, [axis]: index }; return at(c.x, c.y, c.z); };
      const sx = canvas.width / W, sy = canvas.height / H;
      const px = (i) => (i + 0.5) * sx, py = (j) => (H - 1 - j + 0.5) * sy;
      for (let j = 0; j < H - 1; j++) for (let i = 0; i < W - 1; i++) {
        const a = val(i, j) - l.value, b = val(i + 1, j) - l.value, c2 = val(i + 1, j + 1) - l.value, d = val(i, j + 1) - l.value;
        const pts = [];
        if ((a > 0) !== (b > 0)) pts.push([px(i + a / (a - b)), py(j)]);
        if ((b > 0) !== (c2 > 0)) pts.push([px(i + 1), py(j + b / (b - c2))]);
        if ((c2 > 0) !== (d > 0)) pts.push([px(i + 1 - c2 / (c2 - d)), py(j + 1)]);
        if ((d > 0) !== (a > 0)) pts.push([px(i), py(j + 1 - d / (d - a))]);
        for (let k = 0; k + 1 < pts.length; k += 2) { g.moveTo(...pts[k]); g.lineTo(...pts[k + 1]); }
      }
      g.stroke();
    }
    buttons.forEach((b) => b.classList.toggle('active', b.textContent === axis));
    label.textContent = `${axis} = ${index + 1}/${dims[axis]}`;
  }
  const legend = h('div.slice-legend', h('span', tick(signed ? -st.amax : st.lo)),
    h('span.map-ramp', { style: { background: `linear-gradient(90deg, ${[0, 0.25, 0.5, 0.75, 1].map((t) => `rgb(${color(t).join(',')})`).join(', ')})` } }),
    h('span', tick(signed ? st.amax : st.hi)));
  wrap.append(h('div.slice-stage', canvas), h('div.slice-bar', h('span.slice-axes', buttons), slider, label), legend);
  const ro = new ResizeObserver(draw);
  ro.observe(wrap);
  return () => ro.disconnect();
}

export function volume(p, ctx) {
  const gl = webglAvailable();
  const showIso = p.view !== 'slices' && gl;
  const showSlices = p.view !== 'isosurface' || !gl;
  if (!gl && p.view !== 'slices') ctx.issue('volume: WebGL is not available here, so only slices are drawn');
  const iso = showIso ? h('div.mol3d.volume-iso', { style: { height: `${p.height}px` } }) : null;
  const slices = showSlices ? h('div.volume-slices', { style: { height: `${p.height}px` } }) : null;
  const grid = h(`div.volume-grid${showIso && showSlices ? '.two' : ''}`, iso, slices);
  const foot = h('footer.card-foot', `${p.size.join(' × ')} voxels${p.atoms ? ` · ${p.atoms} atom${p.atoms === 1 ? '' : 's'}` : ''}${showIso ? ' · drag to rotate · scroll to zoom' : ''}`);
  ctx.afterMount(async () => {
    const data = await load(p, ctx);
    const st = statsOf(data.data);
    const levels = levelsFor(p, st);
    for (const l of levels)
      if (!(l.value > st.lo && l.value < st.hi)) ctx.issue(`volume: level ${l.value} is outside the data range ${tick(st.lo)} … ${tick(st.hi)}, so it draws nothing`);
    foot.textContent += ` · range ${tick(st.lo)} … ${tick(st.hi)} · levels ${levels.map((l) => tick(l.value)).join(', ')}`;
    const disposers = [];
    if (iso) disposers.push(isoView(iso, data, levels));
    if (slices) disposers.push(sliceView(slices, p, data, st, levels));
    grid.classList.add('live');
    grid.nebulaDispose = () => disposers.forEach((d) => d());
  });
  return card('volume-card', p.title, grid, foot);
}
