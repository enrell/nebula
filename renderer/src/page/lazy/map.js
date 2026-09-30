// Maps: d3-geo projections drawn as SVG, over Natural Earth 1:110m countries bundled with nebula (world-atlas),
// so a map needs no tile server or network. Wheel zooms, drag pans, hover shows the feature's properties.
import { geoArea, geoBounds, geoEqualEarth, geoEquirectangular, geoGraticule10, geoMercator, geoNaturalEarth1, geoOrthographic, geoPath } from 'd3-geo';
import { feature } from 'topojson-client';
import countries from 'world-atlas/countries-110m.json';
import { card, h, readFile } from '../dom.js';

const SVG = 'http://www.w3.org/2000/svg';
const css = (name) => getComputedStyle(document.documentElement).getPropertyValue(`--${name}`).trim();
const svg = (tag, attrs = {}) => { const el = document.createElementNS(SVG, tag); for (const [k, v] of Object.entries(attrs)) if (v !== undefined) el.setAttribute(k, v); return el; };
const tick = (v) => Number(Number(v).toPrecision(4)).toLocaleString();
const escape = (s) => String(s).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' })[c]);
let world;

const PROJECTIONS = { 'equal-earth': geoEqualEarth, 'natural-earth': geoNaturalEarth1, mercator: geoMercator, equirectangular: geoEquirectangular, orthographic: geoOrthographic };

function palette() {
  return ['accent', 'blue', 'green', 'yellow', 'red'].map(css).concat(['#88c0d0', '#d08770', '#b48ead', '#8fbcbb', '#5e81ac', '#bf616a', '#a3be8c']);
}

// sequential ramp between the theme's blue, green, yellow and red
function rampOf() {
  const probe = document.createElement('canvas').getContext('2d');
  const rgb = (c) => { probe.fillStyle = c; const s = probe.fillStyle; return [1, 3, 5].map((i) => parseInt(s.slice(i, i + 2), 16)); };
  const stops = ['blue', 'green', 'yellow', 'red'].map((n) => rgb(css(n)));
  return (t) => {
    const x = Math.min(1, Math.max(0, t)) * (stops.length - 1), i = Math.min(stops.length - 2, Math.floor(x)), f = x - i;
    return `rgb(${stops[i].map((c, k) => Math.round(c + (stops[i + 1][k] - c) * f)).join(',')})`;
  };
}

// d3-geo reads polygon rings on the sphere (clockwise exterior); RFC 7946 GeoJSON winds them the other way. A polygon
// that covers more than a hemisphere was meant as its complement: reverse its rings. Works for either convention.
function rewind(geometry) {
  if (!geometry) return geometry;
  const fix = (rings) => (geoArea({ type: 'Polygon', coordinates: rings }) > 2 * Math.PI ? rings.map((r) => [...r].reverse()) : rings);
  if (geometry.type === 'Polygon') return { ...geometry, coordinates: fix(geometry.coordinates) };
  if (geometry.type === 'MultiPolygon') return { ...geometry, coordinates: geometry.coordinates.map(fix) };
  if (geometry.type === 'GeometryCollection') return { ...geometry, geometries: geometry.geometries.map(rewind) };
  return geometry;
}

