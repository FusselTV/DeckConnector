import { atom } from '../lib/atom'
import type { Config, InputEvent, InputLogEntry, Screen, Status, WsMsg } from '../api/types'

type Conn = 'live' | 'reconnecting' | 'offline'
type Auth = 'checking' | 'login' | 'app' | 'expired'
interface Ota {
  target: string
  state: string
  pct: number
  error?: string
  code?: string
}

export interface State {
  conn: Conn
  auth: Auth
  /** When the page last heard from the device, set while not live. */
  asOf: number | null
  status: Status
  config: Config | null
  ota: Ota | null
  restarting: { t0: number; reach?: string[] } | null
  inputs: InputEvent[]
  screenRev: number
  /** The picture a ws `screen` carried, so the live deck needs no extra request. */
  pushed: Screen | null
  /** Wall-clock time of the device's start, from the last full status. Times "after the start" count from it. */
  bootAt: number
}

export const INPUT_CAP = 200

export const initial: State = {
  conn: 'live',
  auth: 'checking',
  asOf: null,
  status: {},
  config: null,
  ota: null,
  restarting: null,
  inputs: [],
  screenRev: 0,
  pushed: null,
  bootAt: 0,
}

export type Action =
  | { type: 'status'; status: Status; now: number }
  | { type: 'ws'; msg: WsMsg; now: number }
  | { type: 'conn'; conn: Conn; now: number }
  | { type: 'auth'; auth: Auth }
  | { type: 'config'; config: Config }
  | { type: 'configPatch'; patch: Partial<Config> }
  | { type: 'restart'; on: boolean; now: number; reach?: string[] }
  | { type: 'inputs'; list: InputEvent[]; replace?: boolean }

/** GET /api/input/log entries (oldest first, time after the start) as page events, newest first. */
export function fromLog(list: InputLogEntry[], bootAt: number): InputEvent[] {
  return list
    .map((e) => ({
      at: bootAt + e.at_ms,
      origin: e.origin === 'tool' ? ('api' as const) : e.origin,
      kind: e.kind,
      key: e.index,
      delta: e.kind === 'turn' ? e.value : undefined,
      source: e.writer,
    }))
    .reverse()
}

/** The new value, but with every part that did not change kept by reference, so a selector of an unchanged part
 *  (and React's memo) sees the same object and skips the render. */
export function share<T>(a: T, b: T): T {
  if (Object.is(a, b) || !a || !b || typeof a !== 'object' || typeof b !== 'object') return b
  if (Array.isArray(a) !== Array.isArray(b)) return b
  const x = a as Record<string, unknown>
  const y = b as Record<string, unknown>
  const out: Record<string, unknown> = Array.isArray(b) ? ([] as unknown as Record<string, unknown>) : {}
  let same = Object.keys(x).length === Object.keys(y).length
  for (const k of Object.keys(y)) {
    out[k] = share(x[k], y[k])
    if (out[k] !== x[k]) same = false
  }
  return same ? a : (out as T)
}

export function reduce(s: State, a: Action): State {
  switch (a.type) {
    case 'status':
      return {
        ...s,
        status: share(s.status, a.status),
        bootAt: a.status.uptime_s === undefined ? s.bootAt : a.now - a.status.uptime_s * 1000,
      }
    case 'ws': {
      const m = a.msg
      if (m.t === 'state') {
        // `state` leaves out uptime, signal and counters: keep the values from the last full status.
        const { t: _t, ...rest } = m
        // One level deeper for the network, or the Wi-Fi signal from the last full status would be lost.
        const net = s.status.network
        const network = rest.network && {
          ...rest.network,
          wlan: rest.network.wlan && { rssi: net?.wlan?.rssi, ...rest.network.wlan },
        }
        const status = share(s.status, { ...s.status, ...rest, ...(network && { network }) })
        return status === s.status ? s : { ...s, status }
      }
      if (m.t === 'screen') return { ...s, screenRev: s.screenRev + 1, pushed: m.screen ?? null }
      if (m.t === 'ota')
        return {
          ...s,
          ota:
            m.state === 'idle' ? null : { target: m.target, state: m.state, pct: m.pct, error: m.error, code: m.code },
        }
      return s
    }
    case 'conn':
      if (s.conn === a.conn) return s
      return { ...s, conn: a.conn, asOf: a.conn === 'live' ? null : (s.asOf ?? a.now) }
    case 'auth':
      return s.auth === a.auth ? s : { ...s, auth: a.auth }
    case 'config':
      return { ...s, config: a.config }
    case 'configPatch':
      return s.config ? { ...s, config: { ...s.config, ...a.patch } } : s
    case 'restart':
      return { ...s, restarting: a.on ? { t0: a.now, reach: a.reach } : null }
    case 'inputs':
      return { ...s, inputs: [...a.list, ...(a.replace ? [] : s.inputs)].slice(0, INPUT_CAP) }
  }
}

const store = atom(initial)
export const getState = store.get
export const subscribe = store.subscribe
export const dispatch = (a: Action) => store.set((s) => reduce(s, a))
/** Re-renders only when the selected slice changes (by Object.is), so a WS tick touches only its readers. */
export const useStore = <R>(sel: (s: State) => R): R => store.use(sel)
