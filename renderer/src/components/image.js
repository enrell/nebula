import { extname, isRemote } from '../core/paths.js';

const TYPES = ['png', 'jpg', 'jpeg', 'gif', 'webp', 'svg'];
const MAX_BYTES = 20 * 1024 * 1024;

export default {
  name: 'image',
  summary: 'An image file from the project (png, jpg, gif, webp, svg): zoom and pan, a before/after comparison slider (compare), and a scale bar from the pixel size (scale: "0.65 µm").',
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
      compare: { type: 'string', description: 'relative path of a second image of the same scene, revealed with a slider' },
      labels: { type: 'array', items: { type: 'string' }, minItems: 2, maxItems: 2, description: 'names of src and compare, e.g. [before, after]' },
      zoom: { type: 'boolean', default: false, description: 'wheel to zoom, drag to pan, double-click to reset' },
      scale: { type: 'string', description: 'size of one image pixel, e.g. "0.65 µm", "2.5 nm", "30 m": draws a scale bar' },
    },
  },
  example: 'src: img/cells.png\ncompare: img/cells-segmented.png\nlabels: [raw, segmented]\nscale: "0.65 µm"\nzoom: true',
  resolve(props, ctx) {
    for (const key of ['src', 'compare']) {
      const path = props[key];
      if (path === undefined) continue;
      if (isRemote(path)) return ctx.error(`/${key}`, 'remote images are blocked (views have no network access)', 'download the file into the project and reference its relative path');
      if (!TYPES.includes(extname(path))) return ctx.error(`/${key}`, `unsupported image type "${extname(path) || '(none)'}"`, `use one of: ${TYPES.join(', ')}`);
      const st = ctx.stat(path);
      if (!st.ok) return ctx.error(`/${key}`, st.error);
      if (st.size > MAX_BYTES) return ctx.error(`/${key}`, `image is too large (${Math.round(st.size / 1048576)} MB, max 20 MB)`);
      ctx.ref(path);
    }
    if (props.labels && !props.compare) ctx.warn('/labels', 'labels name the two images of a comparison; there is no compare image');
    let scale;
    if (props.scale !== undefined) {
      const m = /^\s*(\d+(?:\.\d+)?(?:[eE][-+]?\d+)?)\s*([^\s\d][^\s]*)\s*$/.exec(props.scale);
      if (!m || !(Number(m[1]) > 0)) return ctx.error('/scale', `"${props.scale}" is not a pixel size`, 'write a number and a unit, e.g. "0.65 µm" or "30 m"');
      scale = { size: Number(m[1]), unit: m[2] };
    }
    if (props.caption) ctx.markdown('/caption', props.caption);
    return { ...props, scale };
  },
};
