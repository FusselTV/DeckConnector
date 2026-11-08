import type {
  Config,
  ConfigPatch,
  DeckInfo,
  DeviceEvent,
  InputBody,
  InputLogEntry,
  Rect,
  Slot,
  Source,
  Status,
  Token,
  WifiAp,
} from '../api/types'

// The bridge as the demo page sees it: one plain object, saved to sessionStorage after every change. Every timed
// thing (a restart, an update, a Wi-Fi test, a pairing request) is a timestamp, so `tick` and the replies only need
// the current time.

export interface Dock {
  ip: string
  mac: string
  name: string
  kind: 'companion' | 'app'
  connected: boolean
}

/** What the hosts behind the deck show and remember. */
export interface Faces {
  page: number
  mic: boolean
  live: boolean
  rec: boolean
  scene: number
  vol: number[]
  mute: boolean[]
  lights: boolean[]
  light: number[]
  brb: boolean
  play: boolean
  track: number
  appVol: number[]
  presses: number[]
  dial: number[]
}

export interface Demo {
  v: 3
  bootAt: number
  /** A restart: answers stop at downFrom (1 s after it was asked, like the firmware) until downUntil. */
  downFrom: number
  downUntil: number
  started: number
  authed: boolean
  pass: string
  cfg: Config
  firmware: string
  slots: Slot[]
  pagePartition: string | null
  tls: boolean
  docks: Dock[]
  pairOpenUntil: number
  request: { ip: string; name: string; at: number } | null
  requested: boolean
  denied: { ip: string; name?: string; since_s: number }[]
  /** When a host address was set, so it shows "connecting" for a moment. */
  since: Record<string, number>
  tokens: Token[]
  events: DeviceEvent[]
  inputs: InputLogEntry[]
  seq: number
  faces: Faces
  menu: boolean
  pwUntil: number
  resetUntil: number
  loginReq: { id: string; at: number; state: 'pending' | 'accepted' | 'denied' | 'expired' | 'used' } | null
  join: { ssid: string; bssid: string; pass: string; until: number } | null
  joinState: '' | 'testing' | 'ok' | 'failed'
  joinError: string
  ota: { at: number; version: string; slot: number } | null
  setupStep: number
  bootReason: string
  images: number
  log: string[]
}

export const WIDTH = 800
export const HEIGHT = 480
const KX = [13, 232, 451, 670]
const KY = [12, 172]
export const KEYS: Rect[] = KY.flatMap((y) => KX.map((x) => ({ x, y, w: 120, h: 120 })))
export const STRIP: Rect = { x: 0, y: 380, w: 800, h: 100 }

export const LAN_IP = '192.168.1.214'
const WLAN_IP = '192.168.1.215'
const LATEST = 'v1.3.0'
const COMPANION = { name: 'Studio Companion', version: '5.2.0' }

export const DECK: DeckInfo = {
  model: 'Stream Deck +',
  serial: 'EL00K1A00001',
  firmware: '1.05.008',
  keys: 8,
  encoders: 4,
  width: 800,
  height: 480,
  usb_speed: 'high',
  hub: false,
}

export const WIFI: WifiAp[] = [
  { ssid: 'Studio', bssid: '02:00:00:00:01:01', rssi: -47, channel: 6, open: false },
  { ssid: 'Studio', bssid: '02:00:00:00:01:02', rssi: -66, channel: 1, open: false },
  { ssid: 'Studio Guest', bssid: '02:00:00:00:01:03', rssi: -49, channel: 6, open: true },
  { ssid: 'Office', bssid: '02:00:00:00:02:01', rssi: -63, channel: 11, open: false },
  { ssid: 'Control Room', bssid: '02:00:00:00:03:01', rssi: -71, channel: 1, open: false },
  { ssid: 'DIRECT-27-Printer', bssid: '02:00:00:00:04:01', rssi: -77, channel: 6, open: false },
  { ssid: 'Neighbour', bssid: '02:00:00:00:05:01', rssi: -86, channel: 13, open: false },
]

const ip = (empty = true) => ({ dhcp: empty, ip: '', mask: '', gateway: '', dns: '' })

