import { ChevronDown, type LucideIcon } from 'lucide-react'
import type { ReactNode } from 'react'
import { SIcon, type Tone } from './controls'

const stop = (e: { stopPropagation(): void; preventDefault?(): void }) => e.stopPropagation()

/** The one expandable row (rule 3). Rows sharing `group` form an accordion (rule 17). */
export function Row(p: {
  id?: string
  icon: LucideIcon
  tone?: Tone
  title: ReactNode
  status?: ReactNode
  act?: ReactNode
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
        {p.act && (
          // eslint-disable-next-line jsx-a11y/no-static-element-interactions -- keeps clicks on row actions from toggling the row
          <span className="xact" onClick={stop} onKeyDown={stop}>
            {p.act}
          </span>
        )}
        <ChevronDown size={16} className="ic chev" aria-hidden />
      </summary>
      <div className="xbody">{p.children}</div>
    </details>
  )
}
