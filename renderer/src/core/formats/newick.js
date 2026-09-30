// Newick trees: (A:0.1,(B:0.2,C:0.3)90:0.4)root; -> { name, length, support, children } or { error, col }.
export function parseNewick(text) {
  const s = text.trim();
  let i = 0;
  const fail = (message) => ({ error: message, col: i + 1 });
  const ws = () => { while (i < s.length && /\s/.test(s[i])) i++; };
  const label = () => {
    ws();
    if (s[i] === "'") {
      const end = s.indexOf("'", i + 1);
      if (end < 0) throw fail('unclosed quoted label');
      const v = s.slice(i + 1, end); i = end + 1; return v;
    }
    const m = /^[^():,;[\]\s]*/.exec(s.slice(i))[0];
    i += m.length;
    return m.replace(/_/g, ' ');
  };
  const comment = () => { ws(); if (s[i] === '[') { const end = s.indexOf(']', i); if (end < 0) throw fail('unclosed [comment]'); i = end + 1; } };
  function node(depth) {
    if (depth > 2000) throw fail('tree is nested too deeply');
    ws();
    const n = { children: [] };
    if (s[i] === '(') {
      i++;
      for (;;) {
        n.children.push(node(depth + 1));
        ws();
        if (s[i] === ',') { i++; continue; }
        if (s[i] === ')') { i++; break; }
        throw fail(i >= s.length ? 'missing ")"' : `expected "," or ")" but found "${s[i]}"`);
      }
    }
    const name = label();
    comment();
    if (n.children.length && /^\d+(\.\d+)?$/.test(name)) n.support = Number(name); else if (name) n.name = name;
    ws();
    if (s[i] === ':') {
      i++; ws();
      const m = /^[-+]?(\d+\.?\d*|\.\d+)([eE][-+]?\d+)?/.exec(s.slice(i));
      if (!m) throw fail('expected a branch length after ":"');
      n.length = Number(m[0]); i += m[0].length;
    }
    comment();
    return n;
  }
  try {
    const root = node(0);
    ws();
    if (s[i] === ';') i++;
    ws();
    if (i < s.length) return fail(`unexpected "${s[i]}" after the tree`);
    return { tree: root };
  } catch (e) {
    if (e && e.error) return e;
    throw e;
  }
}

export function leaves(n) { return n.children.length ? n.children.reduce((a, c) => a + leaves(c), 0) : 1; }