export function fresh(now: number, o: { setup?: boolean; login?: boolean } = {}): Demo {
  const setup = !!o.setup
  return {
    v: 3,
    bootAt: now - (setup ? 40_000 : 3 * 3600_000 + 17 * 60_000),
    downFrom: 0,
    downUntil: 0,
    started: now,
    authed: !o.login,
    pass: 'studio-demo',
    cfg: {
      name: setup ? 'deckconnector-a1b2c3' : 'studio-deck',
      controller: 'companion',
      host: setup ? '' : 'satellite',
      brightness: 80,
      lan: ip(),
      wlan: ip(),
      wifi_ssid: setup ? '' : 'Studio',
      wifi_pass_set: !setup,
      wifi_bssid: '',
      servers: setup ? ['', '', '', ''] : ['ws://192.168.1.40:8765/deckconnector', 'wss://deck.example.org/ws', '', ''],
      server_pins: ['', '', '', ''],
      satellite: setup ? '' : '192.168.1.20',
      satellite_auto: true,
      satellite_pin: '',
      https_only: false,
      qr_login: setup,
      ntp_server: '',
      cors_origins: '',
      menu_chord: 'auto',
      menu_hold_ms: 2000,
      usb_speed: 'auto',
      dock_paired_only: !setup,
      setup_done: !setup,
      token_set: false,
    },
    firmware: 'v1.2.0',
    slots: [
      {
        slot: 1,
        label: 'ota_0',
        state: 'valid',
        version: 'v1.1.4',
        built: '2026-09-28',
        running: false,
        next: false,
        level: 1,
        bootable: true,
      },
      {
        slot: 2,
        label: 'ota_1',
        state: 'valid',
        version: 'v1.2.0',
        built: '2026-10-21',
        running: true,
        next: true,
        level: 1,
        bootable: true,
      },
      { slot: 3, label: 'ota_2', state: 'empty', running: false, next: false, bootable: false, why_not: 'empty' },
      { slot: 4, label: 'ota_3', state: 'empty', running: false, next: false, bootable: false, why_not: 'empty' },
    ],
    pagePartition: null,
    tls: false,
    docks: setup
      ? []
      : [{ ip: '192.168.1.31', mac: '02:00:5e:10:00:31', name: 'edit-pc', kind: 'app', connected: true }],
    pairOpenUntil: 0,
    request: null,
    requested: setup,
    denied: [],
    since: {},
    tokens: setup
      ? []
      : [
          {
            name: 'home-assistant',
            right: 'full',
            created: '2026-10-02',
            last_used: new Date(now - 60_000).toISOString(),
          },
        ],
    events: [],
    inputs: [],
    seq: 0,
    faces: {
      page: 0,
      mic: true,
      live: false,
      rec: false,
      scene: 0,
      vol: [72, 45, 30, 80],
      mute: [false, false, true, false],
      lights: [true, true, false],
      light: [65, 40, 4800, 55],
      brb: false,
      play: true,
      track: 0,
      appVol: [60, 35, 50, 75],
      presses: [0, 0, 0, 0, 0, 0, 0, 0],
      dial: [0, 0, 0, 0],
    },
    menu: false,
    pwUntil: 0,
    resetUntil: 0,
    loginReq: null,
    join: null,
    joinState: '',
    joinError: '',
    ota: null,
    setupStep: 0,
    bootReason: setup ? 'power' : 'restart',
    images: 412,
    log: [],
  }
}

/** Seconds after the start of the bridge. */
const up = (s: Demo, now: number) => Math.max(0, Math.floor((now - s.bootAt) / 1000))

function event(s: Demo, now: number, kind: string, text: string) {
  s.events = [...s.events, { since_s: up(s, now), at: Math.floor(now / 1000), kind, text }].slice(-32)
}

export function logLine(s: Demo, now: number, text: string) {
  const line = `${new Date(now).toISOString().slice(11, 23)} ${text}`
  s.log = [...s.log, line].slice(-120)
  return line
}

/** What a fresh start writes, also after a simulated restart. */
export function boot(s: Demo, now: number, reason = s.bootReason) {
  s.bootAt = now
  s.bootReason = reason
  s.downUntil = 0
  s.menu = false
  s.inputs = []
  s.seq = 0
  s.ota = null
  s.join = null
  s.joinState = ''
  s.events = []
  s.log = []
  s.since = {}
  const nx = s.slots.find((x) => x.next && x.version)
  if (nx?.version) {
    s.slots = s.slots.map((x) => ({
      ...x,
      running: x.slot === nx.slot,
      state: x.slot === nx.slot && x.state === 'pending' ? 'valid' : x.state,
    }))
    s.firmware = nx.version
  }
  event(s, now, 'boot', `started (${reason}), firmware ${s.firmware}`)
  event(s, now, 'lan', `LAN up, ${lanIp(s)}`)
  if (s.cfg.wifi_ssid) event(s, now, 'wlan', `WLAN ${s.cfg.wifi_ssid}, ${WLAN_IP}`)
  const lines = [
    `I (21) boot: DeckConnector ${s.firmware}, ESP32-P4 rev v3.1`,
    'I (412) deck: Stream Deck + EL00K1A00001 at high speed',
    'I (2688) net: LAN link up 100 Mbit/s full duplex',
    s.cfg.wifi_ssid ? `I (5724) net: WLAN ${s.cfg.wifi_ssid} ${WLAN_IP}, -47 dBm` : 'I (5724) net: no WLAN set',
    `I (6692) net: LAN ${lanIp(s)}/24 via DHCP`,
    s.cfg.satellite ? `I (7439) sat: connected to ${s.cfg.satellite}:16622, Companion ${COMPANION.version}` : '',
    'I (7665) web: web UI on port 80 and 443',
  ].filter(Boolean)
  // The clock stamp follows the uptime in brackets, so the lines read in order and end now.
  const ms = (l: string) => Number(/\((\d+)\)/.exec(l)?.[1] ?? 0)
  for (const l of lines) logLine(s, now - 7665 + ms(l), l)
}

const lanIp = (s: Demo) => (s.cfg.lan.dhcp || !s.cfg.lan.ip ? LAN_IP : s.cfg.lan.ip)

/** Any address in the example LAN answers, everything else (the internet) does not: the demo has no network. */
export const reachable = (addr: string) => /^(wss?:\/\/)?192\.168\.1\.\d{1,3}(?=[:/]|$)/.test(addr)

const connectingMs = 1500

