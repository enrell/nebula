// Charts: ECharts (canvas) for 2D, echarts-gl (WebGL) for 3D. Colours come from the nebula theme (CSS variables),
// so a chart re-created after a theme change matches the new theme.
import { BarChart, BoxplotChart, CustomChart, GraphChart, HeatmapChart, LineChart, PieChart, ScatterChart } from 'echarts/charts';
import { DataZoomComponent, GridComponent, LegendComponent, PolarComponent, TitleComponent, TooltipComponent, VisualMapComponent } from 'echarts/components';
import * as echarts from 'echarts/core';
import { CanvasRenderer } from 'echarts/renderers';
import { parse } from '../../core/expr.js';
import { fit } from '../../core/fit.js';
import { sliderBar } from '../controls.js';
import { card, h } from '../dom.js';
import { Bar3DChart, Line3DChart, Scatter3DChart, SurfaceChart } from 'echarts-gl/charts';
import { Grid3DComponent } from 'echarts-gl/components';

echarts.use([LineChart, BarChart, PieChart, ScatterChart, BoxplotChart, CustomChart, HeatmapChart, GraphChart, GridComponent, TooltipComponent, LegendComponent, TitleComponent,
  DataZoomComponent, VisualMapComponent, PolarComponent, CanvasRenderer, Scatter3DChart, Bar3DChart, Line3DChart, SurfaceChart, Grid3DComponent]);

const tick = (v) => Number(Number(v).toPrecision(4)).toString();
const css = (name) => getComputedStyle(document.documentElement).getPropertyValue(`--${name}`).trim();

function palette() {
  return ['accent', 'blue', 'green', 'yellow', 'red'].map(css).concat(['#88c0d0', '#d08770', '#b48ead', '#8fbcbb']);
}

function base() {
  const fg = css('fg'), muted = css('muted'), border = css('border');
  return {
    fg, muted, border,
    textStyle: { color: fg, fontFamily: css('font') },
    tooltip: { backgroundColor: css('panel'), borderColor: border, textStyle: { color: fg, fontFamily: css('font') } },
    axis: { axisLine: { lineStyle: { color: border } }, axisTick: { lineStyle: { color: border } }, axisLabel: { color: muted },
      splitLine: { lineStyle: { color: border, opacity: 0.6 } }, nameTextStyle: { color: muted } },
  };
}

// Keeps a chart sized to its box (panes and the modal resize) and cleans up when the node leaves the page.
function mount(el, option) {
  const chart = echarts.init(el, null, { renderer: 'canvas' });
  chart.setOption(option);
  const ro = new ResizeObserver(() => chart.resize());
  ro.observe(el);
  el.classList.add('live');
  el.nebulaDispose = () => { ro.disconnect(); chart.dispose(); };
  return chart;
}

const webglAvailable = () => { try { return !!document.createElement('canvas').getContext('webgl'); } catch { return false; } };

// ---- chart

const quantile = (sorted, q) => {
  const i = (sorted.length - 1) * q, lo = Math.floor(i), hi = Math.ceil(i);
  return sorted[lo] + (sorted[hi] - sorted[lo]) * (i - lo);
};

function histogramBins(values, bins) {
  const v = [...values].sort((a, b) => a - b);
  const lo = v[0], hi = v[v.length - 1];
  if (!bins) {   // Freedman–Diaconis, clamped
    const iqr = quantile(v, 0.75) - quantile(v, 0.25);
    const w = 2 * iqr / Math.cbrt(v.length);
    bins = Math.min(80, Math.max(5, w > 0 ? Math.ceil((hi - lo) / w) : 10));
  }
  const width = (hi - lo) / bins || 1;
  return { lo, width, bins };
}

function errorBars(name, points, color) {
  // points: [x, y, err]; draws a vertical whisker with caps through each point
  return {
    name, type: 'custom', z: 3, silent: true, legendHoverLink: false, tooltip: { show: false },
    renderItem: (params, api) => {
      const x = api.value(0), y = api.value(1), e = api.value(2);
      const top = api.coord([x, y + e]), bottom = api.coord([x, y - e]);
      const cap = Math.min(6, api.size([1, 0])[0] * 0.2 || 6);
      const style = { stroke: color, lineWidth: 1.4 };
      return { type: 'group', children: [
        { type: 'line', shape: { x1: top[0], y1: top[1], x2: bottom[0], y2: bottom[1] }, style },
        { type: 'line', shape: { x1: top[0] - cap, y1: top[1], x2: top[0] + cap, y2: top[1] }, style },
        { type: 'line', shape: { x1: bottom[0] - cap, y1: bottom[1], x2: bottom[0] + cap, y2: bottom[1] }, style },
      ] };
    },
    data: points.filter((q) => q[1] !== null && q[2] !== null),
  };
}

