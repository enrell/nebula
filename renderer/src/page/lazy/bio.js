// Biology: sequences and alignments, trees (Newick), genome tracks. Plain SVG and HTML, themed by CSS variables.
import { card, h } from '../dom.js';

const SVG = 'http://www.w3.org/2000/svg';
const svg = (tag, attrs = {}, ...children) => {
  const el = document.createElementNS(SVG, tag);
  for (const [k, v] of Object.entries(attrs)) if (v !== undefined && v !== null) el.setAttribute(k, v);
  for (const c of children.flat()) if (c) el.append(c instanceof Node ? c : document.createTextNode(String(c)));
  return el;
};

// ---- sequence

const NUC = { A: 'green', C: 'blue', G: 'yellow', T: 'red', U: 'red' };
const AA = {};
for (const c of 'AILMFWV') AA[c] = 'blue';
for (const c of 'KRH') AA[c] = 'red';
for (const c of 'DE') AA[c] = 'accent';
for (const c of 'STNQ') AA[c] = 'green';
for (const c of 'CGPY') AA[c] = 'yellow';
const colorOf = (type, c) => (type === 'protein' ? AA : NUC)[c.toUpperCase()] ?? 'none';

function residueRow(type, letters) {
  // one span per run of equally coloured letters keeps big alignments light
  const row = h('span.seq-letters');
  let i = 0;
  while (i < letters.length) {
    const col = colorOf(type, letters[i]);
    let j = i + 1;
    while (j < letters.length && colorOf(type, letters[j]) === col) j++;
    row.append(h(`span.res.res-${col}`, letters.slice(i, j)));
    i = j;
  }
  return row;
}

function consensusOf(seqs, from, to) {
  let cons = '';
  const cons2 = [];
  for (let k = from; k < to; k++) {
    const counts = {};
    for (const s of seqs) { const c = (s.seq[k] ?? '-').toUpperCase(); counts[c] = (counts[c] ?? 0) + 1; }
    const [best, n] = Object.entries(counts).filter(([c]) => c !== '-' && c !== '.').sort((a, b) => b[1] - a[1])[0] ?? ['-', 0];
    cons += n / seqs.length >= 0.5 ? best : '.';
    cons2.push(n / seqs.length);
  }
  return { cons, conservation: cons2 };
}

export function sequence(p) {
  const total = Math.max(...p.seqs.map((s) => s.seq.length));
  const nameW = Math.min(18, Math.max(4, ...p.seqs.map((s) => s.name.length)));
  const blocks = [];
  for (let from = 0; from < total; from += p.width) {
    const to = Math.min(total, from + p.width);
    const rows = [];
    const ruler = [...' '.repeat(to - from)];
    for (let k = from; k < to; k++) {
      const n = p.start + k;
      if (n % 10 === 0) { const label = String(n); for (let c = 0; c < label.length && k - from - label.length + 1 + c >= 0; c++) ruler[k - from - label.length + 1 + c] = label[c]; }
    }
    rows.push(h('div.seq-row.seq-ruler', h('span.seq-name', ''.padEnd(nameW)), h('span.seq-letters', ruler.join(''))));
    for (const s of p.seqs) {
      const part = s.seq.slice(from, to);
      const shown = s.seq.slice(0, to).replace(/[-.]/g, '').length;
      rows.push(h('div.seq-row', h('span.seq-name', { title: s.name }, s.name.slice(0, nameW).padEnd(nameW)), residueRow(p.type, part), h('span.seq-pos', String(p.start + shown - 1))));
    }
    if (p.consensus) {
      const { cons, conservation } = consensusOf(p.seqs, from, to);
      rows.push(h('div.seq-row.seq-consensus', h('span.seq-name', 'consensus'.slice(0, nameW).padEnd(nameW)), h('span.seq-letters', cons)));
      rows.push(h('div.seq-row', h('span.seq-name', ''.padEnd(nameW)), h('span.seq-bars', conservation.map((c) => h('span.seq-bar', { style: { height: `${Math.round(c * 100)}%` } })))));
    }
    for (const a of p.annotations) {
      const s = Math.max(a.start - p.start, from), e = Math.min(a.end - p.start + 1, to);
      if (e <= s) continue;
      rows.push(h('div.seq-row.seq-annot', h('span.seq-name', ''.padEnd(nameW)),
        h('span.seq-letters', ' '.repeat(s - from), h(`span.annot.tone-${a.color ?? 'accent'}`, ' '.repeat(e - s)),
          a.label ? h(`span.annot-label.tone-${a.color ?? 'accent'}`, ` ${a.label}`) : null)));
    }
    blocks.push(h('div.seq-block', rows));
  }
  const kind = { dna: 'DNA', rna: 'RNA', protein: 'protein' }[p.type];
  const footer = `${p.seqs.length} ${kind} sequence${p.seqs.length > 1 ? 's' : ''}${p.aligned ? ' (aligned)' : ''} · ${total.toLocaleString()} ${p.type === 'protein' ? 'residues' : 'nt'}`;
  return card('seq-card', p.title, h('div.seq-scroll', blocks), h('footer.card-foot', footer));
}