export function sources(s: Demo, now: number): Source[] {
  const out: Source[] = []
  const sinceOf = (h: string) => s.since[h] ?? s.bootAt
  s.cfg.servers.forEach((url) => {
    if (!url) return
    const t0 = sinceOf(url)
    const busy = now - t0 < connectingMs
    const ok = reachable(url) && !busy
    out.push({
      kind: 'server',
      host: url,
      name: '',
      connected: ok,
      connecting: busy,
      chosen: false,
      drawing: false,
      error: ok || busy ? '' : 'no connection',
      since_s: Math.max(0, Math.floor((t0 + (busy ? 0 : connectingMs) - s.bootAt) / 1000)),
      iface: ok ? 'lan' : undefined,
    })
  })
  if (s.cfg.satellite) {
    const t0 = sinceOf('satellite')
    const busy = now - t0 < connectingMs
    const ok = reachable(s.cfg.satellite) && !busy
    out.push({
      kind: 'companion',
      host: s.cfg.satellite,
      name: ok ? COMPANION.name : '',
      via: 'satellite',
      connected: ok,
      connecting: busy,
      chosen: false,
      drawing: false,
      error: ok || busy ? '' : 'no answer on port 16622',
      since_s: Math.max(0, Math.floor((t0 + (busy ? 0 : connectingMs) - s.bootAt) / 1000)),
      iface: ok ? 'lan' : undefined,
    })
  }
  for (const d of s.docks)
    out.push({
      kind: d.kind,
      host: d.ip,
      name: d.name,
      via: 'dock',
      connected: d.connected,
      chosen: false,
      drawing: false,
    })
  const c = s.cfg
  const of = (x: Source) => (x.kind === 'server' ? 'server' : x.kind)
  let chosen = out.find((x) =>
    c.host === 'satellite' ? x.via === 'satellite' : c.host ? x.host === c.host && of(x) === c.controller : false,
  )
  if (!c.host) chosen = out.find((x) => of(x) === c.controller && x.connected)
  if (chosen) chosen.chosen = true
  const draws = chosen?.connected && !covered(s, now)
  if (draws && chosen) chosen.drawing = true
  return out
}

/** The deck shows the bridge's own screen instead of the host's picture. */
const covered = (s: Demo, now: number) => s.menu || !!s.ota || now < s.pwUntil || now < s.resetUntil

export function writer(s: Demo, now: number, list = sources(s, now)): string {
  const d = list.find((x) => x.drawing)
  if (!d) return 'ui'
  if (d.via === 'satellite') return 'satellite'
  if (d.kind === 'server') return `server${s.cfg.servers.indexOf(d.host) + 1}`
  return `dock${s.docks.findIndex((x) => x.ip === d.host) + 1}`
}

export function status(s: Demo, now: number): Status {
  const list = sources(s, now)
  const w = writer(s, now, list)
  const c = s.cfg
  const u = up(s, now)
  return {
    via_ap: false,
    id: '02005e0a1b2c',
    name: c.name,
    firmware: s.firmware,
    wlan_firmware: '3.0.10',
    api: 2,
    simulated_input: 'local',
    https_fingerprint:
      '3F:1A:9C:52:7E:04:B8:6D:21:C0:5F:93:AA:17:4E:2B:88:D1:60:3C:75:E9:0B:46:1D:F2:58:A7:C3:09:6E:B4',
    uptime_s: u,
    controller: c.controller,
    writer: w,
    menu_open: covered(s, now) && !s.ota,
    boot: { reason: s.bootReason, at: Math.floor(s.bootAt / 1000) },
    setup: { active: !c.setup_done || s.setupStep > 0, step: s.setupStep, rerun: c.setup_done && s.setupStep > 0 },
    device: DECK,
    decks: [DECK],
    https_custom: s.tls,
    network: {
      lan: { cable: true, dead: false, ip: lanIp(s) },
      wlan: {
        ssid: c.wifi_ssid,
        ip: c.wifi_ssid ? (c.wlan.dhcp || !c.wlan.ip ? WLAN_IP : c.wlan.ip) : '',
        bssid: c.wifi_ssid ? c.wifi_bssid || '02:00:00:00:01:01' : '',
        channel: c.wifi_ssid ? 6 : 0,
        lock: c.wifi_bssid,
        rssi: c.wifi_ssid ? -47 - Math.round(Math.abs(Math.sin(now / 9000)) * 6) : 0,
        join: s.joinState,
        join_error: s.joinError,
      },
      ap: { on: false, ssid: '' },
    },
    sources: list,
    pairing: {
      only_paired: c.dock_paired_only,
      request: s.request?.ip ?? null,
      request_name: s.request?.name ?? null,
      open: now < s.pairOpenUntil,
      open_s: Math.max(0, Math.ceil((s.pairOpenUntil - now) / 1000)),
      auto_open: s.docks.length === 0,
      denied: s.denied,
      hosts: s.docks.map((d) => ({ ip: d.ip, mac: d.mac, name: d.name, kind: d.kind, connected: d.connected })),
    },
    counters: {
      images: s.images,
      superseded: Math.floor(s.images / 40),
      rejected: 0,
      usb_errors: 0,
      usb_recoveries: 0,
      usb_speed: 'high',
      inputs: s.seq,
      inputs_dropped: 0,
    },
    boot_ms: { deck: 412, lan_link: 2688, wlan_ip: 5724, lan_ip: 6692, companion: 7439, host_draws: 7665 },
    memory: {
      heap_free: 351_000 - (u % 97) * 40,
      heap_min_free: 289_440,
      psram_free: 32_180_000,
      heap_largest: 208_896,
      psram_largest: 30_932_992,
      tasks: 37,
      sockets: 9,
      sockets_max: 32,
    },
  }
}

