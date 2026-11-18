import { prefix, type Lang } from '~/lib/i18n'
import { repo } from '~/site.config'
import { legalPath } from './legal'
import { Logo } from './site-header'

const t = {
  en: {
    docs: 'Docs',
    flash: 'Flasher',
    demo: 'Demo',
    compare: 'Compare',
    releases: 'Firmware releases',
    source: 'Source code',
    license: 'AGPL-3.0 license',
    start: 'Getting started',
    nav: 'Footer',
    contact: 'Contact',
    imprint: 'Imprint',
    privacy: 'Privacy',
    line: 'Free software for the Waveshare ESP32-P4-WIFI6-POE-ETH. Not affiliated with Elgato, Corsair or Bitfocus. Elgato and Stream Deck are trademarks of Corsair Memory, Inc.',
  },
  de: {
    docs: 'Anleitung',
    flash: 'Flasher',
    demo: 'Demo',
    compare: 'Vergleich',
    releases: 'Firmware-Versionen',
    source: 'Quellcode',
    license: 'Lizenz AGPL-3.0',
    start: 'Erste Schritte',
    nav: 'Fußzeile',
    contact: 'Kontakt',
    imprint: 'Impressum',
    privacy: 'Datenschutz',
    line: 'Freie Software für die Platine Waveshare ESP32-P4-WIFI6-POE-ETH. Kein Produkt von Elgato, Corsair oder Bitfocus. Elgato und Stream Deck sind Marken der Corsair Memory, Inc.',
  },
}

export function SiteFooter({ lang }: { lang: Lang }) {
  const s = t[lang]
  const p = prefix(lang)
  const links: [string, string][] = [
    [s.flash, `${p}/flash/`],
    [s.demo, `${p}/demo/`],
    [s.compare, `${p}/compare/`],
    [s.docs, `${p}/docs/`],
    [s.start, `${p}/docs/getting-started/`],
    [s.releases, `${repo}/releases`],
    [s.source, repo],
    [s.license, `${repo}/blob/main/LICENSE`],
  ]
  return (
    <footer className="mt-auto border-t text-sm text-fd-muted-foreground">
      <div className="mx-auto flex max-w-6xl flex-col gap-6 px-4 py-10 sm:px-6 md:flex-row md:items-start md:justify-between">
        <p className="max-w-sm leading-relaxed">
          <span className="mb-2 flex items-center gap-2 font-semibold text-fd-foreground">
            <Logo />
            DeckConnector
          </span>
          {s.line}
          <span className="mt-3 flex gap-4">
            {(['contact', 'imprint', 'privacy'] as const).map((k) => (
              <a
                key={k}
                href={legalPath[k][lang]}
                className="flex min-h-11 items-center hover:text-fd-foreground lg:min-h-9"
              >
                {s[k]}
              </a>
            ))}
          </span>
        </p>
        <nav aria-label={s.nav} className="grid grid-cols-2 gap-x-6 sm:grid-cols-3 md:gap-x-10">
          {links.map(([text, url]) => (
            <a
              key={url}
              href={url}
              className="flex min-h-11 items-center rounded-md transition-colors hover:text-fd-foreground lg:min-h-9"
            >
              {text}
            </a>
          ))}
        </nav>
      </div>
    </footer>
  )
}
