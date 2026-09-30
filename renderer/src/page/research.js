// The research record: the numbered reference list and the provenance card.
import { card, h } from './dom.js';

const link = (href, text) => h('a', { href }, text);

export function referenceList(entries, title = 'References') {
  return h('section.card.references-card', h('header.card-title', title), h('ol.references', entries.map((e) => h('li', { id: `ref-${e.n}`, value: e.n },
    e.authors ? `${e.authors}${e.editors ? ' (eds.)' : ''}. ` : '',
    e.year ? `${e.year}. ` : '',
    e.title ? h('span.ref-title', `${e.title}. `) : '',
    e.container ? h('i', e.container) : '',
    e.volume ? ` ${e.volume}` : '', e.number ? `(${e.number})` : '', e.pages ? `:${e.pages}` : '', e.container || e.volume || e.pages ? '. ' : '',
    e.doi ? link(`https://doi.org/${e.doi}`, `doi:${e.doi}`) : null,
    e.arxiv ? link(`https://arxiv.org/abs/${e.arxiv}`, `arXiv:${e.arxiv}`) : null,
    e.url ? link(e.url, e.url) : null))));
}

export function references(p, ctx) {
  return referenceList(ctx.bibliography, p.title);
}

const size = (n) => (n < 1024 ? `${n} B` : n < 1048576 ? `${(n / 1024).toFixed(1)} KB` : `${(n / 1048576).toFixed(1)} MB`);
const when = (iso) => { const d = new Date(iso); return Number.isNaN(d.getTime()) ? '' : d.toLocaleString(undefined, { dateStyle: 'medium', timeStyle: 'short' }); };

export function provenance(p, ctx) {
  const rows = [
    p.command ? ['command', h('code', p.command)] : null,
    p.script ? ['script', h('code', p.script)] : null,
    p.seed !== undefined ? ['seed', h('code', String(p.seed))] : null,
    p.environment ? ['environment', h('span.prov-env', Object.entries(p.environment).map(([k, v]) => h('code', `${k} ${v}`)))] : null,
    p.git ? ['git', h('span', h('code', p.git.commit.slice(0, 12)), p.git.branch ? ` on ${p.git.branch}` : '')] : null,
    ['checked', when(p.checked)],
  ].filter(Boolean);
  const inputs = p.inputs ?? [];
  return card('provenance-card', p.title,
    h('dl.prov', rows.map(([k, v]) => [h('dt', k), h('dd', v)])),
    inputs.length ? h('div.table-scroll', h('table.prov-inputs',
      h('thead', h('tr', h('th', 'input'), h('th.align-right', 'size'), h('th', 'sha256'), h('th', 'modified'))),
      h('tbody', inputs.map((f) => h('tr', h('td', h('code', f.path)), h('td.align-right', f.size !== undefined ? size(f.size) : ''),
        h('td', f.sha256 ? h('code', { title: f.sha256 }, f.sha256.slice(0, 16)) : ''), h('td', f.modified ? when(f.modified) : '')))))) : null,
    p.notes ? h('div.prov-notes', ctx.markdown(p.notes)) : null,
    h('footer.card-foot', `${inputs.length} input file${inputs.length === 1 ? '' : 's'} · hashes computed by nebula when the document was checked`));
}
