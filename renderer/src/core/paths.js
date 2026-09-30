export const isRemote = (p) => /^[a-z][a-z0-9+.-]*:/i.test(p);
export function extname(p) {
  const base = p.split('/').pop();
  const i = base.lastIndexOf('.');
  return i > 0 ? base.slice(i + 1).toLowerCase() : '';
}