export function renderChart(el, p) {
  const b = base();
  const colors = palette();
  const fmt = (v) => (v === null || v === undefined ? '–' : `${Number(v).toLocaleString()}${p.unit}`);
  const valueAxis = (log, name) => ({ type: log ? 'log' : 'value', name, ...b.axis, scale: !log, axisLabel: { ...b.axis.axisLabel, formatter: (v) => `${Number(v).toLocaleString()}${p.unit}` } });
  const common = { color: colors, textStyle: b.textStyle };

  if (p.type === 'pie') {
    return mount(el, {
      ...common, tooltip: { ...b.tooltip, trigger: 'item', valueFormatter: fmt },
      legend: { bottom: 0, textStyle: { color: b.muted }, type: 'scroll' },
      series: [{ type: 'pie', radius: ['38%', '68%'], center: ['50%', '45%'], itemStyle: { borderColor: css('panel'), borderWidth: 2 },
        label: { color: b.fg }, data: p.xs.map((name, i) => ({ name, value: p.series[0].values[i] })) }],
    });
  }
  if (p.type === 'histogram') {
    const all = p.series.flatMap((s) => s.values);
    const { lo, width, bins } = histogramBins(all, p.bins);
    const series = p.series.map((s) => {
      const counts = new Array(bins).fill(0);
      for (const v of s.values) counts[Math.min(bins - 1, Math.floor((v - lo) / width))]++;
      return { name: s.name, type: 'bar', barGap: '-100%', barCategoryGap: '4%', itemStyle: { opacity: p.series.length > 1 ? 0.6 : 0.9 },
        data: counts.map((c, i) => [lo + (i + 0.5) * width, c]), barWidth: '96%' };
    });
    return mount(el, {
      ...common, tooltip: { ...b.tooltip, trigger: 'axis', axisPointer: { type: 'shadow' },
        formatter: (items) => `${tick(items[0].value[0] - width / 2)} – ${tick(items[0].value[0] + width / 2)}${p.unit}<br>` + items.map((i) => `${i.marker}${i.seriesName}: ${i.value[1]}`).join('<br>') },
      legend: p.series.length > 1 ? { top: 0, textStyle: { color: b.muted } } : undefined,
      grid: { left: 12, right: 18, top: p.series.length > 1 ? 34 : 14, bottom: 30, containLabel: true },
      xAxis: { type: 'value', min: lo, max: lo + bins * width, ...b.axis, splitLine: { show: false },
        axisLabel: { ...b.axis.axisLabel, showMinLabel: false, showMaxLabel: false, formatter: (v) => `${tick(v)}${p.unit}` } },
      yAxis: { type: p.logy ? 'log' : 'value', name: 'count', ...b.axis, minInterval: 1 },
      series,
    });
  }
  if (p.type === 'box') {
    const stats = p.groups.map((g) => {
      const v = [...g.values].sort((a, c) => a - c);
      const q1 = quantile(v, 0.25), q2 = quantile(v, 0.5), q3 = quantile(v, 0.75), iqr = q3 - q1;
      const lo = v.find((x) => x >= q1 - 1.5 * iqr), hi = [...v].reverse().find((x) => x <= q3 + 1.5 * iqr);
      return { box: [lo, q1, q2, q3, hi], outliers: v.filter((x) => x < lo || x > hi), n: v.length };
    });
    return mount(el, {
      ...common,
      tooltip: { ...b.tooltip, trigger: 'item', formatter: (i) => (i.seriesType === 'boxplot'
        ? `${i.name} (n = ${stats[i.dataIndex].n})<br>max ${tick(i.value[5])}<br>Q3 ${tick(i.value[4])}<br>median ${tick(i.value[3])}<br>Q1 ${tick(i.value[2])}<br>min ${tick(i.value[1])}`
        : `${i.name}: ${tick(i.value[1])} (outlier)`) },
      grid: { left: 12, right: 18, top: 14, bottom: 30, containLabel: true },
      xAxis: { type: 'category', data: p.groups.map((g) => g.name), ...b.axis },
      yAxis: { ...valueAxis(p.logy, p.yName) },
      series: [
        { name: 'box', type: 'boxplot', data: stats.map((s) => s.box), itemStyle: { color: 'transparent', borderColor: colors[0], borderWidth: 1.6 } },
        { name: 'outliers', type: 'scatter', symbolSize: 6, itemStyle: { color: colors[1] },
          data: stats.flatMap((s, i) => s.outliers.map((v) => [p.groups[i].name, v])) },
      ],
    });
  }
  if (p.type === 'heatmap') {
    const vals = p.cells.map((c) => c[2]).filter((v) => v !== null);
    return mount(el, {
      ...common, tooltip: { ...b.tooltip, formatter: (i) => `${p.xName} ${i.value[0]}, ${p.yName} ${i.value[1]}<br>${p.valueName}: ${tick(i.value[2])}${p.unit}` },
      grid: { left: 12, right: 70, top: 10, bottom: 30, containLabel: true },
      xAxis: { type: 'category', data: p.xs, name: p.xName, nameLocation: 'middle', nameGap: 26, ...b.axis, splitArea: { show: false } },
      yAxis: { type: 'category', data: p.ysCat, name: p.yName, ...b.axis },
      visualMap: { min: Math.min(...vals), max: Math.max(...vals), calculable: true, orient: 'vertical', right: 4, top: 'middle', itemHeight: 140,
        textStyle: { color: b.muted }, inRange: { color: [css('panel'), css('blue'), css('accent'), css('yellow')] } },
      series: [{ type: 'heatmap', data: p.cells, itemStyle: { borderColor: css('bg'), borderWidth: 1 }, emphasis: { itemStyle: { borderColor: css('fg') } } }],
    });
  }

  // line, bar, area, scatter (+ error bars, band, fit)
  const kind = p.type === 'area' ? 'line' : p.type;
  const many = p.xs.length > 60;
  const catAxis = { type: p.xNumeric ? (p.logx ? 'log' : 'value') : 'category', data: p.xNumeric ? undefined : p.xs, name: p.xName, nameLocation: 'middle', nameGap: 28,
    ...b.axis, scale: p.xNumeric && !p.logx, splitLine: { show: p.xNumeric, ...b.axis.splitLine }, boundaryGap: p.type === 'bar' };
  const valAxis = valueAxis(p.logy);
  const point = (s, i) => (p.xNumeric ? [p.xs[i], s.values[i]] : s.values[i]);
  const series = [];
  const legend = [];
  p.series.forEach((s, k) => {
    legend.push(s.name);
    series.push({
      name: s.name, type: kind, color: colors[k % colors.length], stack: p.stack ? 'all' : undefined, showSymbol: kind === 'line' ? p.xs.length <= 40 : undefined,
      symbolSize: kind === 'scatter' ? 8 : 5, areaStyle: p.type === 'area' ? { opacity: 0.25 } : undefined, barMaxWidth: 36,
      emphasis: { focus: 'series' }, data: s.values.map((_, i) => point(s, i)),
    });
    if (s.errors && p.type !== 'area' && !p.horizontal) {
      const x = (i) => (p.xNumeric ? p.xs[i] : i);
      series.push(errorBars(s.name, s.values.map((v, i) => [x(i), v, s.errors[i]]), colors[k % colors.length]));
    }
    if (p.fit) {
      const f = fit(p.fit, p.xs, s.values);
      if (f.error) throw new Error(`fit ${p.fit} of ${s.name}: ${f.error}`);
      const finite = p.xs.filter(Number.isFinite);
      const [x0, x1] = [Math.min(...finite), Math.max(...finite)];
      const xsFit = Array.from({ length: 200 }, (_, i) => (p.logx ? x0 * (x1 / x0) ** (i / 199) : x0 + ((x1 - x0) * i) / 199));
      const name = `${f.label}   R² = ${f.r2.toFixed(4)}`;
      legend.push(name);
      series.push({ name, type: 'line', showSymbol: false, lineStyle: { type: 'dashed', width: 1.6 }, color: colors[(k + p.series.length) % colors.length],
        data: xsFit.map((x) => [x, f.predict(x)]), tooltip: { show: false } });
    }
  });
  if (p.band) {
    // one polygon: along `low` left to right, back along `high`
    const idx = p.xs.map((_, i) => i).filter((i) => p.band.low[i] !== null && p.band.high[i] !== null && Number.isFinite(p.xs[i])).sort((a, c) => p.xs[a] - p.xs[c]);
    const bandColor = css('accent');
    series.unshift({
      name: p.band.label, type: 'custom', silent: true, z: 1, color: bandColor, tooltip: { show: false }, data: [0],
      renderItem: (params, api) => ({
        type: 'polygon', silent: true,
        shape: { points: [...idx.map((i) => api.coord([p.xs[i], p.band.low[i]])), ...[...idx].reverse().map((i) => api.coord([p.xs[i], p.band.high[i]]))] },
        style: { fill: bandColor, opacity: 0.16 },
      }),
    });
    legend.push(p.band.label);
  }
  return mount(el, {
    ...common, tooltip: { ...b.tooltip, trigger: p.type === 'scatter' ? 'item' : 'axis', valueFormatter: fmt },
    legend: legend.length > 1 ? { top: 0, textStyle: { color: b.muted }, type: 'scroll', data: legend } : undefined,
    grid: { left: 12, right: 18, top: legend.length > 1 ? 34 : 14, bottom: many ? 58 : 34, containLabel: true },
    xAxis: p.horizontal ? valAxis : catAxis,
    yAxis: p.horizontal ? { ...catAxis, nameLocation: 'end', nameGap: 12 } : valAxis,
    dataZoom: many ? [{ type: 'inside' }, { type: 'slider', height: 16, bottom: 6, borderColor: b.border, textStyle: { color: b.muted } }] : undefined,
    series,
  });
}

