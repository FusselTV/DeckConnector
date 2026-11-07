import type { LucideIcon } from 'lucide-react'
import type { ReactNode } from 'react'
import { cx } from '../lib/util'
import { SIcon } from './controls'

export interface NoticeAct {
  label: string
  icon?: LucideIcon
  run: () => void
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

/** The one notification block (rule 13): icon, title, one line, actions on the right. */
export function Notice({ n }: { n: NoticeData }) {
  const k = n.lvl === 'pair' ? 'warn' : n.lvl
  return (
    <div className={cx('notice', k)} id={`ntc-${n.id}`} role={n.lvl === 'pair' || n.lvl === 'bad' ? 'alert' : 'status'}>
      <SIcon icon={n.icon} tone={k === 'info' ? '' : k} size={20} />
      <div className="ntext">
        <b>{n.title}</b>
        {n.text && <span>{n.text}</span>}
        {n.extra}
      </div>
      {n.acts && n.acts.length > 0 && (
        <div className="nacts">
          {n.acts.map((a) => {
            const I = a.icon
            return (
              <button
                key={a.label}
                type="button"
                className={cx('btn sm', a.ghost && 'ghost', a.pri && 'pri')}
                onClick={a.run}
              >
                {I && <I size={15} className="ic" aria-hidden />}
                {a.label}
              </button>
            )
          })}
        </div>
      )}
    </div>
  )
}
