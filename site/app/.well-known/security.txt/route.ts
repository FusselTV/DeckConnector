import { owner, repo, site } from '~/site.config'

export const dynamic = 'force-static'

// RFC 9116 wants an expiry; each deploy moves it a year ahead.
const expires = new Date(Date.now() + 365 * 24 * 3600 * 1000).toISOString().replace(/\.\d+Z$/, 'Z')

export const GET = () =>
  new Response(
    `Contact: ${repo}/security/advisories/new
${owner.email.includes('@') ? `Contact: mailto:${owner.email}\n` : ''}Expires: ${expires}
Preferred-Languages: en, de
Policy: ${repo}/security/policy
Canonical: ${site}/.well-known/security.txt
`,
    { headers: { 'Content-Type': 'text/plain; charset=utf-8' } },
  )
