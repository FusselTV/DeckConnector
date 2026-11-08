'use client'
import type { Lang } from '~/lib/i18n'

const t = {
  en: { title: 'Flash the firmware', lead: 'Flasher placeholder.' },
  de: { title: 'Firmware flashen', lead: 'Platzhalter für den Flasher.' },
}

export function Flasher({ lang }: { lang: Lang }) {
  return (
    <main className="mx-auto w-full max-w-3xl px-4 py-16">
      <h1 className="text-3xl font-semibold tracking-tight">{t[lang].title}</h1>
      <p className="mt-4 text-fg-2">{t[lang].lead}</p>
    </main>
  )
}
