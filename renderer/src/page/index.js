// The view page: receives an already checked document from nebula over QWebChannel and draws it.
// It never parses the source itself; problems found while drawing are reported back as render issues.
import { renderers } from './components.js';
import { h } from './dom.js';
import { createMarkdown } from './markdown.js';

let bridge;
let fileBase = '';
let files = new Set();   // files the checker verified; anything else would be refused by nebula anyway
const issues = [];
const ctx = {
  hasFile: (path) => files.has(path),
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

function renderBlock(block) {
  if (!block.ok) return errorCard(block);
  if (block.type === 'markdown') return ctx.markdown(block.text);
  const render = renderers[block.component];
  if (!render) return errorCard({ ...block, errors: [{ line: block.line, message: `this nebula build cannot draw "${block.component}"` }] });
  return render(block.props, ctx);
}

function render(doc) {
  issues.length = 0;
  files = new Set(doc?.refs ?? []);
  const root = document.getElementById('doc');
  const nodes = [];
  if (doc?.title) nodes.push(h('h1.doc-title', doc.title));
  for (const block of doc?.blocks ?? []) {
    try {
      const el = renderBlock(block);
      el.dataset.block = block.index;
      if (block.id) el.id = `block-${block.id}`;
      nodes.push(el);
    } catch (e) {
      ctx.issue(`block at line ${block.line}: ${e.message}`);
      nodes.push(errorCard({ ...block, errors: [{ line: block.line, message: `failed to draw: ${e.message}` }] }));
    }
  }
  root.replaceChildren(...nodes);
  bridge?.rendered(JSON.stringify({ blocks: nodes.length, issues }));
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

window.addEventListener('DOMContentLoaded', () => {
  // eslint-disable-next-line no-undef
  new QWebChannel(qt.webChannelTransport, (channel) => {
    bridge = channel.objects.view;
    fileBase = bridge.fileBase;
    applyTheme(bridge.theme);
    render(bridge.document);
    bridge.themeChanged.connect(() => applyTheme(bridge.theme));
    bridge.documentChanged.connect(() => render(bridge.document));
  });
});
