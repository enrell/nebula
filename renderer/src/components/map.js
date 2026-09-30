// Maps with no network: GeoJSON (regions, lines, points) from a file and/or inline points, over a built-in world
// basemap (Natural Earth 1:110m countries). Regions can be coloured by a property (choropleth). The checker verifies
// the GeoJSON structure and that coordinates are WGS84 longitude/latitude; the page draws it with d3-geo.
import { extname } from '../core/paths.js';
import { closest } from '../core/suggest.js';

const MAX_VERTICES = 400000;
const GEOMETRIES = ['Point', 'MultiPoint', 'LineString', 'MultiLineString', 'Polygon', 'MultiPolygon', 'GeometryCollection'];
const DEPTH = { Point: 0, MultiPoint: 1, LineString: 1, MultiLineString: 2, Polygon: 2, MultiPolygon: 3 };

// -> { features, vertices } or { error }
export function checkGeoJson(doc) {
  if (!doc || typeof doc !== 'object') return { error: 'not a GeoJSON object' };
  const features = doc.type === 'FeatureCollection' ? doc.features : doc.type === 'Feature' ? [doc] : GEOMETRIES.includes(doc.type) ? [{ type: 'Feature', geometry: doc, properties: {} }] : null;
  if (!Array.isArray(features)) return { error: `"type" must be FeatureCollection, Feature or a geometry (got ${JSON.stringify(doc.type)})` };
  let vertices = 0;
  const walk = (coords, depth, where) => {
    if (depth === 0) {
      if (!Array.isArray(coords) || coords.length < 2 || !coords.every((c) => typeof c === 'number' && Number.isFinite(c))) return `${where}: a position must be [longitude, latitude]`;
      const [lon, lat] = coords;
      if (Math.abs(lon) > 180.0001 || Math.abs(lat) > 90.0001) {
        return Math.abs(lon) <= 90 && Math.abs(lat) <= 180
          ? `${where}: [${lon}, ${lat}] looks like [latitude, longitude]; GeoJSON positions are [longitude, latitude]`
          : `${where}: [${lon}, ${lat}] is not longitude/latitude (projected coordinates?); GeoJSON must be WGS84 degrees`;
      }
      vertices++;
      return null;
    }
    if (!Array.isArray(coords)) return `${where}: coordinates must be nested lists`;
    for (const c of coords) { const e = walk(c, depth - 1, where); if (e) return e; }
    return null;
  };
  const geometry = (g, where) => {
    if (g === null) return null;   // a feature without geometry is valid GeoJSON
    if (!g || !GEOMETRIES.includes(g.type)) return `${where}: unknown geometry type ${JSON.stringify(g?.type)}`;
    if (g.type === 'GeometryCollection') {
      for (const [i, sub] of (g.geometries ?? []).entries()) { const e = geometry(sub, `${where} geometry ${i + 1}`); if (e) return e; }
      return null;
    }
    return walk(g.coordinates, DEPTH[g.type], where);
  };
  for (const [i, f] of features.entries()) {
    if (f?.type !== 'Feature') return { error: `feature ${i + 1}: "type" must be "Feature"` };
    const e = geometry(f.geometry, `feature ${i + 1}`);
    if (e) return { error: e };
    if (vertices > MAX_VERTICES) return { error: `more than ${MAX_VERTICES.toLocaleString()} vertices; simplify the shapes (e.g. mapshaper -simplify)` };
  }
  return { features, vertices };
}

export default {
  name: 'map',
  summary: 'Offline maps: GeoJSON regions, routes and sites (file) and/or inline points (lat, lon, value) over a built-in world basemap; colour regions by a property (choropleth); several projections; zoom and pan.',
  schema: {
    type: 'object',
    additionalProperties: false,
    properties: {
      title: { type: 'string' },
      data: { type: 'string', description: 'relative path to a .geojson / .json file (WGS84 longitude, latitude)' },
      color: { type: 'string', description: 'feature property to colour regions by (numeric: a colour ramp; text: categories)' },
      label: { type: 'string', description: 'feature property shown as the name in tooltips (default: name)' },
      points: { type: 'array', maxItems: 5000, items: { type: 'object', additionalProperties: false, required: ['lat', 'lon'],
        properties: { lat: { type: 'number', minimum: -90, maximum: 90 }, lon: { type: 'number', minimum: -180, maximum: 180 }, label: { type: 'string' }, value: { type: 'number' } } },
        description: 'markers; sized by value when given' },
      basemap: { enum: ['world', 'none'], default: 'world' },
      projection: { enum: ['auto', 'equal-earth', 'natural-earth', 'mercator', 'equirectangular', 'orthographic'], default: 'auto',
        description: 'auto: equal-earth for world-wide data, mercator for a region' },
      height: { type: 'integer', minimum: 200, maximum: 1400, default: 460 },
    },
  },
  example: 'title: Field stations\npoints:\n  - {lat: -3.47, lon: -62.37, label: Amazon, value: 41}\n  - {lat: 64.13, lon: -21.94, label: Reykjavík, value: 12}\n  - {lat: -77.85, lon: 166.67, label: McMurdo, value: 7}\n  - {lat: 35.36, lon: 138.73, label: Fuji, value: 23}',
  resolve(props, ctx) {
    if (props.data === undefined && props.points === undefined && props.basemap === 'none') return ctx.error('', 'nothing to draw: give "data" or "points", or keep the world basemap');
    const out = { title: props.title, points: props.points ?? [], basemap: props.basemap, projection: props.projection, height: props.height, label: props.label ?? 'name' };
    if (props.data === undefined) {
      if (props.color) return ctx.error('/color', 'color applies to the regions of a GeoJSON file (data)');
      return out;
    }
    if (!['geojson', 'json'].includes(extname(props.data))) return ctx.error('/data', 'expected a .geojson or .json file');
    const f = ctx.readText(props.data);
    if (!f.ok) return ctx.error('/data', f.error);
    let doc;
    try { doc = JSON.parse(f.text); } catch (e) { return ctx.error('/data', `${props.data}: invalid JSON (${e.message})`); }
    const g = checkGeoJson(doc);
    if (g.error) return ctx.error('/data', `${props.data}: ${g.error}`);
    const keys = [...new Set(g.features.flatMap((x) => Object.keys(x.properties ?? {})))];
    if (props.color !== undefined) {
      const values = g.features.map((x) => x.properties?.[props.color]).filter((v) => v !== undefined && v !== null && v !== '');
      if (!values.length) {
        const guess = closest(props.color, keys);
        return ctx.error('/color', `no feature has the property "${props.color}"`, (guess ? `did you mean "${guess}"? ` : '') + (keys.length ? `properties: ${keys.slice(0, 20).join(', ')}` : 'the features have no properties'));
      }
      const numeric = values.every((v) => typeof v === 'number' || (typeof v === 'string' && v.trim() !== '' && Number.isFinite(Number(v))));
      out.color = numeric
        ? { key: props.color, numeric: true, range: [Math.min(...values.map(Number)), Math.max(...values.map(Number))] }
        : { key: props.color, numeric: false, categories: [...new Set(values.map(String))].slice(0, 24) };
      if (!numeric && new Set(values.map(String)).size > 24) ctx.warn('/color', `"${props.color}" has more than 24 distinct values; the rest share a colour`);
    }
    if (props.label !== undefined && !keys.includes(props.label)) ctx.warn('/label', `no feature has the property "${props.label}"`);
    ctx.ref(props.data);
    return { ...out, file: props.data, features: g.features.length };
  },
};
