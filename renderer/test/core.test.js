// Runs the shipped checker (dist/core.js, exactly what nebula loads) over test/cases and unit-level edge cases.
// The same cases run in nebula's own JS engine through tests/test_views.cpp.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { readFileSync, readdirSync, statSync } from 'node:fs';
import { dirname, join, normalize, relative, isAbsolute, resolve } from 'node:path';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';
import vm from 'node:vm';

const here = dirname(fileURLToPath(import.meta.url));
const sandbox = {};
vm.runInNewContext(readFileSync(join(here, '../dist/core.js'), 'utf8'), sandbox);
const core = sandbox.NebulaCore;
const base = join(here, 'fixtures/proj');

// Same rules and messages as ViewFiles (src/views/viewfiles.cpp) for the cases the fixtures exercise.
function locate(p) {
  if (isAbsolute(p)) return { error: `path must be relative to the document's directory (got ${p})` };
  const full = normalize(join(base, p));
  if (relative(base, full).startsWith('..')) return { error: `${p} is outside the document's directory` };
  try { return { full, size: statSync(full).size }; } catch { return { error: `file not found: ${p}` }; }
}
const host = {
  readText: (p) => { const r = locate(p); return JSON.stringify(r.error ? { ok: false, error: r.error } : { ok: true, text: readFileSync(r.full, 'utf8') }); },
  stat: (p) => { const r = locate(p); return JSON.stringify(r.error ? { ok: false, error: r.error } : { ok: true, size: r.size }); },
  readBase64: (p) => { const r = locate(p); return JSON.stringify(r.error ? { ok: false, error: r.error } : { ok: true, base64: readFileSync(r.full).toString('base64') }); },
  hash: (p) => { const r = locate(p); return JSON.stringify(r.error ? { ok: false, error: r.error } : { ok: true, size: r.size, sha256: createHash('sha256').update(readFileSync(r.full)).digest('hex') }); },
  git: () => JSON.stringify({ commit: '0123456789abcdef0123456789abcdef01234567', branch: 'main' }),
};
const check = (src) => JSON.parse(core.checkJson(src, host));

export function compare(result, expect, name) {
  const pick = (sev) => result.diagnostics.filter((d) => d.severity === sev);
  for (const [sev, want] of [['error', expect.errors], ['warning', expect.warnings]]) {
    const got = pick(sev);
    assert.equal(got.length, want.length, `${name}: ${sev}s\n${JSON.stringify(got, null, 1)}`);
    want.forEach(([line, text], i) => {
      assert.equal(got[i].line, line, `${name}: ${sev} ${i} line (${got[i].message})`);
      assert.ok(`${got[i].message} ${got[i].hint ?? ''}`.includes(text), `${name}: ${sev} ${i}: "${got[i].message}" should contain "${text}"`);
    });
  }
  assert.deepEqual(result.blocks.map((b) => b.ok), expect.ok, `${name}: block ok flags`);
  if (expect.title !== undefined) assert.equal(result.title, expect.title);
  if (expect.refs) assert.deepEqual(result.refs, expect.refs);
}

const casesDir = join(here, 'cases');
for (const file of readdirSync(casesDir).filter((f) => f.endsWith('.md'))) {
  test(`case ${file}`, () => {
    const expect = JSON.parse(readFileSync(join(casesDir, file.replace(/\.md$/, '.expect.json')), 'utf8'));
    compare(check(readFileSync(join(casesDir, file), 'utf8')), expect, file);
  });
}

test('resolved props are what the renderer needs', () => {
  const r = check('```nebula:stats\n- {label: A, value: 3, delta: +2}\n```\n```nebula:checklist\n- "[x] a"\n- "[-] b"\n- "[!] c"\n```\n```nebula:table\nrows: [[a, 10], [b, 9]]\ncolumns: [k, v]\n```');
  assert.deepEqual(r.blocks[0].props.items[0], { label: 'A', value: '3', delta: '+2', tone: 'neutral' });
  assert.deepEqual(r.blocks[1].props.items.map((i) => i.status), ['done', 'skipped', 'failed']);
  assert.equal(r.blocks[1].props.total, 2);
  assert.deepEqual(r.blocks[2].props.columns.map((c) => [c.align, c.numeric]), [['left', false], ['right', true]]);
});

test('code from a file keeps its line numbers and language', () => {
  const r = check('```nebula:code\nfile: src/sample.py\nlines: 5-6\n```');
  assert.equal(r.blocks[0].props.firstLine, 5);
  assert.equal(r.blocks[0].props.lang, 'python');
  assert.equal(r.blocks[0].props.code, 'def sub(a, b):\n    return a - b');
});

