import {
  Activity,
  Bug,
  Cable,
  Check,
  EthernetPort,
  LayoutGrid,
  Link2,
  Plug,
  Radio,
  RefreshCw,
  Search,
  Settings,
  Unplug,
  Usb,
  X,
} from 'lucide-react'
import { createContext, createElement, useContext, useEffect, useMemo, useState, type ReactNode } from 'react'
import { api } from '../api/client'
import type { UpdateCheck } from '../api/types'
import type { NoticeData, PopData } from '../components/Notice'
import { useActions } from './actions'
import { compDock, decksOf, deckName, hostLabel, KIND_ICON } from './derive'
import {
  bootAckAtom,
  configErrAtom,
  statusErrAtom,
  crashAckAtom,
  crashAtom,
  crashKey,
  deckSelAtom,
  logFocusAtom,
} from './state'
import { ls, ss } from '../lib/util'
import { useStore } from './store'
import { errText, useUi } from './ui'
import { refreshConfig } from './connection'

interface Issues {
  list: NoticeData[]
  notes: NoticeData[]
  offer?: PopData
}
const Ctx = createContext<Issues>({ list: [], notes: [] })
/** Need-attention items for the top of Overview and the banner on other tabs, worst first. */
export const useIssues = () => useContext(Ctx)
/** Works the issues out once for every reader; its children are passed in, so they do not re-render with it. */
export const IssuesProvider = ({ children }: { children: ReactNode }) =>
  createElement(Ctx.Provider, { value: useIssueList() }, children)

const LOST = ['companion_lost', 'companion_silent', 'server_lost', 'host_left']

function useIssueList(): Issues {
  const { t, go, jump, fmt } = useUi()
  const a = useActions()
  const st = useStore((s) => s.status)
  const bootAt = useStore((s) => s.bootAt)
  const satellite = useStore((s) => s.config?.satellite)
  const maxDecks = useStore((s) => s.config?.max_decks)
  const cfgErr = configErrAtom.use()
  const stErr = statusErrAtom.use()
  const crash = crashAtom.use()
  const bootAck = bootAckAtom.use()
  const ack = crashAckAtom.use()
  const crashed = !!crash && crashKey(crash) !== ack
  const auto = useStore((s) => s.config?.update_auto)
  const fw = st.firmware
  const [upd, setUpd] = useState<UpdateCheck | null>(null)
  const [updHid, setUpdHid] = useState(() => ss.get('dc.updHide', ''))
  useEffect(() => {
    // no toast: a failed check at login stays silent
    if (auto && fw) api.updateCheck(true).then(setUpd, () => {})
  }, [auto, fw])
  return useMemo(() => {
    const showLog = (kinds: string[], codes: string[]) => {
      logFocusAtom.set({ kinds, codes })
      go('log')
    }
    const list: NoticeData[] = []
    const notes: NoticeData[] = []
    const sources = st.sources ?? []
    const decks = decksOf(st)
    const chosen = sources.find((s) => s.chosen)
    const lan = st.network?.lan
    const lanDead = !!lan?.cable && !!lan.dead
    if (cfgErr)
      list.push({
        id: 'cfg',
        icon: Settings,
        lvl: 'bad',
        title: t('is.cfg'),
        text: errText(cfgErr, t),
        acts: [{ label: t('ban.retry'), icon: RefreshCw, run: () => void refreshConfig() }],
      })
    if (stErr)
      list.push({
        id: 'status',
        icon: Activity,
        lvl: 'warn',
        title: t('is.status'),
        text: errText(stErr, t),
      })
    if (!decks.length)
      list.push({
        id: 'nodeck',
        icon: Unplug,
        lvl: 'bad',
        title: t('is.nodeck'),
        text: t('is.nodeck_h'),
        acts: [{ label: t('act.reconnect'), icon: Usb, run: () => void a.reconnect(0) }],
      })
    if (st.decks_over_limit && maxDecks)
      list.push({
        id: 'overlimit',
        icon: LayoutGrid,
        lvl: 'warn',
        title: t('is.over', { n: st.decks_over_limit }),
        text: t('is.over_h', { max: maxDecks }),
        acts: [{ label: t('act.deck'), icon: LayoutGrid, run: () => jump('deck', 'max-decks') }],
      })
    if (chosen && !chosen.connected && !chosen.connecting)
      list.push({
        id: 'chosen',
        icon: KIND_ICON[chosen.kind],
        lvl: 'bad',
        title: t('is.chosen_off', { host: hostLabel(chosen, t) }),
        text: [
          chosen.error,
          chosen.since_s !== undefined &&
            bootAt > 0 &&
            t('is.since', { time: fmt.time(bootAt + chosen.since_s * 1000) }),
          t('is.chosen_off_h'),
          lanDead && t('is.lan_too'),
        ]
          .filter(Boolean)
          .join(' · '),
        acts: [
          { label: t('act.retry'), icon: RefreshCw, run: () => void a.retry(chosen) },
          { label: t('act.check'), icon: Search, run: () => jump('sources', 'card-src') },
          { label: t('log.show_in'), ghost: true, run: () => showLog(['source'], LOST) },
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
          { label: t('act.accept'), icon: Check, run: () => a.accept(P.request ?? '', name) },
          { label: t('act.deny'), icon: X, run: () => a.deny(P.request ?? '') },
          { label: t('log.show_in'), ghost: true, run: () => showLog(['pairing'], ['pair_asks']) },
        ],
      })
    }
    // A start the user did not ask for, with no crash report to explain it.
    const reason = st.boot?.reason
    const startedAt = st.boot?.at ?? 0
    const acked = startedAt === bootAck
    if (!crash && !acked && (reason === 'watchdog' || reason === 'brownout' || reason === 'crash'))
      notes.push({
        id: 'boot',
        icon: Bug,
        lvl: 'info',
        title: `${t('dia.boot')}: ${t(`boot.${reason}`)}`,
        acts: [
          { label: t('log.show_in'), ghost: true, run: () => showLog(['start', 'tech'], ['crashed']) },
          {
            label: t('is.hide'),
            ghost: true,
            run: () => {
              bootAckAtom.set(startedAt)
              ls.set('dc.bootAck', startedAt)
            },
          },
        ],
      })
    if (crash && crashed)
      notes.push({
        id: 'crash',
        icon: Bug,
        lvl: 'info',
        title: t('is.crash'),
        text: crash.task ?? t('dia.no_sum'),
        acts: [
          { label: t('is.view_crash'), ghost: true, run: () => jump('diagnose', 'dia-crash') },
          { label: t('log.show_in'), ghost: true, run: () => showLog(['start'], ['crashed']) },
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
    const offer: PopData | undefined =
      auto && upd?.url && upd.latest && upd.current === fw && upd.latest !== updHid
        ? {
            id: 'upd',
            title: t('upd.new', { v: upd.latest }),
            text: t('upd.new_h'),
            acts: a.updateActs({ ...upd, latest: upd.latest, url: upd.url }),
            close: () => {
              setUpdHid(upd.latest ?? '')
              ss.set('dc.updHide', upd.latest)
            },
          }
        : undefined
    const rank = { bad: 0, warn: 1, pair: 2, ok: 3, info: 4 }
    list.sort((x, y) => rank[x.lvl] - rank[y.lvl])
    return { list, notes, offer }
  }, [
    st,
    bootAt,
    satellite,
    maxDecks,
    cfgErr,
    stErr,
    crash,
    crashed,
    bootAck,
    auto,
    fw,
    upd,
    updHid,
    t,
    fmt,
    go,
    jump,
    a,
  ])
}