export function render3d(el, p) {
  const b = base();
  const axis = (name) => ({ type: 'value', name, nameTextStyle: { color: b.muted }, axisLine: { lineStyle: { color: b.muted } },
    axisLabel: { textStyle: { color: b.muted } }, splitLine: { lineStyle: { color: b.border } }, axisPointer: { lineStyle: { color: css('accent') } } });
  const series = { type: p.type === 'surface' ? 'surface' : `${p.type}3D`, data: p.type === 'surface' ? p.points.map((q) => q.slice(0, 3)) : p.points, shading: 'lambert',
    emphasis: { itemStyle: { color: css('accent') } } };
  if (p.type === 'scatter') Object.assign(series, { symbolSize: 7, itemStyle: { opacity: 0.9 } });
  if (p.type === 'bar') Object.assign(series, { barSize: 1.2, bevelSize: 0.2 });
  if (p.type === 'line') Object.assign(series, { lineStyle: { width: 3 } });
  if (p.type === 'surface') Object.assign(series, { wireframe: { show: true, lineStyle: { color: b.border, opacity: 0.6 } } });
  return mount(el, {
    textStyle: b.textStyle,
    tooltip: { ...b.tooltip, formatter: (i) => `${p.names.x}: ${i.value[0]}<br>${p.names.y}: ${i.value[1]}<br>${p.names.z}: ${i.value[2]}` },
    // surfaces take [x, y, z] only: their colour follows z; the other types carry the colour column as dimension 3
    visualMap: { show: true, dimension: p.type === 'surface' ? 2 : 3, min: p.colorRange[0], max: p.colorRange[1], right: 6, top: 'middle', itemHeight: 120, calculable: true,
      text: [p.names.color, ''], textStyle: { color: b.muted }, inRange: { color: [css('blue'), css('green'), css('yellow'), css('red')] } },
    xAxis3D: axis(p.names.x), yAxis3D: axis(p.names.y), zAxis3D: axis(p.names.z),
    grid3D: { boxWidth: 100, boxDepth: 100, boxHeight: 70, environment: 'none',
      viewControl: { autoRotate: !!p.rotate, autoRotateSpeed: 18, autoRotateAfterStill: 6, distance: 185, alpha: 24, beta: 38 },
      light: { main: { intensity: 1.1, shadow: false, alpha: 40, beta: 30 }, ambient: { intensity: 0.35 } },
      axisLine: { lineStyle: { color: b.muted } }, splitLine: { lineStyle: { color: b.border } }, axisPointer: { lineStyle: { color: css('accent') } } },
    series: [series],
  });
}

