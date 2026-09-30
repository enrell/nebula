// BibTeX: entries @type{key, field = {value} | "value" | number | macro, ...}, @string macros, # concatenation,
// @comment/@preamble skipped. LaTeX accents and braces in values are turned into plain Unicode text.

const ACCENTS = { '`': '̀', "'": '́', '^': '̂', '~': '̃', '=': '̄', '.': '̇', '"': '̈', u: '̆', v: '̌', H: '̋', c: '̧', k: '̨', r: '̊' };
const SYMBOLS = { ss: 'ß', ae: 'æ', AE: 'Æ', oe: 'œ', OE: 'Œ', o: 'ø', O: 'Ø', aa: 'å', AA: 'Å', l: 'ł', L: 'Ł', i: 'ı', j: 'ȷ', '&': '&', '%': '%', $: '$', _: '_', '#': '#' };
const MONTHS = { jan: 'January', feb: 'February', mar: 'March', apr: 'April', may: 'May', jun: 'June', jul: 'July', aug: 'August', sep: 'September', oct: 'October', nov: 'November', dec: 'December' };

export function latexToText(s) {
  return String(s)
    // formatting commands first, so their names are not read as accents (\url, \rm, \textrm)
    .replace(/\\(textit|textbf|textsc|textrm|texttt|emph|mathrm|text|url|href|rm|it|bf|sc|tt|em)(?![A-Za-z])\s*/g, '')
    .replace(/\\([`'^~=."uvHckr])\s*\{?\\?([A-Za-z])\}?/g, (_, a, ch) => (ch + ACCENTS[a]).normalize('NFC'))
    .replace(/\\(ss|ae|AE|oe|OE|aa|AA|[oOlLij])(?![A-Za-z])\s?/g, (_, n) => SYMBOLS[n])
    .replace(/\\([&%$_#])/g, (_, c) => c)
    .replace(/--/g, '–')
    .replace(/~/g, ' ')
    .replace(/[{}]/g, '')
    .replace(/\s+/g, ' ')
    .trim();
}

// -> { entries: Map(key -> {type, key, fields, line}), problems: [{line, message}] }
export function parseBibtex(text) {
  const entries = new Map(), problems = [], macros = { ...MONTHS };
  const src = text.replace(/\r/g, '');
  const lineAt = (i) => src.slice(0, i).split('\n').length;
  let i = 0;
  const ws = () => { while (i < src.length && /\s/.test(src[i])) i++; };
  const fail = (message, at = i) => { throw Object.assign(new Error(message), { line: lineAt(at) }); };
  function braced() {   // at "{": returns the content up to the matching "}"
    let depth = 0;
    const start = i;
    for (; i < src.length; i++) {
      if (src[i] === '\\') { i++; continue; }
      if (src[i] === '{') depth++;
      else if (src[i] === '}' && --depth === 0) { i++; return src.slice(start + 1, i - 1); }
    }
    return fail('unbalanced braces', start);
  }
  function value() {
    const parts = [];
    for (;;) {
      ws();
      if (src[i] === '{') parts.push(braced());
      else if (src[i] === '"') {
        const start = i++;
        let depth = 0;
        for (; i < src.length; i++) {
          if (src[i] === '\\') { i++; continue; }
          if (src[i] === '{') depth++;
          else if (src[i] === '}') depth--;
          else if (src[i] === '"' && depth === 0) break;
        }
        if (i >= src.length) fail('unterminated "string"', start);
        parts.push(src.slice(start + 1, i++));
      } else {
        const m = /^[A-Za-z0-9_:.+\-/]+/.exec(src.slice(i));
        if (!m) fail(`expected a value, found "${src[i] ?? 'end of file'}"`);
        i += m[0].length;
        parts.push(/^\d+$/.test(m[0]) ? m[0] : macros[m[0].toLowerCase()] ?? fail(`unknown macro "${m[0]}" (quote it or define it with @string)`, i - m[0].length));
      }
      ws();
      if (src[i] === '#') { i++; continue; }
      return parts.join('');
    }
  }
  while (i < src.length) {
    const at = src.indexOf('@', i);
    if (at < 0) break;
    i = at + 1;
    const start = at;
    try {
      const type = /^[A-Za-z]+/.exec(src.slice(i))?.[0];
      if (!type) fail('expected an entry type after @');
      i += type.length;
      ws();
      const open = src[i];
      if (open !== '{' && open !== '(') fail(`expected "{" after @${type}`);
      const close = open === '{' ? '}' : ')';
      const kind = type.toLowerCase();
      if (kind === 'comment' || kind === 'preamble') { if (open === '{') braced(); else i = src.indexOf(')', i) + 1; continue; }
      i++;
      ws();
      if (kind === 'string') {
        const name = /^[A-Za-z][\w-]*/.exec(src.slice(i))?.[0];
        if (!name) fail('expected a macro name in @string');
        i += name.length; ws();
        if (src[i] !== '=') fail('expected "=" in @string');
        i++;
        macros[name.toLowerCase()] = value();
        ws();
        if (src[i] !== close) fail(`expected "${close}" to end @string`);
        i++;
        continue;
      }
      const key = /^[^\s,{}()"#%'=]+/.exec(src.slice(i))?.[0];
      if (!key) fail(`@${type} has no citation key`);
      i += key.length;
      ws();
      const fields = {};
      while (src[i] === ',') {
        i++; ws();
        if (src[i] === close) break;
        const name = /^[A-Za-z][\w-]*/.exec(src.slice(i))?.[0];
        if (!name) fail(`expected a field name in ${key}`);
        i += name.length; ws();
        if (src[i] !== '=') fail(`expected "=" after ${name} in ${key}`);
        i++;
        fields[name.toLowerCase()] = value();
        ws();
      }
      if (src[i] !== close) fail(`expected "," or "${close}" in ${key}`);
      i++;
      if (entries.has(key)) problems.push({ line: lineAt(start), message: `duplicate key "${key}" (also on line ${entries.get(key).line})` });
      else entries.set(key, { type: kind, key, fields, line: lineAt(start) });
    } catch (e) {
      if (e.line === undefined) throw e;
      problems.push({ line: e.line, message: e.message });
      // skip to the next entry
      const next = src.indexOf('\n@', i);
      i = next < 0 ? src.length : next + 1;
    }
  }
  return { entries, problems };
}

// "Watson, James D. and Crick, F. H. C." -> ["Watson JD", "Crick FHC"]; {Consortium Name} stays whole
function authors(s) {
  if (!s) return [];
  return s.split(/\s+and\s+/).map((raw) => {
    const a = raw.trim();
    if (/^\{.*\}$/.test(a)) return latexToText(a);
    let last, first;
    if (a.includes(',')) [last, first] = a.split(',').map((x) => x.trim());
    else { const w = a.split(/\s+/); last = w.pop(); first = w.join(' '); }
    const initials = (first ?? '').split(/[\s.-]+/).filter(Boolean).map((w) => latexToText(w)[0]?.toUpperCase() ?? '').join('');
    return `${latexToText(last)}${initials ? ` ${initials}` : ''}`;
  });
}

// the parts a numbered reference list shows
export function formatEntry(e) {
  const f = e.fields;
  const list = authors(f.author ?? f.editor);
  const names = list.length > 6 ? `${list.slice(0, 6).join(', ')}, et al.` : list.join(', ');
  const container = f.journal ?? f.booktitle ?? f.publisher ?? f.school ?? f.institution ?? f.howpublished;
  const doi = f.doi ? latexToText(f.doi).replace(/^https?:\/\/(dx\.)?doi\.org\//i, '') : undefined;
  return {
    key: e.key, type: e.type,
    authors: names || undefined, editors: !f.author && f.editor ? true : undefined,
    year: f.year ? latexToText(f.year) : undefined,
    title: f.title ? latexToText(f.title) : undefined,
    container: container ? latexToText(container) : undefined,
    volume: f.volume ? latexToText(f.volume) : undefined,
    number: f.number ? latexToText(f.number) : undefined,
    pages: f.pages ? latexToText(f.pages).replace(/-+/g, '–') : undefined,
    doi,
    url: !doi && f.url && /^https?:\/\//.test(f.url.trim()) ? f.url.trim() : undefined,
    arxiv: (f.archiveprefix ?? f.eprinttype)?.toLowerCase() === 'arxiv' && f.eprint ? latexToText(f.eprint) : undefined,
  };
}