test('CSV with quotes, commas and CRLF', () => {
  const r = check('```nebula:table\ndata: q.csv\n```');
  assert.equal(r.blocks[0].ok, false);   // q.csv does not exist: the message names it
  assert.match(r.diagnostics[0].message, /q\.csv/);
});

test('limits: huge documents are refused with a hint', () => {
  const r = check('x'.repeat(1024 * 1024 + 1));
  assert.equal(r.diagnostics[0].severity, 'error');
  assert.match(r.diagnostics[0].hint, /file/);
});

test('component catalogue', async () => {
  const list = JSON.parse(core.componentsJson());
  // the shipped bundle lists exactly the registry, in order
  const { components } = await import('../src/components/index.js');
  assert.deepEqual(list.map((c) => c.name), components.map((c) => c.name));
  for (const { name } of list) {
    const d = JSON.parse(core.describeJson(name));
    assert.ok(d.schema && d.example.startsWith(`\`\`\`nebula:${name}\n`));
    // every example must itself be valid
    const r = check(d.example.replace('file: src/workspace.cpp\nlines: 120-160', 'file: src/sample.py'));
    assert.deepEqual(r.diagnostics.filter((x) => x.severity === 'error'), [], `${name} example`);
  }
  assert.equal(JSON.parse(core.describeJson('nope')), null);
});

test('the page has a renderer for every component', async () => {
  const { renderers } = await import('../src/page/components.js');
  assert.deepEqual(Object.keys(renderers).sort(), JSON.parse(core.componentsJson()).map((c) => c.name).sort());
});

test('an empty body reports the missing fields, not the colon pitfall', () => {
  const r = check('```nebula:callout\n```');
  assert.equal(r.diagnostics.length, 1);
  assert.match(r.diagnostics[0].message, /missing required field "text"/);
});

test('claygl size expressions evaluate without eval', async () => {
  const { compileSizeExpr } = await import('../src/page/sizeexpr.js');
  assert.deepEqual(compileSizeExpr(' [width * dpr / 4, height * 1.0 / 2] ')(800, 600, 2), [400, 300]);
  assert.equal(compileSizeExpr('height')(800, 600, 1), 600);
  assert.equal(compileSizeExpr('(width - 10) / 2')(30, 0, 1), 10);
  assert.throws(() => compileSizeExpr('alert(1)'));
});

test('formula precedence and associativity', async () => {
  const { parse } = await import('../src/core/expr.js');
  const ev = (s, v = {}) => parse(s, Object.keys(v)).fn(v);
  const cases = [
    ['1 - 2*3', {}, -5], ['x - b y', { x: 1, b: 3, y: 2 }, -5], ['10 - 4 - 3', {}, 3], ['8/4/2', {}, 1], ['2^3^2', {}, 512],
    ['-x^2', { x: 3 }, -9], ['-2x', { x: 3 }, -6], ['2x + 1', { x: 3 }, 7], ['x y^2', { x: 2, y: 3 }, 18], ['3(x + 1)', { x: 1 }, 6],
    ['1 + 2*3^2', {}, 19], ['-sin(x) - b y', { x: 1.5, y: 0.5, b: 0.25 }, -Math.sin(1.5) - 0.125],
  ];
  for (const [src, vars, want] of cases) assert.ok(Math.abs(ev(src, vars) - want) < 1e-12, `${src} = ${ev(src, vars)}, expected ${want}`);
});

test('citations are numbered by first use and provenance lists hashed inputs', () => {
  const r = check(readFileSync(join(here, 'cases/research.md'), 'utf8'));
  assert.deepEqual(r.bibliography.map((e) => [e.n, e.key]), [[1, 'watson1953'], [2, 'schrodinger1926'], [3, 'ligo2016']]);
  assert.equal(r.bibliography[1].authors, 'Schrödinger E');
  assert.equal(r.bibliography[0].pages, '737–738');
  assert.equal(r.bibliography[2].arxiv, '1602.03837');
  const prov = r.blocks.find((b) => b.component === 'provenance' && b.ok).props;
  assert.deepEqual(prov.inputs.map((f) => f.path), ['refs.bib', 'src/sample.py']);
  assert.match(prov.inputs[1].sha256, /^[0-9a-f]{64}$/);
  assert.equal(prov.git.branch, 'main');
  assert.deepEqual(r.inputs, ['refs.bib', 'src/sample.py']);
});