export function chart(p, ctx) {
  const box = h('div.chart-box', { style: { height: `${p.height}px` } });
  ctx.afterMount(() => renderChart(box, p));
  return card('chart-card', p.title, box);
}

export function chart3d(p, ctx) {
  if (!webglAvailable()) {
    ctx.issue('chart3d: WebGL is not available in this view (no GPU / GL support)');
    return card('chart-card', p.title, h('div.unavailable', 'WebGL is not available here, so this 3D chart cannot be drawn.'));
  }
  const box = h('div.chart-box', { style: { height: `${p.height}px` } });
  ctx.afterMount(() => render3d(box, p));
  return card('chart-card', p.title, box, h('footer.card-foot', 'drag to rotate · scroll to zoom'));
}


// ---- plot: formulas sampled here, with the same expression language the checker validated them with


const VARS = { function: ['x'], parametric: ['t'], polar: ['t', 'theta'], surface: ['x', 'y'] };

function compile(p, source) {
  const r = parse(source, [...VARS[p.type], ...Object.keys(p.params)]);
  if (!r.ok) throw new Error(`${source}: ${r.message}`);
  return r.fn;
}

const linspace = (a, b, n) => Array.from({ length: n }, (_, i) => a + ((b - a) * i) / (n - 1));

// The view range: everything, unless a few values run off towards a pole (tan, 1/x), then the middle 96%.
// Bounds are rounded to a "nice" step so the axis reads 1, 0.5, 0 rather than 0.7465.
function autoRange(values) {
  const v = values.filter(Number.isFinite).sort((a, b) => a - b);
  if (!v.length) return [-1, 1];
  let lo = v[0], hi = v[v.length - 1];
  const plo = v[Math.floor(v.length * 0.02)], phi = v[Math.ceil(v.length * 0.98) - 1];
  if (hi - lo > 4 * (phi - plo) && phi > plo) { lo = plo; hi = phi; }
  if (hi === lo) { lo -= 1; hi += 1; }
  const step = 10 ** Math.floor(Math.log10((hi - lo) / 4));
  const nice = [1, 2, 2.5, 5, 10].map((m) => m * step).find((s) => (hi - lo) / s <= 8) ?? step * 10;
  return [Math.floor(lo / nice) * nice, Math.ceil(hi / nice) * nice];
}


