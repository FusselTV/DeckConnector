// Search ranking: label prefix, then word prefix, then substring, then ordered fuzzy,
// then typo-tolerant fuzzy. Recent picks for a similar query rank higher and fade with a two-week half-life.
import uFuzzy from '@leeoniya/ufuzzy'

export interface Cmd {
  id: string
  group: Group
  label: string
  /** Second line: the help text of a setting, or where it lives. */
  hint?: string
  /** Help text of a setting, shown as its second line. */
  detail?: string
  /** Hidden search words. */
  kw?: string
  icon?: import('lucide-react').LucideIcon
  /** Changes the device: refused while offline. */
  mut?: boolean
  run: () => unknown
  extras?: { id: string; label: string; run: () => unknown }[]
}

export const GROUPS = ['pal.recent', 'pal.act', 'pal.set', 'pal.src', 'pal.go', 'pal.help'] as const
export type Group = (typeof GROUPS)[number]

export interface Pick {
  q: string
  id: string
  at: number
}

const HALF_LIFE = 14 * 86_400_000
const MAX_BOOST = 3

export const fold = (s: string) => s.normalize('NFD').replace(/\p{M}/gu, '').toLowerCase().trim()

function similarity(q: string, picked: string) {
  if (!q || q === picked) return 1
  if (picked.startsWith(q)) return q.length / picked.length
  if (picked && q.startsWith(picked)) return picked.length / q.length
  return 0
}

export function boostOf(picks: Pick[], query: string, now: number) {
  const q = fold(query)
  const out = new Map<string, number>()
  for (const p of picks) {
    const w = similarity(q, p.q)
    if (w) out.set(p.id, Math.min(MAX_BOOST, (out.get(p.id) ?? 0) + w * 0.5 ** (Math.max(0, now - p.at) / HALF_LIFE)))
  }
  return out
}

export function remember(picks: Pick[], query: string, id: string, now = Date.now()): Pick[] {
  const q = fold(query)
  return [{ q, id, at: now }, ...picks.filter((p) => !(p.id === id && p.q === q))].slice(0, 50)
}

// IntraMode is a const enum without a runtime value: 0 = MultiInsert, 1 = SingleError.
/* eslint-disable @typescript-eslint/no-unsafe-enum-assignment */
const ordered = new uFuzzy({ intraMode: 0 as uFuzzy.IntraMode, intraIns: 2 })
const typo = new uFuzzy({ intraMode: 1 as uFuzzy.IntraMode })
/* eslint-enable @typescript-eslint/no-unsafe-enum-assignment */

export function rank(cmds: Cmd[], query: string, picks: Pick[] = [], now = Date.now()): Cmd[] {
  const q = fold(query)
  const boost = boostOf(picks, query, now)
  const b = (c: Cmd) => boost.get(c.id) ?? 0
  if (!q)
    return cmds
      .filter((c) => b(c) > 0)
      .sort((x, y) => b(y) - b(x))
      .slice(0, 5)
  const names = cmds.map((c) => fold(c.label))
  const hay = cmds.map((c, i) => `${names[i] ?? ''} ${fold(c.hint ?? '')} ${fold(c.kw ?? '')} ${fold(c.detail ?? '')}`)
  const tiers = new Map<number, number>()
  names.forEach((n, i) => {
    const t = n.startsWith(q)
      ? 0
      : n.split(/[\s\-_./›]+/).some((w) => w.startsWith(q))
        ? 1
        : hay[i]?.includes(q)
          ? 2
          : -1
    if (t >= 0) tiers.set(i, t)
  })
  for (const i of ordered.filter(hay, q) ?? []) if (!tiers.has(i)) tiers.set(i, 3)
  for (const i of typo.filter(hay, q) ?? []) if (!tiers.has(i)) tiers.set(i, 4)
  return [...tiers]
    .map(([i, t]) => [i, t - b(cmds[i] as Cmd)] as const)
    .sort(
      ([a, ta], [c, tc]) => ta - tc || GROUPS.indexOf((cmds[a] as Cmd).group) - GROUPS.indexOf((cmds[c] as Cmd).group),
    )
    .map(([i]) => cmds[i] as Cmd)
}
