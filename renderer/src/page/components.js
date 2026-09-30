// One renderer per registry component. Each gets the props produced by the checker (already validated and
// resolved) and returns a DOM node. They never see raw YAML or unchecked input.
import { card, h } from './dom.js';
import { highlight } from './highlight.js';
import { sandboxFrame } from './sandbox.js';

const tones = { good: 'good', bad: 'bad', neutral: 'neutral' };


function callout(p, ctx) {
  const icon = { info: 'i', success: '✓', warning: '!', danger: '✗', note: '✎' }[p.tone];
  return h(`aside.callout.tone-${p.tone}`, h('span.callout-icon', { 'aria-hidden': true }, icon),
    h('div.callout-body', p.title ? h('div.callout-title', p.title) : null, ctx.markdown(p.text)));
}

function stats(p) {
  return h('section.stats', p.title ? h('header.section-title', p.title) : null,
    h('div.stats-grid', p.items.map((it) => h(`div.stat.tone-${tones[it.tone] ?? 'neutral'}`,
      h('div.stat-label', it.label),
      h('div.stat-value', String(it.value)),
      it.delta !== undefined ? h('div.stat-delta', String(it.delta)) : null,
      it.hint ? h('div.stat-hint', it.hint) : null))));
}

function table(p) {
  let sort = p.sort ? { ...p.sort } : null;
  const tbody = h('tbody');
  const heads = p.columns.map((c, i) => h(`th.align-${c.align}`, {
    scope: 'col', tabindex: 0, title: 'sort',
    onclick: () => { sort = sort?.column === i ? { column: i, desc: !sort.desc } : { column: i, desc: false }; draw(); },
    onkeydown: (e) => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); e.currentTarget.click(); } },
  }, c.label, h('span.sort-mark')));
  const cmp = (col) => (a, b) => {
    const x = a[col], y = b[col];
    if (p.columns[col].numeric) return (x === '' ? -Infinity : parseFloat(x)) - (y === '' ? -Infinity : parseFloat(y));   // parseFloat: "3.21 ± 0.12"
    return String(x).localeCompare(String(y), undefined, { numeric: true });
  };
  function draw() {
    const rows = sort ? [...p.rows].sort(cmp(sort.column)) : p.rows;
    if (sort?.desc) rows.reverse();
    heads.forEach((th, i) => { th.dataset.sort = sort?.column === i ? (sort.desc ? 'desc' : 'asc') : ''; });
    tbody.replaceChildren(...rows.map((r) => h('tr', r.map((v, i) => h(`td.align-${p.columns[i].align}`, String(v))))));
  }
  draw();
  return card('table-card', p.title,
    h('div.table-scroll', h('table', h('thead', h('tr', heads)), tbody)),
    h('footer.card-foot', `${p.rows.length} row${p.rows.length === 1 ? '' : 's'}`));
}

function checklist(p, ctx) {
  const icons = { done: '✓', todo: '○', running: '◐', failed: '✗', skipped: '–' };
  const pct = p.total ? Math.round((100 * p.done) / p.total) : 0;
  return card('checklist', p.title,
    h('div.progress', { role: 'progressbar', 'aria-valuenow': pct, 'aria-valuemin': 0, 'aria-valuemax': 100 },
      h('div.progress-bar', { style: { width: `${pct}%` } })),
    h('div.progress-label', `${p.done}/${p.total} done`),
    h('ul.checklist-items', p.items.map((it) => h(`li.status-${it.status}`,
      h('span.check-icon', { 'aria-label': it.status }, icons[it.status]),
      h('div.check-body', ctx.inline(it.text), it.note ? h('div.check-note', it.note) : null)))));
}

function code(p) {
  const lines = p.code.split('\n');
  const gutter = h('pre.gutter', { 'aria-hidden': true }, lines.map((_, i) => `${p.firstLine + i}\n`).join(''));
  const body = h('pre.code', h(`code.hljs${p.lang ? `.language-${p.lang}` : ''}`));
  body.firstChild.innerHTML = highlight(p.code, p.lang);   // highlight() escapes everything it does not mark up
  const range = p.firstLine > 1 || lines.length > 1 ? `  ${p.firstLine}–${p.firstLine + lines.length - 1}` : '';
  return card('code-card', p.title ? `${p.title}${range}` : null, h('div.code-scroll', gutter, body));
}

function image(p, ctx) {
  const img = h('img', { src: ctx.fileUrl(p.src), alt: p.alt ?? '', loading: 'lazy', style: p.width ? { maxWidth: `${p.width}px` } : null });
  img.addEventListener('error', () => ctx.issue(`image ${p.src} could not be displayed`));
  return h('figure.image', img, p.caption ? h('figcaption', ctx.inline(p.caption)) : null);
}

function html(p, ctx, block) {
  return card('html-card', p.title, sandboxFrame(ctx.sandboxUrl(block.index), p.height));
}

// Heavy renderers live in their own chunks (src/page/lazy/*), fetched from nebula-view:// the first time a document
// uses them, so a plain report never pays for charting, math or molecule libraries.
const lazy = (load, name) => async (p, ctx, block) => (await load())[name](p, ctx, block);
const charts = () => import('./lazy/charts.js');
const mathChunk = () => import('./lazy/math.js');

export const renderers = {
  callout, stats, table, checklist, code, image, html,
  chart: lazy(charts, 'chart'),
  chart3d: lazy(charts, 'chart3d'),
  plot: lazy(charts, 'plot'),
  matrix: lazy(charts, 'matrix'),
  math: lazy(mathChunk, 'math'),
};
