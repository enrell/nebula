// A 2D plotting frame on a canvas: axes with nice ticks, data <-> pixel mapping, an optional colour bar and a hover
// read-out. Used by the renderers that draw many primitives per frame (vector fields, animations), where a chart
// library's per-element objects would be too slow.
import { h } from './dom.js';

export const css = (name) => getComputedStyle(document.documentElement).getPropertyValue(`--${name}`).trim();
export const tick = (v) => Number(Number(v).toPrecision(4)).toString();

export function niceTicks(lo, hi, target = 6) {
  const raw = (hi - lo) / target;
  const p = 10 ** Math.floor(Math.log10(raw));
  const step = [1, 2, 2.5, 5, 10].map((m) => m * p).find((s) => s >= raw) ?? raw;
  const out = [];
  for (let v = Math.ceil(lo / step) * step; v <= hi + step * 1e-9; v += step) out.push(Math.abs(v) < step * 1e-9 ? 0 : v);
  return out;
}

// "#rrggbb" / "rgb(...)" / names -> [r, g, b], through the canvas colour parser
const probe = document.createElement('canvas').getContext('2d');
export function rgb(color) {
  probe.fillStyle = '#000';
  probe.fillStyle = color;
  const s = probe.fillStyle;
  if (s.startsWith('#')) return [1, 3, 5].map((i) => parseInt(s.slice(i, i + 2), 16));
  return (s.match(/[\d.]+/g) ?? [0, 0, 0]).slice(0, 3).map(Number);
}

// The theme's sequential ramp (the same stops as the charts' visual maps); t in [0, 1] -> [r, g, b]
export function ramp() {
  const stops = ['blue', 'green', 'yellow', 'red'].map((n) => rgb(css(n)));
  return (t) => {
    const x = Math.min(1, Math.max(0, Number.isFinite(t) ? t : 0)) * (stops.length - 1);
    const i = Math.min(stops.length - 2, Math.floor(x)), f = x - i;
    return stops[i].map((c, k) => Math.round(c + (stops[i + 1][k] - c) * f));
  };
}

// createFrame(box, { x: [lo, hi], y: [lo, hi], xlabel, ylabel, colorbar: { label } | null, readout(x, y) -> text })
// frame.draw(paint) clears, paints the data layer with paint(g, frame) and draws the axes on top.
export function createFrame(box, opts) {
  const canvas = h('canvas.plot-canvas');
  const tip = h('div.plot-tip', { hidden: true });
  box.append(canvas, tip);
  const m = { left: 54, right: opts.colorbar ? 74 : 16, top: 12, bottom: 40 };
  const frame = {
    canvas, width: 0, height: 0, dpr: 1, m,
    x: opts.x, y: opts.y, scale: null, paint: null,
    px: (x) => m.left + ((x - frame.x[0]) / (frame.x[1] - frame.x[0])) * (frame.width - m.left - m.right),
    py: (y) => frame.height - m.bottom - ((y - frame.y[0]) / (frame.y[1] - frame.y[0])) * (frame.height - m.top - m.bottom),
    dx: (px) => frame.x[0] + ((px - m.left) / (frame.width - m.left - m.right)) * (frame.x[1] - frame.x[0]),
    dy: (py) => frame.y[0] + ((frame.height - m.bottom - py) / (frame.height - m.top - m.bottom)) * (frame.y[1] - frame.y[0]),
    plotWidth: () => frame.width - m.left - m.right,
    plotHeight: () => frame.height - m.top - m.bottom,
    draw(paint) {
      frame.paint = paint ?? frame.paint;
      const g = canvas.getContext('2d');
      g.setTransform(frame.dpr, 0, 0, frame.dpr, 0, 0);
      g.clearRect(0, 0, frame.width, frame.height);
      g.save();
      g.beginPath();
      g.rect(m.left, m.top, frame.plotWidth(), frame.plotHeight());
      g.clip();
      frame.paint?.(g, frame);
      g.restore();
      axes(g, frame, opts);
    },
    resize() {
      const r = box.getBoundingClientRect();
      frame.width = Math.max(200, Math.floor(r.width));
      frame.height = Math.max(160, Math.floor(r.height));
      frame.dpr = window.devicePixelRatio || 1;
      canvas.width = Math.round(frame.width * frame.dpr);
      canvas.height = Math.round(frame.height * frame.dpr);
      canvas.style.width = `${frame.width}px`;
      canvas.style.height = `${frame.height}px`;
    },
  };
  frame.resize();
  if (opts.readout) {
    canvas.addEventListener('mousemove', (e) => {
      const r = canvas.getBoundingClientRect();
      const px = e.clientX - r.left, py = e.clientY - r.top;
      const inside = px >= m.left && px <= frame.width - m.right && py >= m.top && py <= frame.height - m.bottom;
      tip.hidden = !inside;
      if (!inside) return;
      tip.textContent = opts.readout(frame.dx(px), frame.dy(py));
      tip.style.left = `${Math.min(px + 14, frame.width - tip.offsetWidth - 4)}px`;
      tip.style.top = `${Math.max(py - 30, 2)}px`;
    });
    canvas.addEventListener('mouseleave', () => { tip.hidden = true; });
  }
  return frame;
}

