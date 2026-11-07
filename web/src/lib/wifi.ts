import type { WifiAp } from '../api/types'

export interface Group {
  ssid: string
  open: boolean
  aps: WifiAp[]
  rssi: number
}

/** One AP per scan entry, grouped by SSID with the best signal. A group returns sorted, strongest first. */
export function groupScan(scan: WifiAp[], current?: string): Group[] {
  const by = new Map<string, Group>()
  for (const n of scan) {
    if (!n.ssid) continue
    const g = by.get(n.ssid) ?? { ssid: n.ssid, open: n.open, aps: [], rssi: -999 }
    g.aps.push(n)
    g.rssi = Math.max(g.rssi, n.rssi)
    by.set(n.ssid, g)
  }
  return [...by.values()]
    .map((g) => ({ ...g, aps: g.aps.sort((a, b) => b.rssi - a.rssi) }))
    .sort((a, b) => Number(b.ssid === current) - Number(a.ssid === current) || b.rssi - a.rssi)
}
