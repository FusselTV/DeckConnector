import type { WsMsg } from '../api/types'
import { fontsReady, paint } from './deck'
import { deckUiReady, TONE_RATE } from './deck-ui'
import { boot, face, fresh, history, input, isDown, PASS, putConfig, tick, wsState, type Demo } from './model'
import { ls, store } from '../lib/util'
import { handle, type Body } from './server'

await deckUiReady

// Demo mode: every /api request and the /api/ws socket are answered here, in the browser. Nothing goes out.

const KEY = 'dc.demo'
const params = new URLSearchParams(location.search)
const mode = params.has('setup') ? 'setup' : params.has('login') ? 'login' : ''

// The demo's starting values live in memory: storage is written only once the visitor changes a setting.
const start: Record<string, unknown> = {}
const siteTheme = (() => {
  try {
    return localStorage.getItem('theme')
  } catch {
    return null
  }
})()
for (const [k, ok] of [
  ['lang', ['de', 'en']],
  ['theme', ['light', 'dark']],
] as const) {
  // Without ?theme the demo takes the website's theme, which next-themes keeps under "theme" on this origin.
  const v = params.get(k) ?? (k === 'theme' ? siteTheme : null)
  if (!v || !(ok as readonly string[]).includes(v)) continue
  start[`dc.${k}`] = v
  // React sets it only after its first render, and until then a light system theme flashed through the site's dark one.
  if (k === 'theme') document.documentElement.dataset.theme = v
  if (k === 'lang') document.documentElement.lang = v
}
const real = store(() => localStorage)
// A visitor came to press keys: the live deck starts open, also on a phone.
ls.get = <V>(k: string, d: V): V => (k in start ? (start[k] as V) : real.get(k, k === 'dc.deckOpen' ? (true as V) : d))
ls.set = (k, v) => real.set(k, v)
if (window.parent !== window) document.documentElement.classList.add('embed')

function load(): Demo {
  try {
    const raw = sessionStorage.getItem(KEY)
    const s = raw ? (JSON.parse(raw) as { v?: number; mode?: string }) : null
    if (s?.v === 11 && s.mode === mode) return { ...(s as Demo), pass: PASS }
  } catch {
    /* a fresh demo */
  }
  const s = Object.assign(fresh(Date.now(), { setup: mode.startsWith('setup'), login: mode.startsWith('login') }), {
    mode,
  })
  boot(s, s.bootAt, s.bootReason)
  if (!mode.startsWith('setup')) history(s, Date.now())
  return s
}

const s = load()
let resetting = false
const save = () => {
  // a request still in flight while Reset reloads would write the old state back
  if (resetting) return
  try {
    // A password the visitor typed is never stored: after a reload the demo's own one is back.
    sessionStorage.setItem(KEY, JSON.stringify({ ...s, pass: undefined, join: s.join && { ...s.join, pass: '' } }))
  } catch {
    /* private mode: the demo still runs, a reload starts over */
  }
}

const down = () => isDown(s, Date.now())
const SLOW: Record<string, number> = {
  '/api/wifi/scan': 2200,
  '/api/sources/probe': 900,
  '/api/update/check': 1200,
  '/api/ota/inspect': 400,
}

async function bodyOf(b: BodyInit | null | undefined): Promise<Body> {
  if (b == null) return undefined
  if (typeof b === 'string') {
    try {
      return JSON.parse(b) as Record<string, unknown>
    } catch {
      return {}
    }
  }
  if (b instanceof Blob) return { blob: b.size, head: [...new Uint8Array(await b.slice(0, 144).arrayBuffer())] }
  return {}
}

// Every answer the mock gave, for the crawler in e2e-demo: a 404 there is an endpoint the mock lacks.
const calls: { method: string; url: string; status: number }[] = []
const busy = { n: 0 }
Object.assign(window, { __demoCalls: calls, __demoBusy: busy })

