export const repo = 'https://github.com/FusselTV/DeckConnector'
export const site = 'https://deckconnector.com'

// Imprint and privacy policy. Fill in before launch: site.yml builds with LEGAL_STRICT=1 outside pull requests, which fails on a TODO.
export const owner = {
  name: 'TODO',
  street: 'TODO',
  city: 'TODO',
  email: 'TODO',
  // Data protection authority of the owner's Land, e.g. BayLDA in Bavaria.
  authority: { name: 'TODO', url: 'TODO' },
}
if (process.env.LEGAL_STRICT === '1' && JSON.stringify(owner).includes('TODO')) throw new Error('site.config.ts: owner still has TODO placeholders')
