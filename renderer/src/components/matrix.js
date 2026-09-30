// A numeric grid: heatmap (default, with a colour scale and optional numbers in the cells), contour lines or a
// WebGL surface. From a .csv/.tsv grid (labels in the first row/column are detected), a .npy file or inline values.
import { parseDelimited } from '../core/csv.js';
import { isNumeric } from '../core/data.js';
import { base64ToBytes, parseNpy } from '../core/npy.js';
import { extname } from '../core/paths.js';

const MAX_CELLS = 250000;

export default {
  name: 'matrix',
  summary: 'A numeric grid as a heatmap, contour lines or a WebGL surface: correlation/confusion matrices, fields, images of data. From a .csv/.tsv grid (row/column labels detected), a NumPy .npy file or inline values.',
  schema: {
    type: 'object',
    additionalProperties: false,
    properties: {
      title: { type: 'string' },
      data: { type: 'string', description: 'relative path to a .csv, .tsv or .npy (1-D or 2-D) file' },
      values: { type: 'array', items: { type: 'array', items: { type: ['number', 'null'] } }, description: 'inline rows of numbers' },
      rows: { type: 'array', items: { type: ['string', 'number'] }, description: 'row labels' },
      cols: { type: 'array', items: { type: ['string', 'number'] }, description: 'column labels' },
      style: { enum: ['heatmap', 'contour', 'surface'], default: 'heatmap' },
      scale: { enum: ['sequential', 'diverging'], description: 'colour scale; diverging centres on 0 (default: diverging when values have both signs)' },
      levels: { type: 'integer', minimum: 2, maximum: 40, default: 10, description: 'contour levels' },
      annotate: { type: 'boolean', description: 'print values in the cells (default: when there are at most 150 cells)' },
      digits: { type: 'integer', minimum: 1, maximum: 8, default: 3, description: 'significant figures in cells and tooltips' },
      unit: { type: 'string' },
      height: { type: 'integer', minimum: 160, maximum: 1200 },
    },
  },
  example: 'title: Feature correlation\ndata: corr.csv\nscale: diverging\nannotate: true',
  resolve(props, ctx) {
    if ((props.data === undefined) === (props.values === undefined)) return ctx.error('', 'give exactly one of "data" or "values"');
    let grid, rows = props.rows, cols = props.cols;
    if (props.values) {
      grid = props.values;
      const w = grid[0]?.length ?? 0;
      const bad = grid.findIndex((r) => r.length !== w);
      if (!grid.length || !w) return ctx.error('/values', 'values must be a non-empty list of rows');
      if (bad >= 0) return ctx.error(`/values/${bad}`, `row has ${grid[bad].length} values, the first row has ${w}`);
    } else {
      const ext = extname(props.data);
      if (ext === 'npy') {
        const f = ctx.readBase64(props.data);
        if (!f.ok) return ctx.error('/data', f.error);
        const a = parseNpy(base64ToBytes(f.base64));
        if (a.error) return ctx.error('/data', `${props.data}: ${a.error}`);
        const [r, c = 1] = a.shape.length === 1 ? [1, a.shape[0]] : a.shape;
        grid = Array.from({ length: r }, (_, i) => a.data.slice(i * c, (i + 1) * c).map((v) => (Number.isFinite(v) ? v : null)));
      } else if (ext === 'csv' || ext === 'tsv') {
        const f = ctx.readText(props.data);
        if (!f.ok) return ctx.error('/data', f.error);
        let table = parseDelimited(f.text, ext === 'tsv' ? '\t' : ',');
        if (!table.length) return ctx.error('/data', `${props.data} is empty`);
        const numericRow = (r, from) => r.slice(from).every((v) => v.trim() === '' || isNumeric(v));
        const hasColLabels = !numericRow(table[0], 1);
        const body = hasColLabels ? table.slice(1) : table;
        const hasRowLabels = body.length > 0 && body.some((r) => r[0].trim() !== '' && !isNumeric(r[0]));
        if (hasColLabels) cols ??= table[0].slice(hasRowLabels ? 1 : 0);
        if (hasRowLabels) rows ??= body.map((r) => r[0]);
        const start = hasRowLabels ? 1 : 0;
        for (let i = 0; i < body.length; i++) {
          const bad = body[i].slice(start).findIndex((v) => v.trim() !== '' && !isNumeric(v));
          if (bad >= 0) return ctx.error('/data', `${props.data} row ${i + 1 + (hasColLabels ? 1 : 0)}: "${body[i][start + bad]}" is not a number`);
        }
        grid = body.map((r) => r.slice(start).map((v) => (v.trim() === '' ? null : Number(v))));
        const w = grid[0]?.length ?? 0;
        const bad = grid.findIndex((r) => r.length !== w);
        if (bad >= 0) return ctx.error('/data', `${props.data} row ${bad + 1 + (hasColLabels ? 1 : 0)} has ${grid[bad].length} values, expected ${w}`);
      } else return ctx.error('/data', `unsupported matrix file ".${ext}"`, 'use a .csv, .tsv or .npy file');
    }
    const nr = grid.length, nc = grid[0].length;
    if (nr * nc > MAX_CELLS) return ctx.error(props.data ? '/data' : '/values', `${nr}×${nc} is ${nr * nc} cells (max ${MAX_CELLS})`, 'downsample first');
    if (rows && rows.length !== nr) return ctx.error('/rows', `${rows.length} row labels for ${nr} rows`);
    if (cols && cols.length !== nc) return ctx.error('/cols', `${cols.length} column labels for ${nc} columns`);
    if (props.style === 'contour' && (nr < 2 || nc < 2)) return ctx.error('/style', 'contours need at least 2 rows and 2 columns');
    const finite = grid.flat().filter((v) => v !== null);
    if (!finite.length) return ctx.error(props.data ? '/data' : '/values', 'no numeric values');
    const min = Math.min(...finite), max = Math.max(...finite);
    return {
      title: props.title, grid, rows: rows?.map(String), cols: cols?.map(String), style: props.style, levels: props.levels,
      scale: props.scale ?? (min < 0 && max > 0 ? 'diverging' : 'sequential'), min, max,
      annotate: props.annotate ?? nr * nc <= 150, digits: props.digits, unit: props.unit ?? '',
      height: props.height ?? (props.style === 'surface' ? 440 : Math.min(640, Math.max(220, nr * 34 + 80))),
    };
  },
};