const realFetch = window.fetch.bind(window)
window.fetch = async (req: RequestInfo | URL, init?: RequestInit) => {
  const url = typeof req === 'string' ? req : req instanceof URL ? req.href : req.url
  const path = new URL(url, location.href).pathname
  if (!url.startsWith('/api') && !/\/api(\/|$)/.test(path)) return realFetch(req, init)
  const apiUrl = url.startsWith('/api') ? url : url.slice(url.indexOf('/api'))
  const body = await bodyOf(init?.body)
  busy.n++
  await new Promise((r) => setTimeout(r, SLOW[path.replace(/.*(?=\/api)/, '')] ?? 40 + Math.random() * 80))
  busy.n--
  init?.signal?.throwIfAborted()
  if (down()) throw new TypeError('Failed to fetch')
  const r = handle(s, Date.now(), init?.method ?? 'GET', apiUrl, body, paint)
  calls.push({ method: init?.method ?? 'GET', url: apiUrl, status: r.status })
  save()
  after()
  try {
    if (r.pcm?.length) play(r.pcm)
  } catch {
    // a browser without Web Audio stays silent, like a board without a speaker
  }
  const text = typeof r.body === 'string'
  return new Response(text ? (r.body as string) : JSON.stringify(r.body), {
    status: r.status,
    headers: { 'content-type': text ? 'text/plain' : 'application/json' },
  })
}

let audio: AudioContext | undefined
let audioEnd = 0
function play(pcm: Int16Array) {
  audio ??= new AudioContext()
  if (audio.state === 'suspended') void audio.resume()
  const buf = audio.createBuffer(1, pcm.length, TONE_RATE)
  buf.getChannelData(0).set(Float32Array.from(pcm, (v) => v / 32768))
  const src = audio.createBufferSource()
  src.buffer = buf
  src.connect(audio.destination)
  // one after the other, each with the 100 ms of silence db_audio adds (TAIL)
  audioEnd = Math.max(audio.currentTime, audioEnd)
  src.start(audioEnd)
  audioEnd += buf.duration + 0.1
}

class DemoSocket {
  static readonly CONNECTING = 0
  static readonly OPEN = 1
  static readonly CLOSING = 2
  static readonly CLOSED = 3
  readonly url: string
  readyState = 0
  onopen: ((e: Event) => void) | null = null
  onmessage: ((e: MessageEvent) => void) | null = null
  onclose: ((e: CloseEvent) => void) | null = null
  onerror: ((e: Event) => void) | null = null
  log = false
  constructor(url: string) {
    this.url = url
    setTimeout(() => {
      if (this.readyState !== 0) return
      if (down() || !s.authed) return this.close()
      this.readyState = 1
      sockets.add(this)
      this.onopen?.(new Event('open'))
      this.push({ t: 'state', ...wsState(s, Date.now()) })
    }, 60)
  }
  push(m: WsMsg) {
    if (this.readyState === 1) this.onmessage?.(new MessageEvent('message', { data: JSON.stringify(m) }))
  }
  send(data: string) {
    let m: { t?: string; id?: number } & Record<string, unknown>
    try {
      m = JSON.parse(data) as typeof m
    } catch {
      return
    }
    const now = Date.now()
    if (m.t === 'log') {
      this.log = true
      this.push({ t: 'log', text: s.log.join('\n') + '\n' })
      return
    }
    const { t: _t, id, ...rest } = m
    const r = m.t === 'set' ? putConfig(s, now, rest) : m.t === 'input' ? input(s, now, rest as never) : null
    calls.push({ method: 'WS', url: String(m.t), status: r?.status ?? 404 })
    if (r && id !== undefined) this.push({ t: 'result', id, status: r.status, reply: r.body })
    save()
    after()
  }
  close() {
    if (this.readyState === 3) return
    this.readyState = 3
    sockets.delete(this)
    setTimeout(() => this.onclose?.(new CloseEvent('close')), 0)
  }
}

const sockets = new Set<DemoSocket>()
const RealWs = window.WebSocket
window.WebSocket = new Proxy(RealWs, {
  construct(target, args: [string | URL, (string | string[])?]) {
    const u = String(args[0])
    return /\/api\/ws$/.test(new URL(u, location.href).pathname)
      ? new DemoSocket(u)
      : Reflect.construct<typeof args, WebSocket>(target, args)
  },
})

let lastState = ''
let lastFace = ''
let lastSeq = s.seq
let lastEvent = s.events.at(-1)?.seq ?? 0
const broadcast = (m: WsMsg, logOnly = false) => sockets.forEach((x) => (!logOnly || x.log) && x.push(m))

