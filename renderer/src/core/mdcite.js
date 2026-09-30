// markdown-it plugin for citations: [@key], [@key, p. 12], [see @a; @b, ch. 2]. Produces a `citation` token with
// meta.items = [{key, prefix, locator}]. The checker resolves the keys against the bibliography and numbers them
// in order of first use; the page renders the numbers (env.citations: key -> number).
const KEY = /^@([A-Za-z0-9_][\w:.#$%&+?<>~/-]*)/;

function parseItems(body) {
  const items = [];
  for (const part of body.split(';')) {
    const at = part.indexOf('@');
    if (at < 0) return null;
    const prefix = part.slice(0, at).trim();
    const m = KEY.exec(part.slice(at));
    if (!m) return null;
    // a key may end in "." or ":" only inside the key, not as punctuation after it
    const key = m[1].replace(/[.:]+$/, '');
    const rest = part.slice(at + 1 + key.length).trim();
    if (rest && !rest.startsWith(',')) return null;
    items.push({ key, prefix: prefix || undefined, locator: rest.replace(/^,\s*/, '') || undefined });
  }
  return items.length ? items : null;
}

function citation(state, silent) {
  const s = state.src, start = state.pos;
  if (s[start] !== '[' || !s.slice(start + 1, start + 80).includes('@')) return false;
  const end = s.indexOf(']', start);
  if (end < 0 || s[end + 1] === '(' || s[end + 1] === '[') return false;   // a link, not a citation
  const items = parseItems(s.slice(start + 1, end));
  if (!items) return false;
  if (!silent) {
    const t = state.push('citation', 'cite', 0);
    t.meta = { items };
    t.content = s.slice(start, end + 1);
  }
  state.pos = end + 1;
  return true;
}

export function citePlugin(md) {
  md.inline.ruler.before('link', 'citation', citation);
}
