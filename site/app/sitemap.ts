import type { MetadataRoute } from 'next'
import { source } from '~/lib/source'
import { site } from '~/site.config'

export const dynamic = 'force-static'

export default function sitemap(): MetadataRoute.Sitemap {
  const docs = source.getPages('en').map((p) => `/docs/${p.slugs.map((s) => `${s}/`).join('')}`)
  const de = new Set(source.getPages('de').map((p) => p.slugs.join('/')))
  return ['/', '/compare/', '/flash/', '/demo/', ...docs].flatMap((path) => {
    const langs = { en: `${site}${path}`, ...(path.startsWith('/docs/') && !de.has(path.slice(6, -1)) ? {} : { de: `${site}/de${path}` }) }
    const alternates = { languages: { ...langs, 'x-default': langs.en } }
    return Object.values(langs).map((url) => ({ url, alternates }))
  })
}
