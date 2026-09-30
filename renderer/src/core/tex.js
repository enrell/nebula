// TeX validation with KaTeX: the same parser the page renders with, so what passes here renders there.
import katex from 'katex';

// -> undefined when fine, or { message, offset } (offset into the formula, when KaTeX knows it)
export function checkTex(tex, displayMode = false) {
  try {
    katex.renderToString(tex, { displayMode, throwOnError: true, strict: 'ignore', trust: false, maxExpand: 1000, maxSize: 50 });
    return undefined;
  } catch (e) {
    if (!(e instanceof katex.ParseError)) throw e;
    return { message: `TeX: ${e.rawMessage ?? e.message}`, offset: e.position };
  }
}
