import { Faq } from './faq'
import { ArrowRight, ArrowUpRight, MonitorPlay } from 'lucide-react'
import type { ReactNode } from 'react'
import { prefix, type Lang } from '~/lib/i18n'
import { copy } from './copy'
import { docs, out } from './links'
import { Stage } from './Stage'
import { dots, primary, secondary, stage, textLink, wrap } from './ui'
import './landing.css'

const h2 = 'text-[2.1rem] leading-[1.06] font-semibold tracking-[-0.03em] text-balance sm:text-5xl lg:text-6xl'
const chip = 'inline-flex min-h-11 items-center rounded-full border border-line-2 px-4 text-[15px] font-medium transition-colors hover:border-edge hover:bg-panel-2'

function Arrow({ href, children, className = '' }: { href: string; children: ReactNode; className?: string }) {
  return (
    <a href={href} className={`${textLink} ${className}`}>
      {children}
      <ArrowRight className="size-4" aria-hidden />
    </a>
  )
}

/** One idea per screen: a short heading, one line, and the big picture below it or, from lg up, beside it (pic says on which side). */
function Feature({ id, title, text, more, pic, wide, children, className = '' }: { id: string; title: string; text?: string; more?: ReactNode; pic?: 'left' | 'right'; wide?: boolean; children: ReactNode; className?: string }) {
  const cols = wide ? 'lg:grid-cols-[1fr_2fr]' : pic === 'right' ? 'lg:grid-cols-[5fr_7fr]' : 'lg:grid-cols-[7fr_5fr]'
  return (
    <section aria-labelledby={id} className={`py-14 sm:py-20 ${className}`}>
      <div className={`${wrap} grid gap-10 sm:gap-12 ${pic ? `lg:items-center lg:gap-16 ${cols}` : ''}`}>
        <div className={`mx-auto max-w-3xl text-center ${pic ? `lg:mx-0 lg:text-left ${pic === 'left' ? 'lg:order-2' : ''}` : ''}`}>
          <h2 id={id} className={`${h2} ${pic ? 'lg:text-5xl' : ''}`}>
            {title}
          </h2>
          {text && <p className="mt-4 text-lg leading-relaxed text-pretty text-fg-2 sm:text-xl">{text}</p>}
          {more && <div className={`mt-6 flex flex-wrap justify-center gap-x-6 gap-y-1 ${pic ? 'lg:justify-start' : ''}`}>{more}</div>}
        </div>
        <div className="reveal">{children}</div>
      </div>
    </section>
  )
}

