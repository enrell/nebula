// Charts: ECharts (canvas) for 2D, echarts-gl (WebGL) for 3D. Colours come from the nebula theme (CSS variables),
// so a chart re-created after a theme change matches the new theme.
import { BarChart, LineChart, PieChart, ScatterChart } from 'echarts/charts';
import { DataZoomComponent, GridComponent, LegendComponent, PolarComponent, TitleComponent, TooltipComponent, VisualMapComponent } from 'echarts/components';
import * as echarts from 'echarts/core';
import { CanvasRenderer } from 'echarts/renderers';
import { parse } from '../../core/expr.js';
import { card, h } from '../dom.js';
import { Bar3DChart, Line3DChart, Scatter3DChart, SurfaceChart } from 'echarts-gl/charts';
import { Grid3DComponent } from 'echarts-gl/components';

echarts.use([LineChart, BarChart, PieChart, ScatterChart, GridComponent, TooltipComponent, LegendComponent, TitleComponent,
  DataZoomComponent, VisualMapComponent, PolarComponent, CanvasRenderer, Scatter3DChart, Bar3DChart, Line3DChart, SurfaceChart, Grid3DComponent]);

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
  el.nebulaDispose = () => { ro.disconnect(); chart.dispose(); };
  return chart;
}

const webglAvailable = () => { try { return !!document.createElement('canvas').getContext('webgl'); } catch { return false; } };

