import type { LucideIcon } from 'lucide-react'
import type { ReactNode } from 'react'
import { cx } from '../lib/util'
import { SIcon, useRun } from './controls'

export interface NoticeAct {
  label: string
  icon?: LucideIcon
  run: () => unknown
  pri?: boolean
  ghost?: boolean
}
export interface NoticeData {
  id: string
  icon: LucideIcon
  lvl: 'bad' | 'warn' | 'ok' | 'info' | 'pair'
  title: string
  text?: ReactNode
  extra?: ReactNode
  acts?: NoticeAct[]
}
/** A note for the floating popup: title, one short line, up to two actions and a close button. */
export interface PopData {
  id: string
  title: string
  text?: string
  acts: NoticeAct[]
  close: () => unknown
}

/** The one notification block: icon, title, one line, actions on the right. */
export function Notice({ n }: { n: NoticeData }) {
  const k = n.lvl === 'pair' ? 'warn' : n.lvl
  return (
    <div className={cx('notice', k)} id={`ntc-${n.id}`} role={n.lvl === 'pair' || n.lvl === 'bad' ? 'alert' : 'status'}>
      <SIcon icon={n.icon} tone={k === 'info' ? '' : k} size={20} />
      <div className="ntext">
        <b>
          {n.title}
          {n.text && <span className="sr">{/[.!?:…]$/.test(n.title) ? ' ' : '. '}</span>}
        </b>
        {n.text && <span>{n.text}</span>}
        {n.extra}
      </div>
      {n.acts && n.acts.length > 0 && (
        <div className="nacts">
          {n.acts.map((a) => (
            <ActBtn key={a.label} a={a} />
          ))}
        </div>
      )}
    </div>
  )
}

export function ActBtn({ a, sm = true }: { a: NoticeAct; sm?: boolean }) {
  const [busy, run] = useRun(a.run)
  const I = a.icon
  return (
    <button
      type="button"
      className={cx('btn', sm && 'sm', a.ghost && 'ghost', a.pri && 'pri')}
      onClick={run}
      aria-disabled={busy || undefined}
      aria-busy={busy || undefined}
    >
      {I && <I size={15} className="ic" aria-hidden />}
      {a.label}
    </button>
  )
}
