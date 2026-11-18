import { Box, Building2, Cable, Check, LayoutGrid, Repeat, Wifi, X } from 'lucide-react'
import type { copy, VsRow } from './copy'

const icons = { wifi: Wifi, hosts: Repeat, case: Box, lan: Cable, apps: LayoutGrid, support: Building2 }
const cols = 'grid grid-cols-[1fr_7rem_7rem] items-center sm:grid-cols-[1fr_13rem_13rem]'

function Mark({ v, label }: { v: 'y' | 'n'; label: string }) {
  const Icon = v === 'y' ? Check : X
  return (
    <span
      className={`grid size-10 place-items-center rounded-full ${v === 'y' ? 'bg-ok-soft text-ok-fg' : 'bg-bad-soft text-bad-fg'}`}
    >
      <Icon className="size-5" strokeWidth={2.5} aria-hidden />
      <span className="sr-only">{label}</span>
    </span>
  )
}

export function Versus({ t, rows = t.rows }: { t: (typeof copy)['en']['vs']; rows?: VsRow[] }) {
  return (
    <div
      role="table"
      aria-label={t.title}
      className="overflow-hidden rounded-2xl border border-line bg-panel shadow-panel"
    >
      <div role="row" className={`${cols} border-b border-line`}>
        <span aria-hidden />
        {t.cols.map((c, i) => (
          <span
            key={c}
            role="columnheader"
            className={`px-2 pt-6 pb-4 text-center text-sm font-semibold tracking-tight sm:text-lg ${i ? 'text-amber-fg' : 'text-fg-2'}`}
          >
            {c}
          </span>
        ))}
      </div>
      <div role="row" className={`${cols} border-b border-line bg-well/50`}>
        <span role="rowheader" className="ps-4 text-[15px] font-medium sm:ps-7 sm:text-lg">
          {t.priceLabel}
        </span>
        {t.price.map((p, i) => (
          <span key={p} role="cell" className="grid place-items-center px-2 py-5 text-center">
            <span
              className={`text-base font-semibold tracking-[-0.03em] whitespace-nowrap tabular-nums sm:text-3xl ${i ? 'text-fg' : 'text-fg-2'}`}
            >
              {p}
            </span>
            {t.priceNote[i] && <span className="mt-1 text-xs text-fg-3 sm:text-sm">{t.priceNote[i]}</span>}
          </span>
        ))}
      </div>
      {rows.map((r) => {
        const Icon = icons[r.icon]
        return (
          <div key={r.k} role="row" className={`${cols} border-t border-line first:border-t-0`}>
            <span
              role="rowheader"
              className="flex items-center gap-4 py-5 ps-4 text-[15px] font-medium sm:ps-7 sm:text-lg"
            >
              <Icon className="size-5 shrink-0 text-fg-3 max-sm:hidden" aria-hidden />
              {r.k}
            </span>
            {r.v.map((v, i) => (
              <span key={i} role="cell" className="grid place-items-center px-2 py-4">
                <Mark v={v} label={v === 'y' ? t.yes : t.no} />
              </span>
            ))}
          </div>
        )
      })}
    </div>
  )
}
