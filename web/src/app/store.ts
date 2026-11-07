import { useSyncExternalStore } from 'react'
import type { Config, InputEvent, Route, Status, WsMsg } from '../api/types'

export type Conn = 'live' | 'reconnecting' | 'offline'
export type Auth = 'checking' | 'login' | 'app' | 'expired'
export interface Ota {
  target: string
  state: string
  pct: number
  error?: string
}

export interface State {
  conn: Conn
  auth: Auth
  /** When the page last heard from the device, set while not live. */
  asOf: number | null
  status: Status
  config: Config | null
  ota: Ota | null
  restarting: { t0: number; addr?: string } | null
  inputs: InputEvent[]
  /** True once the firmware answered GET /api/input/log; until then the page keeps its own ring. */
  inputLogApi: boolean
  screenRev: number
  /** GET /api: what this firmware offers. Empty until known, so nothing is hidden by mistake on old firmware. */
  routes: Route[]
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
  inputLogApi: false,
  screenRev: 0,
  routes: [],
}

export type Action =
  | { type: 'status'; status: Status }
  | { type: 'ws'; msg: WsMsg; now: number }
  | { type: 'conn'; conn: Conn; now: number }
  | { type: 'auth'; auth: Auth }
  | { type: 'config'; config: Config }
  | { type: 'configPatch'; patch: Partial<Config> }
  | { type: 'restart'; on: boolean; now: number; addr?: string }
  | { type: 'inputs'; list: InputEvent[] }
  | { type: 'input'; ev: InputEvent }
  | { type: 'routes'; routes: Route[] }

/** Consecutive turns of one dial from one origin within 1.5 s collapse into one entry. */
export function addInput(list: InputEvent[], ev: InputEvent): InputEvent[] {
  const p = list[0]
  if (
    ev.kind === 'turn' &&
    p?.kind === 'turn' &&
    p.key === ev.key &&
    p.origin === ev.origin &&
    p.deck === ev.deck &&
    ev.at - p.at < 1500
  )
    return [{ ...p, at: ev.at, delta: (p.delta ?? 0) + (ev.delta ?? 0) }, ...list.slice(1)]
  return [ev, ...list].slice(0, INPUT_CAP)
}

export function reduce(s: State, a: Action): State {
  switch (a.type) {
    case 'status':
      return { ...s, status: a.status }
    case 'ws': {
      const m = a.msg
      if (m.t === 'state') {
        // `state` leaves out uptime, signal and counters: keep the values from the last full status.
        return { ...s, status: { ...s.status, ...m } }
      }
      if (m.t === 'screen') return { ...s, screenRev: s.screenRev + 1 }
      if (m.t === 'ota')
        return {
          ...s,
          ota: m.state === 'idle' ? null : { target: m.target, state: m.state, pct: m.pct, error: m.error },
        }
      if (m.t === 'input' && !m.simulated) {
        const ev: InputEvent = {
          at: a.now,
          origin: 'deck',
          kind: m.kind,
          key: m.index,
          delta: m.kind === 'turn' ? m.value : undefined,
          source: m.writer,
        }
        return { ...s, inputs: addInput(s.inputs, ev) }
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
      return { ...s, restarting: a.on ? { t0: a.now, addr: a.addr } : null }
    case 'inputs':
      return { ...s, inputs: a.list.slice(0, INPUT_CAP), inputLogApi: true }
    case 'input':
      return { ...s, inputs: addInput(s.inputs, a.ev) }
    case 'routes':
      return { ...s, routes: a.routes }
  }
}

let state = initial
const subs = new Set<() => void>()

export const getState = () => state
export function dispatch(a: Action) {
  const next = reduce(state, a)
  if (next === state) return
  state = next
  subs.forEach((f) => f())
}
const subscribe = (f: () => void) => {
  subs.add(f)
  return () => void subs.delete(f)
}

/** Re-renders only when the selected slice changes (by Object.is), so a WS tick touches only its readers. */
export function useStore<R>(sel: (s: State) => R): R {
  return useSyncExternalStore(subscribe, () => sel(state))
}

/** Whether the firmware offers a route from api-gaps.md. Unknown counts as no: the feature stays hidden. */
export const hasRoute = (routes: Route[], method: string, path: string) =>
  routes.some(
    (r) => r.method === method && (r.path === path || (r.path.endsWith('/*') && path.startsWith(r.path.slice(0, -1)))),
  )
export const useRoute = (method: string, path: string) => useStore((s) => hasRoute(s.routes, method, path))
