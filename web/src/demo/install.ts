import type { WsMsg } from '../api/types'
import { paint } from './deck'
import { boot, face, fresh, input, isDown, putConfig, tick, wsState, type Demo } from './model'
import { handle, type Body } from './server'

// Demo mode: every /api request and the /api/ws socket are answered here, in the browser. Nothing goes out.

const KEY = 'dc.demo'
const params = new URLSearchParams(location.search)
const mode = params.has('setup') ? 'setup' : params.has('login') ? 'login' : ''

for (const [k, ok] of [
  ['lang', ['de', 'en']],
  ['theme', ['light', 'dark']],
] as const) {
  // Without ?theme the demo takes the website's theme, which next-themes keeps under "theme" on this origin.
  const v = params.get(k) ?? (k === 'theme' ? localStorage.getItem('theme') : null)
  if (v && (ok as readonly string[]).includes(v)) localStorage.setItem(`dc.${k}`, JSON.stringify(v))
}
// A visitor came to press keys: the live deck starts open, also on a phone.
if (localStorage.getItem('dc.deckOpen') === null) localStorage.setItem('dc.deckOpen', 'true')
if (window.parent !== window) document.documentElement.classList.add('embed')

function load(): Demo {
  try {
    const raw = sessionStorage.getItem(KEY)
    const s = raw ? (JSON.parse(raw) as { v?: number; mode?: string }) : null
    if (s?.v === 3 && s.mode === mode) return s as Demo
  } catch {
    /* a fresh demo */
  }
  const s = Object.assign(fresh(Date.now(), { setup: mode === 'setup', login: mode === 'login' }), { mode })
  boot(s, s.bootAt, s.bootReason)
  return s
}

const s = load()
const save = () => {
  try {
    sessionStorage.setItem(KEY, JSON.stringify(s))
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

function bodyOf(init: RequestInit | undefined): Body {
  const b = init?.body
  if (b == null) return undefined
  if (typeof b === 'string') {
    try {
      return JSON.parse(b) as Record<string, unknown>
    } catch {
      return {}
    }
  }
  if (b instanceof Blob) return { blob: b.size }
  return {}
}

const realFetch = window.fetch.bind(window)
window.fetch = async (req: RequestInfo | URL, init?: RequestInit) => {
  const url = typeof req === 'string' ? req : req instanceof URL ? req.href : req.url
  const path = new URL(url, location.href).pathname
  if (!url.startsWith('/api') && !/\/api(\/|$)/.test(path)) return realFetch(req, init)
  const apiUrl = url.startsWith('/api') ? url : url.slice(url.indexOf('/api'))
  const body = bodyOf(init)
  await new Promise((r) => setTimeout(r, SLOW[path.replace(/.*(?=\/api)/, '')] ?? 40 + Math.random() * 80))
  init?.signal?.throwIfAborted()
  if (down()) throw new TypeError('Failed to fetch')
  const r = handle(s, Date.now(), init?.method ?? 'GET', apiUrl, body, paint)
  save()
  after()
  const text = typeof r.body === 'string'
  return new Response(text ? (r.body as string) : JSON.stringify(r.body), {
    status: r.status,
    headers: { 'content-type': text ? 'text/plain' : 'application/json' },
  })
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

function badge() {
  const el = document.createElement('div')
  el.className = 'demo-badge'
  el.innerHTML = '<b>Demo</b><span></span><button type="button"></button>'
  const btn = el.querySelector('button') as HTMLButtonElement
  const hint = el.querySelector('span') as HTMLSpanElement
  const label = () => {
    const de = document.documentElement.lang === 'de'
    hint.textContent = document.getElementById('lpw') ? (de ? 'jedes Passwort geht' : 'any password works') : ''
    btn.textContent = de ? 'Zurücksetzen' : 'Reset'
    btn.title = de ? 'Demo von vorn beginnen' : 'Start the demo over'
    el.title = de
      ? 'Demo: alles läuft im Browser, nichts geht an ein Gerät'
      : 'Demo: everything runs in the browser, nothing reaches a device'
  }
  btn.onclick = () => {
    sessionStorage.removeItem(KEY)
    location.reload()
  }
  new MutationObserver(label).observe(document.documentElement, { attributes: true, attributeFilter: ['lang'] })
  relabel = label
  label()
  const css = document.createElement('style')
  css.textContent = `.demo-badge{position:fixed;z-index:60;right:12px;top:calc(5px + env(safe-area-inset-top));display:flex;align-items:center;gap:6px;padding:2px 2px 2px 9px;border-radius:999px;background:var(--panel,#171a1d);border:1px solid var(--line,#2a2f35);color:var(--fg,#e8e6e1);font:600 11px/1 var(--mono,ui-monospace,monospace);box-shadow:0 4px 16px #0004;opacity:.92}
.demo-badge b{color:var(--accent,#ffb020);letter-spacing:.08em;text-transform:uppercase}
.demo-badge span:empty{display:none}
.demo-badge button{font:inherit;font-weight:500;color:inherit;background:transparent;border:1px solid var(--line,#2a2f35);border-radius:999px;padding:4px 8px;cursor:pointer}
.demo-badge button:hover{border-color:var(--accent,#ffb020)}
@media (max-width:599px){.demo-badge{top:calc(16px + env(safe-area-inset-top))}}`
  document.head.append(css)
  document.body.append(el)
}
badge()
save()
