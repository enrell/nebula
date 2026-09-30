// Markdown -> HTML with raw HTML disabled (markdown-it escapes it) and safe links only.
import MarkdownIt from 'markdown-it';
import { citePlugin } from '../core/mdcite.js';
import { mathPlugin } from '../core/mdmath.js';
import { highlight } from './highlight.js';

const isRemote = (p) => /^[a-z][a-z0-9+.-]*:/i.test(p);

export function createMarkdown(ctx) {
  const md = new MarkdownIt({ html: false, linkify: true, typographer: false, highlight: (code, lang) => highlight(code, lang) }).use(mathPlugin).use(citePlugin);
  const image = md.renderer.rules.image;
  md.renderer.rules.image = (tokens, idx, opts, env, self) => {
    const t = tokens[idx];
    const src = t.attrGet('src');
    if (isRemote(src)) return `<span class="blocked" title="${md.utils.escapeHtml(src)}">[remote image not loaded]</span>`;
    if (!ctx.hasFile(decodeURI(src))) return `<span class="blocked">[image not found: ${md.utils.escapeHtml(src)}]</span>`;
    t.attrSet('src', ctx.fileUrl(decodeURI(src)));
    t.attrSet('loading', 'lazy');
    return image(tokens, idx, opts, env, self);
  };
  // [@a, p. 3; @b] -> [1, p. 3; 2], numbered by the checker in order of first citation
  md.renderer.rules.citation = (tokens, idx) => {
    const esc = md.utils.escapeHtml;
    const parts = tokens[idx].meta.items.map((it) => {
      const n = ctx.citation(it.key);
      const num = n ? `<a href="#ref-${n}">${n}</a>` : `<span class="blocked">?</span>`;
      return `${it.prefix ? `${esc(it.prefix)} ` : ''}${num}${it.locator ? `, ${esc(it.locator)}` : ''}`;
    });
    return `<span class="cite">[${parts.join('; ')}]</span>`;
  };
  return md;
}
