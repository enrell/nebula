import { extname } from '../core/paths.js';

const MAX_BYTES = 512 * 1024;

export default {
  name: 'html',
  summary: 'Escape hatch for anything the other components cannot draw: your own HTML/CSS/JS (canvas, WebGL, SVG), in a sandbox without network access or access to nebula. Theme colours are CSS variables (--bg, --fg, --accent, ...).',
  schema: {
    type: 'object',
    additionalProperties: false,
    properties: {
      html: { type: 'string', description: 'the markup (body content or a full document)' },
      file: { type: 'string', description: 'relative path to an .html file instead of html' },
      title: { type: 'string' },
      height: { type: 'integer', minimum: 60, maximum: 2000, default: 360 },
    },
  },
  example: 'height: 240\nhtml: |\n  <canvas id="c" width="400" height="200"></canvas>\n  <script>\n    const g = document.getElementById("c").getContext("2d");\n    g.fillStyle = getComputedStyle(document.body).getPropertyValue("--accent");\n    g.fillRect(20, 20, 160, 120);\n  </script>',
  resolve(props, ctx) {
    if ((props.html === undefined) === (props.file === undefined)) return ctx.error('', 'give exactly one of "html" or "file"');
    let html = props.html;
    if (props.file !== undefined) {
      if (!['html', 'htm'].includes(extname(props.file))) return ctx.error('/file', 'expected an .html file');
      const f = ctx.readText(props.file);
      if (!f.ok) return ctx.error('/file', f.error);
      html = f.text;
    }
    if (html.length > MAX_BYTES) return ctx.error(props.file ? '/file' : '/html', `too large (${Math.round(html.length / 1024)} KB, max 512 KB)`);
    if (/\b(src|href)\s*=\s*["']?\s*(https?:)?\/\//i.test(html) || /\b(fetch|XMLHttpRequest|WebSocket)\b/.test(html))
      ctx.warn(props.file ? '/file' : '/html', 'network access is blocked inside views: remote scripts, styles, images and requests will not load', 'inline what you need (libraries included)');
    return { html, title: props.title, height: props.height };
  },
};
