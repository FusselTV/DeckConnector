import type { ReactNode } from 'react'
import Image from 'next/image'
import { ArrowDown, ArrowRight } from 'lucide-react'

export type Lang = 'en' | 'de'

export function Figure({ title, desc, children }: { title: string; desc?: string; children: ReactNode }) {
  return (
    <figure role="group" aria-label={title} className="@container not-prose my-6 rounded-2xl border border-line bg-well p-4 text-fg @lg:p-5">
      {desc && <p className="sr-only">{desc}</p>}
      {children}
    </figure>
  )
}

/** Points right on wide figures, down when the row stacks. */
export function Flow({ label }: { label?: string }) {
  return (
    <div aria-hidden className="flex shrink-0 items-center justify-center gap-1 text-fg-3 @lg:flex-col">
      <ArrowDown className="size-4 @lg:hidden" />
      <ArrowRight className="size-4 @max-lg:hidden" />
      {label && <span className="font-mono text-xs leading-tight">{label}</span>}
    </div>
  )
}

export function Next({ links, lang = 'en' }: { links: [string, string][]; lang?: Lang }) {
  return (
    <nav aria-label={lang === 'de' ? 'Weiter' : 'Next steps'} className="not-prose mt-10 flex flex-wrap items-center gap-2 border-t border-line pt-5 text-sm">
      <span className="me-1 font-mono text-xs uppercase tracking-wider text-fg-3">{lang === 'de' ? 'Weiter' : 'Next'}</span>
      {links.map(([text, href]) => (
        <a key={href} href={href} className="inline-flex min-h-11 items-center gap-1.5 rounded-lg border border-line bg-panel px-3 py-1.5 font-medium text-fg transition-colors hover:border-amber hover:text-amber-fg">
          {text}
          <ArrowRight aria-hidden className="size-3.5" />
        </a>
      ))}
    </nav>
  )
}

export function Buttons({ links }: { links: [string, string][] }) {
  return (
    <div className="not-prose my-6 flex flex-wrap gap-3">
      {links.map(([text, href], i) => (
        <a
          key={href}
          href={href}
          className={`inline-flex h-11 items-center justify-center rounded-lg px-5 text-sm font-semibold transition-colors focus-visible:outline-2 focus-visible:outline-offset-2 focus-visible:outline-(--focus) ${i ? 'border border-line-2 text-fg hover:bg-panel-2' : 'bg-amber text-amber-ink hover:brightness-110'}`}
        >
          {text}
        </a>
      ))}
    </div>
  )
}

/** A screenshot in both themes: /docs/<src>-dark.webp and -light.webp, shown by the site's theme. */
export function Shot({ src, alt, width, height }: { src: string; alt: string; width: number; height: number }) {
  return (
    <>
      <Image src={`/docs/${src}-dark.webp`} alt={alt} width={width} height={height} className="shot-dark h-auto max-w-full" />
      <Image src={`/docs/${src}-light.webp`} alt={alt} width={width} height={height} className="shot-light h-auto max-w-full" />
    </>
  )
}