// breaks a curve (null) where it is not finite or jumps across most of the view (asymptotes)
function breakJumps(points, yRange) {
  const span = yRange[1] - yRange[0];
  const out = [];
  for (let i = 0; i < points.length; i++) {
    const [x, y] = points[i];
    if (!Number.isFinite(y)) { out.push([x, null]); continue; }
    const prev = out[out.length - 1];
    if (prev && prev[1] !== null && Math.abs(y - prev[1]) > span * 0.9) out.push([x, null]);
    out.push([x, y]);
  }
  return out;
}

export function plot(p, ctx) {
  const box = h('div.chart-box', { style: { height: `${p.height}px` } });
  const env = { ...p.params };
  let update;
  ctx.afterMount(() => { update = p.type === 'surface' ? plotSurface(box, p, env) : plot2d(box, p, env); });
  const controls = p.sliders.length ? sliderBar(p.sliders, (name, v) => { env[name] = v; update?.(); }) : null;
  return card('chart-card', p.title, controls, box, p.type === 'surface' ? h('footer.card-foot', 'drag to rotate · scroll to zoom') : null);
}

// Draws the curves for the parameters in `env`; returns a function that re-samples them after env changes.
// The axes keep the range of the first drawing, so dragging a slider moves the curve, not the frame.
function plot2d(el, p, env) {
  const b = base();
  const fns = p.functions.map((f) => (p.type === 'function' ? { y: compile(p, f.y) } : p.type === 'parametric' ? { x: compile(p, f.x), y: compile(p, f.y) } : { r: compile(p, f.r) }));
  const sample = () => fns.map((f) => {
    if (p.type === 'function') return linspace(p.x[0], p.x[1], p.samples).map((x) => [x, f.y({ ...env, x })]);
    if (p.type === 'parametric') return linspace(p.t[0], p.t[1], p.samples).map((t) => { const v = { ...env, t }; return [f.x(v), f.y(v)]; });
    return linspace(p.t[0], p.t[1], p.samples).map((t) => {
      const r = f.r({ ...env, t, theta: t });
      const deg = (t * 180) / Math.PI;
      return r < 0 ? [-r, deg + 180] : [r, deg];
    });
  });
  const first = sample();
  const names = p.functions.map((f) => f.label);
  if (p.type === 'polar') {
    const chart = mount(el, {
      color: palette(), textStyle: b.textStyle, tooltip: { ...b.tooltip, trigger: 'item' },
      legend: names.length > 1 ? { top: 0, textStyle: { color: b.muted } } : undefined,
      polar: { radius: '78%', center: ['50%', '54%'] },
      angleAxis: { type: 'value', min: 0, max: 360, startAngle: 0, clockwise: false, interval: 30, ...b.axis, splitLine: { show: true, lineStyle: { color: b.border } } },
      radiusAxis: { type: 'value', splitNumber: 3, ...b.axis, axisLabel: { ...b.axis.axisLabel, formatter: tick } },
      series: first.map((data, i) => ({ name: names[i], type: 'line', coordinateSystem: 'polar', showSymbol: false, data, lineStyle: { width: 2 } })),
    });
    return () => chart.setOption({ series: sample().map((data) => ({ data })) });
  }
  const yRange = p.y ?? autoRange(first.flatMap((d) => d.map((q) => q[1])));
  const xRange = p.type === 'function' ? p.x : autoRange(first.flatMap((d) => d.map((q) => q[0])).filter(Number.isFinite));
  const shape = (data) => (p.type === 'function' ? breakJumps(data, yRange) : data);
  const chart = mount(el, {
    color: palette(), textStyle: b.textStyle,
    tooltip: { ...b.tooltip, trigger: 'axis', valueFormatter: (v) => (v === null || v === undefined ? '–' : Number(v).toPrecision(5)) },
    legend: names.length > 1 ? { top: 0, textStyle: { color: b.muted }, type: 'scroll' } : undefined,
    grid: { left: 12, right: 18, top: names.length > 1 ? 34 : 14, bottom: 30, containLabel: true },
    xAxis: { type: 'value', min: xRange[0], max: xRange[1], name: p.xlabel, nameLocation: 'middle', nameGap: 26, ...b.axis,
      splitLine: { show: true, ...b.axis.splitLine }, axisLabel: { ...b.axis.axisLabel, formatter: tick } },
    yAxis: { type: 'value', min: yRange[0], max: yRange[1], name: p.ylabel, ...b.axis, axisLabel: { ...b.axis.axisLabel, formatter: tick } },
    dataZoom: [{ type: 'inside', xAxisIndex: 0, filterMode: 'none' }, { type: 'inside', yAxisIndex: 0, filterMode: 'none' }],
    series: first.map((data, i) => ({ name: names[i], type: 'line', showSymbol: false, connectNulls: false, lineStyle: { width: 2 }, clip: true, data: shape(data) })),
  });
  return () => chart.setOption({ series: sample().map((data) => ({ data: shape(data) })) });
}

