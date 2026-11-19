import { api, ApiError, onAuthLost, READONLY } from '../api/client'
import type { DeviceEvent, Status, WsMsg } from '../api/types'
import { ss } from '../lib/util'
import { configErrAtom, crashAtom, loginReasonAtom, statusErrAtom } from './state'
import { dispatch, fromLog, getState } from './store'

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
let heard = false
// What the last ws `state` said, so a poll reply that left before it cannot undo it.
let stateGen = 0
let lastState: Partial<Status> = {}

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
    heard = true
    if (getState().auth !== 'app') dispatch({ type: 'auth', auth: 'app' })
    const up = status.uptime_s ?? 0
    const r = getState().restarting
    // A restart asked in the first seconds after a start would otherwise end before the device goes down.
    if (!r) live()
    else if (restartSawDown || up < upAtRestart || (up <= 5 && upAtRestart > 5)) {
      dispatch({ type: 'restart', on: false, now: Date.now() })
      live()
      void refreshConfig() // what was saved before the restart
    }
    statusErrAtom.set(null)
    return true
  } catch (e) {
    if (e instanceof ApiError && e.status === 401) return false
    if (e instanceof ApiError && e.status === 0) lost()
    else statusErrAtom.set(e)
    return false
  }
}

let cfgTimer = 0
let cfgBackoff = 2000
export async function refreshConfig() {
  clearTimeout(cfgTimer)
  try {
    const before = getState().config
    const config = await api.config()
    // A save that landed while this was on its way is newer than this answer.
    if (getState().config !== before) return
    dispatch({ type: 'config', config })
    configErrAtom.set(null)
    cfgBackoff = 2000
  } catch (e) {
    configErrAtom.set(e)
    if (started) cfgTimer = window.setTimeout(() => void refreshConfig(), cfgBackoff)
    cfgBackoff = Math.min(cfgBackoff * 2, 30_000)
  }
}

// The firmware's input ring is the one list; each ws `input` asks it for what is new. A failed fetch waits for the next.
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
    const list = await api.inputLog(since).catch(() => null)
    if (!list) return
    const fresh = list.filter((e) => e.seq > since)
    if (!fresh.length && since) return
    const bootAt = getState().bootAt || Date.now()
    dispatch({ type: 'inputs', list: fromLog(fresh, bootAt), replace: !since })
    inputSeq = fresh.at(-1)?.seq ?? inputSeq
    // the first answer is the newest page: the older ones of the firmware's ring follow
    for (let first = list[0]?.seq ?? 0; !since && first > 1;) {
      const older = await api.inputLog(0, first).catch(() => [])
      const head = older[0]
      if (!head) break
      dispatch({ type: 'inputs', list: fromLog(older, bootAt), older: true })
      first = head.seq
    }
  } finally {
    inputBusy = false
    if (inputAgain) {
      inputAgain = false
      void loadInputs()
    }
  }
}
function scheduleInputs() {
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
    if (m.t === 'state') {
      heard = true
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
    if (m.t === 'event') eventListeners.forEach((f) => f(m.event))
    // The deck menu can change settings, brightness for one: read them once it closes.
    if (m.t === 'state' && m.menu_open === false && getState().status.menu_open) void refreshConfig()
    dispatch({ type: 'ws', msg: m, now: Date.now() })
  }
  sock.onclose = () => {
    clearTimeout(deadTimer)
    if (ws === sock) ws = null
    if (!started) return
    void refreshStatus().then((ok) => {
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

const eventListeners = new Set<(e: DeviceEvent) => void>()
/** Each new device event as it happens. Returns a stop function. */
export function onDeviceEvent(fn: (e: DeviceEvent) => void) {
  eventListeners.add(fn)
  return () => void eventListeners.delete(fn)
}

/** Ask the device now, for the "Try now" button. */
export function retryNow() {
  backoff = 1000
  lostAt = Date.now()
  dispatch({ type: 'conn', conn: 'reconnecting', now: Date.now() })
  ws?.close()
  if (!ws) openWs()
}

/** `fresh`: the caller has just read the status, so it is not asked for again. */
export function startSession(fresh = false) {
  started = true
  heard = fresh
  inputSeq = 0
  openWs()
  void (fresh ? Promise.resolve(true) : refreshStatus()).then((ok) => {
    if (!ok) return
    api.coredump().then(
      (c) => crashAtom.set(c.size > 0 ? c : null),
      () => crashAtom.set(null),
    )
  })
  void refreshConfig()
  void loadInputs()
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
  clearTimeout(cfgTimer)
  ws?.close()
  ws = null
}

async function firstStatus() {
  try {
    return await api.status()
  } catch (e) {
    // A device nobody has set up lets the first browser straight into the wizard.
    if (!(e instanceof ApiError && e.status === 401)) throw e
    const refused = await api.claimSetup().then(
      () => null,
      (x: unknown) => x,
    )
    const reset = ss.get<boolean>('dc.reset', false)
    ss.set('dc.reset', undefined)
    if (refused instanceof ApiError && refused.message === 'claimed') loginReasonAtom.set('claimed')
    else if (refused && reset) loginReasonAtom.set('reset')
    if (refused) throw e
    return api.status()
  }
}

/** First load: a session cookie or basic auth from the browser makes /api/status answer. */
export async function boot() {
  onAuthLost((code) => {
    if (code === 401 && getState().auth === 'app') {
      stopSession()
      // A restart ends every session: back to the login page, which says why. Otherwise keep the page.
      if (getState().restarting) {
        // After a factory reset the first browser gets the wizard without a password: be that browser.
        if (loginReasonAtom.get() === 'reset') {
          ss.set('dc.reset', true)
          return location.reload()
        }
        dispatch({ type: 'restart', on: false, now: Date.now() })
        if (loginReasonAtom.get() !== 'pw') loginReasonAtom.set('expired')
        dispatch({ type: 'auth', auth: 'login' })
      } else if (!heard) {
        // Never a status: the login answered but the browser did not keep its cookie. No stale page to show.
        loginReasonAtom.set('cookie')
        dispatch({ type: 'auth', auth: 'login' })
      } else dispatch({ type: 'auth', auth: 'expired' })
    } else if (code === 0) lost()
  })
  try {
    dispatch({ type: 'status', status: await firstStatus(), now: Date.now() })
    dispatch({ type: 'auth', auth: 'app' })
    startSession(true)
  } catch (e) {
    if (e instanceof ApiError && e.status === 401) dispatch({ type: 'auth', auth: 'login' })
    else {
      dispatch({ type: 'auth', auth: 'login' })
      lost()
    }
  }
}

/** A restart from this page: the overlay stays until the device answers with a fresh uptime. */
export function expectRestart(reach?: string[]) {
  upAtRestart = getState().status.uptime_s ?? 0
  restartSawDown = false
  dispatch({ type: 'restart', on: true, now: Date.now(), reach })
  ws?.close()
}
