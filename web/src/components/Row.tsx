import { ChevronDown, type LucideIcon } from 'lucide-react'
import type { ReactNode } from 'react'
import { SIcon, type Tone } from './controls'

/** The one expandable row. Rows sharing `group` form an accordion. */
export function Row(p: {
  id?: string
  icon: LucideIcon
  tone?: Tone
  title: ReactNode
  status?: ReactNode
  group?: string
  open?: boolean
  children: ReactNode
}) {
  return (
    <details className="xrow" id={p.id} name={p.group} open={p.open}>
      <summary>
        <SIcon icon={p.icon} tone={p.tone ?? 'acc'} />
        <b>{p.title}</b>
        <span className="xst">{p.status}</span>
        <ChevronDown size={16} className="ic chev" aria-hidden />
      </summary>
      <div className="xbody">{p.children}</div>
    </details>
  )
}