function plotSurface(el, p, env) {
  const fz = compile(p, p.functions[0].z);
  const n = p.samples;
  const sample = () => {
    const points = [];
    for (const y of linspace(p.y[0], p.y[1], n))
      for (const x of linspace(p.x[0], p.x[1], n)) {
        const z = fz({ ...env, x, y });
        points.push([x, y, Number.isFinite(z) ? z : null, Number.isFinite(z) ? z : null]);
      }
    return points;
  };
  const range = (points) => { const zs = points.map((q) => q[2]).filter((z) => z !== null); return zs.length ? [Math.min(...zs), Math.max(...zs)] : [0, 1]; };
  const points = sample();
  const chart = render3d(el, { type: 'surface', names: { x: p.xlabel ?? 'x', y: p.ylabel ?? 'y', z: 'z', color: 'z' }, points, colorRange: range(points), rotate: p.rotate });
  return () => {
    const next = sample();
    const [min, max] = range(next);
    chart.setOption({ visualMap: { min, max }, series: [{ data: next.map((q) => q.slice(0, 3)) }] });
  };
}

// ---- matrix: heatmap, contour lines (marching squares) or a WebGL surface

// Line segments where the grid crosses `level`; coordinates in (column, row) index space.
function contourSegments(grid, level) {
  const segs = [];
  const nr = grid.length, nc = grid[0].length;
  const lerp = (a, b, va, vb) => a + ((level - va) / (vb - va)) * (b - a);
  for (let i = 0; i < nr - 1; i++)
    for (let j = 0; j < nc - 1; j++) {
      const a = grid[i][j], b = grid[i][j + 1], c = grid[i + 1][j + 1], d = grid[i + 1][j];
      if ([a, b, c, d].some((v) => v === null)) continue;
      const idx = (a > level ? 8 : 0) | (b > level ? 4 : 0) | (c > level ? 2 : 0) | (d > level ? 1 : 0);
      if (idx === 0 || idx === 15) continue;
      const top = [lerp(j, j + 1, a, b), i], right = [j + 1, lerp(i, i + 1, b, c)];
      const bottom = [lerp(j, j + 1, d, c), i + 1], left = [j, lerp(i, i + 1, a, d)];
      const table = { 1: [[left, bottom]], 2: [[bottom, right]], 3: [[left, right]], 4: [[top, right]], 5: [[left, top], [bottom, right]],
        6: [[top, bottom]], 7: [[left, top]], 8: [[left, top]], 9: [[top, bottom]], 10: [[top, right], [left, bottom]], 11: [[top, right]],
        12: [[left, right]], 13: [[bottom, right]], 14: [[left, bottom]] };
      segs.push(...table[idx]);
    }
  return segs;
}

