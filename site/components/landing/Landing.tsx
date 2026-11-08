import type { ReactNode } from 'react'
import { ArrowRight, ArrowUpRight, BookOpen, Check, MonitorPlay, Play, Plus, Usb } from 'lucide-react'
import { prefix, type Lang } from '~/lib/i18n'
import { repo } from '~/site.config'
import { LazyArt } from './LazyArt'
import { copy, waveshare } from './copy'
import { Stage } from './Stage'

const specs = [
  'mDNS · _elg._tcp · :5343',
  'Satellite API · :16622',
  'ECDSA P-256 · rollback',
  'watchdog · 3 s',
  'live deck · DE · EN',
  'REST · /api/ws · zeroconf',
]
const wrap = 'mx-auto w-full max-w-6xl px-4 sm:px-6'
const section = 'py-16 sm:py-24'
const focus = 'focus-visible:outline-2 focus-visible:outline-offset-2 focus-visible:outline-focus'
const button = `inline-flex h-12 items-center justify-center gap-2 rounded-lg px-5 text-[15px] font-semibold transition-[filter,background-color,border-color] ${focus}`
const primary = `${button} bg-amber text-amber-ink shadow-[inset_0_1px_0_rgb(255_255_255/0.35),0_8px_24px_-8px_rgb(255_176_32/0.5)] hover:brightness-95`
const secondary = `${button} border border-line-2 bg-panel text-fg hover:border-edge hover:bg-panel-2`
const textLink = `inline-flex items-center gap-1.5 rounded font-medium text-amber-fg underline-offset-4 hover:underline ${focus}`
const mono = 'font-mono text-xs font-medium tracking-[0.14em] uppercase'
const dots =
  'pointer-events-none absolute inset-0 bg-[radial-gradient(var(--line-2)_1.2px,transparent_1.2px)] bg-[size:28px_28px] opacity-50'
const card = 'rounded-2xl border border-line bg-panel shadow-panel'
// Animations and hardware art always sit on the dark stage, also in the light theme.
const stage =
  'stage relative overflow-hidden rounded-2xl border border-line bg-[#0c0e10] shadow-[inset_0_1px_0_rgb(255_255_255/0.06),var(--stage-shadow)]'
// The How it works visuals share one frame; a composition with another shape sits centred on the same dark.
const bay = `${stage} grid place-items-center !bg-[#060708]`
const h2 = 'text-[2rem] leading-[1.1] font-semibold tracking-[-0.025em] text-balance sm:text-5xl'

function Eyebrow({ children }: { children: ReactNode }) {
  return (
    <p className={`${mono} text-amber-fg`}>{children}</p>
  )
}

function Head({ label, title, children }: { label: string; title: string | string[]; children?: ReactNode }) {
  return (
    <div className="max-w-3xl">
      <Eyebrow>{label}</Eyebrow>
      <h2 className={`mt-4 ${h2}`}>
        {typeof title === 'string'
          ? title
          : title.map((line, i) => (
              <span key={line} className={`block ${i ? 'text-fg-3' : ''}`}>
                {line}
              </span>
            ))}
      </h2>
      {children}
    </div>
  )
}

