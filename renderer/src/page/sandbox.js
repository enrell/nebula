// The html component: the block's markup runs in an iframe with sandbox="allow-scripts" (an opaque origin: no
// access to this page, the bridge, storage or cookies). nebula serves its document (with a CSP that has no
// network source and the theme as CSS variables) at nebula-view://app/sandbox/<view>/<block>.html.
export function sandboxFrame(url, height) {
  const frame = document.createElement('iframe');
  frame.setAttribute('sandbox', 'allow-scripts');
  frame.setAttribute('referrerpolicy', 'no-referrer');
  frame.className = 'sandbox';
  frame.style.height = `${height}px`;
  frame.src = url;
  return frame;
}
