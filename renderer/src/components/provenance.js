// What a result was computed from. nebula fills in every file the document read (SHA-256, size, modification time),
// the git commit of the document's repository and when it was checked; the agent adds how the results were made.
import { extname } from '../core/paths.js';

export default {
  name: 'provenance',
  summary: 'Where the results come from: nebula lists every file the document read (SHA-256, size, modified), the git commit and the check time; you add the command, script, environment, seed and notes.',
  schema: {
    type: 'object',
    additionalProperties: false,
    properties: {
      title: { type: 'string', default: 'Provenance' },
      command: { type: 'string', description: 'the command that produced the results, e.g. "python analyse.py --seed 7"' },
      script: { type: 'string', description: 'relative path of the script or notebook (hashed like the inputs)' },
      environment: { type: 'object', additionalProperties: { type: ['string', 'number'] }, description: 'versions, e.g. {python: "3.12.4", numpy: "2.1.0"}' },
      seed: { type: ['integer', 'string'] },
      notes: { type: 'string', description: 'Markdown' },
    },
  },
  example: 'command: python analyse.py --seed 7\nscript: src/sample.py\nenvironment: {python: "3.12.4", numpy: "2.1.0"}\nseed: 7',
  resolve(props, ctx) {
    if (props.script !== undefined) {
      const st = ctx.stat(props.script);   // reading it through the host adds it to the hashed inputs
      if (!st.ok) return ctx.error('/script', st.error);
      if (['png', 'jpg', 'npy'].includes(extname(props.script))) ctx.warn('/script', `${props.script} does not look like a script`);
    }
    if (props.notes) ctx.markdown('/notes', props.notes);
    return props;
  },
};
