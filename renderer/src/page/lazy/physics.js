// Physics: vector fields (evenly spaced streamlines, arrows, scalar backgrounds) and animations of formulas or
// simulation frames. Drawn on a canvas frame (canvasplot.js); sliders re-sample live.
import { parse } from '../../core/expr.js';
import { parseNpy } from '../../core/npy.js';
import { createFrame, css, follow, ramp, tick } from '../canvasplot.js';
import { sliderBar } from '../controls.js';
import { card, h, readFile } from '../dom.js';

const compile = (src, vars) => {
  const r = parse(String(src), vars);
  if (!r.ok) throw new Error(`${src}: ${r.message}`);
  return r.fn;
};

function live(box, dispose) {
  box.classList.add('live');
  box.nebulaDispose = dispose;
}

// ---- field

// (x, y) -> [u, v] for formulas (with the current params) or a sampled grid (bilinear)
function sampler(p, env) {
  if (p.grid) {
    const { nx, ny, uv } = p.grid;
    return (x, y) => {
      const gx = ((x - p.x[0]) / (p.x[1] - p.x[0])) * (nx - 1), gy = ((y - p.y[0]) / (p.y[1] - p.y[0])) * (ny - 1);
      if (!(gx >= 0 && gx <= nx - 1 && gy >= 0 && gy <= ny - 1)) return [NaN, NaN];
      const i = Math.min(nx - 2, Math.floor(gx)), j = Math.min(ny - 2, Math.floor(gy)), fx = gx - i, fy = gy - j;
      const at = (a, b, k) => uv[(b * nx + a) * 2 + k];
      const lerp = (k) => (at(i, j, k) * (1 - fx) + at(i + 1, j, k) * fx) * (1 - fy) + (at(i, j + 1, k) * (1 - fx) + at(i + 1, j + 1, k) * fx) * fy;
      return [lerp(0), lerp(1)];
    };
  }
  const vars = ['x', 'y', ...Object.keys(p.params)];
  const fu = compile(p.u, vars), fv = compile(p.v, vars);
  return (x, y) => { const s = { ...env, x, y }; return [fu(s), fv(s)]; };
}

