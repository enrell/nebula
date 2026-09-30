// The view page: receives an already checked document from nebula over QWebChannel and draws it.
// It never parses the source itself; problems found while drawing are reported back as render issues.
import { renderers } from './components.js';
import { h } from './dom.js';
import { createMarkdown } from './markdown.js';

let bridge;
let fileBase = '';
let sandboxBase = '';
let renderSeq = 0;   // sandbox URLs change on every render, so a redraw (new document or theme) reloads them
let files = new Set();   // files the checker verified; anything else would be refused by nebula anyway
const issues = [];
const ctx = {
  afterMount() { throw new Error('afterMount is per block'); },
  hasFile: (path) => files.has(path),
  sandboxUrl: (index) => `${sandboxBase}${index}.html?r=${renderSeq}`,
  fileUrl: (path) => fileBase + path.split('/').map(encodeURIComponent).join('/'),
  markdown(text) { const el = h('div.md'); el.innerHTML = md.render(text); return el; },
  inline(text) { const el = h('span.md-inline'); el.innerHTML = md.renderInline(text); return el; },
  issue(message) { issues.push(message); bridge?.reportIssue(message); },
};
const md = createMarkdown(ctx);

function applyTheme(t) {
  if (!t) return;
  const s = document.documentElement.style;
  for (const [k, v] of Object.entries(t.colors ?? {})) s.setProperty(`--${k}`, v);
  if (t.fontFamily) s.setProperty('--font', `"${t.fontFamily}", monospace`);
  if (t.fontSize) s.setProperty('--font-size', `${t.fontSize}px`);
}

function errorCard(block) {
  const tag = block.type === 'markdown' ? 'markdown' : `nebula:${block.component}`;
  return h('section.card.error-card', h('header.card-title', `${tag} — not rendered`),
    h('ul', block.errors.map((e) => h('li', h('span.error-line', `line ${e.line}`), ' ', e.message, e.hint ? h('div.error-hint', e.hint) : null))));
}

async function renderBlock(block, bctx) {
  if (!block.ok) return errorCard(block);
  if (block.type === 'markdown') return bctx.markdown(block.text);
  const draw = renderers[block.component];
  if (!draw) return errorCard({ ...block, errors: [{ line: block.line, message: `this nebula build cannot draw "${block.component}"` }] });
  return draw(block.props, bctx, block);
}

let lastDoc;
// Blocks are drawn into placeholders in document order; a block whose renderer lives in a lazy chunk fills its
// placeholder when the chunk arrives. nebula gets the render report once every block is drawn.
function render(doc) {
  lastDoc = doc;
  const seq = ++renderSeq;
  issues.length = 0;
  files = new Set(doc?.refs ?? []);
  const root = document.getElementById('doc');
  root.querySelectorAll('.chart-box').forEach((el) => el.nebulaDispose?.());
  const blocks = doc?.blocks ?? [];
  // the title belongs to the chrome around the page (pane title bar or modal header), not to the page itself
  const slots = blocks.map((b) => h('div.slot', { 'data-block': b.index }));
  root.replaceChildren(...slots);
  const jobs = blocks.map(async (block, i) => {
    const mounted = [];
    const bctx = { ...ctx, afterMount: (fn) => mounted.push(fn) };
    let el;
    try {
      el = await renderBlock(block, bctx);
    } catch (e) {
      ctx.issue(`block at line ${block.line}: ${e.message}`);
      el = errorCard({ ...block, errors: [{ line: block.line, message: `failed to draw: ${e.message}` }] });
    }
    if (seq !== renderSeq) return;   // a newer document replaced this one while a chunk was loading
    el.dataset.block = block.index;
    if (block.id) el.id = `block-${block.id}`;
    slots[i].replaceWith(el);
    for (const fn of mounted) {
      try { fn(); } catch (e) { ctx.issue(`drawing failed: ${e.message}`); }
    }
  });
  Promise.allSettled(jobs).then(() => {
    if (seq === renderSeq) bridge?.rendered(JSON.stringify({ blocks: blocks.length, issues }));
  });
}

// Links open in the system browser (nebula decides); the view itself never navigates.
document.addEventListener('click', (e) => {
  const a = e.target.closest?.('a[href]');
  if (!a) return;
  e.preventDefault();
  const href = a.getAttribute('href');
  if (href.startsWith('#')) document.getElementById(href.slice(1))?.scrollIntoView({ behavior: 'smooth' });
  else bridge?.openLink(href);
});

window.addEventListener('error', (e) => ctx.issue(`script error: ${e.message}`));
// errors inside html sandboxes arrive as messages from their frames
window.addEventListener('message', (e) => {
  if (e.data && typeof e.data.nebulaIssue === 'string' && [...document.querySelectorAll('iframe.sandbox')].some((f) => f.contentWindow === e.source))
    ctx.issue(`html block: ${e.data.nebulaIssue.slice(0, 300)}`);
});

window.addEventListener('DOMContentLoaded', () => {
  // eslint-disable-next-line no-undef
  new QWebChannel(qt.webChannelTransport, (channel) => {
    bridge = channel.objects.view;
    fileBase = bridge.fileBase;
    sandboxBase = bridge.sandboxBase;
    applyTheme(bridge.theme);
    render(bridge.document);
    // charts and sandboxes read the theme when they are drawn: redraw everything on a theme change
    bridge.themeChanged.connect(() => { applyTheme(bridge.theme); render(lastDoc); });
    bridge.documentChanged.connect(() => render(bridge.document));
  });
});