function axes(g, f, opts) {
  const { m } = f;
  const fg = css('fg'), muted = css('muted'), border = css('border');
  const font = css('font') || 'monospace';
  g.font = `11px ${font}`;
  g.lineWidth = 1;
  g.strokeStyle = border;
  g.strokeRect(m.left + 0.5, m.top + 0.5, f.plotWidth(), f.plotHeight());
  g.fillStyle = muted;
  g.textAlign = 'center';
  g.textBaseline = 'top';
  for (const v of niceTicks(f.x[0], f.x[1], Math.max(3, Math.round(f.plotWidth() / 90)))) {
    const x = Math.round(f.px(v)) + 0.5;
    g.beginPath(); g.moveTo(x, f.height - m.bottom); g.lineTo(x, f.height - m.bottom + 4); g.stroke();
    g.fillText(tick(v), x, f.height - m.bottom + 6);
  }
  g.textAlign = 'right';
  g.textBaseline = 'middle';
  for (const v of niceTicks(f.y[0], f.y[1], Math.max(3, Math.round(f.plotHeight() / 60)))) {
    const y = Math.round(f.py(v)) + 0.5;
    g.beginPath(); g.moveTo(m.left - 4, y); g.lineTo(m.left, y); g.stroke();
    g.fillText(tick(v), m.left - 6, y);
  }
  g.fillStyle = fg;
  g.textAlign = 'center';
  g.textBaseline = 'bottom';
  if (opts.xlabel) g.fillText(opts.xlabel, m.left + f.plotWidth() / 2, f.height - 2);
  if (opts.ylabel) {
    g.save(); g.translate(12, m.top + f.plotHeight() / 2); g.rotate(-Math.PI / 2); g.textBaseline = 'middle'; g.fillText(opts.ylabel, 0, 0); g.restore();
  }
  if (opts.colorbar && f.scale) colorbar(g, f, opts.colorbar, muted, border);
}

// f.scale = { min, max, color(t) -> [r, g, b] }
function colorbar(g, f, cb, muted, border) {
  const { min, max, color } = f.scale;
  const x = f.width - f.m.right + 16, w = 10, top = f.m.top + 18, hgt = f.plotHeight() - 26;
  for (let i = 0; i < hgt; i++) {
    const [r, gg, b] = color(1 - i / hgt);
    g.fillStyle = `rgb(${r},${gg},${b})`;
    g.fillRect(x, top + i, w, 1);
  }
  g.strokeStyle = border;
  g.strokeRect(x + 0.5, top + 0.5, w, hgt);
  g.fillStyle = muted;
  g.textAlign = 'left';
  g.textBaseline = 'middle';
  g.fillText(tick(max), x + w + 4, top);
  g.fillText(tick(min), x + w + 4, top + hgt);
  if (cb.label) { g.textAlign = 'center'; g.textBaseline = 'bottom'; g.fillText(cb.label, x + w / 2, top - 9); }
}

// Resizes and redraws with the box; returns the observer so the owner can disconnect it.
export function follow(box, frame, onResize) {
  const ro = new ResizeObserver(() => { frame.resize(); onResize?.(); frame.draw(); });
  ro.observe(box);
  return ro;
}
