import type { Metadata } from 'next'
import { prefix, type Lang } from './i18n'

/** Canonical, the other language and the share card for a page at `path` (without the /de prefix, with a trailing slash). */
export function pageMeta(lang: Lang, path: string, m: Metadata = {}): Metadata {
  const url = `${prefix(lang)}${path}`
  return {
    ...m,
    alternates: { canonical: url, languages: { en: path, de: `/de${path}`, 'x-default': path } },
    openGraph: {
      type: 'website',
      siteName: 'DeckConnector',
      url,
      locale: lang === 'de' ? 'de_DE' : 'en_US',
      images: { url: '/og.png', width: 1200, height: 630 },
      ...(typeof m.title === 'string' ? { title: `${m.title} | DeckConnector` } : {}),
      ...(m.description ? { description: m.description } : {}),
    },
    twitter: { card: 'summary_large_image' },
  }
}