/** The web socket's `state`: the status without uptime, signal and counters. */
export function wsState(s: Demo, now: number) {
  const { uptime_s: _u, counters: _c, memory: _m, ...rest } = status(s, now)
  const wlan = rest.network?.wlan
  if (wlan) delete wlan.rssi
  return rest
}

export const config = (s: Demo): Config => ({ ...s.cfg, token_set: s.tokens.length > 0 && s.cfg.token_set })

const LIVE: (keyof ConfigPatch)[] = [
  'brightness',
  'controller',
  'setup_done',
  'servers',
  'host',
  'satellite',
  'server_pins',
  'satellite_pin',
  'dock_paired_only',
  'qr_login',
  'satellite_auto',
  'https_only',
  'cors_origins',
  'menu_chord',
  'menu_hold_ms',
]

const NAME = /^[A-Za-z0-9](?:[A-Za-z0-9-]{0,29}[A-Za-z0-9])?$/

export type Reply = { status: number; body: unknown }
const bad = (status: number, error: string, code?: string): Reply => ({ status, body: { ok: false, error, code } })

/** PUT /api/config. A change outside LIVE restarts the bridge, like the firmware. */
export function putConfig(s: Demo, now: number, p: ConfigPatch): Reply {
  if (p.name !== undefined && !NAME.test(p.name)) return bad(400, 'name: 1 to 31 letters, digits and dashes')
  if (p.admin_pass !== undefined && (p.admin_pass.length < 6 || p.admin_pass.length > 23))
    return bad(400, 'admin_pass: 6 to 23 characters')
  if (p.servers && p.servers.some((u) => u && !/^wss?:\/\/\S+$/.test(u))) return bad(400, 'servers: ws:// or wss://')
  if (p.brightness !== undefined && (p.brightness < 0 || p.brightness > 100)) return bad(400, 'brightness: 0 to 100')
  const old = s.cfg
  const next: Config = {
    ...old,
    ...Object.fromEntries(
      Object.entries(p).filter(([k]) => !['wifi_pass', 'admin_pass', 'token', 'lan', 'wlan'].includes(k)),
    ),
    lan: { ...old.lan, ...p.lan },
    wlan: { ...old.wlan, ...p.wlan },
  }
  if (p.wifi_pass !== undefined) next.wifi_pass_set = !!p.wifi_pass || !next.wifi_ssid
  if (p.token !== undefined) next.token_set = !!p.token
  if (p.satellite !== undefined && p.satellite !== old.satellite) {
    next.satellite_auto = !!p.satellite
    s.since.satellite = now
    if (p.satellite) event(s, now, 'source', `Satellite set to ${p.satellite}`)
  }
  for (const u of next.servers) if (u && !old.servers.includes(u)) s.since[u] = now
  if (p.controller !== undefined || p.host !== undefined) {
    s.menu = false
    event(s, now, 'source', `picked ${next.controller}${next.host ? ` ${next.host}` : ''}`)
  }
  const restart =
    Object.keys(p).some(
      (k) =>
        !LIVE.includes(k as keyof ConfigPatch) &&
        JSON.stringify((p as Record<string, unknown>)[k]) !==
          JSON.stringify((old as unknown as Record<string, unknown>)[k]),
    ) ||
    p.admin_pass !== undefined ||
    p.wifi_pass !== undefined
  s.cfg = next
  if (p.admin_pass !== undefined) {
    s.pass = p.admin_pass
    s.authed = false
  }
  if (!restart) return { status: 200, body: { ok: true, restart: false, config: config(s) } }
  restartSoon(s, now)
  const sch = next.https_only ? 'https' : 'http'
  return {
    status: 200,
    body: {
      ok: true,
      restart: true,
      reach: [`${sch}://${lanIp(s)}`, `${sch}://${next.name}.local`],
      config: config(s),
    },
  }
}

export const RESTART_MS = 4500
export const isDown = (s: Demo, now: number) => !!s.downUntil && now >= s.downFrom && now < s.downUntil

export function restartSoon(s: Demo, now: number) {
  s.downFrom = now + 1000
  s.downUntil = s.downFrom + RESTART_MS
}

