import type { Lang } from './figure'

type Row = [label: string, dc: number, dock: number]

const names = ['Deck Connector', 'Elgato Network Dock']

const fmt = (v: number, unit: string, lang?: Lang) => `${lang === 'de' ? String(v).replace('.', ',') : v} ${unit}`

/** Paired bars on one axis per chart. Values are printed, so color never carries the reading alone. */
export function BenchBars({ title, unit, rows, lang }: { title: string; unit: string; rows: Row[]; lang?: Lang }) {
  const max = Math.max(...rows.flatMap(([, a, b]) => [a, b]))
  const bar = (v: number, dc: boolean, label: string) => (
    <div className="flex items-center gap-2" title={`${label}, ${dc ? names[0] : names[1]}: ${fmt(v, unit, lang)}`}>
      <span
        className={`h-3 rounded-e-[4px] ${dc ? 'bg-amber [.light_&]:bg-[#c27f00]' : 'bg-[#858c94] [.light_&]:bg-[#8a929b]'}`}
        style={{ width: `${Math.max((v / max) * 100, 1.5)}%` }}
      />
      <span className="shrink-0 font-mono text-xs tabular-nums text-fg-2">{fmt(v, unit, lang)}</span>
    </div>
  )
  return (
    <figure className="@container not-prose my-6 rounded-2xl border border-line bg-well p-4 @lg:p-5">
      <figcaption className="mb-3 flex flex-wrap items-baseline justify-between gap-x-4 gap-y-1">
        <span className="text-sm font-semibold text-fg">{title}</span>
        <span className="flex gap-3 text-xs text-fg-3">
          <span className="flex items-center gap-1.5">
            <span aria-hidden className="size-2.5 rounded-sm bg-amber [.light_&]:bg-[#c27f00]" />
            {names[0]}
          </span>
          <span className="flex items-center gap-1.5">
            <span aria-hidden className="size-2.5 rounded-sm bg-[#858c94] [.light_&]:bg-[#8a929b]" />
            {names[1]}
          </span>
        </span>
      </figcaption>
      <dl className="grid gap-3">
        {rows.map(([label, dc, dock]) => (
          <div key={label} className="grid gap-1 @lg:grid-cols-[13rem_1fr] @lg:gap-3">
            <dt className="text-sm leading-snug text-fg-2">{label}</dt>
            <dd className="grid gap-1 border-s border-line ps-0.5">
              {bar(dc, true, label)}
              {bar(dock, false, label)}
            </dd>
          </div>
        ))}
      </dl>
    </figure>
  )
}
