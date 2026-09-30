// Optional YAML front matter at the top of a document (between --- lines).
export const FRONT_MATTER = {
  type: 'object',
  additionalProperties: false,
  properties: {
    title: { type: 'string' },
    bibliography: { type: ['string', 'array'], items: { type: 'string' }, description: 'relative path(s) of BibTeX files; cite with [@key]' },
  },
};
