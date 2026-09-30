import { checkTex } from '../core/tex.js';

export default {
  name: 'math',
  summary: 'A displayed equation in TeX (KaTeX), optionally numbered. Inline math works in any Markdown text as $...$, display math as $$...$$.',
  shorthand: { scalar: 'tex' },
  schema: {
    type: 'object',
    additionalProperties: false,
    required: ['tex'],
    properties: {
      tex: { type: 'string', description: 'TeX source; use a | block for several lines' },
      number: { type: 'boolean', default: false, description: 'number the equation (1), (2), …' },
      caption: { type: 'string', description: 'Markdown (inline) under the equation' },
    },
  },
  example: 'tex: |\n  \\mathcal{L}(\\theta) = -\\frac{1}{N}\\sum_{i=1}^{N} y_i \\log \\hat y_i\nnumber: true',
  resolve(props, ctx) {
    const bad = checkTex(props.tex, true);
    if (bad) return ctx.error('/tex', bad.message + (bad.offset !== undefined ? ` (at character ${bad.offset + 1})` : ''), 'KaTeX supports most of LaTeX math: https://katex.org/docs/supported');
    if (props.caption) ctx.markdown('/caption', props.caption);
    return props;
  },
};
