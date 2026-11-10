import type { ReactNode } from 'react'
import type { Lang } from '~/lib/i18n'
import type { Rich } from './copy'
import { out } from './links'

export const wrap = 'mx-auto w-full max-w-6xl px-4 sm:px-6'
export const section = 'py-16 sm:py-24'
export const focus = 'focus-visible:outline-2 focus-visible:outline-offset-2 focus-visible:outline-focus'
const button = `inline-flex h-12 items-center justify-center gap-2 rounded-lg px-5 text-[15px] font-semibold transition-[filter,background-color,border-color] ${focus}`
export const primary = `${button} bg-amber text-amber-ink shadow-[inset_0_1px_0_rgb(255_255_255/0.35),0_8px_24px_-8px_rgb(255_176_32/0.5)] hover:brightness-95`
export const secondary = `${button} border border-line-2 bg-panel text-fg hover:border-edge hover:bg-panel-2`
export const textLink = `inline-flex min-h-11 items-center gap-1.5 rounded font-medium text-amber-fg underline-offset-4 hover:underline ${focus}`
export const inline = `rounded underline decoration-line-2 underline-offset-[3px] transition-colors hover:text-fg hover:decoration-amber ${focus}`
export const mono = 'font-mono text-xs font-medium tracking-[0.14em] uppercase'
export const dots =
  'pointer-events-none absolute inset-0 bg-[radial-gradient(var(--line-2)_1.2px,transparent_1.2px)] bg-[size:28px_28px] opacity-50'
export const card = 'rounded-2xl border border-line bg-panel shadow-panel'
export const lift = 'transition-[translate,border-color,box-shadow] duration-300 motion-safe:hover:-translate-y-0.5 hover:border-line-2'
export const stage =
  'stage relative overflow-hidden rounded-2xl border border-line bg-[#0c0e10] shadow-[inset_0_1px_0_rgb(255_255_255/0.06),var(--stage-shadow)] [:root.light_&]:border-[#d3d7dc] [:root.light_&]:bg-[#eceeef]'
export const h2 = 'text-[2rem] leading-[1.1] font-semibold tracking-[-0.025em] text-balance sm:text-5xl'

export function Text({ lang, r }: { lang: Lang; r: Rich }) {
  const o = out(lang)
  return r.map((x, i) =>
    typeof x === 'string' ? (
      x
    ) : (
      <a key={i} href={o[x[0]]} className={inline}>
        {x[1]}
      </a>
    ),
  )
}

export function Head({ label, title, id, children }: { label: string; title: string | string[]; id?: string; children?: ReactNode }) {
  return (
    <div className="max-w-3xl">
      <p className={`${mono} text-amber-fg`}>{label}</p>
      <h2 id={id} className={`mt-4 ${h2}`}>
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

export function JsonLd({ data }: { data: object }) {
  // `<` escaped so text in the data can never close the script tag.
  return <script type="application/ld+json" dangerouslySetInnerHTML={{ __html: JSON.stringify(data).replace(/</g, '\\u003c') }} />
}

export const faqLd = (items: [string, string][]) => ({
  '@type': 'FAQPage',
  mainEntity: items.map(([q, a]) => ({ '@type': 'Question', name: q, acceptedAnswer: { '@type': 'Answer', text: a } })),
})
