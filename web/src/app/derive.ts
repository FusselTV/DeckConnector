import { Monitor, Radio, Server, CircleHelp, type LucideIcon } from 'lucide-react'
import type { DeckInfo, IpConf, Source, Status } from '../api/types'
import type { T, TKey } from '../i18n'
import { modelOf, type ModelKey } from '../deck/models'

export const KIND_ICON: Record<Source['kind'], LucideIcon> = {
  companion: Radio,
  app: Monitor,
  server: Server,
  unknown: CircleHelp,
}
export const hk = (s: Source) => `${s.kind}|${s.host}`
export const hostName = (s: Source) => s.name || s.host
export const hostLabel = (s: Source, t: T) => `${hostName(s)} · ${t(`prog.${s.kind}`)}`
export const viaKey = (s: Source): TKey =>
  s.via === 'satellite' ? 'via.satellite' : s.kind === 'server' ? 'via.ws' : 'via.dock'

export function stateKey(s: Source, pending: string | null): TKey {
  if (pending === hk(s)) return 'st.pending'
  if (s.connecting) return 'st.connecting'
  if (s.drawing) return 'st.drawing'
  if (s.chosen && !s.connected) return 'st.chosen_off'
  if (s.kind === 'unknown') return 'st.unknown'
  return s.connected ? 'st.ready' : 'st.off'
}

/** The `host` value PUT /api/config wants for a source. */
export const hostKey = (s: Source) => (s.via === 'satellite' ? 'satellite' : s.host)

export interface Deck extends DeckInfo {
  kind: ModelKey
  speed: 'high' | 'full'
}

/** `decks`, else the single `device`. Per-deck USB speed falls back to the global counter. */
export function decksOf(st: Status): Deck[] {
  const list = st.decks?.length ? st.decks : st.device ? [st.device] : []
  return list.map((d) => ({
    ...d,
    kind: modelOf(d.model, d.keys),
    speed: d.usb_speed ?? st.counters?.usb_speed ?? 'high',
  }))
}

export const deckName = (d: Deck, i: number, n: number, t: T) =>
  n > 1 ? `${t('deck.n', { n: i + 1 })} · …${d.serial.slice(-4)}` : d.model

export const sigWord = (r: number): TKey => (r >= -60 ? 'sig.good' : r >= -72 ? 'sig.ok' : 'sig.weak')

export const compDock = (st: Status, satellite: string | undefined) =>
  (st.sources ?? []).filter((s) => s.kind === 'companion' && s.via === 'dock' && s.connected && satellite !== s.host)

/** Addresses the device answers on after a restart, scheme from https_only (until the firmware sends `reach`). */
/** Where the device answers after a restart. `next`: the IP settings about to be saved; DHCP gives an address
 *  nobody knows yet, so only the name is left for that interface. */
export function reachList(
  st: Status,
  https: boolean,
  name: string,
  next: { lan?: Partial<IpConf>; wlan?: Partial<IpConf> } = {},
): string[] {
  const sch = https ? 'https' : 'http'
  const out: string[] = []
  const n = st.network
  const ip = (k: 'lan' | 'wlan', now?: string) => {
    const c = next[k]
    return !c ? now : c.dhcp ? undefined : c.ip
  }
  const lan = ip('lan', n?.lan?.ip)
  const wlan = ip('wlan', n?.wlan?.ip)
  if (n?.lan?.cable && lan) out.push(`${sch}://${lan}`)
  if (n?.wlan?.ip && wlan) out.push(`${sch}://${wlan}`)
  if (name) out.push(`${sch}://${name}.local`)
  return out
}