// Streamlines in the spirit of Jobard & Lefer: seeds on a grid, integrated both ways with RK4 in pixel space
// (so the spacing is even on screen whatever the axis ranges), stopped where another line already runs close.
function streamlines(frame, field, density) {
  const W = frame.plotWidth(), H = frame.plotHeight();
  const sep = Math.max(6, Math.min(W, H) / (density * 1.15));
  const cols = Math.ceil(W / (sep / 2)), rows = Math.ceil(H / (sep / 2));
  const taken = new Uint8Array(cols * rows);
  const cell = (px, py) => {
    const c = Math.floor((px - frame.m.left) / (sep / 2)), r = Math.floor((py - frame.m.top) / (sep / 2));
    return c >= 0 && r >= 0 && c < cols && r < rows ? r * cols + c : -1;
  };
  // direction in pixel space, unit length
  const dir = (px, py) => {
    const [u, v] = field(frame.dx(px), frame.dy(py));
    const du = (u * W) / (frame.x[1] - frame.x[0]), dv = (-v * H) / (frame.y[1] - frame.y[0]);
    const n = Math.hypot(du, dv);
    return n > 1e-12 && Number.isFinite(n) ? [du / n, dv / n] : null;
  };
  const stepLen = 1.5;
  const trace = (x0, y0, sign, mine) => {
    const pts = [];
    let x = x0, y = y0;
    for (let k = 0; k < 2000; k++) {
      const k1 = dir(x, y); if (!k1) break;
      const k2 = dir(x + (sign * stepLen * k1[0]) / 2, y + (sign * stepLen * k1[1]) / 2); if (!k2) break;
      const k3 = dir(x + (sign * stepLen * k2[0]) / 2, y + (sign * stepLen * k2[1]) / 2); if (!k3) break;
      const k4 = dir(x + sign * stepLen * k3[0], y + sign * stepLen * k3[1]); if (!k4) break;
      const nx = x + (sign * stepLen * (k1[0] + 2 * k2[0] + 2 * k3[0] + k4[0])) / 6;
      const ny = y + (sign * stepLen * (k1[1] + 2 * k2[1] + 2 * k3[1] + k4[1])) / 6;
      const c = cell(nx, ny);
      if (c < 0 || (taken[c] && !mine.has(c))) break;
      // closed orbits: stop when the line comes back to where it started
      if (k > 20 && Math.hypot(nx - x0, ny - y0) < stepLen * 1.2) { pts.push([nx, ny]); break; }
      mine.add(c);
      pts.push([nx, ny]);
      x = nx; y = ny;
    }
    return pts;
  };
  const lines = [];
  const seeds = [];
  for (let r = 0; r < density; r++) for (let c = 0; c < density; c++)
    seeds.push([frame.m.left + ((c + 0.5) / density) * W, frame.m.top + ((r + 0.5) / density) * H]);
  // seed in a fixed shuffled order so lines spread over the whole frame before they fill it in
  for (let i = seeds.length - 1; i > 0; i--) { const j = (i * 7919) % (i + 1); [seeds[i], seeds[j]] = [seeds[j], seeds[i]]; }
  for (const [sx, sy] of seeds) {
    const c0 = cell(sx, sy);
    if (c0 < 0 || taken[c0]) continue;
    const mine = new Set([c0]);
    const back = trace(sx, sy, -1, mine).reverse();
    const fwd = trace(sx, sy, 1, mine);
    const pts = [...back, [sx, sy], ...fwd];
    if (pts.length * stepLen < sep * 1.5) continue;   // stubs squeezed between neighbours only add noise
    // mark a band around the line as taken (the separation distance)
    for (const [px, py] of pts) {
      const c = Math.floor((px - frame.m.left) / (sep / 2)), r = Math.floor((py - frame.m.top) / (sep / 2));
      for (let dr = -1; dr <= 1; dr++) for (let dc = -1; dc <= 1; dc++) {
        const rr = r + dr, cc = c + dc;
        if (rr >= 0 && cc >= 0 && rr < rows && cc < cols) taken[rr * cols + cc] = 1;
      }
    }
    lines.push(pts);
  }
  return lines;
}

