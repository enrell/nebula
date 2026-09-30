// Tiny DOM builder: h('div.card', {title: 'x'}, child, 'text', ...). Text is always set as text, never parsed as HTML.
// A titled card, the common frame of most components.
export function card(kind, title, ...body) {
  return h(`section.card.${kind}`, title ? h('header.card-title', title) : null, ...body);
}

export function h(tag, attrs, ...children) {
  const [name, ...classes] = tag.split('.');
  const el = document.createElement(name || 'div');
  if (classes.length) el.className = classes.join(' ');
  if (attrs && (typeof attrs !== 'object' || attrs instanceof Node || Array.isArray(attrs))) { children.unshift(attrs); attrs = null; }
  for (const [k, v] of Object.entries(attrs ?? {})) {
    if (v === undefined || v === null || v === false) continue;
    if (k.startsWith('on')) el.addEventListener(k.slice(2), v);
    else if (k === 'style' && typeof v === 'object') Object.assign(el.style, v);
    else el.setAttribute(k, v === true ? '' : v);
  }
  append(el, children);
  return el;
}

function append(el, children) {
  for (const c of children) {
    if (c === undefined || c === null || c === false) continue;
    if (Array.isArray(c)) append(el, c);
    else el.append(c instanceof Node ? c : document.createTextNode(String(c)));
  }
}

// Text of a document file served by nebula-view:. XMLHttpRequest rather than fetch(): Chromium only lets fetch()
// reach a custom scheme registered with FetchApiAllowed, which Qt added in 6.6, and nebula supports 6.5.
// type 'arraybuffer' resolves to the bytes (Uint8Array).
export function readFile(url, type = 'text') {
  return new Promise((resolve, reject) => {
    const xhr = new XMLHttpRequest();
    xhr.open('GET', url);
    xhr.responseType = type;
    const body = () => (type === 'arraybuffer' ? new Uint8Array(xhr.response) : xhr.response);
    xhr.onload = () => (xhr.status === 200 || xhr.status === 0 ? resolve(body()) : reject(new Error(`${url}: HTTP ${xhr.status}`)));
    xhr.onerror = () => reject(new Error(`could not read ${decodeURIComponent(url.split('/').pop())}`));
    xhr.send();
  });
}
