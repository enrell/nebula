// Chemistry: 2D structures and reactions (smiles-drawer, SVG) and 3D structures (3Dmol.js, WebGL).
// Everything is themed from the nebula palette; the 3D viewer reads the file the checker verified.
import * as $3Dmol from '3dmol/build/3Dmol.es6.js';
import SmilesDrawer from 'smiles-drawer';
import { card, h, readFile } from '../dom.js';

const css = (name) => getComputedStyle(document.documentElement).getPropertyValue(`--${name}`).trim();
const SVG = 'http://www.w3.org/2000/svg';

function theme() {
  const fg = css('fg');
  return { FOREGROUND: fg, BACKGROUND: css('panel'), C: fg, O: css('red'), N: css('blue'), F: css('green'), CL: css('green'), BR: css('red'),
    I: css('accent'), P: css('yellow'), S: css('yellow'), B: css('yellow'), SI: css('yellow'), H: css('muted') };
}

function moleculeOptions(size, hydrogens) {
  return { width: size, height: size, bondThickness: 1.2, fontSizeLarge: 7, fontSizeSmall: 5, padding: 14, explicitHydrogens: !!hydrogens,
    terminalCarbons: false, compactDrawing: false, themes: { nebula: theme() } };
}

function drawSmiles(smiles, size, hydrogens) {
  const svg = document.createElementNS(SVG, 'svg');
  svg.setAttribute('width', size);
  svg.setAttribute('height', size);
  const drawer = new SmilesDrawer.SvgDrawer(moleculeOptions(size, hydrogens));
  SmilesDrawer.parse(smiles, (tree) => drawer.draw(tree, svg, 'nebula'), (e) => { throw e; });
  return svg;
}

export function molecule(p, ctx) {
  const tiles = p.items.map((it) => {
    let art;
    try { art = drawSmiles(it.smiles, p.size, p.hydrogens); } catch (e) { ctx.issue(`molecule ${it.smiles}: ${e.message}`); art = h('div.unavailable', 'could not draw'); }
    return h('figure.mol', art, h('figcaption', it.name ? h('b', it.name) : null, it.name ? h('br') : null, h('code.mol-smiles', it.smiles)));
  });
  return card('mol-card', p.title, h('div.mol-grid', { style: { gridTemplateColumns: `repeat(auto-fill, minmax(${p.size}px, 1fr))` } }, tiles));
}

export function reaction(p, ctx) {
  const svg = document.createElementNS(SVG, 'svg');
  const drawer = new SmilesDrawer.ReactionDrawer({ scale: 1.8, fontSize: 9, spacing: 12, plus: { size: 9, thickness: 1 }, arrow: { length: 120, headSize: 6, thickness: 1 } },
    moleculeOptions(220, false));
  try {
    SmilesDrawer.parseReaction(p.smiles, (r) => drawer.draw(r, svg, 'nebula', null, p.conditions ?? '{reagents}', ''), (e) => { throw e; });
  } catch (e) { ctx.issue(`reaction: ${e.message}`); }
  svg.classList.add('reaction-svg');
  return card('reaction-card', p.title, h('div.reaction', svg), p.caption ? h('footer.card-foot', ctx.inline(p.caption)) : null);
}

// "45-60,70" -> [45..60, 70]
const residues = (s) => s.split(',').flatMap((part) => {
  const [a, b = a] = part.split('-').map((v) => Number(v.trim()));
  return Array.from({ length: b - a + 1 }, (_, i) => a + i);
});

const STYLE = {
  cartoon: (c) => ({ cartoon: { color: c } }),
  stick: (c) => ({ stick: { radius: 0.18, colorscheme: c } }),
  sphere: (c) => ({ sphere: { scale: 0.9, colorscheme: c } }),
  line: (c) => ({ line: { colorscheme: c } }),
  ballstick: (c) => ({ stick: { radius: 0.14, colorscheme: c }, sphere: { scale: 0.25, colorscheme: c } }),
  surface: (c) => ({ cartoon: { color: c, opacity: 0.6 } }),
};

export function structure(p, ctx) {
  const box = h('div.mol3d', { style: { height: `${p.height}px` } });
  const info = [p.atoms ? `${p.atoms.toLocaleString()} atoms` : null, p.chains?.length ? `chains ${p.chains.join(', ')}` : null, 'drag to rotate · scroll to zoom']
    .filter(Boolean).join(' · ');
  ctx.afterMount(async () => {
    const text = await readFile(ctx.fileUrl(p.file));
    const viewer = $3Dmol.createViewer(box, { backgroundColor: css('panel'), antialias: true });
    viewer.addModel(text, p.format);
    const scheme = { element: 'default', chain: 'chain', spectrum: 'spectrum', secondary: 'ssPyMOL' }[p.color];
    const colored = (style) => {
      const s = STYLE[style](scheme);
      if (s.cartoon) s.cartoon = p.color === 'element' ? { colorscheme: 'default' } : p.color === 'chain' ? { colorscheme: 'chain' } : p.color === 'secondary' ? { colorscheme: 'ssPyMOL' } : { color: 'spectrum' };
      return s;
    };
    viewer.setStyle({}, colored(p.style));
    if (p.style === 'surface') viewer.addSurface($3Dmol.SurfaceType.VDW, { opacity: 0.75, color: css('blue') });
    for (const hl of p.highlight) {
      const sel = {};
      if (hl.chain) sel.chain = hl.chain;
      if (hl.residues) sel.resi = residues(hl.residues);
      if (hl.resn) sel.resn = hl.resn;
      const c = hl.color ?? css('accent');
      viewer.setStyle(sel, { ...STYLE[hl.style ?? 'stick']('default'), ...(hl.style === 'cartoon' ? { cartoon: { color: c } } : { stick: { radius: 0.22, color: c } }) });
      if (hl.label) viewer.addLabel(hl.label, { fontColor: css('fg'), backgroundColor: css('bg'), backgroundOpacity: 0.8, fontSize: 12, borderThickness: 0 }, sel);
    }
    viewer.zoomTo();
    if (p.spin) viewer.spin('y', 0.6);
    viewer.render();
    const ro = new ResizeObserver(() => { viewer.resize(); viewer.render(); });
    ro.observe(box);
    box.nebulaDispose = () => { ro.disconnect(); viewer.clear(); };
  });
  return card('chart-card', p.title, box, h('footer.card-foot', info));
}