export function field(p, ctx) {
  const box = h('div.plot-box', { style: { height: `${p.height}px` } });
  const env = { ...p.params };
  let redraw;
  ctx.afterMount(() => {
    let f = sampler(p, env);
    const vars = ['x', 'y', ...Object.keys(p.params)];
    const bg = p.background && p.background !== 'magnitude' ? compile(p.background, vars) : null;
    const scalar = bg ? (x, y) => bg({ ...env, x, y }) : p.background === 'magnitude' ? (x, y) => Math.hypot(...f(x, y)) : null;
    const frame = createFrame(box, {
      x: p.x, y: p.y, xlabel: p.xlabel, ylabel: p.ylabel,
      colorbar: { label: bg ? p.background.length > 14 ? 'value' : p.background : '|F|' },
      readout: (x, y) => {
        const [u, v] = f(x, y);
        return `${p.xlabel} ${tick(x)}  ${p.ylabel} ${tick(y)}\nu ${tick(u)}  v ${tick(v)}  |F| ${tick(Math.hypot(u, v))}${bg ? `\n${p.background} = ${tick(scalar(x, y))}` : ''}`;
      },
    });
    const color = ramp();
    let lines = [];
    let bgImage = null;
    const compute = () => {
      f = sampler(p, env);
      // colour scale: the 95th percentile of |F| on a grid, so a singularity does not wash everything out
      const mags = [];
      for (let i = 0; i < 40; i++) for (let j = 0; j < 40; j++) {
        const [u, v] = f(p.x[0] + ((i + 0.5) / 40) * (p.x[1] - p.x[0]), p.y[0] + ((j + 0.5) / 40) * (p.y[1] - p.y[0]));
        const m = Math.hypot(u, v);
        if (Number.isFinite(m)) mags.push(m);
      }
      mags.sort((a, b) => a - b);
      const magMax = mags[Math.floor(mags.length * 0.95)] || 1;
      if (scalar) {
        const W = Math.max(2, Math.round(frame.plotWidth() / 3)), H = Math.max(2, Math.round(frame.plotHeight() / 3));
        const vals = new Float64Array(W * H);
        for (let r = 0; r < H; r++) for (let c = 0; c < W; c++)
          vals[r * W + c] = scalar(p.x[0] + ((c + 0.5) / W) * (p.x[1] - p.x[0]), p.y[1] - ((r + 0.5) / H) * (p.y[1] - p.y[0]));
        const finite = [...vals].filter(Number.isFinite).sort((a, b) => a - b);
        const lo = finite[Math.floor(finite.length * 0.02)] ?? 0, hi = finite[Math.ceil(finite.length * 0.98) - 1] ?? 1;
        const img = new ImageData(W, H);
        vals.forEach((v, i) => {
          const [r, g, b] = color((v - lo) / (hi - lo || 1));
          img.data.set([r, g, b, Number.isFinite(v) ? 150 : 0], i * 4);
        });
        bgImage = { img, W, H };
        frame.scale = { min: lo, max: hi, color };
      } else frame.scale = { min: 0, max: magMax, color };
      lines = p.style === 'arrows' ? [] : streamlines(frame, f, p.density);
      return magMax;
    };
    let magMax = compute();
    const paint = (g, fr) => {
      if (bgImage) {
        const off = new OffscreenCanvas(bgImage.W, bgImage.H);
        off.getContext('2d').putImageData(bgImage.img, 0, 0);
        g.imageSmoothingEnabled = true;
        g.drawImage(off, fr.m.left, fr.m.top, fr.plotWidth(), fr.plotHeight());
      }
      const lineColor = (x, y) => {
        if (bgImage) return css('fg');
        const [r, gg, b] = color(Math.hypot(...f(fr.dx(x), fr.dy(y))) / magMax);
        return `rgb(${r},${gg},${b})`;
      };
      g.lineWidth = 1.3;
      g.lineCap = 'round';
      g.lineJoin = 'round';
      for (const pts of lines) {
        // colour changes along the line: draw it in short runs
        for (let i = 0; i < pts.length - 1; i += 6) {
          g.strokeStyle = lineColor(...pts[i]);
          g.beginPath();
          g.moveTo(...pts[i]);
          for (let k = i + 1; k <= Math.min(i + 6, pts.length - 1); k++) g.lineTo(...pts[k]);
          g.stroke();
        }
        // an arrowhead in the middle shows the direction of flow
        const mid = Math.floor(pts.length / 2);
        if (pts.length > 24) arrowHead(g, pts[mid - 2], pts[mid + 2], lineColor(...pts[mid]), 5);
      }
      if (p.style !== 'streamlines') {
        const n = p.density, W = fr.plotWidth(), H = fr.plotHeight(), cellPx = Math.min(W, H) / n;
        for (let r = 0; r < n; r++) for (let c = 0; c < Math.round((n * W) / H); c++) {
          const px = fr.m.left + ((c + 0.5) / Math.round((n * W) / H)) * W, py = fr.m.top + ((r + 0.5) / n) * H;
          const [u, v] = f(fr.dx(px), fr.dy(py));
          const m = Math.hypot(u, v);
          if (!Number.isFinite(m) || m === 0) continue;
          const du = (u * W) / (fr.x[1] - fr.x[0]), dv = (-v * H) / (fr.y[1] - fr.y[0]), dn = Math.hypot(du, dv);
          const len = cellPx * 0.85 * Math.min(1, 0.25 + (0.75 * m) / magMax);
          const x0 = px - ((du / dn) * len) / 2, y0 = py - ((dv / dn) * len) / 2, x1 = px + ((du / dn) * len) / 2, y1 = py + ((dv / dn) * len) / 2;
          const col = bgImage ? css('fg') : lineColor(px, py);
          g.strokeStyle = col;
          g.lineWidth = 1.3;
          g.beginPath(); g.moveTo(x0, y0); g.lineTo(x1, y1); g.stroke();
          arrowHead(g, [x0, y0], [x1, y1], col, Math.max(3, len * 0.3));
        }
      }
    };
    frame.draw(paint);
    const ro = follow(box, frame, () => { magMax = compute(); });
    redraw = () => { magMax = compute(); frame.draw(); };
    live(box, () => ro.disconnect());
  });
  const controls = p.sliders.length ? sliderBar(p.sliders, (name, v) => { env[name] = v; redraw?.(); }) : null;
  return card('chart-card', p.title, controls, box);
}

