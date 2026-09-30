// Slider controls for formula parameters. onChange(name, value) runs at most once per frame while dragging.
import { h } from './dom.js';

const shown = (v, step) => Number(v.toFixed(Math.max(0, -Math.floor(Math.log10(step))))).toString();

export function sliderBar(sliders, onChange) {
  let frame = 0;
  const pending = new Map();
  const flush = () => { frame = 0; for (const [n, v] of pending) onChange(n, v); pending.clear(); };
  return h('div.controls', sliders.map((s) => {
    const out = h('output.control-value', shown(s.value, s.step));
    const input = h('input', { type: 'range', min: s.min, max: s.max, step: s.step, value: s.value, 'aria-label': s.label,
      oninput: (e) => {
        const v = Number(e.target.value);
        out.textContent = shown(v, s.step);
        pending.set(s.name, v);
        frame ||= requestAnimationFrame(flush);
      } });
    return h('label.control', h('span.control-name', s.label), input, out);
  }));
}
