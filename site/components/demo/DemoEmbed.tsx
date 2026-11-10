import { existsSync } from 'node:fs'
import type { Lang } from '~/lib/i18n'
import { DemoFrame } from './DemoFrame'

export const demoText = {
  en: {
    title: 'Live demo with a virtual Stream Deck',
    h1: 'DeckConnector web UI demo',
    description: 'Click through the real DeckConnector web UI with a virtual Stream Deck. Everything runs in your browser.',
    missing: 'The demo is not part of this build. Run npm run build:demo in web/, then build the site again.',
  },
  de: {
    title: 'Live-Demo mit virtuellem Stream Deck',
    h1: 'Demo der DeckConnector-Weboberfläche',
    description: 'Klick dich durch die echte Weboberfläche von DeckConnector, mit einem virtuellen Stream Deck. Alles läuft im Browser.',
    missing: 'Die Demo fehlt in diesem Build. Führe npm run build:demo in web/ aus und baue die Seite danach neu.',
  },
}

// Checked at build time: a static export has no server to ask later.
const built = existsSync('public/demo/app/index.html')

export function DemoEmbed({ lang }: { lang: Lang }) {
  const t = demoText[lang]
  return (
    <main id="main" className="flex h-[calc(100dvh-3.5rem)] flex-col">
      <h1 className="sr-only">{t.h1}</h1>
      {built ? <DemoFrame lang={lang} title={t.h1} /> : <p className="mx-auto max-w-xl px-4 py-24 text-center text-fg-2">{t.missing}</p>}
    </main>
  )
}