/** Tells every socket what changed: the state, the deck picture. */
function after() {
  const now = Date.now()
  if (down()) {
    sockets.forEach((x) => x.close())
    return
  }
  if (!s.authed) sockets.forEach((x) => x.close())
  const st = JSON.stringify(wsState(s, now))
  if (st !== lastState) {
    lastState = st
    broadcast({ t: 'state', ...(JSON.parse(st) as object) })
  }
  // Each press, turn or tap: the page reloads the input log, as it does on the firmware's `input` message.
  if (s.seq !== lastSeq) {
    lastSeq = s.seq
    broadcast({ t: 'input', kind: 'key' })
  }
  for (const e of s.events) if (e.seq > lastEvent) broadcast({ t: 'event', event: e })
  lastEvent = s.events.at(-1)?.seq ?? lastEvent
  const f = JSON.stringify(face(s, now))
  if (f !== lastFace) {
    if (lastFace) s.images += 9
    lastFace = f
    broadcast({ t: 'screen' })
  }
}

let n = 0
let relabel: () => void = () => undefined
const loadedFirmware = s.firmware
setInterval(() => {
  relabel()
  const bootAt = s.bootAt
  const out = tick(s, Date.now())
  // New firmware brings its own page on a real device, so the page starts over once the bridge is back.
  if (s.bootAt !== bootAt && s.firmware !== loadedFirmware) {
    save()
    location.reload()
  }
  if (out.ota) broadcast({ t: 'ota', ...out.ota })
  for (const text of out.log ?? []) broadcast({ t: 'log', text: text + '\n' }, true)
  if (++n % 5 === 0) broadcast({ t: 'ping' })
  after()
  save()
}, 1000)

// Links to the certificate and the crash dump would leave the page: they download from the mock instead.
document.addEventListener(
  'click',
  (e) => {
    const a = (e.target as Element | null)?.closest('a[href^="/api/"]')
    if (!(a instanceof HTMLAnchorElement)) return
    e.preventDefault()
    const r = handle(s, Date.now(), 'GET', a.getAttribute('href') ?? '', undefined, paint)
    const blob = new Blob([typeof r.body === 'string' ? r.body : JSON.stringify(r.body)], { type: 'text/plain' })
    const link = document.createElement('a')
    link.href = URL.createObjectURL(blob)
    link.download = a.download || 'download.txt'
    link.click()
    setTimeout(() => URL.revokeObjectURL(link.href), 1000)
  },
  true,
)

// Demo helpers inside the real page, added next to its own fields:
// - the login form says the demo's password (it changes with a password change or a factory reset), with a
//   button that fills it in;
// - the firmware dialog offers sample files, so the whole update flow can be tried without a real .dbfw.
// React keeps an input's value, so it is set the way typing sets it.
const setValue = (el: HTMLInputElement, v: string) =>
  Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value')?.set?.call(el, v)

/** A .dbfw file with a valid header (tools/dbtools/dbfw.py) and an empty body, for `target`. */
function sample(target: number, version: string, name: string) {
  const body = 4096
  const h = new Uint8Array(144 + body)
  const enc = new TextEncoder()
  h.set(enc.encode('DBFW'))
  h[4] = 1
  h[5] = target
  new DataView(h.buffer).setUint32(8, body, true)
  h.set(enc.encode(version), 12)
  return new File([h], name)
}

