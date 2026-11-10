import { Faq } from '~/components/landing/faq'
import { ArrowRight, ChevronDown } from 'lucide-react'
import { prefix, type Lang } from '~/lib/i18n'
import { copy } from '~/components/landing/copy'
import { docs } from '~/components/landing/links'
import { Versus } from '~/components/landing/Versus'
import { card, dots, h2, lift, primary, secondary, section, Text, textLink, wrap } from '~/components/landing/ui'
import '~/components/landing/landing.css'
import { compare } from './data'
import { Table } from './Table'

export function Compare({ lang }: { lang: Lang }) {
  const c = compare[lang]
  const p = prefix(lang)
  const d = (slug: string) => `${p}/docs/${slug}`
  return (
    <main id="main" className="overflow-x-clip">
      <section className="relative">
        <div aria-hidden className={`${dots} [mask-image:radial-gradient(ellipse_80%_60%_at_50%_0%,#000,transparent)]`} />
        <div className={`${wrap} relative pt-10 pb-14 sm:pt-14`}>
          <div className="max-w-3xl">
            <h1 className="text-3xl font-semibold tracking-tight text-balance sm:text-4xl">{c.h1}</h1>
            <p className="mt-3 text-base text-pretty text-fg-2 sm:text-lg">
              <Text lang={lang} r={c.lead} />
            </p>
            <div className="mt-6 grid grid-cols-2 gap-3 sm:flex">
              <a href={`${p}/flash/`} className={`${primary} max-sm:px-3`}>
                {copy[lang].flash}
              </a>
              <a href={d(docs.start)} className={`${secondary} max-sm:px-3`}>
                {copy[lang].cta.docs}
              </a>
            </div>
          </div>
          <div className="mt-10 max-w-3xl">
            <Versus t={copy[lang].vs} rows={c.summary} />
          </div>
        </div>
      </section>

      <section aria-label={c.h1} className={`border-t border-line bg-well/60 ${section} !py-12`}>
        <div className={wrap}>
          <details className="group">
            <summary className="mx-auto flex w-fit cursor-pointer list-none items-center gap-2 rounded-full border border-line-2 px-5 py-2.5 font-medium transition-colors hover:border-edge hover:bg-panel-2 [&::-webkit-details-marker]:hidden">
              {c.all}
              <ChevronDown className="size-4 transition-transform group-open:rotate-180" aria-hidden />
            </summary>
            <div className="mt-8">
              <Table cols={c.cols} rows={c.rows} groups={c.groups} better={c.better} caption={c.h1} />
              <p className="mt-4 text-sm leading-relaxed text-fg-3">
                <Text lang={lang} r={c.sources} />
              </p>
            </div>
          </details>
        </div>
      </section>

      <section aria-labelledby="dock" className={`border-t border-line ${section}`}>
        <div className={wrap}>
          <h2 id="dock" className={h2}>
            {c.dockWins.title}
          </h2>
          <ul className="mt-10 grid gap-4 sm:grid-cols-2 lg:grid-cols-4">
            {c.dockWins.items.map(([k, v]) => (
              <li key={k} className={`reveal ${card} ${lift} p-6`}>
                <h3 className="font-semibold tracking-tight">{k}</h3>
                <p className="mt-2 text-[15px] leading-relaxed text-fg-2">{v}</p>
              </li>
            ))}
          </ul>
        </div>
      </section>


      <section aria-labelledby="faq" className={`border-t border-line ${section}`}>
        <div className={`${wrap} grid gap-10 lg:grid-cols-[1fr_2fr] lg:gap-16`}>
          <h2 id="faq" className={h2}>
            {c.faq.title}
          </h2>
          <Faq items={c.faq.items} />
        </div>
      </section>

      <section className={`${wrap} pb-20 sm:pb-28`}>
        <div className="rounded-2xl border border-line bg-panel px-6 py-10 shadow-panel sm:px-10">
          <h2 className="text-2xl font-semibold tracking-tight sm:text-3xl">{c.next.title}</h2>
          <ul className="mt-4 grid gap-x-8 sm:grid-cols-2">
            {(
              [
                [c.next.flash, `${p}/flash/`],
                [c.next.start, d(docs.start)],
                [c.next.hardware, d(docs.hardware)],
                [c.next.speed, d(docs.speed)],
              ] as const
            ).map(([text, href]) => (
              <li key={href}>
                <a href={href} className={textLink}>
                  {text}
                  <ArrowRight className="size-4" aria-hidden />
                </a>
              </li>
            ))}
          </ul>
        </div>
      </section>
    </main>
  )
}
