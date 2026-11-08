import { prefix, type Lang } from '~/lib/i18n'
import { repo } from '~/site.config'

const t = {
  en: { home: 'Home', docs: 'Docs', flash: 'Flash', demo: 'Demo', license: 'License', free: 'DeckConnector is free software under the AGPL-3.0.' },
  de: { home: 'Startseite', docs: 'Anleitung', flash: 'Flashen', demo: 'Demo', license: 'Lizenz', free: 'DeckConnector ist freie Software unter der AGPL-3.0.' },
}

export function SiteFooter({ lang }: { lang: Lang }) {
  const s = t[lang]
  const p = prefix(lang)
  const links: [string, string][] = [
    [s.home, `${p}/`],
    [s.docs, `${p}/docs/`],
    [s.flash, `${p}/flash/`],
    [s.demo, `${p}/demo/`],
    ['GitHub', repo],
    [s.license, `${repo}/blob/main/LICENSE`],
  ]
  return (
    <footer className="mt-auto border-t text-sm text-fd-muted-foreground">
      <div className="mx-auto flex max-w-(--fd-layout-width) flex-wrap items-center gap-x-5 gap-y-2 px-4 py-6">
        <nav aria-label="Footer" className="flex flex-wrap gap-x-5 gap-y-2">
          {links.map(([text, url]) => (
            <a key={url} href={url} className="hover:text-fd-foreground">
              {text}
            </a>
          ))}
        </nav>
        <span className="md:ml-auto">{s.free}</span>
      </div>
    </footer>
  )
}
