import type { Metadata } from 'next'
import { builtManifest } from '~/components/flash/built'
import { compare } from '~/components/compare/data'
import { prefix, type Lang } from '~/lib/i18n'
import { pageMeta } from '~/lib/meta'
import { repo, site } from '~/site.config'
import { copy } from './copy'
import { faqLd, JsonLd } from './ui'

const owner = { '@type': 'Person', name: repo.split('/')[3], url: repo.split('/').slice(0, 4).join('/') }
const version = builtManifest?.version.match(/^v(\d+\.\d+\.\d+)$/)?.[1]
const url = (lang: Lang, path: string) => `${site}${prefix(lang)}${path}`

export function landingMeta(lang: Lang): Metadata {
  const { title, description } = copy[lang].meta
  const m = pageMeta(lang, '/', { title: { absolute: title }, description })
  return { ...m, openGraph: { ...m.openGraph, title, description } }
}

export function LandingLd({ lang }: { lang: Lang }) {
  const t = copy[lang]
  return (
    <JsonLd
      data={{
        '@context': 'https://schema.org',
        '@graph': [
          {
            '@type': 'SoftwareApplication',
            name: 'DeckConnector',
            description: t.meta.description,
            url: url(lang, '/'),
            applicationCategory: 'UtilitiesApplication',
            operatingSystem: 'ESP32-P4 firmware',
            offers: { '@type': 'Offer', price: 0, priceCurrency: lang === 'de' ? 'EUR' : 'USD' },
            license: 'https://www.gnu.org/licenses/agpl-3.0.html',
            isAccessibleForFree: true,
            ...(version && { softwareVersion: version }),
            downloadUrl: `${site}${prefix(lang)}/flash/`,
            inLanguage: lang,
            author: owner,
            screenshot: `${site}/og.jpg`,
            sameAs: repo,
          },
          { '@type': 'WebSite', name: 'DeckConnector', url: url(lang, '/'), inLanguage: lang },
          faqLd(t.faq.items),
        ],
      }}
    />
  )
}

export function compareMeta(lang: Lang): Metadata {
  return pageMeta(lang, '/compare/', compare[lang].meta)
}

export function CompareLd({ lang }: { lang: Lang }) {
  const c = compare[lang]
  return (
    <JsonLd
      data={{
        '@context': 'https://schema.org',
        '@graph': [
          {
            '@type': 'WebPage',
            name: c.h1,
            description: c.meta.description,
            url: url(lang, '/compare/'),
            inLanguage: lang,
          },
          {
            '@type': 'BreadcrumbList',
            itemListElement: [url(lang, '/'), url(lang, '/compare/')].map((item, i) => ({ '@type': 'ListItem', position: i + 1, name: c.crumbs[i], item })),
          },
          faqLd(c.faq.items),
        ],
      }}
    />
  )
}
