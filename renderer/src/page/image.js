// Images: plain, or a viewer with zoom and pan, a before/after comparison slider and a scale bar that stays true
// at every zoom (the checker gives the physical size of one image pixel).
import { h } from './dom.js';

const NICE = [1, 2, 5];
// a round bar length (in the scale's unit) close to `target`
function niceLength(target) {
  const p = 10 ** Math.floor(Math.log10(target));
  return NICE.map((m) => m * p).concat(10 * p).reduce((best, v) => (Math.abs(Math.log(v / target)) < Math.abs(Math.log(best / target)) ? v : best));
}
const fmt = (v) => Number(v.toPrecision(3)).toLocaleString();

export function image(p, ctx) {
  const load = (src) => {
    const img = h('img', { src: ctx.fileUrl(src), alt: p.alt ?? '', draggable: 'false' });
    img.addEventListener('error', () => ctx.issue(`image ${src} could not be displayed`));
    return img;
  };
  const caption = p.caption ? h('figcaption', ctx.inline(p.caption)) : null;
  if (!p.compare && !p.zoom && !p.scale) {
    const img = load(p.src);
    img.loading = 'lazy';
    if (p.width) img.style.maxWidth = `${p.width}px`;
    return h('figure.image', img, caption);
  }

  const base = load(p.src);
  const top = p.compare ? load(p.compare) : null;
  const layer = h('div.iv-layer', base, top ? h('div.iv-top', top) : null);
  const stage = h('div.iv-stage', layer);
  const viewer = h('div.iv', { style: p.width ? { maxWidth: `${p.width}px` } : null }, stage);
  let zoom = { k: 1, x: 0, y: 0 }, split = 0.5;
  const apply = () => {
    layer.style.transform = `translate(${zoom.x}px, ${zoom.y}px) scale(${zoom.k})`;
    if (top) top.parentElement.style.clipPath = `inset(0 0 0 ${split * 100}%)`;
    if (divider) divider.style.left = `${zoom.x + split * layer.offsetWidth * zoom.k}px`;
    drawBar();
  };

  // comparison: the second image is revealed to the right of the divider
  let divider = null;
  if (top) {
    const [a, b] = p.labels ?? ['', ''];
    divider = h('div.iv-divider', h('span.iv-handle', '⇔'));
    const slider = h('input.iv-slider', { type: 'range', min: 0, max: 1000, value: 500, 'aria-label': `${a || 'first'} / ${b || 'second'}` });
    slider.addEventListener('input', () => { split = Number(slider.value) / 1000; apply(); });
    viewer.append(divider, a ? h('span.iv-label.left', a) : null, b ? h('span.iv-label.right', b) : null, slider);
  }

  // scale bar: a round length that is about a fifth of the visible width
  const bar = p.scale ? h('div.iv-scale', h('div.iv-scale-line'), h('span.iv-scale-text')) : null;
  if (bar) viewer.append(bar);
  function drawBar() {
    if (!bar || !base.naturalWidth) return;
    const shownPerPixel = (layer.offsetWidth * zoom.k) / base.naturalWidth;   // screen px per image px
    const unitsPerScreenPx = p.scale.size / shownPerPixel;
    const len = niceLength(stage.clientWidth * 0.2 * unitsPerScreenPx);
    bar.firstChild.style.width = `${len / unitsPerScreenPx}px`;
    bar.lastChild.textContent = `${fmt(len)} ${p.scale.unit}`;
  }

  if (p.zoom) {
    viewer.classList.add('zoomable');
    stage.addEventListener('wheel', (e) => {
      e.preventDefault();
      const r = stage.getBoundingClientRect(), mx = e.clientX - r.left, my = e.clientY - r.top;
      const k = Math.min(32, Math.max(1, zoom.k * Math.exp(-e.deltaY * 0.0015)));
      zoom = k === 1 ? { k: 1, x: 0, y: 0 } : { k, x: mx - ((mx - zoom.x) * k) / zoom.k, y: my - ((my - zoom.y) * k) / zoom.k };
      apply();
    }, { passive: false });
    let drag = null;
    stage.addEventListener('pointerdown', (e) => { drag = { x: e.clientX - zoom.x, y: e.clientY - zoom.y }; stage.setPointerCapture(e.pointerId); });
    stage.addEventListener('pointermove', (e) => { if (drag) { zoom = { ...zoom, x: e.clientX - drag.x, y: e.clientY - drag.y }; apply(); } });
    stage.addEventListener('pointerup', () => { drag = null; });
    stage.addEventListener('dblclick', () => { zoom = { k: 1, x: 0, y: 0 }; apply(); });
    // pixels stay sharp when enlarged: measurements and segmentations are about individual pixels
    base.classList.add('pixelated');
    top?.classList.add('pixelated');
  }
  // shown at its own size (or the given width), up to the card; zoom is the way to enlarge
  base.addEventListener('load', () => { if (!p.width) viewer.style.maxWidth = `${Math.max(360, base.naturalWidth)}px`; apply(); });
  ctx.afterMount(() => {
    const ro = new ResizeObserver(apply);
    ro.observe(stage);
    viewer.classList.add('live');
    viewer.nebulaDispose = () => ro.disconnect();
    apply();
  });
  const hint = [p.zoom ? 'scroll to zoom · drag to pan · double-click to reset' : null, p.compare ? 'slide to compare' : null].filter(Boolean).join(' · ');
  return h('figure.image', viewer, hint ? h('div.iv-hint', hint) : null, caption);
}
