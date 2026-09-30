// Mermaid diagrams, themed from the nebula palette. Mermaid runs with securityLevel "strict" (no click callbacks or
// HTML labels from the text) and only ever sees source the checker accepted; its parse errors become render issues.
import mermaid from 'mermaid';
import { card, h } from '../dom.js';

const css = (name) => getComputedStyle(document.documentElement).getPropertyValue(`--${name}`).trim();
let themed = '';
let serial = 0;

function configure() {
  const vars = {
    darkMode: true, background: css('panel'), fontFamily: css('font') || 'monospace', fontSize: '13px',
    primaryColor: css('bg'), primaryTextColor: css('fg'), primaryBorderColor: css('accent'),
    secondaryColor: css('panel'), secondaryTextColor: css('fg'), secondaryBorderColor: css('blue'),
    tertiaryColor: css('panel'), tertiaryTextColor: css('fg'), tertiaryBorderColor: css('border'),
    lineColor: css('muted'), textColor: css('fg'), mainBkg: css('bg'), nodeBorder: css('accent'), clusterBkg: css('panel'), clusterBorder: css('border'),
    titleColor: css('fg'), edgeLabelBackground: css('panel'), noteBkgColor: css('panel'), noteTextColor: css('fg'), noteBorderColor: css('yellow'),
    actorBkg: css('bg'), actorBorder: css('accent'), actorTextColor: css('fg'), signalColor: css('muted'), signalTextColor: css('fg'),
    labelBoxBkgColor: css('bg'), labelBoxBorderColor: css('border'), labelTextColor: css('fg'), loopTextColor: css('fg'),
    pie1: css('accent'), pie2: css('blue'), pie3: css('green'), pie4: css('yellow'), pie5: css('red'), pieStrokeColor: css('panel'), pieTitleTextColor: css('fg'),
    pieSectionTextColor: css('bg'), pieLegendTextColor: css('fg'),
    git0: css('accent'), git1: css('blue'), git2: css('green'), git3: css('yellow'), git4: css('red'),
  };
  const key = JSON.stringify(vars);
  if (key === themed) return;
  themed = key;
  mermaid.initialize({ startOnLoad: false, securityLevel: 'strict', theme: 'base', themeVariables: vars, fontFamily: vars.fontFamily,
    flowchart: { htmlLabels: false, curve: 'basis' }, suppressErrorRendering: true });
}

export async function diagram(p, ctx) {
  configure();
  const box = h('div.diagram');
  const id = `nebula-diagram-${++serial}`;
  try {
    const { svg } = await mermaid.render(id, p.source);
    box.innerHTML = svg;   // mermaid sanitises its output (DOMPurify) under securityLevel strict
    const el = box.querySelector('svg');
    el?.removeAttribute('height');
    el?.style.setProperty('max-width', el.style.maxWidth || '100%');
  } catch (e) {
    const message = String(e?.message ?? e).split('\n').filter(Boolean).slice(0, 4).join(' ');
    ctx.issue(`diagram (${p.type}): ${message}`);
    box.append(h('div.unavailable', h('b', 'mermaid could not draw this diagram'), h('pre.diagram-error', String(e?.message ?? e))));
  } finally {
    document.getElementById(id)?.remove();
    document.getElementById(`d${id}`)?.remove();
  }
  return card('diagram-card', p.title, box, p.caption ? h('footer.card-foot', ctx.inline(p.caption)) : null);
}
