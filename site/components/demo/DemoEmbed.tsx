import { existsSync } from 'node:fs'
import type { Lang } from '~/lib/i18n'

const t = {
  en: { title: 'DeckConnector web UI demo', missing: 'The demo is not part of this build. Run npm run build:demo in web/, then build the site again.' },
  de: { title: 'Demo der DeckConnector-Weboberfläche', missing: 'Die Demo fehlt in diesem Build. Führe npm run build:demo in web/ aus und baue die Seite danach neu.' },
}

// Checked at build time: a static export has no server to ask later.
const built = existsSync('public/demo/app/index.html')

export function DemoEmbed({ lang }: { lang: Lang }) {
  return (
    <main className="flex flex-1 flex-col">
      {built ? (
        <iframe src={`/demo/app/index.html?lang=${lang}`} title={t[lang].title} className="w-full flex-1 border-0" style={{ minHeight: 'calc(100dvh - 3.5rem)' }} />
      ) : (
        <p className="mx-auto max-w-xl px-4 py-24 text-center text-fg-2">{t[lang].missing}</p>
      )}
    </main>
  )
}
