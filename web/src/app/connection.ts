import { api, ApiError, onAuthLost, optional, READONLY } from '../api/client'
import type { Status, WsMsg } from '../api/types'
import { crashAtom, loginReasonAtom } from './state'
import { dispatch, fromLog, getState, hasRoute } from './store'

// One WebSocket for the whole page. The status poll fills what ws `state` leaves out (uptime, counters, signal)
// and is how a dead socket is told apart from an ended session.
const PING_DEAD_MS = 12_000
const OFFLINE_AFTER_MS = 15_000
const POLL_MS = 10_000

let ws: WebSocket | null = null
let wsTimer = 0
let pollTimer = 0
let lostAt = 0
let backoff = 1000
let started = false
// What the last ws `state` said, so a poll reply that left before it cannot undo it.
let stateGen = 0
let lastState: Partial<Status> = {}

// Without GET /api/input/log the page logs its own presses and drops the echo the firmware sends back. Each
// echo consumes one entry, so a real deck press on the same control right after is still shown.
let ownPresses: { kind: string; index: number; at: number }[] = []
export function noteWebPress(kind: string, index: number) {
  const now = Date.now()
  ownPresses = [...ownPresses.filter((p) => now - p.at < 1500), { kind, index, at: now }]
}
function isEcho(kind: string, index: number | undefined) {
  const i = ownPresses.findIndex((p) => p.kind === kind && p.index === (index ?? 0) && Date.now() - p.at < 1500)
  if (i < 0) return false
  ownPresses.splice(i, 1)
  return true
}

let upAtRestart = 0
let restartSawDown = false

function lost() {
  const now = Date.now()
  restartSawDown = true
  if (!lostAt) lostAt = now
  dispatch({ type: 'conn', conn: now - lostAt > OFFLINE_AFTER_MS ? 'offline' : 'reconnecting', now })
}

function live() {
  lostAt = 0
  backoff = 1000
  dispatch({ type: 'conn', conn: 'live', now: Date.now() })
}

export async function refreshStatus() {
  try {
    const gen = stateGen
    const status = await api.status()
    dispatch({ type: 'status', status: gen === stateGen ? status : { ...status, ...lastState }, now: Date.now() })
    if (getState().auth !== 'app') dispatch({ type: 'auth', auth: 'app' })
    const up = status.uptime_s ?? 0
    if (!getState().restarting) live()
    else if (restartSawDown || up < upAtRestart || up <= 5) {
      dispatch({ type: 'restart', on: false, now: Date.now() })
      live()
    }
    return true
  } catch (e) {
    if (e instanceof ApiError && e.status === 401) return false
    lost()
    return false
  }
}

export async function refreshConfig() {
  try {
    dispatch({ type: 'config', config: await api.config() })
  } catch {
    /* the cards that need settings show their own error */
  }
}

// With GET /api/input/log the firmware's ring is the one list; each ws `input` asks it for what is new.
let inputSeq = 0
let inputTimer = 0
let inputBusy = false
let inputAgain = false
async function loadInputs() {
  if (inputBusy) return void (inputAgain = true)
  inputBusy = true
  try {
    if (!getState().bootAt) await refreshStatus()
    const since = inputSeq
    const list = await optional(api.inputLog(since)).catch(() => null)
    if (!Array.isArray(list)) return
    const fresh = list.filter((e) => e.seq > since)
    if (!fresh.length && since) return
    dispatch({ type: 'inputs', list: fromLog(fresh, getState().bootAt || Date.now()), replace: !since })
    inputSeq = fresh.at(-1)?.seq ?? inputSeq
  } finally {
    inputBusy = false
    if (inputAgain) {
      inputAgain = false
      void loadInputs()
    }
  }
}
export function scheduleInputs() {
  if (!getState().inputLogApi) return
  clearTimeout(inputTimer)
  inputTimer = window.setTimeout(() => void loadInputs(), 250)
}

