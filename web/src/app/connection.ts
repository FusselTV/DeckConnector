import { api, ApiError, onAuthLost, optional } from '../api/client'
import type { WsMsg } from '../api/types'
import { crashAtom } from './state'
import { dispatch, fromLog, getState, hasRoute } from './store'

// One WebSocket for the whole page. The status poll fills what ws `state` leaves out (uptime, counters, signal)
// and is how a dead socket is told apart from an ended session.
const PING_DEAD_MS = 12_000
const OFFLINE_AFTER_MS = 15_000
const POLL_MS = 10_000

let ws: WebSocket | null = null
let wsTimer = 0
let deadTimer = 0
let pollTimer = 0
let lostAt = 0
let backoff = 1000
let started = false
let lastWeb: { kind: string; index: number; at: number } | null = null

/** The page logs its own presses; the echo the firmware sends back for them is dropped. */
export function noteWebPress(kind: string, index: number) {
  lastWeb = { kind, index, at: Date.now() }
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
    const status = await api.status()
    dispatch({ type: 'status', status, now: Date.now() })
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
async function loadInputs() {
  const list = await optional(api.inputLog(inputSeq)).catch(() => null)
  if (!Array.isArray(list) || (!list.length && inputSeq)) return
  dispatch({ type: 'inputs', list: fromLog(list, getState().bootAt || Date.now()), replace: !inputSeq })
  inputSeq = list.at(-1)?.seq ?? inputSeq
}
export function scheduleInputs() {
  if (!getState().inputLogApi) return
  clearTimeout(inputTimer)
  inputTimer = window.setTimeout(() => void loadInputs(), 250)
}

function openWs() {
  clearTimeout(wsTimer)
  if (ws || getState().auth !== 'app') return
  const sock = new WebSocket(`${location.protocol === 'https:' ? 'wss' : 'ws'}://${location.host}/api/ws`)
  ws = sock
  const alive = () => {
    clearTimeout(deadTimer)
    deadTimer = window.setTimeout(() => sock.close(), PING_DEAD_MS)
  }
  sock.onopen = () => {
    alive()
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
    if (
      m.t === 'input' &&
      lastWeb &&
      lastWeb.kind === m.kind &&
      lastWeb.index === m.index &&
      Date.now() - lastWeb.at < 600
    )
      return
    if (m.t === 'log') logListeners.forEach((f) => f(m.text))
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

const logListeners = new Set<(text: string) => void>()
/** Starts the live log on the shared socket. Returns a stop function. */
export function followLog(fn: (text: string) => void) {
  logListeners.add(fn)
  if (ws?.readyState === WebSocket.OPEN) ws.send(JSON.stringify({ t: 'log', id: 3 }))
  else if (ws) ws.addEventListener('open', () => ws?.send(JSON.stringify({ t: 'log', id: 3 })), { once: true })
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
  clearInterval(pollTimer)
  pollTimer = window.setInterval(() => void refreshStatus(), POLL_MS)
}

export function stopSession() {
  started = false
  clearInterval(pollTimer)
  clearTimeout(wsTimer)
  ws?.close()
  ws = null
}

/** First load: a session cookie or basic auth from the browser makes /api/status answer. */
export async function boot() {
  onAuthLost((code) => {
    if (code === 401 && getState().auth === 'app') {
      dispatch({ type: 'auth', auth: 'expired' })
      stopSession()
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
