import { prefix, type Lang } from '~/lib/i18n'

const t = {
  en: {
    title: 'Page not found',
    text: 'This page does not exist, or it moved.',
    home: 'Home',
    docs: 'Docs',
    other: ['de', 'Deutsch'],
  },
  de: {
    title: 'Seite nicht gefunden',
    text: 'Diese Seite gibt es nicht oder nicht mehr.',
    home: 'Startseite',
    docs: 'Anleitung',
    other: ['en', 'English'],
  },
} as const

export const notFoundTitle = (lang: Lang) => `${t[lang].title} | DeckConnector`

export function NotFound({ lang }: { lang: Lang }) {
  const s = t[lang]
  const [ol, oname] = s.other
  const p = prefix(lang)
  return (
    <main id="main" className="mx-auto w-full max-w-3xl px-4 py-24 text-center">
      <h1 className="text-3xl font-semibold tracking-tight">{s.title}</h1>
      <p className="mt-4 text-fd-muted-foreground">{s.text}</p>
      <p className="mt-8 flex justify-center gap-6 font-medium text-fd-primary underline underline-offset-4">
        <a href={`${p}/`}>{s.home}</a>
        <a href={`${p}/docs/`}>{s.docs}</a>
        <a href={`${prefix(ol)}/`} lang={ol} hrefLang={ol}>
          {oname}
        </a>
      </p>
    </main>
  )
}
