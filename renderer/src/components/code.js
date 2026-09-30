import { extname } from '../core/paths.js';

const MAX_LINES = 2000;
const LANGS = {
  c: 'c', h: 'cpp', cc: 'cpp', cpp: 'cpp', cxx: 'cpp', hpp: 'cpp', js: 'javascript', mjs: 'javascript', cjs: 'javascript', jsx: 'javascript',
  ts: 'typescript', tsx: 'typescript', py: 'python', rs: 'rust', go: 'go', java: 'java', kt: 'kotlin', rb: 'ruby', php: 'php', cs: 'csharp',
  swift: 'swift', sh: 'bash', bash: 'bash', zsh: 'bash', fish: 'bash', json: 'json', yml: 'yaml', yaml: 'yaml', toml: 'ini', ini: 'ini',
  md: 'markdown', html: 'xml', xml: 'xml', svg: 'xml', css: 'css', scss: 'scss', sql: 'sql', lua: 'lua', qml: 'javascript', cmake: 'cmake',
  diff: 'diff', patch: 'diff', dockerfile: 'dockerfile', makefile: 'makefile',
};

export default {
  name: 'code',
  summary: 'Source code, inline (code) or from a file (file, optional lines "10-40"), with syntax highlighting.',
  schema: {
    type: 'object',
    additionalProperties: false,
    properties: {
      file: { type: 'string', description: 'relative path; read from disk, so the code need not be pasted' },
      lines: { type: 'string', pattern: '^[1-9][0-9]*(-[1-9][0-9]*)?$', description: 'line range, e.g. 10-40' },
      code: { type: 'string' },
      lang: { type: 'string', description: 'language; inferred from the file extension' },
      title: { type: 'string' },
    },
  },
  example: 'file: src/workspace.cpp\nlines: 120-160\ntitle: Tab::closePane',
  resolve(props, ctx) {
    if ((props.file === undefined) === (props.code === undefined)) return ctx.error('', 'give exactly one of "file" or "code"');
    if (props.lines && !props.file) return ctx.error('/lines', '"lines" needs "file"');
    let code = props.code;
    let first = 1;
    if (props.file) {
      const f = ctx.readText(props.file);
      if (!f.ok) return ctx.error('/file', f.error);
      let lines = f.text.replace(/\r\n/g, '\n').replace(/\n$/, '').split('\n');
      if (props.lines) {
        const [a, b = a] = props.lines.split('-').map(Number);
        if (b < a) return ctx.error('/lines', `range ${props.lines} is reversed`);
        if (a > lines.length) return ctx.error('/lines', `range starts after the end of the file (${lines.length} lines)`);
        lines = lines.slice(a - 1, b);
        first = a;
      }
      code = lines.join('\n');
    }
    let lines = code.split('\n');
    if (lines.length > MAX_LINES) {
      ctx.warn(props.file ? '/lines' : '/code', `showing the first ${MAX_LINES} of ${lines.length} lines`, props.file ? 'narrow it down with "lines"' : undefined);
      lines = lines.slice(0, MAX_LINES);
    }
    const ext = props.file ? extname(props.file) || props.file.split('/').pop().toLowerCase() : '';
    return { title: props.title ?? props.file, lang: props.lang ?? LANGS[ext] ?? '', code: lines.join('\n'), firstLine: first };
  },
};
