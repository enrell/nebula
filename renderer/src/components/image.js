import { extname, isRemote } from '../core/paths.js';

const TYPES = ['png', 'jpg', 'jpeg', 'gif', 'webp', 'svg'];
const MAX_BYTES = 20 * 1024 * 1024;

export default {
  name: 'image',
  summary: 'An image file from the project (png, jpg, gif, webp, svg), path relative to the document.',
  shorthand: { scalar: 'src' },
  schema: {
    type: 'object',
    additionalProperties: false,
    required: ['src'],
    properties: {
      src: { type: 'string', description: 'relative path' },
      alt: { type: 'string' },
      caption: { type: 'string', description: 'Markdown (inline)' },
      width: { type: 'integer', minimum: 16, maximum: 4096, description: 'max display width in px' },
    },
  },
  example: 'src: docs/screenshots/main.png\ncaption: The new layout',
  resolve(props, ctx) {
    if (isRemote(props.src)) return ctx.error('/src', 'remote images are blocked (views have no network access)', 'download the file into the project and reference its relative path');
    if (!TYPES.includes(extname(props.src))) return ctx.error('/src', `unsupported image type "${extname(props.src) || '(none)'}"`, `use one of: ${TYPES.join(', ')}`);
    const st = ctx.stat(props.src);
    if (!st.ok) return ctx.error('/src', st.error);
    if (st.size > MAX_BYTES) return ctx.error('/src', `image is too large (${Math.round(st.size / 1048576)} MB, max 20 MB)`);
    ctx.ref(props.src);
    if (props.caption) ctx.markdown('/caption', props.caption);
    return props;
  },
};
