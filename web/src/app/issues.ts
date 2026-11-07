import {
  Bug,
  Cable,
  Check,
  Cpu,
  EthernetPort,
  LayoutGrid,
  Link2,
  Plug,
  Radio,
  RefreshCw,
  Search,
  Unplug,
  Usb,
  X,
} from 'lucide-react'
import { useMemo } from 'react'
import type { NoticeData } from '../components/Notice'
import { useActions } from './actions'
import { compDock, decksOf, deckName, hostLabel, KIND_ICON } from './derive'
import { crashAckAtom, crashAtom, crashKey, deckSelAtom } from './state'
import { ls } from '../lib/util'
import { useRoute, useStore } from './store'
import { useUi } from './ui'

/** Need-attention items for the top of Overview and the banner on other tabs (rule 13), worst first. */
export function useIssues(): { list: NoticeData[]; notes: NoticeData[] } {
  const { t, go, jump, fmt } = useUi()
  const a = useActions()
  const st = useStore((s) => s.status)
  const satellite = useStore((s) => s.config?.satellite)
  const canRetry = useRoute('POST', '/api/sources/retry')
  const crash = crashAtom.use()
  const ack = crashAckAtom.use()
  const crashed = !!crash && crashKey(crash) !== ack
  return useMemo(() => {
    const list: NoticeData[] = []
    const notes: NoticeData[] = []
    const sources = st.sources ?? []
    const decks = decksOf(st)
    const chosen = sources.find((s) => s.chosen)
    const lan = st.network?.lan
    const lanDead = !!lan?.cable && !!lan.dead
    if (!decks.length)
      list.push({
        id: 'nodeck',
        icon: Unplug,
        lvl: 'bad',
        title: t('is.nodeck'),
        text: t('is.nodeck_h'),
        acts: [{ label: t('act.reconnect'), icon: Usb, run: () => void a.reconnect(0) }],
      })
    if (chosen && !chosen.connected && !chosen.connecting)
      list.push({
        id: 'chosen',
        icon: KIND_ICON[chosen.kind],
        lvl: 'bad',
        title: t('is.chosen_off', { host: hostLabel(chosen, t) }),
        text: [
          chosen.error,
          chosen.since && t('is.since', { time: fmt.time(Date.parse(chosen.since)) }),
          t('is.chosen_off_h'),
          lanDead && t('is.lan_too'),
        ]
          .filter(Boolean)
          .join(' · '),
        acts: [
          ...(canRetry ? [{ label: t('act.retry'), icon: RefreshCw, run: () => void a.retry(chosen) }] : []),
          { label: t('act.check'), icon: Search, run: () => jump('sources', 'card-src') },
        ],
      })
    else if (!sources.some((s) => s.connected && s.kind !== 'unknown'))
      list.push({
        id: 'nohost',
        icon: Plug,
        lvl: 'warn',
        title: t('is.nohost'),
        text: t('is.nohost_h'),
        acts: [{ label: t('act.sources'), icon: Cable, run: () => go('sources') }],
      })
    if (lanDead)
      list.push({
        id: 'landead',
        icon: EthernetPort,
        lvl: 'warn',
        title: t('is.landead'),
        text: t('is.landead_h'),
        acts: [{ label: t('act.network'), icon: EthernetPort, run: () => jump('network', 'row-lan') }],
      })
    decks.forEach((d, i) => {
      if (d.speed === 'high') return
      list.push({
        id: `full${i}`,
        icon: Usb,
        lvl: 'warn',
        title: decks.length > 1 ? t('is.full_n', { deck: deckName(d, i, decks.length, t) }) : t('is.full'),
        text: t('is.full_h'),
        acts: [
          {
            label: t('act.deck'),
            icon: LayoutGrid,
            run: () => {
              deckSelAtom.set(i)
              jump('deck', 'usb-speed')
            },
          },
        ],
      })
    })
    for (const s of compDock(st, satellite))
      list.push({
        id: `cd${s.host}`,
        icon: Radio,
        lvl: 'warn',
        title: t('is.compdock', { name: s.name || s.host }),
        text: t('is.compdock_h'),
        acts: [{ label: t('act.to_sat'), icon: Radio, run: () => void a.toSatellite(s) }],
      })
    if ((st.api ?? 1) < 1)
      list.push({
        id: 'oldfw',
        icon: Cpu,
        lvl: 'warn',
        title: t('is.oldfw'),
        text: t('is.oldfw_h', { need: 1, have: st.api ?? 0 }),
        acts: [{ label: t('act.firmware'), icon: Cpu, run: () => go('firmware') }],
      })
    const P = st.pairing
    if (P?.request) {
      const name = P.request_name || P.request
      list.push({
        id: 'pair',
        icon: Link2,
        lvl: 'pair',
        title: t('pair.t', { name, ip: P.request }),
        text: t('pair.h'),
        acts: [
          { label: t('act.accept'), icon: Check, run: () => void a.accept(name) },
          { label: t('act.deny'), icon: X, run: () => void a.deny(P.request ?? '') },
        ],
      })
    }
    if (crash && crashed)
      notes.push({
        id: 'crash',
        icon: Bug,
        lvl: 'info',
        title: t('is.crash'),
        text: crash.task,
        acts: [
          { label: t('is.view_crash'), ghost: true, run: () => jump('diagnose', 'dia-crash') },
          {
            label: t('is.hide'),
            ghost: true,
            run: () => {
              crashAckAtom.set(crashKey(crash))
              ls.set('dc.crashAck', crashKey(crash))
            },
          },
        ],
      })
    const rank = { bad: 0, warn: 1, pair: 2, ok: 3, info: 4 }
    list.sort((x, y) => rank[x.lvl] - rank[y.lvl])
    return { list, notes }
  }, [st, satellite, canRetry, crash, crashed, t, fmt, go, jump, a])
}
