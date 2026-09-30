// Where the numbered reference list goes. Without this block, a document that cites [@key] gets the list at the end.
export default {
  name: 'references',
  summary: 'The numbered list of works cited with [@key] (BibTeX file in the front matter: bibliography: refs.bib). Optional: without it the list is added at the end.',
  shorthand: { scalar: 'title' },
  schema: { type: 'object', additionalProperties: false, properties: { title: { type: 'string', default: 'References' } } },
  example: 'title: References',
};