export function Landing({ lang }: { lang: Lang }) {
  const t = copy[lang]
  const f = t.features
  const p = prefix(lang)
  return (
    <main id="main" className="overflow-x-clip">
      <section className="relative">
        <div
          aria-hidden
          className={`${dots} [mask-image:radial-gradient(ellipse_80%_60%_at_50%_0%,#000,transparent)]`}
        />
        <div className={`${wrap} relative pt-10 pb-16 sm:pt-12 md:pt-14 md:pb-20`}>
          <div className="grid items-center gap-x-14 lg:grid-cols-[1fr_minmax(0,27rem)]">
          <div>
          <p className={`${mono} flex items-center gap-2 text-fg-2`}>
            <span className="size-1.5 rounded-full bg-ok shadow-[0_0_0_3px_var(--ok-soft)]" aria-hidden />
            {t.eyebrow}
          </p>
          <h1 className="mt-5 text-[2.6rem] leading-[1.02] font-semibold tracking-[-0.04em] sm:text-7xl lg:text-[4.25rem] lg:leading-[1]">
            <span className="block">{t.title[0]}</span>
            <span className="block text-fg-3">{t.title[1]}</span>
          </h1>
          <p className="mt-6 max-w-[56ch] text-[17px] leading-relaxed text-pretty text-fg-2 sm:mt-6 sm:text-xl">{t.lead}</p>
          <div className="mt-7 flex flex-wrap items-center gap-x-6 gap-y-4">
            <div className="grid w-full grid-cols-2 gap-3 sm:flex sm:w-auto">
              <a href={`${p}/flash/`} className={`${primary} max-sm:px-3`}>
                {t.flash}
                <ArrowRight className="size-4 max-sm:hidden" aria-hidden />
              </a>
              <a href={`${p}/demo/`} className={`${secondary} max-sm:px-3`}>
                <MonitorPlay className="size-4 text-fg-2" aria-hidden />
                {t.demo}
              </a>
            </div>
            <a href={`${p}/docs/`} className={textLink}>
              {t.docs}
              <ArrowRight className="size-4" aria-hidden />
            </a>
          </div>
          </div>

          <Stage
            id="hero"
            label={t.heroAlt}
            eager
            className={`mt-10 aspect-[4/5] sm:mt-12 sm:aspect-video lg:mt-0 lg:aspect-[4/5] ${stage}`}
          />
          </div>

          <div className="mt-10 grid grid-cols-2 gap-x-6 gap-y-8 sm:mt-14 md:grid-cols-4">
            {t.stats.map(([value, unit, label]) => (
              <div key={label} className="border-t border-line pt-4">
                <p className="text-4xl font-semibold tracking-[-0.03em] tabular-nums sm:text-5xl">
                  {value}
                  {unit && <span className="ms-1 text-lg font-medium tracking-normal text-fg-3 sm:text-2xl">{unit}</span>}
                </p>
                <p className="mt-1 text-sm leading-snug text-fg-2">{label}</p>
              </div>
            ))}
          </div>
          <p className="mt-5 text-sm text-fg-3">{t.measured}</p>
        </div>
      </section>

      <section className={`border-t border-line ${section}`}>
        <div className={wrap}>
          <Head label={t.why.label} title={t.why.title} />
          <ul className="mt-12 grid gap-4 md:grid-cols-3">
            {t.why.items.map((x, i) => (
              <li key={x.title} className={`${card} flex flex-col overflow-hidden`}>
                <div className="stage aspect-[640/264] border-b border-line bg-[#0c0e10]">
                  <LazyArt kind="why" i={i} lang={lang} labels={t.why.art} className="size-full" />
                </div>
                <div className="flex flex-1 flex-col p-6 sm:p-7">
                <h3 className="text-xl font-semibold tracking-tight">{x.title}</h3>
                <p className="mt-2 leading-relaxed text-fg-2">{x.text}</p>
                {i === 2 && (
                  <ul className="mt-5 flex flex-wrap gap-2">
                    {t.works.items.map((name) => (
                      <li key={name} className="rounded-md border border-line-2 px-2.5 py-1 text-sm font-medium">
                        {name}
                      </li>
                    ))}
                  </ul>
                )}
                </div>
              </li>
            ))}
          </ul>
        </div>
      </section>

      <section className={`border-t border-line bg-well/60 ${section}`}>
        <div className={wrap}>
          <Head label={t.how.label} title={t.how.title} />
          <ol className="mt-12 space-y-14 sm:space-y-20">
            {t.how.steps.map((s, i) => (
              <li key={s.n} className={`grid items-center gap-6 lg:gap-16 ${i === 1 ? 'lg:grid-cols-[7fr_5fr]' : 'lg:grid-cols-[5fr_7fr]'}`}>
                <div className={i === 1 ? 'lg:order-2' : ''}>
                  <span className="inline-grid h-7 place-items-center rounded-md border border-line-2 bg-panel px-2 font-mono text-xs font-semibold text-amber-fg tabular-nums">
                    {s.n}
                  </span>
                  <h3 className="mt-4 text-2xl font-semibold tracking-tight sm:text-3xl">{s.title}</h3>
                  <p className="mt-3 max-w-md text-lg leading-relaxed text-fg-2">{s.text}</p>
                  {i === 0 && (
                    <a href={`${p}/flash/`} className={`mt-5 ${textLink}`}>
                      {t.flash}
                      <ArrowRight className="size-4" aria-hidden />
                    </a>
                  )}
                </div>
                {i === 1 ? (
                  <div role="img" aria-label={s.alt} className={`aspect-[16/10] ${bay} lg:order-1`}>
                    <div aria-hidden className={dots} />
                    <LazyArt kind="wiring" lang={lang} labels={t.wiring} className="absolute inset-0" />
                  </div>
                ) : (
                  <Stage
                    id={i === 0 ? 'flash' : 'hosts'}
                    label={s.alt}
                    className={`${i === 2 ? 'aspect-square sm:aspect-[16/10] sm:[&>div]:[mask-image:linear-gradient(90deg,transparent_19%,#000_28%,#000_72%,transparent_81%)]' : 'aspect-[16/10]'} ${bay}`}
                  />
                )}
              </li>
            ))}
          </ol>
        </div>
      </section>

      <section className={`border-t border-line ${section}`}>
        <div className={wrap}>
          <Head label={f.label} title={f.title} />
          <div className="mt-12 grid gap-4">
            <article className={`${card} grid items-center gap-8 p-6 sm:p-8 md:grid-cols-2 lg:p-10`}>
              <div>
                <h3 className="text-2xl font-semibold tracking-tight sm:text-3xl">{f.failover.title}</h3>
                <p className="mt-3 max-w-md text-lg leading-relaxed text-fg-2">{f.failover.text}</p>
                <ol className="mt-8 grid gap-3 border-t border-line pt-6">
                  {f.failover.points.map((x, k) => (
                    <li key={x} className="flex items-center gap-3 font-medium">
                      <span className={`size-2 rounded-full ${['bg-ok', 'bg-amber', 'bg-ok'][k]}`} aria-hidden />
                      {x}
                    </li>
                  ))}
                </ol>
              </div>
              <Stage id="failover" label={f.failover.alt} className={`aspect-square ${stage} md:max-w-[28rem] md:justify-self-end`} />
            </article>
            <article className={`${card} grid items-center gap-8 p-6 sm:p-8 md:grid-cols-2 lg:p-10`}>
              <div>
                <h3 className="text-2xl font-semibold tracking-tight sm:text-3xl">{f.speed.title}</h3>
                <p className="mt-3 max-w-md text-lg leading-relaxed text-fg-2">{f.speed.text}</p>
              </div>
              <div className="stage w-full max-w-md rounded-xl border border-line bg-[#0c0e10] p-5 sm:p-6 md:justify-self-end">
                <LazyArt kind="speed" label={f.speed.queue[0] ?? ''} className="aspect-[560/190]" />
                <div className="mt-3 flex justify-between text-sm">
                  <span className="text-fg-3">{f.speed.queue[1]}</span>
                  <span className="font-medium text-amber-fg">{f.speed.queue[2]}</span>
                </div>
              </div>
            </article>
          </div>
          <ul className="mt-14 grid gap-x-10 gap-y-7 sm:mt-16 sm:grid-cols-2 sm:gap-y-10 lg:grid-cols-3">
            {f.list.map((x, i) => (
              <li key={x.title} className="border-t border-line pt-5">
                <p className="font-mono text-xs text-amber-fg">{specs[i]}</p>
                <h3 className="mt-3 text-lg font-semibold tracking-tight">{x.title}</h3>
                <p className="mt-1.5 leading-relaxed text-fg-2">{x.text}</p>
              </li>
            ))}
          </ul>
        </div>
      </section>

      <section className={`border-t border-line ${section}`}>
        <div className={`${wrap} grid gap-12 lg:grid-cols-2 lg:gap-16`}>
          <div>
            <Head label={t.hw.label} title={t.hw.title} />
            <ol className="mt-10 divide-y divide-line border-y border-line">
              {t.hw.parts.map(([name, text], i) => (
                <li key={name} className="flex gap-4 py-5">
                  <span className="font-mono text-sm font-semibold text-amber-fg tabular-nums">0{i + 1}</span>
                  <div>
                    <h3 className="font-semibold tracking-tight">{name}</h3>
                    <p className="mt-1 text-[15px] leading-relaxed text-fg-2">{text}</p>
                    {i === 0 && (
                      <a href={waveshare} className={`mt-2 text-sm ${textLink}`}>
                        {t.hw.buy}
                        <ArrowUpRight className="size-3.5" aria-hidden />
                      </a>
                    )}
                  </div>
                </li>
              ))}
            </ol>
          </div>
          <div className="flex flex-col gap-4 lg:pt-[8.5rem]">
            <div role="img" aria-label={t.hw.parts[0]?.[0]} className={`aspect-[16/10] ${stage}`}>
              <div aria-hidden className={dots} />
              <LazyArt kind="board" labels={t.hw.ports} className="absolute inset-0" />
            </div>
            <div className={`${card} p-6`}>
              <h3 className={`${mono} text-fg-3`}>{t.hw.decks}</h3>
              <p className="mt-4 flex items-center gap-2 font-medium">
                <Check className="size-4 text-ok-fg" aria-hidden />
                {t.hw.list[0]}
                <span className="rounded-full bg-ok-soft px-2.5 py-0.5 font-mono text-xs text-ok-fg">{t.hw.tested}</span>
              </p>
              <p className="mt-4 text-sm text-fg-3">{t.hw.untested}</p>
              <ul className="mt-2 flex flex-wrap gap-2">
                {t.hw.list.slice(1).map((name) => (
                  <li key={name} className="rounded-md border border-line px-2.5 py-1 text-sm text-fg-2">
                    {name}
                  </li>
                ))}
              </ul>
            </div>
          </div>
        </div>
      </section>

      <section className={`border-t border-line bg-well/60 ${section}`}>
        <div className={wrap}>
          <div className="flex flex-col justify-between gap-6 md:flex-row md:items-end">
            <Head label={t.proof.label} title={t.proof.title} />
            <a href={repo} className={`${textLink} shrink-0`}>
              {t.proof.source}
              <ArrowUpRight className="size-4" aria-hidden />
            </a>
          </div>
          <dl className="mt-12 grid gap-px overflow-hidden rounded-2xl border border-line bg-line md:grid-cols-3">
            {t.proof.items.map(([k, v]) => (
              <div key={k} className="bg-panel p-6">
                <dt className="flex items-center gap-2 font-semibold tracking-tight">
                  <Check className="size-4 text-ok-fg" aria-hidden />
                  {k}
                </dt>
                <dd className="mt-2 text-[15px] leading-relaxed text-fg-2">{v}</dd>
              </div>
            ))}
          </dl>
        </div>
      </section>

      <section className={`border-t border-line ${section}`}>
        <div className={`${wrap} grid gap-10 lg:grid-cols-[1fr_2fr] lg:gap-16`}>
          <Head label={t.faq.label} title={t.faq.title}>
            <a href={`${p}/docs/`} className={`mt-6 ${textLink}`}>
              {t.faq.more}
              <ArrowRight className="size-4" aria-hidden />
            </a>
          </Head>
          <div>
            <div className="divide-y divide-line border-y border-line">
              {t.faq.items.map(([q, a]) => (
                <details key={q} className="group">
                  <summary
                    className={`flex min-h-16 cursor-pointer list-none items-center justify-between gap-4 rounded py-4 text-lg font-medium ${focus} [&::-webkit-details-marker]:hidden`}
                  >
                    {q}
                    <Plus className="size-4 shrink-0 text-fg-3 transition-transform group-open:rotate-45" aria-hidden />
                  </summary>
                  <p className="-mt-1 pb-5 leading-relaxed text-fg-2">{a}</p>
                </details>
              ))}
            </div>
          </div>
        </div>
      </section>

      <section className={`${wrap} pb-20 sm:pb-28`}>
        <div className={`stage relative overflow-hidden rounded-2xl border border-line bg-bg px-6 py-12 text-fg sm:px-12 sm:py-16`}>
          <div aria-hidden className={`${dots} [mask-image:radial-gradient(ellipse_70%_80%_at_80%_50%,#000,transparent)]`} />
          <div className="relative">
            <div>
              <h2 className={h2}>{t.cta.title}</h2>
              <p className="mt-3 text-lg text-fg-2">{t.cta.text}</p>
              <ul className="mt-10 grid gap-3 md:grid-cols-3">
                {(
                  [
                    ['flash', `${p}/flash/`, <Usb key="f" />],
                    ['demo', `${p}/demo/`, <Play key="d" />],
                    ['docs', `${p}/docs/`, <BookOpen key="o" />],
                  ] as const
                ).map(([k, href, svg], i) => (
                  <li key={k}>
                    <a
                      href={href}
                      className={`group flex h-full flex-col rounded-xl border p-5 sm:p-6 transition-colors ${focus} ${i === 0 ? 'border-amber bg-amber text-amber-ink hover:brightness-95' : 'border-line-2 bg-panel hover:border-edge'}`}
                    >
                      <span className="flex items-center justify-between text-lg font-semibold [&_svg]:size-4">
                        <span className="flex items-center gap-2">
                          {svg}
                          {t.actions[k][0]}
                        </span>
                        <ArrowRight className="transition-transform group-hover:translate-x-0.5" aria-hidden />
                      </span>
                      <span className={`mt-2 text-[15px] leading-snug ${i === 0 ? 'text-amber-ink/80' : 'text-fg-2'}`}>
                        {t.actions[k][1]}
                      </span>
                    </a>
                  </li>
                ))}
              </ul>
            </div>
          </div>
        </div>
      </section>
    </main>
  )
}