function arrowHead(g, from, to, color, size) {
  const a = Math.atan2(to[1] - from[1], to[0] - from[0]);
  g.fillStyle = color;
  g.beginPath();
  g.moveTo(to[0], to[1]);
  g.lineTo(to[0] - size * Math.cos(a - 0.45), to[1] - size * Math.sin(a - 0.45));
  g.lineTo(to[0] - size * Math.cos(a + 0.45), to[1] - size * Math.sin(a + 0.45));
  g.closePath();
  g.fill();
}


// ---- animation

const palette = () => ['accent', 'blue', 'green', 'yellow', 'red'].map(css).concat(['#88c0d0', '#d08770', '#8fbcbb']);
const linspace = (a, b, n) => Array.from({ length: n }, (_, i) => a + ((b - a) * i) / (n - 1));

function niceRange(values, pad = 0.06) {
  const v = values.filter(Number.isFinite);
  if (!v.length) return [-1, 1];
  let lo = Math.min(...v), hi = Math.max(...v);
  if (hi === lo) { lo -= 1; hi += 1; }
  const d = (hi - lo) * pad;
  return [lo - d, hi + d];
}

function legend(g, fr, items) {
  g.font = `11px ${css('font') || 'monospace'}`;
  g.textBaseline = 'middle';
  g.textAlign = 'left';
  items.forEach((it, i) => {
    const y = fr.m.top + 12 + i * 16;
    g.fillStyle = it.color;
    g.fillRect(fr.m.left + 10, y - 1, 14, 3);
    g.fillStyle = css('fg');
    g.fillText(it.label, fr.m.left + 30, y);
  });
}

