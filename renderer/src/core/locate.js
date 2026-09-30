// Maps a JSON pointer inside a parsed YAML body back to a source line.
import { isMap, isSeq } from 'yaml';

export function pointerSegments(pointer) {
  if (!pointer) return [];
  return pointer.split('/').slice(1).map((s) => s.replace(/~1/g, '/').replace(/~0/g, '~')).map((s) => (/^\d+$/.test(s) ? Number(s) : s));
}

// Returns the character offset of the deepest YAML node on the path (or of the key `key` in the map found there).
export function offsetOf(doc, segments, key) {
  let node = doc.contents;
  let offset = node?.range?.[0] ?? 0;
  for (const seg of segments) {
    if (isMap(node)) {
      const pair = node.items.find((p) => (p.key?.value ?? p.key) === seg);
      if (!pair) break;
      offset = pair.key?.range?.[0] ?? offset;
      node = pair.value;
    } else if (isSeq(node)) {
      node = node.items[seg];
      if (!node) break;
    } else break;
    if (node?.range) offset = node.range[0];
  }
  if (key !== undefined && isMap(node)) {
    const pair = node.items.find((p) => (p.key?.value ?? p.key) === key);
    if (pair?.key?.range) offset = pair.key.range[0];
  }
  return offset;
}
