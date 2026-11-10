import { Check, Minus, X } from 'lucide-react'
import { Fragment } from 'react'
import type { Cell, Row } from './data'

const icon = {
  y: <Check className="size-4 text-ok-fg" aria-hidden />,
  n: <X className="size-4 text-bad-fg" aria-hidden />,
  p: <Minus className="size-4 text-amber-fg" aria-hidden />,
  i: <span className="m-[5px] block size-1.5 rounded-full bg-fg-3" aria-hidden />,
}

function Value({ c, win, better }: { c: Cell; win: boolean; better: string }) {
  return (
    <td className={`px-3 py-3 align-top sm:px-4 ${win ? 'bg-ok-soft' : ''}`}>
      <span className="flex gap-2">
        <span className="mt-[3px] shrink-0">{icon[c[0]]}</span>
        <span className={win ? 'font-medium text-fg' : 'text-fg-2'}>
          {c[1]}
          {win && <span className="sr-only"> ({better})</span>}
        </span>
      </span>
    </td>
  )
}

/** Dock first, like the compare animation: the reference on the left, DeckConnector on the right. groups: [title, row count] in row order. */
export function Table({ cols, rows, better, groups, caption }: { cols: string[]; rows: Row[]; better: string; groups?: [string, number][]; caption: string }) {
  const starts = new Map<number, string>()
  groups?.reduce((at, [title, n]) => (starts.set(at, title), at + n), 0)
  return (
    <div className="overflow-hidden rounded-2xl border border-line bg-panel shadow-panel">
      <table role="table" className="w-full border-collapse text-left text-[15px] leading-snug">
        <caption className="sr-only">{caption}</caption>
        <thead>
          <tr role="row" className="border-b border-line max-sm:grid max-sm:grid-cols-2">
            <td className="max-sm:hidden" />
            {[...cols].reverse().map((c, i) => (
              <th role="columnheader" key={c} scope="col" className={`px-3 py-4 text-base font-semibold tracking-tight sm:px-4 ${i ? 'text-amber-fg' : 'text-fg-2'}`}>
                {c}
              </th>
            ))}
          </tr>
        </thead>
        <tbody>
          {rows.map((r, i) => (
            <Fragment key={r.k}>
              {starts.has(i) && (
                <tr role="row" className="max-sm:block">
                  <th role="columnheader" colSpan={3} scope="colgroup" className="border-t border-line bg-well/60 px-3 pt-5 pb-2 font-mono text-xs font-medium tracking-[0.14em] text-fg-3 uppercase max-sm:block sm:px-4">
                    {starts.get(i)}
                  </th>
                </tr>
              )}
              <tr role="row" className="border-t border-line transition-colors hover:bg-panel-2 max-sm:grid max-sm:grid-cols-2">
                <th role="rowheader" scope="row" className="px-3 pt-3 font-medium text-fg max-sm:col-span-2 sm:w-[30%] sm:px-4 sm:py-3">
                  {r.k}
                </th>
                <Value c={r.b} win={r.win === 'b'} better={better} />
                <Value c={r.a} win={r.win === 'a'} better={better} />
              </tr>
            </Fragment>
          ))}
        </tbody>
      </table>
    </div>
  )
}