/** Turns, presses and taps on what the deck shows. `origin` 'web' is a person at the live deck. */
export function input(s: Demo, now: number, b: InputBody, origin: 'web' | 'deck' = 'web'): Reply {
  if (isDown(s, now)) return bad(503, 'restarting')
  const w = writer(s, now)
  const log = (kind: string, index?: number, value?: number) => {
    s.seq++
    s.inputs = [...s.inputs, { seq: s.seq, at_ms: now - s.bootAt, origin, kind, index, value, writer: w }].slice(-200)
  }
  const f = s.faces
  const clamp = (n: number, lo = 0, hi = 100) => Math.max(lo, Math.min(hi, n))
  if ('key' in b) {
    if (b.key < 0 || b.key > 7) return bad(400, 'key out of range')
    const act = b.action ?? 'press'
    if (act !== 'up') log('key_down', b.key)
    if (act !== 'down') log('key_up', b.key)
    if (act === 'up') return ok()
    if (now < s.pwUntil || now < s.resetUntil) {
      s.pwUntil = 0
      return ok()
    }
    if (s.menu) return menuKey(s, now, b.key)
    if (w === 'satellite' || (w.startsWith('dock') && s.docks[+w.slice(4) - 1]?.kind === 'companion'))
      studioKey(f, b.key)
    else if (w.startsWith('dock')) appKey(f, b.key)
    else if (w.startsWith('server')) f.presses[b.key] = (f.presses[b.key] ?? 0) + 1
    return ok()
  }
  if ('encoder' in b) {
    const i = b.encoder
    if (i < 0 || i > 3) return bad(400, 'encoder out of range')
    const act = b.action ?? 'press'
    if (act === 'turn') {
      const n = b.ticks ?? 1
      log('encoder_turn', i, n)
      if (s.menu) {
        s.cfg = { ...s.cfg, brightness: clamp(s.cfg.brightness + n * 5) }
        return ok()
      }
      if (w === 'satellite' || isCompDock(s, w)) {
        if (f.page === 0) f.vol[i] = clamp((f.vol[i] ?? 0) + n * 2)
        else if (i === 2) f.light[2] = clamp((f.light[2] ?? 4800) + n * 100, 2900, 7000)
        else f.light[i] = clamp((f.light[i] ?? 0) + n * 2)
      } else if (w.startsWith('dock')) f.appVol[i] = clamp((f.appVol[i] ?? 0) + n * 2)
      else if (w.startsWith('server')) f.dial[i] = (f.dial[i] ?? 0) + n
      return ok()
    }
    if (act !== 'up') log('encoder_down', i)
    if (act !== 'down') log('encoder_up', i)
    if (act !== 'up') dialPress(s, w, i)
    return ok()
  }
  if ('touch' in b) {
    const { x, y } = b
    if (y < STRIP.y || y >= STRIP.y + STRIP.h || x < 0 || x >= WIDTH)
      return bad(400, 'touch outside the strip', 'strip_outside')
    log(`touch_${b.touch}`, undefined, x)
    if (b.touch === 'swipe' && b.x2 !== undefined) {
      if (w === 'satellite' || isCompDock(s, w)) f.page = b.x2 < x ? 1 : 0
      return ok()
    }
    dialPress(s, w, Math.floor(x / 200))
    return ok()
  }
  return bad(400, 'nothing to do')
}

const ok = (): Reply => ({ status: 200, body: { ok: true } })
const isCompDock = (s: Demo, w: string) => w.startsWith('dock') && s.docks[+w.slice(4) - 1]?.kind === 'companion'

function dialPress(s: Demo, w: string, i: number) {
  const f = s.faces
  if (s.menu) return
  if (w === 'satellite' || isCompDock(s, w)) {
    if (f.page === 0) f.mute[i] = !f.mute[i]
    else if (i < 2 || i === 3) f.lights[i === 3 ? 2 : i] = !f.lights[i === 3 ? 2 : i]
  } else if (w.startsWith('dock')) {
    if (i === 0) f.play = !f.play
  } else if (w.startsWith('server')) f.dial[i] = 0
}

function studioKey(f: Faces, k: number) {
  if (f.page === 0) {
    if (k === 1) f.mic = !f.mic
    if (k === 2) f.live = !f.live
    if (k === 3) f.rec = !f.rec
    if (k >= 4 && k <= 6) f.scene = k - 4
    if (k === 7) f.page = 1
    return
  }
  if (k <= 2) f.lights[k] = !f.lights[k]
  if (k === 3) f.brb = !f.brb
  if (k === 7) f.page = 0
}

function appKey(f: Faces, k: number) {
  if (k === 0) f.play = !f.play
  if (k === 1) f.track = (f.track + 1) % TRACKS.length
  if (k === 2) f.lights[0] = !f.lights[0]
}

function menuKey(s: Demo, now: number, k: number): Reply {
  const list = sources(s, now).filter((x) => x.kind !== 'unknown')
  if (k < 4) {
    const x = list[k]
    if (!x) return ok()
    s.cfg = {
      ...s.cfg,
      controller: x.kind === 'server' ? 'server' : x.kind,
      host: x.via === 'satellite' ? 'satellite' : x.host,
    }
    s.menu = false
    event(s, now, 'source', `picked on the deck: ${x.name || x.host}`)
  }
  if (k === 4) s.cfg = { ...s.cfg, brightness: Math.max(10, s.cfg.brightness - 10) }
  if (k === 5) s.cfg = { ...s.cfg, brightness: Math.min(100, s.cfg.brightness + 10) }
  if (k === 7) s.menu = false
  return ok()
}

export const TRACKS = ['Opening Theme', 'Lo-Fi Break', 'Outro Groove']

// What the deck shows: one face per key and one for the strip. deck.ts paints them; equal faces share a picture.
export interface KeyFace {
  t: string
  sub?: string
  bg: string
  fg?: string
  on?: boolean
  bar?: number
  icon?:
    | 'mic'
    | 'live'
    | 'rec'
    | 'cam'
    | 'light'
    | 'next'
    | 'back'
    | 'play'
    | 'pause'
    | 'skip'
    | 'clock'
    | 'host'
    | 'sun'
    | 'x'
    | 'info'
    | 'brb'
    | 'dot'
}
export interface StripZone {
  t: string
  v: number
  text: string
  off?: boolean
  color: string
}
export type StripFace = { zones: StripZone[] } | { text: string; sub?: string; bar?: number }
export interface Face {
  keys: KeyFace[]
  strip: StripFace
}

const pad = (n: number) => String(n).padStart(2, '0')

