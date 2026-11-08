import type { Lang } from '~/lib/i18n'
import { RemotionStage } from '~/remotion/RemotionStage'

const t = {
  en: { title: 'Your Stream Deck on the network', lead: 'Landing page placeholder.' },
  de: { title: 'Dein Stream Deck im Netzwerk', lead: 'Platzhalter für die Startseite.' },
}

export function Landing({ lang }: { lang: Lang }) {
  return (
    <main className="mx-auto w-full max-w-5xl px-4 py-16">
      <h1 className="text-4xl font-semibold tracking-tight">{t[lang].title}</h1>
      <p className="mt-4 text-fg-2">{t[lang].lead}</p>
      <RemotionStage id="sample" className="mt-10 overflow-hidden rounded-lg border border-line" />
    </main>
  )
}