export function matrix(p, ctx) {
  const box = h('div.chart-box', { style: { height: `${p.height}px` } });
  ctx.afterMount(() => (p.style === 'surface' ? matrixSurface(box, p) : matrixMap(box, p)));
  return card('chart-card', p.title, box, p.style === 'surface' ? h('footer.card-foot', 'drag to rotate · scroll to zoom') : null);
}

function matrixMap(el, p) {
  const b = base();
  const nr = p.grid.length, nc = p.grid[0].length;
  const cols = p.cols ?? Array.from({ length: nc }, (_, j) => String(j));
  const rows = p.rows ?? Array.from({ length: nr }, (_, i) => String(i));
  const fmt = (v) => (v === null ? '–' : `${Number(Number(v).toPrecision(p.digits))}${p.unit}`);
  const diverging = p.scale === 'diverging';
  const m = Math.max(Math.abs(p.min), Math.abs(p.max));
  const range = diverging ? [-m, m] : [p.min, p.max];
  const colors = diverging ? [css('blue'), css('panel'), css('red')] : [css('panel'), css('blue'), css('accent'), css('yellow')];
  const cells = [];
  p.grid.forEach((r, i) => r.forEach((v, j) => cells.push([j, i, v])));
  const contour = p.style === 'contour';
  const series = [{ type: 'heatmap', data: cells, itemStyle: { opacity: contour ? 0.45 : 1, borderColor: nr * nc <= 2500 ? css('bg') : undefined, borderWidth: nr * nc <= 2500 ? 1 : 0 },
    label: { show: p.annotate && !contour, color: b.fg, fontFamily: css('font'), fontSize: 11, formatter: (i) => (i.value[2] === null ? '' : `${Number(Number(i.value[2]).toPrecision(p.digits))}`) },
    emphasis: { itemStyle: { borderColor: css('fg'), borderWidth: 1 } } }];
  if (contour) {
    const levels = Array.from({ length: p.levels }, (_, k) => p.min + ((k + 1) * (p.max - p.min)) / (p.levels + 1));
    levels.forEach((level) => {
      const segs = contourSegments(p.grid, level);
      series.push({ type: 'custom', name: `${Number(level.toPrecision(3))}`, silent: true, tooltip: { show: false }, data: segs.map((s) => [s[0][0], s[0][1], s[1][0], s[1][1]]),
        renderItem: (params, api) => {
          const a = api.coord([api.value(0), api.value(1)]), c = api.coord([api.value(2), api.value(3)]);
          return { type: 'line', shape: { x1: a[0], y1: a[1], x2: c[0], y2: c[1] }, style: { stroke: css('fg'), lineWidth: 1.2, opacity: 0.85 } };
        } });
    });
  }
  return mount(el, {
    textStyle: b.textStyle,
    tooltip: { ...b.tooltip, formatter: (i) => (i.seriesType === 'heatmap' ? `${rows[i.value[1]]} · ${cols[i.value[0]]}<br><b>${fmt(i.value[2])}</b>` : '') },
    grid: { left: 12, right: 96, top: 10, bottom: 12, containLabel: true },
    xAxis: { type: 'category', data: cols, position: 'top', ...b.axis, splitArea: { show: false }, axisLabel: { ...b.axis.axisLabel, interval: nc > 40 ? 'auto' : 0, rotate: cols.some((c) => c.length > 4) && nc > 6 ? 40 : 0 } },
    yAxis: { type: 'category', data: rows, inverse: true, ...b.axis, axisLabel: { ...b.axis.axisLabel, interval: nr > 40 ? 'auto' : 0 } },
    visualMap: { min: range[0], max: range[1], calculable: true, orient: 'vertical', right: 10, top: 'middle', itemHeight: Math.min(180, p.height - 80), seriesIndex: 0,
      precision: 3, textStyle: { color: b.muted }, inRange: { color: colors } },
    series,
  });
}

