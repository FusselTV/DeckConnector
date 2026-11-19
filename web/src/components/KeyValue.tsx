import { Fragment, type ReactNode } from 'react'

/** Key-value rows for technical facts. A missing value is left out instead of shown as a dash. */
export function KeyValue({ rows, compact }: { rows: [ReactNode, ReactNode, boolean?][]; compact?: boolean }) {
  return (
    <dl className={compact ? 'dl compact' : 'dl'}>
      {rows
        .filter(([, v]) => v !== undefined && v !== null && v !== '')
        .map(([k, v, mono], i) => (
          <Fragment key={i}>
            <dt>{k}</dt>
            <dd className={mono ? 'mono' : undefined}>{v}</dd>
          </Fragment>
        ))}
    </dl>
  )
}
