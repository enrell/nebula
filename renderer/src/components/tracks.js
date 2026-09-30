import { extname } from '../core/paths.js';
import { parseBed, parseRegion } from '../core/formats/bed.js';
import { parseDelimited } from '../core/csv.js';

const MAX_FEATURES = 20000;

export default {
  name: 'tracks',
  summary: 'Genome-browser style tracks over a region: genes/features (BED or inline, with exons and strand), signal (bedGraph or position,value) and variants.',
  schema: {
    type: 'object',
    additionalProperties: false,
    required: ['region', 'tracks'],
    properties: {
      title: { type: 'string' },
      region: { type: 'string', description: 'e.g. "chr7:117,480,000-117,670,000"' },
      tracks: {
        type: 'array', minItems: 1, maxItems: 20,
        items: {
          type: 'object', additionalProperties: false, required: ['type'],
          properties: {
            type: { enum: ['features', 'signal', 'variants'] },
            label: { type: 'string' },
            data: { type: 'string', description: 'features: .bed; signal: .bedgraph/.bg or .csv (position,value)' },
            features: { type: 'array', items: { type: 'object', additionalProperties: false, required: ['start', 'end'], properties: {
              start: { type: 'integer' }, end: { type: 'integer' }, name: { type: 'string' }, strand: { enum: ['+', '-'] },
              exons: { type: 'array', items: { type: 'array', items: { type: 'integer' }, minItems: 2, maxItems: 2 } } } } },
            values: { type: 'array', items: { type: 'array', items: { type: 'number' }, minItems: 2, maxItems: 2 }, description: 'signal: [[position, value], …]' },
            variants: { type: 'array', items: { type: 'object', additionalProperties: false, required: ['pos'], properties: {
              pos: { type: 'integer' }, ref: { type: 'string' }, alt: { type: 'string' }, label: { type: 'string' }, impact: { enum: ['high', 'moderate', 'low'] } } } },
            color: { enum: ['accent', 'blue', 'green', 'yellow', 'red'] },
            height: { type: 'integer', minimum: 20, maximum: 400 },
          },
        },
      },
    },
  },
  example: 'region: "chr7:117,480,000-117,670,000"\ntracks:\n  - {type: features, label: genes, data: genes.bed}\n  - {type: signal, label: coverage, data: coverage.bedgraph}\n  - {type: variants, variants: [{pos: 117559590, ref: CTT, alt: C, label: F508del, impact: high}]}',
  resolve(props, ctx) {
    const region = parseRegion(props.region);
    if (!region) return ctx.error('/region', `"${props.region}" is not a region`, 'write chrom:start-end, e.g. chr1:1,000,000-1,050,000');
    const inRegion = (s, e) => e >= region.start && s <= region.end;
    const tracks = [];
    for (const [i, t] of props.tracks.entries()) {
      const at = (k) => `/tracks/${i}${k ? `/${k}` : ''}`;
      const sources = ['data', 'features', 'values', 'variants'].filter((k) => t[k] !== undefined);
      const allowed = { features: ['data', 'features'], signal: ['data', 'values'], variants: ['variants'] }[t.type];
      if (sources.length !== 1 || !allowed.includes(sources[0])) return ctx.error(at(), `a ${t.type} track takes exactly one of: ${allowed.join(', ')}`);
      const out = { type: t.type, label: t.label ?? (t.data ?? t.type), color: t.color, height: t.height };
      if (t.type === 'features') {
        let feats = t.features;
        if (t.data !== undefined) {
          if (!['bed', 'txt'].includes(extname(t.data))) return ctx.error(at('data'), 'expected a .bed file');
          const f = ctx.readText(t.data);
          if (!f.ok) return ctx.error(at('data'), f.error);
          const r = parseBed(f.text);
          if (r.error) return ctx.error(at('data'), `${t.data}: ${r.error}`);
          feats = r.features.filter((x) => x.chrom === region.chrom);
        }
        out.features = feats.filter((x) => inRegion(x.start, x.end)).slice(0, MAX_FEATURES);
      } else if (t.type === 'signal') {
        let pts = t.values;
        if (t.data !== undefined) {
          const ext = extname(t.data);
          const f = ctx.readText(t.data);
          if (!f.ok) return ctx.error(at('data'), f.error);
          if (ext === 'bedgraph' || ext === 'bg') {
            const r = parseBed(f.text, { graph: true });
            if (r.error) return ctx.error(at('data'), `${t.data}: ${r.error}`);
            pts = r.features.filter((x) => x.chrom === region.chrom).map((x) => [Math.round((x.start + x.end) / 2), x.value]);
          } else if (ext === 'csv' || ext === 'tsv') {
            const rows = parseDelimited(f.text, ext === 'tsv' ? '\t' : ',');
            const body = rows.filter((r) => Number.isFinite(Number(r[0])));
            pts = body.map((r) => [Number(r[0]), Number(r[1])]);
            const bad = pts.findIndex((p) => !Number.isFinite(p[1]));
            if (bad >= 0) return ctx.error(at('data'), `${t.data}: row ${bad + 1} has no numeric value`);
          } else return ctx.error(at('data'), 'expected a .bedgraph/.bg or .csv (position,value) file');
        }
        out.values = pts.filter((p) => p[0] >= region.start && p[0] <= region.end);
        if (out.values.length > 100000) return ctx.error(at('data'), `${out.values.length} points in the region is too many (max 100,000)`, 'bin the signal first');
      } else out.variants = t.variants.filter((v) => inRegion(v.pos, v.pos));
      if (!(out.features ?? out.values ?? out.variants).length) ctx.warn(at(), `nothing in ${props.region}`);
      tracks.push(out);
    }
    return { title: props.title, region, tracks };
  },
};