export function Landing({ lang }: { lang: Lang }) {
  const t = copy[lang]
  const p = prefix(lang)
  const d = (slug: string) => `${p}/docs/${slug}`
  const o = out(lang)

  return (
    <main id="main" className="overflow-x-clip">
      <section className="relative">
        <div aria-hidden className={`${dots} [mask-image:radial-gradient(ellipse_80%_60%_at_50%_0%,#000,transparent)]`} />
        <div className={`${wrap} relative pt-7 pb-16 text-center sm:pt-8 md:pb-24`}>
          <h1 className="text-[2.9rem] leading-[1] font-semibold tracking-[-0.045em] sm:text-7xl lg:text-[4.5rem]">
            <span className="max-lg:block">{t.title[0]} </span>
            <span className="text-amber-fg max-lg:block">{t.title[1]}</span>
          </h1>
          <p className="mx-auto mt-5 max-w-[44ch] text-lg leading-relaxed text-pretty text-fg-2 sm:text-xl lg:max-w-none">{t.lead}</p>
          <div className="mt-7 grid w-full grid-cols-2 gap-3 sm:flex sm:w-auto sm:justify-center">
            <a href={`${p}/flash/`} className={`${primary} max-sm:px-3`}>
              {t.flash}
              <ArrowRight className="size-4 max-sm:hidden" aria-hidden />
            </a>
            <a href={`${p}/demo/`} className={`${secondary} max-sm:px-3`}>
              <MonitorPlay className="size-4 text-fg-2" aria-hidden />
              {t.demo}
            </a>
          </div>
          <div className="relative isolate mx-auto mt-8 max-w-[52rem]">
            <div aria-hidden className="pointer-events-none absolute -inset-x-16 -inset-y-12 -z-10 bg-[radial-gradient(closest-side,rgb(255_176_32/0.18),transparent)] blur-2xl" />
            <Stage id="hero" lang={lang} label={t.heroAlt} eager className={`aspect-[4/5] sm:aspect-video ${stage}`} />
          </div>
        </div>
      </section>

      <Feature id="net" pic="right" title={t.net.title} text={t.net.text}>
        <Stage id="failover" lang={lang} label={t.net.alt} className={`mx-auto aspect-square max-w-xl ${stage}`} />
      </Feature>

      <Feature
        id="hosts"
        pic="left"
        title={t.hosts.title}
        text={t.hosts.text}
        more={
          <ul className="flex flex-wrap justify-[inherit] gap-3">
            {t.works.items.map(([k, name]) => (
              <li key={k}>
                <a href={o[k]} className={chip}>
                  {name}
                </a>
              </li>
            ))}
            <li>
              <a href={d(t.works.ownLink)} className={chip}>
                {t.works.own}
              </a>
            </li>
          </ul>
        }
      >
        <Stage id="hosts" lang={lang} label={t.hosts.alt} className={`mx-auto aspect-square max-w-xl ${stage}`} />
      </Feature>

      <Feature id="vs" title={t.vs.title} className="border-y border-line bg-well/60">
        <div className="mx-auto w-full max-w-3xl">
          <Stage id="compare" lang={lang} label={t.vs.alt} className={`aspect-[4/5] sm:aspect-video ${stage}`} />
          <div className="mt-4 text-center">
            <Arrow href={`${p}/compare/`} className="text-lg">
              {t.vs.link}
            </Arrow>
          </div>
        </div>
      </Feature>

      <Feature
        id="flash"
        pic="right"
        wide
        title={t.flashing.title}
        text={t.flashing.text}
        more={
          <>
            <Arrow href={`${p}/flash/`}>{t.flash}</Arrow>
            <a href={o.board} className={textLink}>
              {t.flashing.board}
              <ArrowUpRight className="size-4" aria-hidden />
            </a>
          </>
        }
      >
        <Stage id="flash" lang={lang} label={t.flashing.alt} className={`mx-auto aspect-[4/5] max-w-[52rem] sm:aspect-video ${stage}`} />
      </Feature>

      <section aria-labelledby="faq" className="border-t border-line py-16 sm:py-24">
        <div className={`${wrap} grid max-w-3xl gap-8`}>
          <h2 id="faq" className={`text-center ${h2}`}>
            {t.faq.title}
          </h2>
          <Faq items={t.faq.items} />
          <Arrow href={d(docs.start)} className="justify-self-center">
            {t.faq.more}
          </Arrow>
        </div>
      </section>

      <section className={`${wrap} pb-20 sm:pb-28`}>
        <div className="relative isolate overflow-hidden rounded-3xl border border-line bg-panel px-6 py-16 text-center shadow-panel sm:py-24">
          <div aria-hidden className={`${dots} [mask-image:radial-gradient(ellipse_60%_70%_at_50%_50%,#000,transparent)]`} />
          <div aria-hidden className="pointer-events-none absolute inset-0 -z-10 bg-[radial-gradient(50%_60%_at_50%_100%,rgb(255_176_32/0.14),transparent)]" />
          <h2 className={h2}>{t.cta.title}</h2>
          <div className="mt-8 flex flex-wrap justify-center gap-3">
            <a href={`${p}/flash/`} className={primary}>
              {t.flash}
              <ArrowRight className="size-4" aria-hidden />
            </a>
            <a href={`${p}/demo/`} className={secondary}>
              {t.demo}
            </a>
          </div>
        </div>
      </section>
    </main>
  )
}