// ---- tree

export function tree(p, ctx) {
  const box = h('div.tree-box');
  ctx.afterMount(() => {
    const draw = () => drawTree(box, p);
    draw();
    const ro = new ResizeObserver(draw);
    ro.observe(box);
    box.nebulaDispose = () => ro.disconnect();
  });
  return card('tree-card', p.title, box);
}

function drawTree(box, p) {
  const rowH = 20, pad = 14;
  const nodes = [];
  let leafIndex = 0, maxX = 0, depthMax = 0;
  (function layout(n, x, depth) {
    n._x = p.lengths ? x + (n.length ?? 0) : depth;
    maxX = Math.max(maxX, n._x);
    depthMax = Math.max(depthMax, depth);
    if (!n.children.length) n._y = leafIndex++;
    else { n.children.forEach((c) => layout(c, n._x, depth + 1)); n._y = (n.children[0]._y + n.children[n.children.length - 1]._y) / 2; }
    nodes.push(n);
  })(p.tree, 0, 0);
  if (!p.lengths) { const leafDepth = depthMax; nodes.forEach((n) => { if (!n.children.length) n._x = leafDepth; }); maxX = depthMax; }
  const labelW = Math.min(260, 8 + 7.2 * Math.max(...nodes.filter((n) => !n.children.length).map((n) => (n.name ?? '').length)));
  const width = Math.max(320, box.clientWidth || 600);
  const height = leafIndex * rowH + pad * 2 + (p.lengths ? 26 : 0);
  const sx = (x) => pad + (maxX ? (x / maxX) * (width - labelW - pad * 2) : 0);
  const sy = (y) => pad + y * rowH + rowH / 2;
  const hl = new Set(p.highlight);
  const g = svg('svg', { width, height, class: 'tree-svg' });
  const lines = [];
  const labels = [];
  for (const n of nodes) {
    for (const c of n.children) lines.push(svg('path', { d: `M${sx(n._x)},${sy(c._y)} L${sx(n._x)},${sy(n._y)} M${sx(n._x)},${sy(c._y)} L${sx(c._x)},${sy(c._y)}`, class: 'tree-branch' }));
    if (!n.children.length) {
      labels.push(svg('text', { x: sx(n._x) + 6, y: sy(n._y) + 4, class: hl.has(n.name) ? 'tree-leaf hl' : 'tree-leaf' }, n.name ?? ''));
      labels.push(svg('circle', { cx: sx(n._x), cy: sy(n._y), r: hl.has(n.name) ? 3.5 : 2, class: hl.has(n.name) ? 'tree-dot hl' : 'tree-dot' }));
    } else if (p.support && n.support !== undefined) {
      labels.push(svg('text', { x: sx(n._x) - 4, y: sy(n._y) - 4, class: 'tree-support', 'text-anchor': 'end' }, n.support));
    }
  }
  if (p.lengths && maxX > 0) {   // scale bar: a round length about a fifth of the tree
    const target = maxX / 5, step = 10 ** Math.floor(Math.log10(target));
    const len = [1, 2, 5, 10].map((m) => m * step).find((v) => v >= target) ?? step;
    const x0 = sx(0), x1 = sx(len), y = height - 12;
    lines.push(svg('path', { d: `M${x0},${y} L${x1},${y} M${x0},${y - 4} L${x0},${y + 4} M${x1},${y - 4} L${x1},${y + 4}`, class: 'tree-branch' }));
    labels.push(svg('text', { x: (x0 + x1) / 2, y: y - 6, class: 'tree-support', 'text-anchor': 'middle' }, Number(len.toPrecision(3))));
  }
  g.append(...lines, ...labels);
  box.replaceChildren(g);
}

// ---- tracks

export function tracks(p, ctx) {
  const box = h('div.tracks-box');
  ctx.afterMount(() => {
    const draw = () => drawTracks(box, p);
    draw();
    const ro = new ResizeObserver(draw);
    ro.observe(box);
    box.nebulaDispose = () => ro.disconnect();
  });
  const r = p.region;
  return card('tracks-card', p.title, box, h('footer.card-foot', `${r.chrom}:${r.start.toLocaleString()}-${r.end.toLocaleString()} · ${(r.end - r.start).toLocaleString()} bp`));
}

function niceStep(span, target) {
  const raw = span / target, step = 10 ** Math.floor(Math.log10(raw));
  return [1, 2, 5, 10].map((m) => m * step).find((v) => v >= raw);
}

