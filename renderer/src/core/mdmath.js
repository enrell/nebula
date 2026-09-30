// markdown-it plugin for TeX math: $inline$ and $$display$$ (display on its own lines). Used by the checker, which
// validates every formula with KaTeX, and by the page, which renders placeholders that the math chunk typesets.
// $ followed or preceded by a space is plain text, so "costs $5 and $10" stays prose.

function inline(state, silent) {
  const s = state.src, start = state.pos;
  if (s[start] !== '$' || s[start + 1] === '$') return false;
  if (start > 0 && s[start - 1] === '\\') return false;
  const next = s[start + 1];
  if (next === undefined || /\s/.test(next)) return false;
  let end = start + 1;
  for (;;) {
    end = s.indexOf('$', end);
    if (end < 0) return false;
    if (s[end - 1] !== '\\') break;
    end++;
  }
  if (/\s/.test(s[end - 1]) || /\d/.test(s[end + 1] ?? '')) return false;
  if (!silent) {
    const t = state.push('math_inline', 'math', 0);
    t.content = s.slice(start + 1, end);
    t.markup = '$';
  }
  state.pos = end + 1;
  return true;
}

function block(state, startLine, endLine, silent) {
  let pos = state.bMarks[startLine] + state.tShift[startLine];
  const max = state.eMarks[startLine];
  if (state.src.slice(pos, pos + 2) !== '$$') return false;
  const firstRest = state.src.slice(pos + 2, max);
  let content, last = startLine;
  if (firstRest.trim().endsWith('$$') && firstRest.trim().length >= 2) content = firstRest.trim().slice(0, -2);
  else {
    const lines = [firstRest];
    let found = false;
    for (let l = startLine + 1; l < endLine; l++) {
      const text = state.src.slice(state.bMarks[l] + state.tShift[l], state.eMarks[l]);
      if (text.trim().endsWith('$$')) { lines.push(text.trim().slice(0, -2)); last = l; found = true; break; }
      lines.push(text);
    }
    if (!found) return false;
    content = lines.join('\n');
  }
  if (silent) return true;
  const t = state.push('math_block', 'math', 0);
  t.block = true;
  t.content = content.trim();
  t.map = [startLine, last + 1];
  t.markup = '$$';
  state.line = last + 1;
  return true;
}

const escape = (s) => s.replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' })[c]);

export function mathPlugin(md) {
  md.inline.ruler.after('escape', 'math_inline', inline);
  md.block.ruler.after('blockquote', 'math_block', block, { alt: ['paragraph', 'reference', 'blockquote', 'list'] });
  md.renderer.rules.math_inline = (tokens, i) => `<span class="math" data-tex="${escape(tokens[i].content)}">${escape(tokens[i].content)}</span>`;
  md.renderer.rules.math_block = (tokens, i) => `<div class="math display" data-tex="${escape(tokens[i].content)}">${escape(tokens[i].content)}</div>\n`;
}
