import type { LucideIcon } from 'lucide-react'
import type { ReactNode } from 'react'
import { useStore } from '../app/store'
import { cx } from '../lib/util'

/** A section with a 65 px title bar: icon, noun, and controls for the whole card on the right. */
export function Card(p: {
  id: string
  icon: LucideIcon
  title: ReactNode
  right?: ReactNode
  children: ReactNode
  className?: string
  /** Stays usable while the page is offline or the session ended (only read-only cards). */
  live?: boolean
}) {
  const locked = useStore((s) => s.conn === 'offline' || s.auth === 'expired' || !!s.restarting)
  const I = p.icon
  return (
    <section className={cx('card', p.className)} id={p.id} aria-labelledby={`${p.id}-h`}>
      <header className="ch">
        <h3 id={`${p.id}-h`}>
          <I size={18} className="ic" aria-hidden />
          {p.title}
        </h3>
        <span className="sp" />
        {p.right}
      </header>
      <fieldset className="body fs" disabled={locked && !p.live}>
        {p.children}
      </fieldset>
    </section>
  )
}