export function renderChart(el, p) {
  const b = base();
  const fmt = (v) => (v === null || v === undefined ? '–' : `${Number(v).toLocaleString()}${p.unit}`);
  const tooltip = { ...b.tooltip, trigger: p.type === 'pie' || p.type === 'scatter' ? 'item' : 'axis', valueFormatter: fmt };
  if (p.type === 'pie') {
    return mount(el, {
      color: palette(), textStyle: b.textStyle, tooltip,
      legend: { bottom: 0, textStyle: { color: b.muted }, type: 'scroll' },
      series: [{ type: 'pie', radius: ['38%', '68%'], center: ['50%', '45%'], itemStyle: { borderColor: css('panel'), borderWidth: 2 },
        label: { color: b.fg }, data: p.xs.map((name, i) => ({ name, value: p.series[0].values[i] })) }],
    });
  }
  const catAxis = { type: p.xNumeric ? 'value' : 'category', data: p.xNumeric ? undefined : p.xs, name: p.xName, nameLocation: 'middle', nameGap: 28, ...b.axis,
    splitLine: { show: p.xNumeric, ...b.axis.splitLine }, boundaryGap: p.type === 'bar' };
  const valAxis = { type: 'value', ...b.axis, axisLabel: { ...b.axis.axisLabel, formatter: (v) => `${v.toLocaleString()}${p.unit}` } };
  const kind = p.type === 'area' ? 'line' : p.type;
  const many = p.xs.length > 60;
  return mount(el, {
    color: palette(), textStyle: b.textStyle, tooltip,
    legend: p.series.length > 1 ? { top: 0, textStyle: { color: b.muted }, type: 'scroll' } : undefined,
    grid: { left: 12, right: 18, top: p.series.length > 1 ? 34 : 14, bottom: many ? 58 : 34, containLabel: true },
    xAxis: p.horizontal ? valAxis : catAxis,
    yAxis: p.horizontal ? { ...catAxis, nameLocation: 'end', nameGap: 12 } : valAxis,
    dataZoom: many ? [{ type: 'inside' }, { type: 'slider', height: 16, bottom: 6, borderColor: b.border, textStyle: { color: b.muted } }] : undefined,
    series: p.series.map((s) => ({
      name: s.name, type: kind, stack: p.stack ? 'all' : undefined, showSymbol: kind === 'line' ? p.xs.length <= 40 : undefined,
      symbolSize: kind === 'scatter' ? 8 : 5, smooth: false, areaStyle: p.type === 'area' ? { opacity: 0.25 } : undefined,
      barMaxWidth: 36, emphasis: { focus: 'series' },
      data: p.xNumeric ? s.values.map((v, i) => [p.xs[i], v]) : s.values,
    })),
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

const tick = (v) => Number(Number(v).toPrecision(4)).toString();

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
  ctx.afterMount(() => (p.type === 'surface' ? plotSurface(box, p) : plot2d(box, p)));
  return card('chart-card', p.title, box, p.type === 'surface' ? h('footer.card-foot', 'drag to rotate · scroll to zoom') : null);
}

function plot2d(el, p) {
  const b = base();
  const env = { ...p.params };
  const series = p.functions.map((f) => {
    let data;
    if (p.type === 'function') {
      const fy = compile(p, f.y);
      data = linspace(p.x[0], p.x[1], p.samples).map((x) => [x, fy({ ...env, x })]);
    } else if (p.type === 'parametric') {
      const fx = compile(p, f.x), fy = compile(p, f.y);
      data = linspace(p.t[0], p.t[1], p.samples).map((t) => { const v = { ...env, t }; return [fx(v), fy(v)]; });
    } else {
      const fr = compile(p, f.r);
      data = linspace(p.t[0], p.t[1], p.samples).map((t) => {
        const r = fr({ ...env, t, theta: t });
        const deg = (t * 180) / Math.PI;
        return r < 0 ? [-r, deg + 180] : [r, deg];
      });
    }
    return { name: f.label, data };
  });
  if (p.type === 'polar') {
    return mount(el, {
      color: palette(), textStyle: b.textStyle, tooltip: { ...b.tooltip, trigger: 'item' },
      legend: series.length > 1 ? { top: 0, textStyle: { color: b.muted } } : undefined,
      polar: { radius: '78%', center: ['50%', '54%'] },
      angleAxis: { type: 'value', min: 0, max: 360, startAngle: 0, clockwise: false, interval: 30, ...b.axis, splitLine: { show: true, lineStyle: { color: b.border } } },
      radiusAxis: { type: 'value', splitNumber: 3, ...b.axis, axisLabel: { ...b.axis.axisLabel, formatter: tick } },
      series: series.map((s) => ({ name: s.name, type: 'line', coordinateSystem: 'polar', showSymbol: false, data: s.data, lineStyle: { width: 2 } })),
    });
  }
  const yRange = p.y ?? autoRange(series.flatMap((s) => s.data.map((d) => d[1])));
  const xs = series.flatMap((s) => s.data.map((d) => d[0])).filter(Number.isFinite);
  const xRange = p.type === 'function' ? p.x : autoRange(xs);
  return mount(el, {
    color: palette(), textStyle: b.textStyle,
    tooltip: { ...b.tooltip, trigger: 'axis', valueFormatter: (v) => (v === null || v === undefined ? '–' : Number(v).toPrecision(5)) },
    legend: series.length > 1 ? { top: 0, textStyle: { color: b.muted }, type: 'scroll' } : undefined,
    grid: { left: 12, right: 18, top: series.length > 1 ? 34 : 14, bottom: 30, containLabel: true },
    xAxis: { type: 'value', min: xRange[0], max: xRange[1], name: p.xlabel, nameLocation: 'middle', nameGap: 26, ...b.axis,
      splitLine: { show: true, ...b.axis.splitLine }, axisLabel: { ...b.axis.axisLabel, formatter: tick } },
    yAxis: { type: 'value', min: yRange[0], max: yRange[1], name: p.ylabel, ...b.axis, axisLabel: { ...b.axis.axisLabel, formatter: tick } },
    dataZoom: [{ type: 'inside', xAxisIndex: 0, filterMode: 'none' }, { type: 'inside', yAxisIndex: 0, filterMode: 'none' }],
    series: series.map((s) => ({ name: s.name, type: 'line', showSymbol: false, connectNulls: false, lineStyle: { width: 2 }, clip: true,
      data: p.type === 'function' ? breakJumps(s.data, yRange) : s.data })),
  });
}

function plotSurface(el, p) {
  const fz = compile(p, p.functions[0].z);
  const n = p.samples;
  const points = [];
  for (const y of linspace(p.y[0], p.y[1], n))
    for (const x of linspace(p.x[0], p.x[1], n)) {
      const z = fz({ ...p.params, x, y });
      points.push([x, y, Number.isFinite(z) ? z : null, Number.isFinite(z) ? z : null]);
    }
  const zs = points.map((q) => q[2]).filter((z) => z !== null);
  return render3d(el, { type: 'surface', names: { x: p.xlabel ?? 'x', y: p.ylabel ?? 'y', z: 'z', color: 'z' },
    points, colorRange: zs.length ? [Math.min(...zs), Math.max(...zs)] : [0, 1], rotate: p.rotate });
}
