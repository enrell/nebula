// A self-contained HTML copy of the drawn document: one file with no references to nebula. Live elements (charts,
// WebGL viewers, canvases) are frozen to images, files and fonts are inlined as data URLs, html blocks keep their
// sandbox as srcdoc frames, and controls that need nebula's scripts (sliders, players) are left out.
import { readFile } from './dom.js';

const MIME = { png: 'image/png', jpg: 'image/jpeg', jpeg: 'image/jpeg', gif: 'image/gif', webp: 'image/webp', svg: 'image/svg+xml', woff2: 'font/woff2' };
const INTERACTIVE = ['.controls', '.anim-bar', '.slice-bar', '.iv-slider', '.iv-divider .iv-handle', '.iv-hint', '.plot-tip', '.slot'];

function toBase64(bytes) {
  let s = '';
  for (let i = 0; i < bytes.length; i += 0x8000) s += String.fromCharCode(...bytes.subarray(i, i + 0x8000));
  return btoa(s);
}

async function dataUrl(url) {
  const ext = url.split('?')[0].split('.').pop().toLowerCase();
  return `data:${MIME[ext] ?? 'application/octet-stream'};base64,${toBase64(await readFile(url, 'arraybuffer'))}`;
}

// KaTeX's stylesheet with its fonts inlined (only when the document has math)
async function katexCss() {
  const link = document.querySelector('link[href$="katex.css"]');
  if (!link) return '';
  let css = await readFile(link.href);
  const urls = [...new Set([...css.matchAll(/url\(([^)]+\.woff2)\)/g)].map((m) => m[1]))];
  for (const u of urls) css = css.split(`url(${u})`).join(`url(${await dataUrl(new URL(u, link.href).href)})`);
  return css;
}

export async function exportHtml(title) {
  const root = document.getElementById('doc');
  // pair every original element that needs work with its copy
  const originals = [...root.querySelectorAll('.live, canvas, img, iframe')];
  originals.forEach((el, i) => el.setAttribute('data-export', i));
  const copy = root.cloneNode(true);
  originals.forEach((el) => el.removeAttribute('data-export'));
  const twin = (i) => copy.querySelector(`[data-export="${i}"]`);
  const frozen = new Set();
  for (const [i, el] of originals.entries()) {
    const c = twin(i);
    if (!c || [...frozen].some((f) => f.contains(c))) continue;
    const box = el.getBoundingClientRect();
    const still = (src) => Object.assign(document.createElement('img'), { src, alt: '', style: `display:block;width:${Math.round(box.width)}px;max-width:100%;height:auto` });
    if (el.classList.contains('live') && el.nebulaSnapshot) {
      c.replaceChildren(still(await el.nebulaSnapshot()));
      c.style.height = 'auto';
      frozen.add(c);
    } else if (el.tagName === 'CANVAS') {
      try { c.replaceWith(still(el.toDataURL('image/png'))); } catch { c.remove(); }
    } else if (el.tagName === 'IMG' && el.src.startsWith('nebula-view:')) {
      c.src = await dataUrl(el.src);
      c.removeAttribute('loading');
    } else if (el.tagName === 'IFRAME') {
      c.setAttribute('srcdoc', await readFile(el.src));
      c.removeAttribute('src');
    }
  }
  copy.querySelectorAll('[data-export]').forEach((el) => el.removeAttribute('data-export'));
  copy.querySelectorAll(INTERACTIVE.join(',')).forEach((el) => el.remove());
  const css = await readFile(new URL('page.css', location.href).href);
  const vars = document.documentElement.getAttribute('style') ?? '';
  const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' })[c]);
  const stamp = new Date().toLocaleString(undefined, { dateStyle: 'medium', timeStyle: 'short' });
  return `<!doctype html>
<html lang="en" style="${esc(vars)}">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="generator" content="nebula">
<title>${esc(title || 'nebula view')}</title>
<style>${css}</style>
<style>${await katexCss()}</style>
<style>.export-foot{margin-top:24px;color:var(--muted);font-size:.85em}</style>
</head>
<body>
<main id="doc">${copy.innerHTML}</main>
<footer class="export-foot">Exported from nebula on ${esc(stamp)}. Charts and 3D views are still images; open the document in nebula to interact.</footer>
</body>
</html>
`;
}

// Scrolls a block into view and waits until it is painted, so nebula can grab it.
export async function prepareSnapshot(block) {
  if (block >= 0) {
    const el = document.querySelector(`#doc > [data-block="${block}"]`);
    if (!el) throw new Error(`no block ${block} (blocks are numbered from 0 in document order)`);
    el.scrollIntoView({ block: 'start' });
  }
  await new Promise((r) => requestAnimationFrame(() => requestAnimationFrame(r)));
  await new Promise((r) => setTimeout(r, 250));
}

// PDF: printing lays the page out at paper width, where canvases would keep their screen size and be cut off.
// Each live element gets a still image that print CSS shows instead of it, full width; removed after printing.
export async function preparePrint() {
  const add = (el, src) => {
    el.classList.add('has-still');
    el.after(Object.assign(document.createElement('img'), { src, className: 'print-still', alt: '' }));
  };
  const frozen = [];
  for (const el of document.querySelectorAll('#doc .live')) {
    if (!el.nebulaSnapshot || frozen.some((f) => f.contains(el))) continue;
    add(el, await el.nebulaSnapshot());
    frozen.push(el);
  }
  for (const canvas of document.querySelectorAll('#doc canvas')) {
    if (frozen.some((f) => f.contains(canvas))) continue;
    try { add(canvas, canvas.toDataURL('image/png')); } catch { /* a tainted or lost canvas prints as it is */ }
  }
}

export function endPrint() {
  document.querySelectorAll('.print-still').forEach((el) => el.remove());
  document.querySelectorAll('.has-still').forEach((el) => el.classList.remove('has-still'));
}
