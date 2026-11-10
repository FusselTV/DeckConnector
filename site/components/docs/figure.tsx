import type { ReactNode } from 'react'
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
