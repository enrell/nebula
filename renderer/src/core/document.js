// Splits a view document into blocks: Markdown text and ```nebula:<component> fenced components. A ```mermaid fence
// is the diagram component with the fence body as its source (raw: no YAML), since agents write mermaid that way.
// Line numbers are 1-based and refer to the whole source, so every diagnostic can point at the exact line.

const FENCE = /^ {0,3}(`{3,}|~{3,})(.*)$/;

export function splitDocument(source) {
  const lines = source.replace(/\r\n?/g, '\n').split('\n');
  const blocks = [];
  const problems = [];
  let i = 0;
  let front = null;

  if (lines[0] === '---') {
    const end = lines.indexOf('---', 1);
    if (end < 0) problems.push({ line: 1, message: 'front matter opened with --- but never closed', hint: 'end it with a line containing only ---' });
    else {
      front = { body: lines.slice(1, end).join('\n'), line: 2 };
      i = end + 1;
    }
  }

  let text = [];
  let textStart = i + 1;
  const flushText = () => {
    if (text.some((l) => l.trim())) blocks.push({ type: 'markdown', line: textStart, endLine: textStart + text.length - 1, text: text.join('\n') });
    text = [];
  };

  while (i < lines.length) {
    const m = FENCE.exec(lines[i]);
    if (!m) { if (!text.length) textStart = i + 1; text.push(lines[i]); i++; continue; }
    const [, fence, rawInfo] = m;
    const info = rawInfo.trim();
    const closes = (l) => new RegExp(`^ {0,3}${fence[0] === '`' ? '`' : '~'}{${fence.length},}\\s*$`).test(l);
    let j = i + 1;
    while (j < lines.length && !closes(lines[j])) j++;
    const closed = j < lines.length;
    const mermaid = /^mermaid$/i.test(info);
    if (!mermaid && !/^nebula:/i.test(info)) {
      // ordinary fenced code stays part of the Markdown around it
      if (!text.length) textStart = i + 1;
      text.push(...lines.slice(i, closed ? j + 1 : j));
      if (!closed) problems.push({ line: i + 1, message: `code fence ${fence} is never closed`, hint: `close it with ${fence}`, severity: 'warning' });
      i = closed ? j + 1 : j;
      continue;
    }
    flushText();
    const [tag, ...attrs] = info.split(/\s+/);
    const component = mermaid ? 'diagram' : tag.slice('nebula:'.length).toLowerCase();
    const block = { type: 'component', component, line: i + 1, bodyLine: i + 2, endLine: closed ? j + 1 : j, body: lines.slice(i + 1, j).join('\n'), attrs, fence };
    if (mermaid) block.raw = 'source';
    if (!closed) block.unclosed = true;
    blocks.push(block);
    i = closed ? j + 1 : j;
  }
  flushText();
  return { front, blocks, problems };
}