// formula animations: returns { x, y, paint(t) }
function formulaScene(p, env) {
  const vars = ['x', 't', ...Object.keys(p.params)];
  const colors = palette();
  const curves = p.functions.map((f) => ({ fn: compile(f.y, vars), label: f.label }));
  const pts = p.points.map((pt) => ({ fx: compile(pt.x, vars), fy: compile(pt.y, vars), label: pt.label, trail: pt.trail }));
  const ts = linspace(p.t[0], p.t[1], 60);
  const xs = p.x ? linspace(p.x[0], p.x[1], 400) : null;
  const at = (t) => ({ ...env, t });
  let xRange = p.x, yRange = p.y;
  if (!xRange) xRange = niceRange(pts.flatMap((q) => linspace(p.t[0], p.t[1], 400).map((t) => q.fx(at(t)))));
  if (!yRange) {
    const ys = [];
    for (const t of ts) {
      for (const c of curves) for (const x of xs.filter((_, i) => i % 4 === 0)) ys.push(c.fn({ ...env, t, x }));
      for (const q of pts) ys.push(q.fy(at(t)));
    }
    yRange = niceRange(ys);
  }
  if (pts.length && !p.x && !p.y) {   // moving points: same scale on both axes, so an orbit looks round
    const span = Math.max(xRange[1] - xRange[0], yRange[1] - yRange[0]);
    const cx = (xRange[0] + xRange[1]) / 2, cy = (yRange[0] + yRange[1]) / 2;
    xRange = [cx - span / 2, cx + span / 2];
    yRange = [cy - span / 2, cy + span / 2];
  }
  const paint = (t) => (g, fr) => {
    g.lineWidth = 2;
    g.lineJoin = 'round';
    curves.forEach((c, i) => {
      g.strokeStyle = colors[i % colors.length];
      g.beginPath();
      let pen = false;
      for (const x of xs) {
        const y = c.fn({ ...env, t, x });
        if (!Number.isFinite(y)) { pen = false; continue; }
        if (pen) g.lineTo(fr.px(x), fr.py(y)); else { g.moveTo(fr.px(x), fr.py(y)); pen = true; }
      }
      g.stroke();
    });
    pts.forEach((q, i) => {
      const color = colors[(curves.length + i) % colors.length];
      if (q.trail && t > p.t[0]) {
        g.strokeStyle = color;
        g.globalAlpha = 0.45;
        g.lineWidth = 1.5;
        g.beginPath();
        linspace(p.t[0], t, Math.max(2, Math.round(((t - p.t[0]) / (p.t[1] - p.t[0])) * 600))).forEach((s, k) => {
          const v = at(s);
          if (k) g.lineTo(fr.px(q.fx(v)), fr.py(q.fy(v))); else g.moveTo(fr.px(q.fx(v)), fr.py(q.fy(v)));
        });
        g.stroke();
        g.globalAlpha = 1;
      }
      const v = at(t);
      g.fillStyle = color;
      g.beginPath();
      g.arc(fr.px(q.fx(v)), fr.py(q.fy(v)), 5, 0, 2 * Math.PI);
      g.fill();
    });
    legend(g, fr, [...curves.map((c, i) => ({ label: c.label, color: colors[i % colors.length] })),
      ...pts.map((q, i) => ({ label: q.label, color: colors[(curves.length + i) % colors.length] }))]);
  };
  return { x: xRange, y: yRange, paint };
}

// .npy frames: (F, n) a curve per frame, (F, ny, nx) a colour map per frame
async function dataScene(p, ctx) {
  const a = parseNpy(await readFile(ctx.fileUrl(p.file), 'arraybuffer'), { dims: [2, 3] });
  if (a.error) throw new Error(`${p.file}: ${a.error}`);
  const F = a.shape[0], per = a.data.length / F;
  const frameOf = (t) => Math.min(F - 1, Math.max(0, Math.round(((t - p.t[0]) / (p.t[1] - p.t[0])) * (F - 1))));
  const finite = [];
  for (let i = 0; i < a.data.length; i += Math.max(1, Math.floor(a.data.length / 200000))) if (Number.isFinite(a.data[i])) finite.push(a.data[i]);
  finite.sort((x, y) => x - y);
  if (p.kind === 'curve') {
    const n = a.shape[1], xs = linspace(p.x[0], p.x[1], n);
    const color = palette()[0];
    return {
      x: p.x, y: p.y ?? niceRange([finite[0], finite[finite.length - 1]]),
      paint: (t) => (g, fr) => {
        const draw = (f, style, width) => {
          g.strokeStyle = style; g.lineWidth = width;
          g.beginPath();
          for (let i = 0; i < n; i++) { const y = a.data[f * per + i]; if (i) g.lineTo(fr.px(xs[i]), fr.py(y)); else g.moveTo(fr.px(xs[i]), fr.py(y)); }
          g.stroke();
        };
        g.globalAlpha = 0.3; draw(0, css('muted'), 1.2); g.globalAlpha = 1;
        draw(frameOf(t), color, 2);
      },
    };
  }
  const [, ny, nx] = a.shape;
  const lo = finite[Math.floor(finite.length * 0.02)] ?? 0, hi = finite[Math.ceil(finite.length * 0.98) - 1] ?? 1;
  const color = ramp();
  const cache = new Map();
  const image = (f) => {
    if (cache.has(f)) return cache.get(f);
    const img = new ImageData(nx, ny);
    for (let r = 0; r < ny; r++) for (let c = 0; c < nx; c++) {
      const v = a.data[f * per + (ny - 1 - r) * nx + c];   // row 0 is y min, drawn at the bottom
      const [R, G, B] = color((v - lo) / (hi - lo || 1));
      img.data.set([R, G, B, Number.isFinite(v) ? 255 : 0], (r * nx + c) * 4);
    }
    const off = new OffscreenCanvas(nx, ny);
    off.getContext('2d').putImageData(img, 0, 0);
    if (cache.size > 64) cache.delete(cache.keys().next().value);
    cache.set(f, off);
    return off;
  };
  return {
    x: p.x, y: p.y, scale: { min: lo, max: hi, color },
    paint: (t) => (g, fr) => {
      g.imageSmoothingEnabled = nx * 3 < fr.plotWidth();
      g.drawImage(image(frameOf(t)), fr.m.left, fr.m.top, fr.plotWidth(), fr.plotHeight());
    },
  };
}