export function map(p, ctx) {
  const box = h('div.map-box', { style: { height: `${p.height}px` } });
  const tip = h('div.plot-tip', { hidden: true });
  const legend = h('div.map-legend');
  box.append(tip);
  ctx.afterMount(async () => {
    world ??= feature(countries, countries.objects.countries);
    const data = p.file ? JSON.parse(await readFile(ctx.fileUrl(p.file))) : null;
    const features = (data ? (data.type === 'FeatureCollection' ? data.features : data.type === 'Feature' ? [data] : [{ type: 'Feature', geometry: data, properties: {} }]) : [])
      .map((f) => ({ ...f, geometry: rewind(f.geometry) }));
    const pointsFc = { type: 'FeatureCollection', features: p.points.map((q) => ({ type: 'Feature', geometry: { type: 'Point', coordinates: [q.lon, q.lat] }, properties: q })) };
    const content = { type: 'FeatureCollection', features: [...features.filter((f) => f.geometry), ...pointsFc.features] };
    // what to fit: the data (with some margin), or the world when there is none or it spans most of it
    const [[w, s], [e, n]] = content.features.length ? geoBounds(content) : [[-180, -60], [180, 85]];
    const global = !content.features.length || e - w > 200 || (e < w && w - e < 160) || n - s > 110;
    const kind = p.projection === 'auto' ? (global ? 'equal-earth' : 'mercator') : p.projection;
    const colors = palette();
    const ramp = rampOf();
    const fill = (f) => {
      if (!p.color) return css('accent');
      const v = f.properties?.[p.color.key];
      if (v === undefined || v === null || v === '') return null;   // no value: regions stay neutral, lines and points take the accent
      if (p.color.numeric) { const [lo, hi] = p.color.range; return ramp((Number(v) - lo) / (hi - lo || 1)); }
      const i = p.color.categories.indexOf(String(v));
      return i < 0 ? css('muted') : colors[i % colors.length];
    };
    const values = p.points.map((q) => q.value).filter((v) => v !== undefined);
    const vmax = values.length ? Math.max(...values.map(Math.abs)) : 0;
    const radius = (q) => (q.value !== undefined && vmax > 0 ? 3 + 11 * Math.sqrt(Math.abs(q.value) / vmax) : 5);

    const root = svg('svg', { class: 'map-svg' });
    box.prepend(root);
    let zoom = { k: 1, x: 0, y: 0 };
    const layer = svg('g');
    root.append(layer);
    const draw = () => {
      const W = box.clientWidth, H = p.height;
      root.setAttribute('viewBox', `0 0 ${W} ${H}`);
      root.setAttribute('width', W);
      root.setAttribute('height', H);
      const projection = PROJECTIONS[kind]();
      if (kind === 'orthographic') projection.rotate([-(w + (e >= w ? e - w : e + 360 - w) / 2), -(s + n) / 2]);
      const pad = global ? 12 : Math.min(W, H) * 0.08;
      projection.fitExtent([[pad, pad], [W - pad, H - pad]], global ? { type: 'Sphere' } : content);
      const path = geoPath(projection);
      layer.replaceChildren();
      layer.append(svg('path', { d: path({ type: 'Sphere' }), class: 'map-sphere' }));
      layer.append(svg('path', { d: path(geoGraticule10()), class: 'map-graticule' }));
      if (p.basemap === 'world') layer.append(svg('path', { d: path(world), class: 'map-land' }));
      for (const f of features) {
        if (!f.geometry) continue;
        const isPoint = f.geometry.type === 'Point' || f.geometry.type === 'MultiPoint';
        const line = /LineString/.test(f.geometry.type);
        const el = isPoint ? svg('circle', { r: 4.5, class: 'map-point' }) : svg('path', { d: path(f), class: line ? 'map-line' : 'map-region' });
        if (isPoint) { const [x, y] = projection(f.geometry.type === 'Point' ? f.geometry.coordinates : f.geometry.coordinates[0]) ?? [-99, -99]; el.setAttribute('cx', x); el.setAttribute('cy', y); }
        const color = fill(f) ?? (isPoint || line ? css('accent') : css('muted'));
        if (line) el.style.stroke = color; else el.style.fill = color;
        el.addEventListener('mousemove', (ev) => showTip(ev, f.properties ?? {}));
        el.addEventListener('mouseleave', hideTip);
        layer.append(el);
      }
      for (const q of p.points) {
        const xy = projection([q.lon, q.lat]);
        if (!xy) continue;
        const c = svg('circle', { cx: xy[0], cy: xy[1], r: radius(q), class: 'map-marker' });
        c.addEventListener('mousemove', (ev) => showTip(ev, { name: q.label, value: q.value, lat: q.lat, lon: q.lon }));
        c.addEventListener('mouseleave', hideTip);
        layer.append(c);
        if (q.label && p.points.length <= 40) layer.append(Object.assign(svg('text', { x: xy[0] + radius(q) + 4, y: xy[1] + 4, class: 'map-label' }), { textContent: q.label }));
      }
      apply();
    };
    const apply = () => {
      layer.setAttribute('transform', `translate(${zoom.x},${zoom.y}) scale(${zoom.k})`);
      layer.style.setProperty('--k', zoom.k);
    };
    const showTip = (ev, props) => {
      const name = props[p.label] ?? props.name;
      const rows = Object.entries(props).filter(([k, v]) => k !== p.label && k !== 'name' && v !== undefined && v !== null && typeof v !== 'object').slice(0, 8);
      tip.innerHTML = `${name !== undefined ? `<b>${escape(name)}</b>\n` : ''}${rows.map(([k, v]) => `${escape(k)}: ${escape(typeof v === 'number' ? tick(v) : v)}`).join('\n')}`;
      tip.hidden = false;
      const r = box.getBoundingClientRect();
      tip.style.left = `${Math.min(ev.clientX - r.left + 14, r.width - tip.offsetWidth - 4)}px`;
      tip.style.top = `${Math.max(ev.clientY - r.top - 12, 2)}px`;
    };
    const hideTip = () => { tip.hidden = true; };
    root.addEventListener('wheel', (ev) => {
      ev.preventDefault();
      const r = root.getBoundingClientRect(), mx = ev.clientX - r.left, my = ev.clientY - r.top;
      const k = Math.min(40, Math.max(1, zoom.k * Math.exp(-ev.deltaY * 0.0015)));
      zoom = { k, x: mx - ((mx - zoom.x) * k) / zoom.k, y: my - ((my - zoom.y) * k) / zoom.k };
      if (k === 1) zoom = { k: 1, x: 0, y: 0 };
      apply();
    }, { passive: false });
    let drag = null;
    root.addEventListener('pointerdown', (ev) => { drag = { x: ev.clientX - zoom.x, y: ev.clientY - zoom.y }; root.setPointerCapture(ev.pointerId); });
    root.addEventListener('pointermove', (ev) => { if (drag) { zoom = { ...zoom, x: ev.clientX - drag.x, y: ev.clientY - drag.y }; apply(); } });
    root.addEventListener('pointerup', () => { drag = null; });
    root.addEventListener('dblclick', () => { zoom = { k: 1, x: 0, y: 0 }; apply(); });
    draw();
    const ro = new ResizeObserver(draw);
    ro.observe(box);
    box.classList.add('live');
    box.nebulaDispose = () => ro.disconnect();
    if (p.color?.numeric) {
      const stops = [0, 0.25, 0.5, 0.75, 1].map((t) => ramp(t)).join(', ');
      legend.append(h('span', p.color.key), h('span', tick(p.color.range[0])), h('span.map-ramp', { style: { background: `linear-gradient(90deg, ${stops})` } }), h('span', tick(p.color.range[1])));
    } else if (p.color) {
      legend.append(h('span', p.color.key), ...p.color.categories.map((c, i) => h('span.map-cat', h('i', { style: { background: colors[i % colors.length] } }), c)));
    }
  });
  const foot = [p.features !== undefined ? `${p.features} feature${p.features === 1 ? '' : 's'}` : null, p.points.length ? `${p.points.length} point${p.points.length === 1 ? '' : 's'}` : null,
    'scroll to zoom · drag to pan · double-click to reset'].filter(Boolean).join(' · ');
  return card('map-card', p.title, legend, box, h('footer.card-foot', foot));
}