function matrixSurface(el, p) {
  const points = [];
  p.grid.forEach((r, i) => r.forEach((v, j) => points.push([j, i, v, v])));
  return render3d(el, { type: 'surface', names: { x: 'column', y: 'row', z: 'value', color: 'value' }, points, colorRange: [p.min, p.max] });
}

// ---- graph: networks with a force or circular layout

export function graph(p, ctx) {
  const box = h('div.chart-box', { style: { height: `${p.height}px` } });
  ctx.afterMount(() => drawGraph(box, p));
  const foot = `${p.nodes.length} node${p.nodes.length === 1 ? '' : 's'} · ${p.edges.length} edge${p.edges.length === 1 ? '' : 's'} · drag nodes · scroll to zoom`;
  return card('chart-card', p.title, box, h('footer.card-foot', foot));
}

function drawGraph(el, p) {
  const b = base();
  const colors = palette();
  const n = p.nodes.length;
  const metric = (node) => (p.size === 'degree' ? node.degree : p.size === 'value' ? node.value ?? 0 : 1);
  const values = p.nodes.map(metric);
  const lo = Math.min(...values), hi = Math.max(...values);
  const unit = Math.max(6, Math.min(18, 260 / Math.sqrt(n)));
  const sizeOf = (v) => (hi > lo ? unit * (0.7 + (1.3 * (v - lo)) / (hi - lo)) : unit);
  const weights = p.edges.map((e) => e.w).filter((w) => w !== undefined);
  const wlo = Math.min(...weights), whi = Math.max(...weights);
  const widthOf = (w) => (w === undefined || !(whi > wlo) ? 1.2 : 0.8 + (3.2 * (w - wlo)) / (whi - wlo));
  const categories = p.groups.length ? p.groups.map((name) => ({ name })) : [{ name: 'nodes' }];
  return mount(el, {
    color: colors, textStyle: b.textStyle,
    tooltip: { ...b.tooltip, formatter: (i) => (i.dataType === 'edge'
      ? `${i.data.source} ${p.directed ? '→' : '—'} ${i.data.target}${i.data.w !== undefined ? `<br>weight ${tick(i.data.w)}` : ''}${i.data.label ? `<br>${i.data.label}` : ''}`
      : `${i.data.label}${i.data.group !== undefined ? `<br>group ${i.data.group}` : ''}<br>degree ${i.data.degree}${i.data.v !== undefined ? `<br>value ${tick(i.data.v)}` : ''}`) },
    legend: p.groups.length ? { top: 0, textStyle: { color: b.muted }, type: 'scroll' } : undefined,
    series: [{
      type: 'graph', layout: p.layout, roam: true, draggable: true, categories,
      top: p.groups.length ? 34 : 16, bottom: 16, left: 16, right: 16,
      circular: { rotateLabel: true },
      force: { repulsion: Math.max(60, 2400 / Math.sqrt(n)), edgeLength: [30, Math.max(50, 600 / Math.sqrt(n))], gravity: 0.08, friction: 0.15, layoutAnimation: n <= 400 },
      edgeSymbol: p.directed ? ['none', 'arrow'] : ['none', 'none'], edgeSymbolSize: 7,
      label: { show: p.labels === 'all', position: 'right', color: b.fg, fontSize: 11 },
      labelLayout: { hideOverlap: true },
      emphasis: { focus: 'adjacency', label: { show: true }, lineStyle: { width: 3 } },
      lineStyle: { color: b.muted, opacity: 0.55, curveness: p.directed ? 0.12 : 0 },
      itemStyle: { borderColor: css('panel'), borderWidth: 1 },
      data: p.nodes.map((node, i) => ({ id: node.id, name: node.id, label: node.label, group: node.group, degree: node.degree, v: node.value,
        category: p.groups.length ? p.groups.indexOf(node.group) : 0, symbolSize: sizeOf(values[i]),
        itemStyle: p.groups.length && node.group === undefined ? { color: b.muted } : undefined,
        ...(p.labels === 'all' ? { label: { formatter: node.label } } : {}) })),
      links: p.edges.map((e) => ({ source: e.s, target: e.t, w: e.w, label: e.label, lineStyle: { width: widthOf(e.w) } })),
    }],
  });
}
