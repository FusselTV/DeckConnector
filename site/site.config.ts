export const repo = 'https://github.com/FusselTV/DeckConnector'
export const site = 'https://deckconnector.com'

// Imprint and privacy policy. The owner's data stays out of git: site.yml takes it from the OWNER_* repository
// secrets, a local build from site/.env.local. LEGAL_STRICT=1 (site.yml outside pull requests) fails on a missing one.
const env = process.env
export const owner = {
  name: env.OWNER_NAME || 'TODO',
  street: env.OWNER_STREET || 'TODO',
  city: env.OWNER_CITY || 'TODO',
  email: env.OWNER_EMAIL || 'TODO',
  // Data protection authority of the owner's Land, e.g. BayLDA in Bavaria.
  authority: { name: env.OWNER_AUTHORITY_NAME || 'TODO', url: env.OWNER_AUTHORITY_URL || 'TODO' },
  // Days Cloudflare keeps the contact worker's Workers Logs on the owner's plan.
  logDays: env.OWNER_LOG_DAYS || 'TODO',
}
if (process.env.LEGAL_STRICT === '1' && JSON.stringify(owner).includes('TODO')) throw new Error('site.config.ts: owner still has TODO placeholders')