function drawTracks(box, p) {
  const width = Math.max(420, box.clientWidth || 800);
  const labelW = 110, right = 12;
  const { start, end } = p.region;
  const x = (pos) => labelW + ((pos - start) / (end - start)) * (width - labelW - right);
  const g = svg('svg', { width, class: 'tracks-svg' });
  let y = 8;
  // ruler
  const step = niceStep(end - start, 8);
  const fmt = (v) => (step >= 1e6 ? `${v / 1e6} Mb` : step >= 1e3 ? `${v / 1e3} kb` : `${v}`);
  g.append(svg('line', { x1: labelW, x2: width - right, y1: y + 14, y2: y + 14, class: 'tr-axis' }));
  for (let v = Math.ceil(start / step) * step; v <= end; v += step) {
    g.append(svg('line', { x1: x(v), x2: x(v), y1: y + 10, y2: y + 18, class: 'tr-axis' }));
    g.append(svg('text', { x: x(v), y: y + 6, class: 'tr-tick', 'text-anchor': 'middle' }, fmt(v)));
  }
  y += 30;
  for (const t of p.tracks) {
    const color = `var(--${t.color ?? { features: 'blue', signal: 'green', variants: 'red' }[t.type]})`;
    const top = y;
    if (t.type === 'features') {
      // greedy lanes so overlapping features do not hide each other
      const lanes = [];
      const placed = [...t.features].sort((a, b) => a.start - b.start).map((f) => {
        let lane = lanes.findIndex((endPx) => endPx < x(f.start) - 4);
        const endPx = Math.max(x(f.end), x(f.start) + 7 * (f.name ?? '').length);
        if (lane < 0) { lane = lanes.length; lanes.push(endPx); } else lanes[lane] = endPx;
        return { f, lane };
      });
      const laneH = 30;
      for (const { f, lane } of placed) {
        const cy = top + lane * laneH + 18;
        g.append(svg('line', { x1: x(f.start), x2: x(f.end), y1: cy, y2: cy, stroke: color, 'stroke-width': 1.2 }));
        for (const [a, b] of f.exons ?? [[f.start, f.end]]) {
          const thick = f.thick && b > f.thick[0] && a < f.thick[1];
          g.append(svg('rect', { x: x(a), y: cy - (thick || !f.thick ? 6 : 3), width: Math.max(1, x(b) - x(a)), height: thick || !f.thick ? 12 : 6, fill: color, rx: 1 }));
        }
        if (f.strand) {   // chevrons along the intron line
          const dir = f.strand === '+' ? 1 : -1;
          for (let px = x(f.start) + 10; px < x(f.end) - 6; px += 18)
            g.append(svg('path', { d: `M${px - 3 * dir},${cy - 3} L${px},${cy} L${px - 3 * dir},${cy + 3}`, class: 'tr-chevron', stroke: color }));
        }
        if (f.name) g.append(svg('text', { x: x(f.start), y: cy - 9, class: 'tr-name' }, f.name));
      }
      y += Math.max(1, lanes.length) * laneH + 8;
    } else if (t.type === 'signal') {
      const hgt = t.height ?? 60;
      const vals = t.values.map((v) => v[1]);
      const max = Math.max(1e-12, ...vals);
      const sorted = [...t.values].sort((a, b) => a[0] - b[0]);
      const ybase = top + hgt;
      const d = `M${x(sorted[0]?.[0] ?? start)},${ybase} ` + sorted.map(([pos, v]) => `L${x(pos)},${ybase - (Math.max(0, v) / max) * (hgt - 6)}`).join(' ') + ` L${x(sorted[sorted.length - 1]?.[0] ?? end)},${ybase} Z`;
      g.append(svg('path', { d, fill: color, 'fill-opacity': 0.35, stroke: color, 'stroke-width': 1 }));
      g.append(svg('text', { x: labelW + 4, y: top + 10, class: 'tr-tick' }, `max ${Number(max.toPrecision(3))}`));
      y += hgt + 10;
    } else {
      const hgt = 46;
      for (const v of t.variants) {
        const c = { high: 'var(--red)', moderate: 'var(--yellow)', low: 'var(--green)' }[v.impact] ?? color;
        g.append(svg('line', { x1: x(v.pos), x2: x(v.pos), y1: top + hgt, y2: top + 16, stroke: c, 'stroke-width': 1.2 }));
        g.append(svg('circle', { cx: x(v.pos), cy: top + 12, r: 5, fill: c }));
        const label = v.label ?? (v.ref && v.alt ? `${v.ref}>${v.alt}` : '');
        if (label) g.append(svg('text', { x: x(v.pos) + 8, y: top + 16, class: 'tr-name' }, label));
      }
      y += hgt + 6;
    }
    g.append(svg('text', { x: 4, y: top + 16, class: 'tr-label' }, t.label.length > 14 ? `${t.label.slice(0, 13)}…` : t.label));
    g.append(svg('line', { x1: 0, x2: width, y1: y - 4, y2: y - 4, class: 'tr-sep' }));
  }
  g.setAttribute('height', y);
  box.replaceChildren(g);
}