function helpers() {
  const de = document.documentElement.lang === 'de'
  const pw = document.getElementById('lpw')
  if (pw instanceof HTMLInputElement) {
    let p = document.getElementById('demo-pw')
    if (!p) {
      p = document.createElement('p')
      p.id = 'demo-pw'
      p.className = 'demo-hint'
      p.innerHTML = '<span></span> <code></code> <button type="button" class="btn sm"></button>'
      ;(p.querySelector('button') as HTMLButtonElement).onclick = () => {
        setValue(pw, s.pass)
        pw.dispatchEvent(new Event('input', { bubbles: true }))
        pw.focus()
      }
      pw.closest('.field, .inrow')?.after(p)
    }
    // Only real changes: each write is a mutation that calls this again.
    const put = (sel: string, v: string) => {
      const el = p.querySelector(sel) as HTMLElement
      if (el.textContent !== v) el.textContent = v
    }
    put('span', de ? 'Demo-Passwort:' : 'Demo password:')
    put('code', s.pass)
    put('button', de ? 'Einsetzen' : 'Fill in')
  }
  const up = document.getElementById('upd-file')
  if (up instanceof HTMLInputElement && !document.getElementById('demo-files')) {
    const p = document.createElement('p')
    p.id = 'demo-files'
    p.className = 'demo-hint'
    const files = [
      sample(1, 'v1.0.1', 'deckconnector-v1.0.1.dbfw'),
      sample(3, 'web demo 3f9a1c', 'deckconnector-web.dbfw'),
    ]
    p.append(de ? 'Demo, Beispieldatei: ' : 'Demo, sample file: ')
    for (const f of files) {
      const b = document.createElement('button')
      b.type = 'button'
      b.className = 'btn sm'
      b.textContent = f.name
      b.onclick = () => {
        const dt = new DataTransfer()
        dt.items.add(f)
        up.files = dt.files
        up.dispatchEvent(new Event('change', { bubbles: true }))
      }
      p.append(b, ' ')
    }
    up.closest('.inrow')?.after(p)
  }
}
// Demo passwords are not worth saving: password managers skip these fields. Browsers that can draw dots in a
// text field (Chromium, Safari) get one, so their own password manager does not ask either; the eye button next
// to it still decides whether the text shows.
const dots = CSS.supports('-webkit-text-security', 'disc')
function noSave() {
  for (const el of document.querySelectorAll<HTMLInputElement>('input[type=password], input[data-demo-pw]')) {
    if (!el.dataset.demoPw) {
      el.dataset.demoPw = '1'
      const skip = { autocomplete: 'off', 'data-1p-ignore': '', 'data-lpignore': 'true', 'data-bwignore': '' }
      for (const [k, v] of Object.entries({ ...skip, 'data-form-type': 'other' })) el.setAttribute(k, v)
    }
    if (!dots) continue
    const shown = el.parentElement?.querySelector('[aria-pressed]')?.getAttribute('aria-pressed') === 'true'
    if (el.type !== 'text') el.type = 'text'
    const want = shown ? 'none' : 'disc'
    if (el.style.getPropertyValue('-webkit-text-security') !== want) el.style.setProperty('-webkit-text-security', want)
  }
}
new MutationObserver(() => (helpers(), noSave())).observe(document.documentElement, {
  childList: true,
  subtree: true,
  attributes: true,
  attributeFilter: ['type', 'aria-pressed'],
})

function badge() {
  const el = document.createElement('div')
  el.className = 'demo-badge'
  el.innerHTML = '<b>Demo</b><button type="button"></button>'
  const btn = el.querySelector('button') as HTMLButtonElement
  const label = () => {
    const de = document.documentElement.lang === 'de'
    btn.textContent = de ? 'Zurücksetzen' : 'Reset'
    btn.title = de ? 'Demo von vorn beginnen' : 'Start the demo over'
    el.title = de
      ? 'Demo: alles läuft im Browser, nichts geht an ein Gerät'
      : 'Demo: everything runs in the browser, nothing reaches a device'
  }
  btn.onclick = () => {
    resetting = true
    sessionStorage.removeItem(KEY)
    location.reload()
  }
  new MutationObserver(label).observe(document.documentElement, { attributes: true, attributeFilter: ['lang'] })
  relabel = label
  label()
  const css = document.createElement('style')
  css.textContent = `.demo-badge{position:fixed;z-index:59;right:12px;top:calc(5px + env(safe-area-inset-top));display:flex;align-items:center;gap:6px;padding:2px 2px 2px 9px;border-radius:999px;background:var(--panel,#171a1d);border:1px solid var(--line,#2a2f35);color:var(--fg,#e8e6e1);font:600 11px/1 var(--mono,ui-monospace,monospace);box-shadow:0 4px 16px #0004;opacity:.92}
.demo-badge b{color:var(--accent,#ffb020);letter-spacing:.08em;text-transform:uppercase}
.demo-hint{margin:6px 0 0;display:flex;flex-wrap:wrap;align-items:center;gap:6px;font-size:13px;color:var(--fg-2)}
.demo-hint code{font:600 13px var(--f-mono);color:var(--fg)}
.demo-hint .btn{width:auto;min-height:0;padding:3px 10px;font-size:12px}
.demo-badge button{position:relative;font:inherit;font-weight:500;color:inherit;background:transparent;border:1px solid var(--line,#2a2f35);border-radius:999px;padding:4px 8px;cursor:pointer}
.demo-badge button::after{content:"";position:absolute;inset:-12px -6px}
.demo-badge button:hover{border-color:var(--accent,#ffb020)}
@media (max-width:599px){.demo-badge{top:calc(16px + env(safe-area-inset-top))}}`
  document.head.append(css)
  document.body.append(el)
}
badge()
save()
void fontsReady.then(() => {
  lastFace = ''
  after()
})