function faceEn(s: Demo, now: number): Face {
  const f = s.faces
  const w = writer(s, now)
  const d = new Date(now)
  const clock = `${pad(d.getHours())}:${pad(d.getMinutes())}`
  if (now < s.pwUntil)
    return {
      keys: [
        { t: 'admin', sub: 'user', bg: '#1b1e22' },
        { t: s.pass, sub: 'password', bg: '#1b1e22', fg: '#ffb020' },
        ...blank(6),
      ],
      strip: { text: 'Password shown for 30 s', sub: 'any key hides it' },
    }
  if (now < s.resetUntil)
    return {
      keys: [
        ...blank(4),
        { t: 'Reset', sub: 'tap twice', bg: '#5a1714', fg: '#ff5b4f', icon: 'x' },
        ...blank(2),
        { t: 'Keep', bg: '#1b1e22', icon: 'back' },
      ],
      strip: { text: 'Reset all settings?', sub: `${Math.ceil((s.resetUntil - now) / 1000)} s` },
    }
  if (s.ota) {
    const pct = otaPct(s, now)
    return {
      keys: blank(8),
      strip: { text: `Update ${s.ota.version}`, sub: pct < 100 ? 'downloading' : 'verifying', bar: pct },
    }
  }
  if (s.menu) {
    const list = sources(s, now)
      .filter((x) => x.kind !== 'unknown')
      .slice(0, 4)
    const keys: KeyFace[] = list.map((x) => ({
      t: x.name || x.host.replace(/^wss?:\/\//, '').split('/')[0] || x.host,
      sub: x.drawing || x.chosen ? 'in use' : x.connected ? x.kind : 'offline',
      bg: x.chosen ? '#3a2a07' : '#1b1e22',
      fg: x.connected ? '#e8e6e1' : '#6b7178',
      on: x.chosen,
      icon: 'host',
    }))
    while (keys.length < 4) keys.push({ t: '', bg: '#0f1113' })
    keys.push(
      { t: 'Dimmer', sub: `${s.cfg.brightness}%`, bg: '#1b1e22', icon: 'sun' },
      { t: 'Brighter', sub: `${s.cfg.brightness}%`, bg: '#1b1e22', icon: 'sun' },
      { t: s.cfg.name, sub: lanIp(s), bg: '#1b1e22', icon: 'info' },
      { t: 'Close', bg: '#1b1e22', icon: 'x' },
    )
    return { keys, strip: { text: 'DeckConnector menu', sub: `${s.firmware} · ${lanIp(s)}` } }
  }
  if (w === 'satellite' || isCompDock(s, w)) {
    if (f.page === 0)
      return {
        keys: [
          { t: clock, sub: 'Clock', bg: '#101418', fg: '#e8e6e1', icon: 'clock' },
          {
            t: f.mic ? 'Mic on' : 'Muted',
            bg: f.mic ? '#123a22' : '#5a1714',
            fg: f.mic ? '#3ddc84' : '#ff5b4f',
            icon: 'mic',
            on: f.mic,
          },
          {
            t: f.live ? 'ON AIR' : 'Go live',
            bg: f.live ? '#b3261e' : '#22262b',
            fg: '#ffffff',
            icon: 'live',
            on: f.live,
          },
          {
            t: f.rec ? 'REC' : 'Record',
            bg: f.rec ? '#5a1714' : '#22262b',
            fg: f.rec ? '#ff5b4f' : '#e8e6e1',
            icon: 'rec',
            on: f.rec,
          },
          ...['Cam 1', 'Cam 2', 'Wide'].map((n, i): KeyFace => ({
            t: n,
            sub: 'Scene',
            bg: f.scene === i ? '#ffb020' : '#1d2a3a',
            fg: f.scene === i ? '#0f1113' : '#cfe3ff',
            icon: 'cam',
            on: f.scene === i,
          })),
          { t: 'Lights', sub: 'Page 2', bg: '#262b31', icon: 'next' },
        ],
        strip: {
          zones: ['Mic', 'Music', 'Game', 'Master'].map((t, i) => ({
            t,
            v: f.vol[i] ?? 0,
            text: f.mute[i] ? 'muted' : `${f.vol[i]}%`,
            off: f.mute[i],
            color: ['#3ddc84', '#4fa3ff', '#c08cff', '#ffb020'][i] ?? '#ffb020',
          })),
        },
      }
    return {
      keys: [
        ...['Key', 'Fill', 'Back'].map((t, i): KeyFace => ({
          t,
          sub: f.lights[i] ? 'on' : 'off',
          bg: f.lights[i] ? '#4a3a0c' : '#1b1e22',
          fg: f.lights[i] ? '#ffcf5a' : '#8a9097',
          icon: 'light',
          on: f.lights[i],
        })),
        {
          t: 'BRB',
          sub: f.brb ? 'showing' : 'Scene',
          bg: f.brb ? '#ffb020' : '#1d2a3a',
          fg: f.brb ? '#0f1113' : '#cfe3ff',
          icon: 'brb',
          on: f.brb,
        },
        { t: clock, sub: 'Clock', bg: '#101418', icon: 'clock' },
        { t: '', bg: '#0f1113' },
        { t: '', bg: '#0f1113' },
        { t: 'Studio', sub: 'Page 1', bg: '#262b31', icon: 'back' },
      ],
      strip: {
        zones: [
          {
            t: 'Key',
            v: f.light[0] ?? 0,
            text: f.lights[0] ? `${f.light[0]}%` : 'off',
            off: !f.lights[0],
            color: '#ffcf5a',
          },
          {
            t: 'Fill',
            v: f.light[1] ?? 0,
            text: f.lights[1] ? `${f.light[1]}%` : 'off',
            off: !f.lights[1],
            color: '#ffcf5a',
          },
          { t: 'Colour', v: (((f.light[2] ?? 4800) - 2900) / 4100) * 100, text: `${f.light[2]} K`, color: '#9fd3ff' },
          {
            t: 'Back',
            v: f.light[3] ?? 0,
            text: f.lights[2] ? `${f.light[3]}%` : 'off',
            off: !f.lights[2],
            color: '#ffcf5a',
          },
        ],
      },
    }
  }
  if (w.startsWith('dock'))
    return {
      keys: [
        {
          t: f.play ? 'Pause' : 'Play',
          sub: TRACKS[f.track],
          bg: '#1a1f3a',
          fg: '#b9c4ff',
          icon: f.play ? 'pause' : 'play',
        },
        { t: 'Next', sub: 'Music', bg: '#1a1f3a', fg: '#b9c4ff', icon: 'skip' },
        {
          t: 'Key Light',
          sub: f.lights[0] ? 'on' : 'off',
          bg: f.lights[0] ? '#4a3a0c' : '#1b1e22',
          fg: f.lights[0] ? '#ffcf5a' : '#8a9097',
          icon: 'light',
          on: f.lights[0],
        },
        { t: clock, sub: 'Clock', bg: '#101418', icon: 'clock' },
        { t: 'Folder', sub: 'Elgato App', bg: '#262b31', icon: 'next' },
        { t: '', bg: '#0f1113' },
        { t: '', bg: '#0f1113' },
        { t: '', bg: '#0f1113' },
      ],
      strip: {
        zones: ['System', 'Music', 'Browser', 'Mic'].map((t, i) => ({
          t,
          v: f.appVol[i] ?? 0,
          text: `${f.appVol[i]}%`,
          off: i === 0 && !f.play ? false : undefined,
          color: '#8c9cff',
        })),
      },
    }
  if (w.startsWith('server'))
    return {
      keys: f.presses.map((n, i) => ({ t: String(n), sub: `key ${i + 1}`, bg: '#14262a', fg: '#7fe0d0', icon: 'dot' })),
      strip: {
        zones: f.dial.map((n, i) => ({
          t: `dial ${i + 1}`,
          v: 50 + Math.max(-50, Math.min(50, n)),
          text: String(n),
          color: '#7fe0d0',
        })),
      },
    }
  const chosen = sources(s, now).find((x) => x.chosen)
  return {
    keys: blank(8),
    strip: {
      text: chosen ? `Waiting for ${chosen.name || chosen.host}` : 'No host picked',
      sub: `${s.cfg.name} · ${lanIp(s)}`,
    },
  }
}

/** The language of the host pages on the virtual deck, set by the demo page. */
export const view = { lang: 'en' as 'en' | 'de' }

const DE: Record<string, string> = {
  user: 'Benutzer',
  password: 'Passwort',
  'Password shown for 30 s': 'Passwort 30 s sichtbar',
  'any key hides it': 'jede Taste blendet es aus',
  Reset: 'Löschen',
  'tap twice': 'zweimal tippen',
  Keep: 'Behalten',
  'Reset all settings?': 'Alle Einstellungen löschen?',
  downloading: 'lädt',
  verifying: 'prüft',
  'in use': 'aktiv',
  Dimmer: 'Dunkler',
  Brighter: 'Heller',
  Close: 'Schließen',
  'DeckConnector menu': 'DeckConnector-Menü',
  Clock: 'Uhr',
  'Mic on': 'Mikro an',
  Muted: 'Stumm',
  'Go live': 'Live gehen',
  Record: 'Aufnahme',
  Scene: 'Szene',
  Lights: 'Licht',
  'Page 1': 'Seite 1',
  'Page 2': 'Seite 2',
  Mic: 'Mikro',
  Music: 'Musik',
  Game: 'Spiel',
  Master: 'Gesamt',
  muted: 'stumm',
  Key: 'Haupt',
  Fill: 'Fülllicht',
  Back: 'Hinten',
  Colour: 'Farbe',
  on: 'an',
  off: 'aus',
  showing: 'läuft',
  Next: 'Weiter',
  Folder: 'Ordner',
  'No host picked': 'Kein Host gewählt',
}

const de = (x: string) =>
  DE[x] ??
  x
    .replace(/^Waiting for /, 'Warte auf ')
    .replace(/^key (\d+)$/, 'Taste $1')
    .replace(/^dial (\d+)$/, 'Regler $1')

export function face(s: Demo, now: number): Face {
  const f = faceEn(s, now)
  if (view.lang === 'en') return f
  const strip = f.strip
  return {
    keys: f.keys.map((k) => ({ ...k, t: de(k.t), sub: k.sub && de(k.sub) })),
    strip:
      'zones' in strip
        ? { zones: strip.zones.map((z) => ({ ...z, t: de(z.t), text: de(z.text) })) }
        : { ...strip, text: de(strip.text), sub: strip.sub && de(strip.sub) },
  }
}

const blank = (n: number): KeyFace[] => Array.from({ length: n }, () => ({ t: '', bg: '#040506' }))

export const OTA_MS = 9000
export const otaPct = (s: Demo, now: number) =>
  s.ota ? Math.min(100, Math.floor(((now - s.ota.at) / OTA_MS) * 100)) : 0

export interface Out {
  ota?: { target: string; state: string; pct: number }
  log?: string[]
}

/** Moves every timed thing on to `now`. Returns what the web socket should say about it. */
export function tick(s: Demo, now: number): Out {
  const out: Out = { log: [] }
  const say = (t: string) => out.log?.push(logLine(s, now, t))
  if (s.downUntil && now >= s.downUntil) {
    boot(s, now, 'restart')
    return out
  }
  if (isDown(s, now)) return out
  if (s.ota) {
    const pct = otaPct(s, now)
    if (pct >= 100) {
      out.ota = { target: 'p4', state: 'done', pct: 100 }
      say(`I ota: ${s.ota.version} written to slot ${s.ota.slot}, restarting`)
      const n = s.ota.slot
      s.slots = s.slots.map((x) =>
        x.slot === n
          ? {
              ...x,
              state: 'valid',
              version: s.ota?.version,
              built: '2026-11-04',
              running: true,
              next: true,
              level: 1,
              bootable: true,
              why_not: undefined,
            }
          : { ...x, running: false, next: false },
      )
      event(s, now, 'update', `updated to ${s.ota.version}`)
      s.ota = null
      restartSoon(s, now)
    } else out.ota = { target: 'p4', state: 'download', pct }
  }
  if (s.join && now >= s.join.until) {
    const j = s.join
    s.join = null
    const known = WIFI.find((w) => w.ssid === j.ssid)
    const okPass = known?.open || j.pass.length >= 8
    if (known && okPass && j.pass !== 'wrongpass') {
      s.joinState = 'ok'
      s.joinError = ''
      s.cfg = { ...s.cfg, wifi_ssid: j.ssid, wifi_bssid: j.bssid, wifi_pass_set: !known.open }
      event(s, now, 'wlan', `joined ${j.ssid}`)
      say(`I net: WLAN ${j.ssid} joined, ${WLAN_IP}`)
    } else {
      s.joinState = 'failed'
      s.joinError = known ? 'wrong password' : 'network not found'
      say(`W net: WLAN ${j.ssid}: ${s.joinError}`)
    }
  }
  if (s.loginReq?.state === 'pending') {
    if (now - s.loginReq.at > 4000) s.loginReq.state = 'accepted'
  }
  if (s.resetUntil && now >= s.resetUntil) s.resetUntil = 0
  // Like the firmware, an unanswered pairing request lapses after a minute.
  if (s.request && now - s.request.at > 60_000) s.request = null
  // A second host asks to pair a little after the visitor arrives, so the pairing flow can be tried.
  if (s.authed && !s.requested && s.cfg.setup_done && now - s.started > 25_000) {
    s.requested = true
    s.request = { ip: '192.168.1.52', name: 'stage-laptop', at: now }
    event(s, now, 'pairing', 'stage-laptop (192.168.1.52) asks to pair')
    say('I dock: 192.168.1.52 asks to pair')
  }
  if (Math.floor(now / 7000) !== Math.floor((now - 1000) / 7000))
    say(
      ['D web: GET /api/status 200', 'D link: satellite ping 3 ms', 'D deck: frame 18 ms', 'D net: WLAN -47 dBm'][
        Math.floor(now / 7000) % 4
      ] ?? '',
    )
  return out
}

export function dock(
  s: Demo,
  now: number,
  b: { accept?: boolean; ip?: string; pairing?: number; forget?: string | true },
): Reply {
  if (b.pairing !== undefined) {
    s.pairOpenUntil = b.pairing > 0 ? now + b.pairing * 1000 : 0
    return ok()
  }
  if (b.forget !== undefined) {
    s.docks = b.forget === true ? [] : s.docks.filter((d) => d.ip !== b.forget)
    return ok()
  }
  if (b.accept !== undefined) {
    if (!s.request || s.request.ip !== b.ip) return bad(409, 'no such request')
    const r = s.request
    s.request = null
    if (b.accept) {
      s.docks = [
        ...s.docks,
        { ip: r.ip, mac: '02:00:5e:10:00:52', name: r.name, kind: 'companion' as const, connected: true },
      ].slice(-8)
      event(s, now, 'pairing', `paired ${r.name}`)
    } else {
      s.denied = [...s.denied, { ip: r.ip, name: r.name, since_s: up(s, now) }].slice(-4)
      event(s, now, 'pairing', `refused ${r.name}`)
    }
    return ok()
  }
  return bad(400, 'nothing to do')
}

export function makeToken(s: Demo, name: string, right: 'control' | 'full', now: number): Reply {
  if (!/^[A-Za-z0-9_-]{1,23}$/.test(name)) return bad(400, 'name: 1 to 23 letters, digits, - or _')
  const rest = s.tokens.filter((t) => t.name !== name)
  if (rest.length >= 8) return bad(409, 'at most 8 tokens')
  const tok = Array.from({ length: 40 }, (_, i) => 'abcdefghjkmnpqrstuvwxyz23456789'[(i * 7 + now) % 31]).join('')
  s.tokens = [...rest, { name, right, created: new Date(now).toISOString().slice(0, 10) }]
  return { status: 200, body: { name, right, token: `dc_${tok}` } }
}

export function startOta(s: Demo, now: number, version = LATEST): Reply {
  if (s.ota) return bad(409, 'an update runs already', 'busy')
  const running = s.slots.find((x) => x.running)?.slot ?? 1
  const slot = (running % s.slots.length) + 1
  s.ota = { at: now, version, slot }
  event(s, now, 'update', `update to ${version} started`)
  return ok()
}

export const latest = LATEST
export const companion = COMPANION