export function animation(p, ctx) {
  const box = h('div.plot-box', { style: { height: `${p.height}px` } });
  const env = { ...p.params };
  const [t0, t1] = p.t;
  let t = t0, playing = !matchMedia('(prefers-reduced-motion: reduce)').matches, visible = true, last = 0, raf = 0;
  const playBtn = h('button.anim-play', { type: 'button' });
  const scrub = h('input.anim-scrub', { type: 'range', min: 0, max: 1000, step: 1, value: 0, 'aria-label': 'time' });
  const clock = h('output.anim-time');
  let scene, frame;
  const show = () => {
    playBtn.textContent = playing ? '❚❚' : '▶';
    playBtn.setAttribute('aria-label', playing ? 'pause' : 'play');
    scrub.value = String(Math.round(((t - t0) / (t1 - t0)) * 1000));
    clock.textContent = `t = ${tick(t)}`;
    frame?.draw(scene.paint(t));
  };
  const step = (now) => {
    raf = 0;
    if (!playing || !visible) return;
    if (last) {
      t += ((now - last) / 1000) * ((t1 - t0) / p.duration);
      if (t > t1) { if (p.loop) t = t0 + ((t - t0) % (t1 - t0)); else { t = t1; playing = false; } }
    }
    last = now;
    show();
    raf = requestAnimationFrame(step);
  };
  const run = () => { last = 0; if (!raf && playing && visible) raf = requestAnimationFrame(step); };
  playBtn.addEventListener('click', () => { if (!playing && t >= t1) t = t0; playing = !playing; show(); run(); });
  scrub.addEventListener('input', () => { t = t0 + (Number(scrub.value) / 1000) * (t1 - t0); show(); });
  ctx.afterMount(async () => {
    scene = p.kind === 'formula' ? formulaScene(p, env) : await dataScene(p, ctx);
    frame = createFrame(box, { x: scene.x, y: scene.y, xlabel: p.xlabel, ylabel: p.ylabel, colorbar: scene.scale ? { label: '' } : null,
      readout: (x, y) => `${p.xlabel} ${tick(x)}  ${p.ylabel ?? 'y'} ${tick(y)}` });
    frame.scale = scene.scale ?? null;
    show();
    const ro = follow(box, frame);
    // offscreen animations stop drawing
    const io = new IntersectionObserver(([e]) => { visible = e.isIntersecting; run(); });
    io.observe(box);
    live(box, () => { playing = false; cancelAnimationFrame(raf); ro.disconnect(); io.disconnect(); });
    run();
  });
  const controls = p.sliders.length ? sliderBar(p.sliders, (name, v) => {
    env[name] = v;
    if (p.kind === 'formula' && frame) { const s = formulaScene({ ...p, x: frame.x, y: frame.y }, env); scene = s; show(); }
  }) : null;
  return card('chart-card.anim-card', p.title, controls, box, h('div.anim-bar', playBtn, scrub, clock));
}
