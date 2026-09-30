import { extname } from '../core/paths.js';
import { checkAlphabet, guessType, parseFasta } from '../core/formats/fasta.js';

export default {
  name: 'sequence',
  summary: 'DNA, RNA or protein sequences and alignments (FASTA file or inline), coloured by residue, with consensus, conservation and annotated regions.',
  shorthand: { scalar: 'seq' },
  schema: {
    type: 'object',
    additionalProperties: false,
    properties: {
      title: { type: 'string' },
      data: { type: 'string', description: 'relative path to a FASTA file (.fa .fasta .fna .faa .aln)' },
      seq: { type: 'string', description: 'one sequence inline' },
      sequences: { type: 'array', minItems: 1, items: { type: 'object', additionalProperties: false, required: ['name', 'seq'], properties: { name: { type: 'string' }, seq: { type: 'string' } } } },
      type: { enum: ['auto', 'dna', 'rna', 'protein'], default: 'auto' },
      annotations: {
        type: 'array',
        items: { type: 'object', additionalProperties: false, required: ['start', 'end'], properties: {
          start: { type: 'integer', minimum: 1 }, end: { type: 'integer', minimum: 1 }, label: { type: 'string' }, color: { enum: ['accent', 'blue', 'green', 'yellow', 'red'] } } },
        description: '1-based inclusive ranges highlighted under the sequences',
      },
      width: { type: 'integer', minimum: 10, maximum: 200, default: 60, description: 'residues per row' },
      start: { type: 'integer', default: 1, description: 'number of the first residue' },
      consensus: { type: 'boolean', description: 'consensus and conservation rows (default: on for alignments)' },
    },
  },
  example: 'sequences:\n  - {name: human, seq: MKTAYIAKQRQISFVKSHFSRQ}\n  - {name: mouse, seq: MKTAYIAKQRQISFVKAHFSRQ}\nannotations:\n  - {start: 5, end: 12, label: helix 1}',
  resolve(props, ctx) {
    const given = ['data', 'seq', 'sequences'].filter((k) => props[k] !== undefined);
    if (given.length !== 1) return ctx.error('', 'give exactly one of "data", "seq" or "sequences"');
    let seqs;
    if (props.data !== undefined) {
      if (!['fa', 'fasta', 'fna', 'faa', 'ffn', 'aln', 'txt'].includes(extname(props.data))) return ctx.error('/data', 'expected a FASTA file (.fa, .fasta, .fna, .faa)');
      const f = ctx.readText(props.data);
      if (!f.ok) return ctx.error('/data', f.error);
      const r = parseFasta(f.text);
      if (r.error) return ctx.error('/data', `${props.data}: ${r.error}`);
      seqs = r.seqs;
    } else seqs = props.sequences ?? [{ name: '', seq: props.seq }];
    seqs = seqs.map((s) => ({ name: s.name, seq: s.seq.replace(/\s+/g, '') }));
    const type = props.type === 'auto' ? guessType(seqs) : props.type;
    const bad = checkAlphabet(seqs, type);
    if (bad) return ctx.error(props.data ? '/data' : props.seq !== undefined ? '/seq' : '/sequences', `${bad.name ? `${bad.name}: ` : ''}"${bad.char}" at position ${bad.pos} is not a valid ${type} letter`, props.type === 'auto' ? 'set type: dna, rna or protein if the guess is wrong' : undefined);
    const lengths = new Set(seqs.map((s) => s.seq.length));
    const aligned = seqs.length > 1 && lengths.size === 1;
    if (seqs.length > 1 && !aligned && props.consensus) ctx.warn('/consensus', 'sequences have different lengths, so there is no consensus (not an alignment)');
    const total = Math.max(...lengths);
    if (seqs.length * total > 400000) return ctx.error(props.data ? '/data' : '/sequences', `${seqs.length} × ${total} residues is too many to show (max 400,000)`);
    for (const [i, a] of (props.annotations ?? []).entries())
      if (a.end < a.start || a.end - props.start + 1 > total) return ctx.error(`/annotations/${i}`, `range ${a.start}–${a.end} is outside the sequence (${props.start}–${props.start + total - 1})`);
    return { title: props.title, seqs, type, aligned, consensus: aligned && props.consensus !== false, annotations: props.annotations ?? [], width: props.width, start: props.start };
  },
};