function openWs() {
  clearTimeout(wsTimer)
  // A read-only dev target has no socket (the dev proxy refuses it); the status poll keeps the page current.
  if (ws || getState().auth !== 'app' || READONLY) return
  const sock = new WebSocket(`${location.protocol === 'https:' ? 'wss' : 'ws'}://${location.host}/api/ws`)
  ws = sock
  let deadTimer = 0
  const alive = () => {
    clearTimeout(deadTimer)
    deadTimer = window.setTimeout(() => sock.close(), PING_DEAD_MS)
  }
  sock.onopen = () => {
    alive()
    if (logListeners.size) askLog(sock)
    if (!getState().restarting) live()
  }
  sock.onmessage = (e: MessageEvent<string>) => {
    alive()
    let m: WsMsg
    try {
      m = JSON.parse(e.data) as WsMsg
    } catch {
      return
    }
    if (m.t === 'input' && !getState().inputLogApi && isEcho(m.kind, m.index)) return
    if (m.t === 'state') {
      stateGen++
      const { t: _t, ...rest } = m
      lastState = rest
    }
    // The firmware never sends `idle` after an update: a finished one leaves the banner after a few seconds.
    if (m.t === 'ota' && m.state === 'done') {
      clearTimeout(otaTimer)
      otaTimer = window.setTimeout(clearOta, 6000)
    }
    if (m.t === 'log') {
      // The first log message after asking repeats the backlog, so it replaces what is shown.
      logListeners.forEach((f) => f(m.text, logFresh))
      logFresh = false
    }
    if (m.t === 'input') scheduleInputs()
    dispatch({ type: 'ws', msg: m, now: Date.now() })
  }
  sock.onclose = () => {
    clearTimeout(deadTimer)
    if (ws === sock) ws = null
    if (!started) return
    void refreshStatus().then((ok) => {
      if (!ok && getState().auth === 'app') lost()
      wsTimer = window.setTimeout(openWs, ok ? 200 : backoff)
      backoff = Math.min(backoff * 2, 8000)
    })
  }
}

let otaTimer = 0
export const clearOta = () =>
  dispatch({ type: 'ws', msg: { t: 'ota', target: '', state: 'idle', pct: 0 }, now: Date.now() })

const logListeners = new Set<(text: string, replace: boolean) => void>()
let logFresh = false
function askLog(sock: WebSocket) {
  logFresh = true
  sock.send(JSON.stringify({ t: 'log', id: 3 }))
}
/** Starts the live log on the shared socket, and again on every new socket. Returns a stop function. */
export function followLog(fn: (text: string, replace: boolean) => void) {
  logListeners.add(fn)
  if (ws?.readyState === WebSocket.OPEN) askLog(ws)
  return () => void logListeners.delete(fn)
}

/** Ask the device now, for the "Try now" button. */
export function retryNow() {
  backoff = 1000
  lostAt = Date.now()
  dispatch({ type: 'conn', conn: 'reconnecting', now: Date.now() })
  ws?.close()
  if (!ws) openWs()
}

export function startSession() {
  started = true
  inputSeq = 0
  openWs()
  void refreshStatus()
  void refreshConfig()
  api.coredump().then(
    (c) => crashAtom.set(c && typeof c === 'object' && 'pc' in c ? c : null),
    () => undefined,
  )
  api.routes().then(
    (routes) => {
      dispatch({ type: 'routes', routes })
      if (hasRoute(routes, 'GET', '/api/input/log')) void loadInputs()
    },
    () => undefined,
  )
  poll()
  document.addEventListener('visibilitychange', poll)
}

// The status poll pauses in a hidden tab; the socket stays, since it carries pairing requests.
function poll(e?: Event) {
  clearInterval(pollTimer)
  if (document.hidden) return
  if (e) void refreshStatus()
  pollTimer = window.setInterval(() => void refreshStatus(), POLL_MS)
}

export function stopSession() {
  started = false
  document.removeEventListener('visibilitychange', poll)
  clearInterval(pollTimer)
  clearTimeout(wsTimer)
  ws?.close()
  ws = null
}

/** First load: a session cookie or basic auth from the browser makes /api/status answer. */
export async function boot() {
  onAuthLost((code) => {
    if (code === 401 && getState().auth === 'app') {
      stopSession()
      // A restart ends every session: back to the login page, which says why. Otherwise keep the page.
      if (getState().restarting) {
        dispatch({ type: 'restart', on: false, now: Date.now() })
        if (loginReasonAtom.get() !== 'pw') loginReasonAtom.set('expired')
        dispatch({ type: 'auth', auth: 'login' })
      } else dispatch({ type: 'auth', auth: 'expired' })
    } else if (code === 0) lost()
  })
  try {
    dispatch({ type: 'status', status: await api.status(), now: Date.now() })
    dispatch({ type: 'auth', auth: 'app' })
    startSession()
  } catch (e) {
    if (e instanceof ApiError && e.status === 401) dispatch({ type: 'auth', auth: 'login' })
    else {
      dispatch({ type: 'auth', auth: 'login' })
      lost()
    }
  }
}

/** A restart from this page: the overlay stays until the device answers with a fresh uptime. */
export function expectRestart(addr?: string) {
  upAtRestart = getState().status.uptime_s ?? 0
  restartSawDown = false
  dispatch({ type: 'restart', on: true, now: Date.now(), addr })
  ws?.close()
}
