'use client'
import type { MouseEvent } from 'react'
import { focus } from './ui'

const timers = new WeakMap<HTMLDetailsElement, number>()

// Safari closes <details> without a transition, so data-closing runs the close animation before open goes false.
function setOpen(d: HTMLDetailsElement, open: boolean) {
  clearTimeout(timers.get(d))
  if (open) {
    delete d.dataset.closing
    d.open = true
    return
  }
  d.dataset.closing = ''
  const ms = matchMedia('(prefers-reduced-motion: reduce)').matches ? 0 : 320
  timers.set(
    d,
    window.setTimeout(() => ((d.open = false), delete d.dataset.closing), ms),
  )
}

function toggle(e: MouseEvent<HTMLElement>) {
  e.preventDefault()
  const d = e.currentTarget.parentElement as HTMLDetailsElement
  const opening = !d.open || 'closing' in d.dataset
  d.parentElement?.querySelectorAll<HTMLDetailsElement>('details[open]').forEach((o) => o !== d && setOpen(o, false))
  setOpen(d, opening)
}

export function Faq({ items, className = '' }: { items: [string, string][]; className?: string }) {
  return (
    <div className={`divide-y divide-line border-y border-line ${className}`}>
      {items.map(([q, a]) => (
        <details key={q} className="group">
          <summary
            onClick={toggle}
            className={`flex min-h-16 cursor-pointer list-none items-center justify-between gap-4 rounded py-4 text-lg font-medium ${focus} [&::-webkit-details-marker]:hidden`}
          >
            <h3>{q}</h3>
            <span
              aria-hidden
              className="relative size-4 shrink-0 text-fg-3 transition-transform duration-300 group-open:rotate-45 group-data-closing:rotate-0"
            >
              <span className="absolute inset-x-0 top-1/2 h-[1.5px] -translate-y-1/2 rounded bg-current" />
              <span className="absolute inset-y-0 left-1/2 w-[1.5px] -translate-x-1/2 rounded bg-current" />
            </span>
          </summary>
          <p className="pb-5 leading-relaxed text-fg-2">{a}</p>
        </details>
      ))}
    </div>
  )
}
