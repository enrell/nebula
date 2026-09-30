// KaTeX, loaded the first time a document contains math. Typesets the placeholders the Markdown renderer emits
// (span.math / div.math.display, with the TeX in data-tex) and draws `math` blocks.
import katex from 'katex';
import { h } from '../dom.js';

function ensureCss() {
  if (document.querySelector('link[data-katex]')) return;
  document.head.append(h('link', { rel: 'stylesheet', href: 'katex.css', 'data-katex': true }));
}

const options = (displayMode) => ({ displayMode, throwOnError: false, strict: 'ignore', trust: false, output: 'htmlAndMathml', maxExpand: 1000, maxSize: 50 });

export function typeset(root) {
  ensureCss();
  for (const el of root.querySelectorAll('.math[data-tex]')) {
    katex.render(el.dataset.tex, el, options(el.classList.contains('display')));
    el.removeAttribute('data-tex');
  }
}

export function math(p, ctx) {
  ensureCss();
  const eq = h('div.math-eq');
  katex.render(p.tex, eq, options(true));
  return h('figure.math-block', h('div.math-row', eq, p.n ? h('span.math-number', `(${p.n})`) : null),
    p.caption ? h('figcaption', ctx.inline(p.caption)) : null);
}
