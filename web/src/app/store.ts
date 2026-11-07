import { useSyncExternalStore } from 'react'
import type { Config, InputEvent, InputLogEntry, Route, Screen, Status, WsMsg } from '../api/types'

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
  restarting: { t0: number; reach?: string[] } | null
  inputs: InputEvent[]
  /** True once the firmware answered GET /api/input/log; until then the page keeps its own ring. */
  inputLogApi: boolean
  screenRev: number
  /** The picture a ws `screen` carried, so the live deck needs no extra request. */
  pushed: Screen | null
  /** GET /api: what this firmware offers. Empty until known, so nothing is hidden by mistake on old firmware. */
  routes: Route[]
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
  inputLogApi: false,
  screenRev: 0,
  pushed: null,
  routes: [],
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
  | { type: 'input'; ev: InputEvent }
  | { type: 'routes'; routes: Route[] }

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
        const status = share(s.status, { ...s.status, ...rest })
        return status === s.status ? s : { ...s, status }
      }
      if (m.t === 'screen') return { ...s, screenRev: s.screenRev + 1, pushed: m.screen ?? null }
      if (m.t === 'ota')
        return {
          ...s,
          ota: m.state === 'idle' ? null : { target: m.target, state: m.state, pct: m.pct, error: m.error },
        }
      if (m.t === 'input' && !m.simulated && !s.inputLogApi) {
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
      return { ...s, restarting: a.on ? { t0: a.now, reach: a.reach } : null }
    case 'inputs':
      return { ...s, inputs: [...a.list, ...(a.replace ? [] : s.inputs)].slice(0, INPUT_CAP), inputLogApi: true }
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
