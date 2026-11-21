import { DO, INPUT, withUi, type UiModel } from './deck-ui'
import type { api } from '../api/client'
import { passwordError } from '../lib/validate'
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
  pause: boolean
  title: boolean
  rec: boolean
  scene: number
  vol: number[]
  mute: boolean[]
  lights: boolean[]
  light: number[]
  brb: boolean
  wall: boolean
  focus: boolean
  play: boolean
  appVol: number[]
  appMute: boolean[]
  presses: number[]
  dial: number[]
}

export interface Demo {
  v: 12
  bootAt: number
  /** A restart: answers stop at downFrom (1 s after it was asked, like the firmware) until downUntil. */
  downFrom: number
  downUntil: number
  started: number
  authed: boolean
  claimed: boolean
  pass: string
  cfg: Config
  firmware: string
  slots: Slot[]
  pagePartition: string | null
  tls: boolean
  /** running: on since the last start, like the firmware, where off needs a restart */
  matter: { on: boolean; running: boolean }
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
  /** The deck menu's state (ui_menu_t of firmware/components/db_ui), which deck-ui.ts runs. */
  ui: number[]
  pwUntil: number
  resetUntil: number
  loginReq: { id: string; at: number; state: 'pending' | 'accepted' | 'denied' | 'expired' | 'used' } | null
  /** Wrong passwords in a row, and the end of the lock they bring, as the firmware's login. */
  loginFails?: number
  lockUntil?: number
  /** The log of the run before the last restart. */
  prev?: string[]
  /** A download from an address that does not answer fails a moment after it was asked. */
  otaFail?: { at: number; why: string } | null
  /** A computer that asks again after its request was denied and then forgiven, or that comes along while pairing
   *  is open. */
  later?: { ip: string; name: string; kind: 'app' | 'companion'; at: number } | null
  /** POST /api/deck/reconnect: the deck is gone for a moment and counts as a USB recovery. */
  recoveries?: number
  join: { ssid: string; bssid: string; pass: string; until: number } | null
  joinState: '' | 'testing' | 'ok' | 'failed'
  joinError: string
  ota: { at: number; version: string; slot: number } | null
  setupStep: number
  /** When setup reached the source step; a restart in between keeps it. */
  setupSrcAt?: number
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
const LATEST = 'v1.0.1'
const COMPANION = { name: 'Studio Companion', version: '5.2.0' }

export const DECK: DeckInfo = {
  model: 'Stream Deck +',
  serial: 'EL00K1A00001',
  firmware: '1.05.008',
  dock: true,
  keys: 8,
  encoders: 4,
  width: 800,
  height: 480,
  touch: true,
  usb_speed: 'high',
  hub: false,
}

/** The deck, at the USB speed the config asks for. */
const deck = (s: Demo): DeckInfo => (s.cfg.usb_speed === 'full' ? { ...DECK, usb_speed: 'full' } : DECK)

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

export const PASS = 'studio-demo'

export function fresh(now: number, o: { setup?: boolean; login?: boolean } = {}): Demo {
  const setup = !!o.setup
  return {
    v: 12,
    bootAt: now - (setup ? 40_000 : 3 * 3600_000 + 17 * 60_000),
    downFrom: 0,
    downUntil: 0,
    started: now,
    authed: !o.login,
    claimed: false,
    pass: PASS,
    cfg: {
      name: setup ? 'deckconnector-a1b2c3' : 'studio-deck',
      controller: 'companion',
      host: setup ? '' : 'satellite',
      brightness: 80,
      volume: 0,
      speaker: !setup,
      lan: ip(),
      wlan: ip(),
      wifi_ssid: setup ? '' : 'Studio',
      wifi_pass_set: !setup,
      wifi2_ssid: '',
      wifi2_pass_set: false,
      wifi_bssid: '',
      servers: setup ? ['', '', '', ''] : ['ws://192.168.1.40:8765', '', '', ''],
      server_pins: ['', '', '', ''],
      satellite: setup ? '' : '192.168.1.20',
      satellite_auto: true,
      satellite_pin: '',
      https_only: false,
      qr_login: setup,
      ntp_server: '',
      cors_origins: '',
      trusted_proxy: '',
      menu_chord: 'auto',
      menu_hold_ms: 2000,
      lang: 'de',
      usb_speed: 'auto',
      max_decks: 2,
      screen_off_s: 0,
      screen_dim: 0,
      // updated from v0.9.0 three hours ago: the star note shows once
      star_off: false,
      star_fw: setup ? '' : 'v0.9.0',
      star_until: 0,
      dock_paired_only: !setup,
      setup_done: !setup,
      token_set: false,
    },
    firmware: 'v1.0.0',
    slots: [
      {
        slot: 1,
        label: 'ota_0',
        state: 'valid',
        version: 'v0.1.0',
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
        version: 'v1.0.0',
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
    matter: { on: false, running: false },
    docks: setup
      ? []
      : [{ ip: '192.168.1.31', mac: '02:00:5e:10:00:31', name: 'edit-pc', kind: 'app', connected: true }],
    pairOpenUntil: 0,
    request: null,
    requested: setup,
    denied: [],
    since: {},
    tokens: setup ? [] : [{ name: 'home-assistant', right: 'full' }],
    events: [],
    inputs: [],
    seq: 0,
    faces: {
      page: 0,
      mic: true,
      pause: false,
      title: true,
      rec: true,
      scene: 1,
      vol: [80, 65, 45, 55],
      mute: [false, false, false, false],
      lights: [true, true, false],
      light: [65, 40, 4800, 55],
      brb: false,
      wall: true,
      focus: false,
      play: true,
      appVol: [60, 35, 50, 75],
      appMute: [false, false, false, false],
      presses: [0, 0, 0, 0, 0, 0, 0, 0],
      dial: [0, 0, 0, 0],
    },
    ui: [],
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

/** firmware/components/db_log/evlog.c's codes: kind, level and English text, {n} the n-th arg. contract.spec.ts
 * compares the two. */
export const EV: Record<string, [string, DeviceEvent['level'], string]> = {
  text: ['boot', 'info', '{0}'],
  more: ['boot', 'info', '{0}'],
  started: ['boot', 'info', 'started ({0})'],
  crashed: ['boot', 'err', 'started ({0})'],
  settings_erased: ['boot', 'err', 'settings storage unreadable ({0}): erased, all settings are back to defaults'],
  settings_failed: ['boot', 'err', 'settings storage failed: {0}, running on defaults'],
  start_failed: ['boot', 'err', '{0} failed to start: {1}'],
  update_aborted: ['update', 'err', 'update aborted: {0}'],
  update_installed: ['update', 'ok', '{0} update installed'],
  update_restart: ['update', 'ok', '{0} update installed, restarting'],
  update_confirmed: ['update', 'ok', 'new firmware confirmed'],
  rollback_unhealthy: ['update', 'err', 'new firmware never became healthy, rolling back'],
  rollback_unconfirmed: ['update', 'err', 'the healthy new firmware could not be confirmed, rolling back'],
  server_lost: ['source', 'warn', 'server {0} lost: {1}'],
  server_connected: ['source', 'ok', 'server {0} connected over {1}: {2}'],
  companion_lost: ['source', 'warn', 'Companion lost: {0}'],
  companion_silent: ['source', 'warn', 'Companion silent for {0} s'],
  companion_connected: ['source', 'ok', 'Companion connected at {0} over {1}'],
  host_connected: ['source', 'ok', '{0} at {1} connected'],
  host_left: ['source', 'info', '{0} left'],
  dock_refused: ['source', 'warn', 'Companion at {0} refused as a dock: it has the deck over Satellite'],
  no_satellite: ['source', 'warn', 'Companion at {0} has no Satellite API; Satellite would be better'],
  moved_to_satellite: ['source', 'ok', 'Companion at {0} moved from Dock to Satellite'],
  companion_asks: ['companion', 'warn', 'Companion {0} asks for the deck'],
  companion_setup: ['companion', 'ok', 'Companion {0} set up over Satellite by its Discover'],
  pair_asks: ['pairing', 'warn', '{0} asks to pair'],
  pair_accepted: ['pairing', 'ok', '{0} accepted'],
  pair_refused: ['pairing', 'err', '{0} refused'],
  deck_parked: ['deck', 'warn', 'deck at USB address {0} left dark: max_decks is {1}'],
  screen_off: ['deck', 'info', 'screen off'],
  screen_dimmed: ['deck', 'info', 'screen dimmed'],
  screen_on: ['deck', 'info', 'screen on'],
  lan_dead: ['lan', 'warn', 'LAN dead: the gateway stopped answering'],
  lan_back: ['lan', 'ok', 'LAN answers again'],
  lan_down: ['lan', 'err', 'LAN link down'],
  wlan_back: ['wlan', 'ok', 'WLAN {0} is back ({1} dBm), leaving the backup {2}'],
  wlan_roam: ['wlan', 'info', 'WLAN roam {0} {3} -> {1}, channel {2}'],
  wlan_joined: ['wlan', 'ok', 'WLAN joined {0} {1}, channel {2}'],
  wlan_no_link: ['wlan', 'warn', 'WLAN {0} gives no link, trying {1}'],
  wlan_no_link_backup: ['wlan', 'warn', 'WLAN {0} gives no link, trying the backup {1}'],
  matter_paired: ['matter', 'ok', 'a controller paired'],
  matter_left: ['matter', 'info', 'a controller left'],
  matter_storage: ['matter', 'err', 'storage failed: {0}'],
  matter_no_code: ['matter', 'err', 'no pairing code'],
  matter_no_memory: ['matter', 'err', 'out of memory'],
  matter_failed: ['matter', 'err', 'failed to start: {0}'],
}

const makeEvent = (s: Demo, at: number, boot: number, code: string, args: string[]): DeviceEvent => {
  const [kind, level, en] = EV[code] ?? ['boot', 'info', '{0}']
  return {
    seq: (s.events.at(-1)?.seq ?? 0) + 1,
    since_s: boot ? 30 : Math.max(0, Math.floor((at - s.bootAt) / 1000)),
    boot,
    at: Math.floor(at / 1000),
    kind,
    code,
    level,
    args,
    text: en.replace(/\{(\d)\}/g, (_, i: string) => args[Number(i)] ?? ''),
  }
}

/** A bridge that has run for hours has a past: events of this start and the ones before, and log lines. */
export function history(s: Demo, now: number) {
  const D = 86_400_000
  const comp = `${s.cfg.satellite || '192.168.1.20'}:16622`
  const before: [number, number, string, string[]][] = [
    [5 * D, 2, 'started', ['power']],
    [5 * D - 9000, 2, 'companion_connected', [comp, 'LAN']],
    [4 * D, 2, 'lan_dead', []],
    [4 * D - 40_000, 2, 'lan_back', []],
    [3 * D, 2, 'pair_asks', ['192.168.1.77']],
    [3 * D - 60_000, 2, 'pair_refused', ['192.168.1.77']],
    [D + 3_600_000, 2, 'companion_lost', ['closed']],
    [D + 3_590_000, 2, 'companion_connected', [comp, 'LAN']],
    [D, 1, 'crashed', ['watchdog']],
    [D - 9000, 1, 'companion_connected', [comp, 'LAN']],
  ]
  for (const [ago, boot, code, args] of before) {
    const at = s.bootAt - ago
    s.events.push(makeEvent(s, at, boot, code, args))
  }
  const past: [number, string, string[], string][] = [
    [41, 'companion_lost', ['closed'], 'W sat: connection lost, reconnecting'],
    [42, 'companion_connected', [`${s.cfg.satellite}:16622`, 'LAN'], `I sat: connected to ${s.cfg.satellite}:16622`],
    [97, 'pair_accepted', ['192.168.1.31'], 'I dock: 192.168.1.31 paired'],
    [
      133,
      'wlan_roam',
      [s.cfg.wifi_ssid, '02:00:00:00:01:02', '6', '02:00:00:00:01:01'],
      'I net: WLAN roamed to channel 6',
    ],
  ]
  for (const [min, code, args, line] of past) {
    const at = s.bootAt + min * 60_000
    if (at >= now || (code === 'wlan_roam' && !s.cfg.wifi_ssid) || (code.startsWith('comp') && !s.cfg.satellite))
      continue
    s.events.push(makeEvent(s, at, 0, code, args))
    logLine(s, at, line)
  }
  // in time order, numbered as the firmware numbers them
  s.events.sort((a, b) => (a.at ?? 0) - (b.at ?? 0))
  s.events.forEach((e, i) => (e.seq = i + 1))
}

/** Seconds after the start of the bridge. */
const up = (s: Demo, now: number) => Math.max(0, Math.floor((now - s.bootAt) / 1000))

/** The flash ring keeps 8064 events at least. */
export function event(s: Demo, now: number, code: string, ...args: string[]) {
  s.events = [...s.events, makeEvent(s, now, 0, code, args)].slice(-8064)
}

/** GET /api/events?before=&since=&n= as the firmware answers it: newest first. */
export function eventPage(s: Demo, q: URLSearchParams) {
  const before = Number(q.get('before')) || Infinity
  const since = Number(q.get('since')) || 0
  const n = Math.min(Number(q.get('n')) || 200, 200)
  const events: DeviceEvent[] = []
  for (let i = s.events.length - 1; i >= 0 && events.length < n; i--) {
    const e = s.events[i]
    if (!e || e.seq >= before) continue
    if (e.at && e.at < since) break
    events.push(e)
  }
  const oldest = s.events[0]?.seq ?? 0
  const newest = s.events.at(-1)?.seq ?? 0
  return { events, oldest, newest, total: oldest ? newest - oldest + 1 : 0, store: 'flash' as const }
}

export function logLine(s: Demo, now: number, text: string) {
  const d = new Date(now)
  const ms = String(d.getMilliseconds()).padStart(3, '0')
  const line = `${pad(d.getHours())}:${pad(d.getMinutes())}:${pad(d.getSeconds())}.${ms} ${text}`
  s.log = [...s.log, line].slice(-120)
  return line
}

/** What a fresh start writes, also after a simulated restart. */
export function boot(s: Demo, now: number, reason = s.bootReason) {
  s.bootAt = now
  s.bootReason = reason
  s.downUntil = 0
  s.ui = []
  s.inputs = []
  s.seq = 0
  s.ota = null
  s.join = null
  s.joinState = ''
  // like the firmware: the events stay in flash and come back as an earlier boot's
  s.events = s.events.map((e) => ({ ...e, boot: e.boot + 1 }))
  if (s.log.length) s.prev = s.log
  s.log = []
  s.since = {}
  s.matter.running = s.matter.on
  const nx = s.slots.find((x) => x.next && x.version)
  if (nx?.version) {
    s.slots = s.slots.map((x) => ({
      ...x,
      running: x.slot === nx.slot,
      state: x.slot === nx.slot && x.state === 'pending' ? 'valid' : x.state,
    }))
    s.firmware = nx.version
  }
  event(s, now, ['crash', 'watchdog', 'brownout'].includes(reason) ? 'crashed' : 'started', reason)
  if (s.cfg.wifi_ssid) event(s, now, 'wlan_joined', s.cfg.wifi_ssid, '02:00:00:00:01:01', '6')
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
  const last = ms(lines[lines.length - 1] ?? '')
  for (const l of lines) logLine(s, now - last + ms(l), l)
}

const lanIp = (s: Demo) => (s.cfg.lan.dhcp || !s.cfg.lan.ip ? LAN_IP : s.cfg.lan.ip)

/** Any address in the example LAN answers, everything else (the internet) does not: the demo has no network. */
export const reachable = (addr: string) => /^(wss?:\/\/)?192\.168\.1\.\d{1,3}(?=[:/]|$)/.test(addr)
/** Companion runs on these two hosts of the example LAN, so a typo in the address finds nothing. */
export const companionAt = (addr: string) =>
  ['192.168.1.20', '192.168.1.21'].includes(addr.replace(/^wss?:\/\//, '').replace(/[:/].*$/, ''))

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
      error: ok || busy ? '' : view.lang === 'de' ? 'keine Verbindung' : 'no connection',
      since_s: Math.max(0, Math.floor((t0 + (busy ? 0 : connectingMs) - s.bootAt) / 1000)),
      iface: ok ? 'lan' : undefined,
    })
  })
  if (s.cfg.satellite) {
    const t0 = sinceOf('satellite')
    const busy = now - t0 < connectingMs
    // Companion also runs on a computer paired over the dock, so its Satellite port answers too.
    const ip = s.cfg.satellite.replace(/:.*$/, '')
    const paired = s.docks.find((d) => d.ip === ip && d.kind === 'companion')
    const ok = (companionAt(s.cfg.satellite) || !!paired) && !busy
    out.push({
      kind: 'companion',
      host: s.cfg.satellite,
      name: !ok ? '' : (paired?.name ?? (ip === '192.168.1.21' ? 'Backup Companion' : COMPANION.name)),
      via: 'satellite',
      connected: ok,
      connecting: busy,
      chosen: false,
      drawing: false,
      error: ok || busy ? '' : view.lang === 'de' ? 'keine Antwort auf Port 16622' : 'no answer on port 16622',
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
    c.host === 'satellite' ? x.via === c.host : c.host ? x.host === c.host && of(x) === c.controller : false,
  )
  if (!c.host) chosen = out.find((x) => of(x) === c.controller && x.connected)
  if (chosen) chosen.chosen = true
  const draws = chosen?.connected && !covered(s, now)
  if (draws && chosen) chosen.drawing = true
  return out
}

/** The deck shows the bridge's own screen instead of the host's picture. */
/** db_ui_menu_open: the menu, the password, the reset question or a login request is on the deck. */
const overlay = (s: Demo, now: number) =>
  ui(s, now, (u) => !!u.deck_is_open()) || now < s.pwUntil || now < s.resetUntil || s.loginReq?.state === 'pending'
const covered = (s: Demo, now: number) => overlay(s, now) || !!s.ota || !!s.request

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
  const ap = c.wifi_ssid
    ? (WIFI.find((w) => w.bssid === c.wifi_bssid) ?? WIFI.find((w) => w.ssid === c.wifi_ssid))
    : undefined
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
    menu_open: overlay(s, now),
    screen_off: false,
    brightness: c.brightness,
    volume: c.volume,
    screen_off_s: c.screen_off_s,
    screen_dim: c.screen_dim,
    max_decks: c.max_decks,
    speaker: !!c.speaker,
    boot: { reason: s.bootReason, at: Math.floor(s.bootAt / 1000) },
    setup: { active: !c.setup_done || s.setupStep > 0, step: s.setupStep, rerun: c.setup_done && s.setupStep > 0 },
    device: deck(s),
    decks: [{ ...deck(s), slot: 0, number: 1, writer: w, brightness: c.brightness, locked: false }],
    decks_over_limit: 0,
    https_custom: s.tls,
    https_custom_unread: false,
    https_ca: s.tls ? '' : '7C2E 91A4 D06B 3F58',
    network: {
      lan: { cable: true, dead: false, ip: lanIp(s) },
      wlan: {
        ssid: c.wifi_ssid,
        ip: c.wifi_ssid ? (c.wlan.dhcp || !c.wlan.ip ? WLAN_IP : c.wlan.ip) : '',
        bssid: c.wifi_ssid ? (ap?.bssid ?? '02:00:00:00:09:01') : '',
        channel: c.wifi_ssid ? (ap?.channel ?? 11) : 0,
        lock: c.wifi_bssid,
        backup: false,
        rssi: c.wifi_ssid ? (ap?.rssi ?? -58) - Math.round(Math.abs(Math.sin(now / 9000)) * 6) : 0,
        join: s.joinState,
        join_error: s.joinError,
      },
      ap: { on: false, ssid: '' },
    },
    sources: list,
    matter: { ...s.matter, controllers: 0, pairing: s.matter.running },
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
      usb_recoveries: s.recoveries ?? 0,
      usb_speed: deck(s).usb_speed,
      inputs: s.seq,
      inputs_dropped: 0,
    },
    boot_ms: { deck: 412, lan_link: 2688, wlan_ip: 5724, lan_ip: 6692, companion: 7439, host_draws: 7665 },
    memory: {
      heap_free: 351_000 - (u % 97) * 40,
      heap_min_free: 289_440,
      psram_free: 32_180_000,
      heap_largest: 208_896,
      heap_lp_free: 9_216,
      psram_largest: 30_932_992,
      tasks: 37,
      sockets: 9,
      sockets_max: 32,
    },
    temp_c: 47,
  }
}

/** The web socket's `state`: the status without uptime, signal and counters. */
export function wsState(s: Demo, now: number) {
  const { uptime_s: _u, counters: _c, memory: _m, ...rest } = status(s, now)
  const wlan = rest.network?.wlan
  if (wlan) delete wlan.rssi
  return rest
}

export const config = (s: Demo): Config => ({
  ...s.cfg,
  token_set: s.tokens.length > 0 && s.cfg.token_set,
  matter: s.matter.on,
})

export const LIVE: (keyof ConfigPatch)[] = [
  'brightness',
  'volume',
  'speaker',
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
  'trusted_proxy',
  'menu_chord',
  'menu_hold_ms',
  'lang',
  'max_decks',
  'screen_off_s',
  'screen_dim',
  'star_off',
  'star_fw',
  'star_until',
  'wifi2_ssid',
  'wifi2_pass',
  'matter',
]

const NAME = /^[A-Za-z0-9](?:[A-Za-z0-9-]{0,29}[A-Za-z0-9])?$/

/** `pcm`: what the board's speaker plays, at TONE_RATE. */
export type Reply = { status: number; body: unknown; pcm?: Int16Array }
type Api = typeof api
/** A 200 reply typed as the page reads it. `more` holds fields the firmware sends that the page does not read. */
export const res = <K extends keyof Api>(_route: K, body: Awaited<ReturnType<Api[K]>>, more: object = {}): Reply => ({
  status: 200,
  body: body && typeof body === 'object' && !Array.isArray(body) ? { ...body, ...more } : body,
})
const bad = (status: number, error: string, code?: string): Reply => ({ status, body: { ok: false, error, code } })

/** PUT /api/config. A change outside LIVE restarts the bridge, like the firmware. */
export function putConfig(s: Demo, now: number, p: ConfigPatch): Reply {
  if (p.name !== undefined && !NAME.test(p.name)) return bad(400, 'name: 1 to 31 letters, digits and dashes')
  if (p.admin_pass !== undefined && (!p.admin_pass || passwordError(p.admin_pass)))
    return bad(400, 'admin_pass: 6 to 23 characters')
  if (p.servers && p.servers.some((u) => u && !/^wss?:\/\/\S+$/.test(u))) return bad(400, 'servers: ws:// or wss://')
  if (p.brightness !== undefined && (p.brightness < 0 || p.brightness > 100)) return bad(400, 'brightness: 0 to 100')
  if (p.volume !== undefined && (p.volume < 0 || p.volume > 100)) return bad(400, 'volume: 0 to 100 expected')
  const old = s.cfg
  const next: Config = {
    ...old,
    ...Object.fromEntries(
      Object.entries(p).filter(
        ([k]) => !['wifi_pass', 'wifi2_pass', 'admin_pass', 'token', 'lan', 'wlan', 'matter'].includes(k),
      ),
    ),
    lan: { ...old.lan, ...p.lan },
    wlan: { ...old.wlan, ...p.wlan },
  }
  if (p.wifi_pass !== undefined) next.wifi_pass_set = !!p.wifi_pass || !next.wifi_ssid
  if (p.wifi2_pass !== undefined) next.wifi2_pass_set = !!p.wifi2_pass
  if (p.token !== undefined) next.token_set = !!p.token
  if (p.satellite !== undefined && p.satellite !== old.satellite) {
    next.satellite_auto = !!p.satellite
    s.since.satellite = now
  }
  for (const u of next.servers) if (u && !old.servers.includes(u)) s.since[u] = now
  if (p.controller !== undefined || p.host !== undefined) {
    ui(s, now, (u) => u.deck_close())
  }
  const restart =
    Object.keys(p).some(
      (k) =>
        !LIVE.includes(k as keyof ConfigPatch) &&
        JSON.stringify((p as Record<string, unknown>)[k]) !==
          JSON.stringify((old as unknown as Record<string, unknown>)[k]),
    ) ||
    p.admin_pass !== undefined ||
    p.wifi_pass !== undefined ||
    (p.matter === false && s.matter.running)
  if (p.matter !== undefined) s.matter.on = p.matter
  if (p.matter) s.matter.running = true
  s.cfg = next
  // Like the firmware: a new password ends the other sessions, the browser that set it gets a fresh one.
  if (p.admin_pass !== undefined) {
    s.pass = p.admin_pass
    s.claimed = true
  }
  if (!restart) return res('putConfig', { ok: true, restart: false }, { config: config(s) })
  restartSoon(s, now)
  const sch = next.https_only ? 'https' : 'http'
  return res(
    'putConfig',
    { ok: true, restart: true, reach: [`${sch}://${lanIp(s)}`, `${sch}://${next.name}.local`] },
    { config: config(s) },
  )
}

export const RESTART_MS = 4500
export const isDown = (s: Demo, now: number) => !!s.downUntil && now >= s.downFrom && now < s.downUntil

export function restartSoon(s: Demo, now: number, ms = RESTART_MS) {
  s.downFrom = now + 1000
  s.downUntil = s.downFrom + ms
}

const u32 = (h: number[], i: number) => h.slice(i, i + 4).reduceRight((a, x) => a * 256 + x, 0)

const TARGETS = ['', 'p4', 'c6', 'web', 'bootloader', 'ptable']

/** What the bridge reads from a .dbfw header (tools/dbtools/dbfw.py) before it takes a file. The signature it
 *  checks too is left out: the demo has no key. */
export function dbfw(
  h: number[],
  len: number,
): { target: string; version: string; length: number; error?: string; code?: string } {
  const version = String.fromCharCode(...h.slice(12, 43)).replace(/\0.*$/s, '')
  const out = { target: TARGETS[h[5] ?? 0] ?? '', version, length: u32(h, 8) }
  if (len < 144) return { ...out, error: 'file too short', code: 'ota_too_short' }
  if (String.fromCharCode(...h.slice(0, 4)) !== 'DBFW' || h[4] !== 1)
    return { ...out, error: 'not a dbfw file', code: 'ota_not_dbfw' }
  // The page sends only the header to check a file (144 bytes); an upload brings all of it.
  if (out.length + 144 !== len && len !== 144)
    return { ...out, error: 'the length in the header does not match', code: 'ota_not_dbfw' }
  if (!out.target) return { ...out, error: 'not for this device', code: 'ota_wrong_target' }
  return out
}

/** Turns, presses and taps on what the deck shows. `origin` 'web' is a person at the live deck. */
export function input(s: Demo, now: number, b: InputBody, origin: 'web' | 'deck' = 'web'): Reply {
  if (isDown(s, now)) return bad(503, 'restarting')
  const w = writer(s, now)
  const log = (kind: string, index?: number, value?: number) => {
    s.seq++
    s.inputs = [...s.inputs, { seq: s.seq, at_ms: now - s.bootAt, origin, kind, index, value, writer: w }].slice(-2000)
  }
  const f = s.faces
  const clamp = (n: number, lo = 0, hi = 100) => Math.max(lo, Math.min(hi, n))
  if ('key' in b) {
    if (b.key < 0 || b.key > 7) return bad(400, 'key out of range')
    const act = b.action ?? 'press'
    if (act !== 'up') log('key_down', b.key)
    if (act !== 'down') log('key_up', b.key)
    if (w === 'ui') {
      if (act !== 'up') uiInput(s, now, INPUT.keyDown, b.key)
      if (act !== 'down') uiInput(s, now, INPUT.keyUp, b.key)
      return ok()
    }
    if (act === 'up') return ok()
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
      if (w === 'ui') {
        uiInput(s, now, INPUT.turn, i, n)
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
    if (w === 'ui') {
      if (act !== 'up') uiInput(s, now, INPUT.encDown, i)
      if (act !== 'down') uiInput(s, now, INPUT.encUp, i)
    } else if (act !== 'up') dialPress(s, w, i)
    return ok()
  }
  if ('touch' in b) {
    const { x, y } = b
    if (y < STRIP.y || y >= STRIP.y + STRIP.h || x < 0 || x >= WIDTH)
      return bad(400, 'touch outside the strip', 'strip_outside')
    log(`touch_${b.touch}`, undefined, x)
    if (w === 'ui') {
      const kind = b.touch === 'swipe' ? INPUT.swipe : b.touch === 'long' ? INPUT.long : INPUT.tap
      uiInput(s, now, kind, 0, 0, x, y, b.x2 ?? x)
      return ok()
    }
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
  if (w === 'satellite' || isCompDock(s, w)) {
    if (f.page === 0) f.mute[i] = !f.mute[i]
    else if (i < 2 || i === 3) f.lights[i === 3 ? 2 : i] = !f.lights[i === 3 ? 2 : i]
  } else if (w.startsWith('dock')) f.appMute[i] = !f.appMute[i]
  else if (w.startsWith('server')) f.dial[i] = 0
}

function studioKey(f: Faces, k: number) {
  if (f.page === 0) {
    if (k <= 2) f.scene = k
    if (k === 3) f.pause = !f.pause
    if (k === 4) f.mic = !f.mic
    if (k === 5) f.title = !f.title
    if (k === 6) f.rec = !f.rec
    if (k === 7) f.page = 1
    return
  }
  if (k <= 2) f.lights[k] = !f.lights[k]
  if (k === 3) f.brb = !f.brb
  if (k === 4) f.wall = !f.wall
  if (k === 5) {
    f.lights = f.lights.map(() => false)
    f.wall = false
  }
  if (k === 7) f.page = 0
}

function appKey(f: Faces, k: number) {
  if (k === 0) f.play = !f.play
  if (k === 2) f.lights[0] = !f.lights[0]
  if (k === 3) f.appMute[3] = !f.appMute[3]
  if (k === 5) f.focus = !f.focus
  if (k === 6) f.lights[1] = !f.lights[1]
}

function lanOctets(s: Demo) {
  const l = s.cfg.lan
  return [l.ip || lanIp(s), l.mask || '255.255.255.0', l.gateway || '192.168.1.1', l.dns || '192.168.1.1'].map((a) =>
    a.split('.').map(Number),
  )
}

const BARE: UiModel = { fields: {}, sources: [], lan_cfg: [] }
const CTRL = { server: 0, companion: 1, app: 2 } as const
const VIA = { server: 0, satellite: 1, dock: 2 } as const

/** What db_ui.c model() fills in from the bridge, here from the demo's state. */
function uiModel(s: Demo, now: number): UiModel {
  // model_sources' order: Satellite, dock hosts, servers.
  const rank = { satellite: 0, dock: 1, server: 2 } as const
  const all = sources(s, now)
  const list = all
    .filter((x) => x.connected && x.kind !== 'unknown')
    .sort((a, b) => rank[a.via ?? 'server'] - rank[b.via ?? 'server'])
  const sat = all.find((x) => x.via === 'satellite')
  const ip = lanIp(s)
  const wlan = s.cfg.wifi_ssid ? (s.cfg.wlan.dhcp || !s.cfg.wlan.ip ? WLAN_IP : s.cfg.wlan.ip) : ''
  const login = s.loginReq?.state === 'pending'
  return {
    fields: {
      controller: (CTRL as Record<string, number>)[s.cfg.controller] ?? CTRL.companion,
      lan_link: true,
      lan_up: true,
      lan_dhcp: s.cfg.lan.dhcp,
      lan_ip: ip,
      wlan_chip: true,
      wlan_up: !!wlan,
      wlan_rssi: -47,
      wlan_ip: wlan,
      wifi_ssid: s.cfg.wifi_ssid,
      request: s.request?.ip ?? '',
      request_name: s.request?.name ?? '',
      menu_chord: s.cfg.menu_chord,
      menu_hold_ms: s.cfg.menu_hold_ms,
      server_url: s.cfg.servers.find(Boolean) ?? '',
      // as db_ui.c: the address only while Companion comes over Satellite, "not reachable" once an attempt failed
      satellite: !s.cfg.host || s.cfg.host === 'satellite' ? s.cfg.satellite : '',
      english: s.cfg.lang === 'en',
      sat_failed: !!sat?.error,
      web_pass: s.pass,
      fw: s.firmware,
      wlan_fw: '3.0.10',
      brightness: s.cfg.brightness,
      update_pct: s.ota ? otaPct(s, now) : -1,
      update_wlan: false,
      setup_done: s.cfg.setup_done,
      setup_step: s.setupStep,
      picked: !!s.cfg.host || s.cfg.controller !== 'companion',
      show_pass: now < s.pwUntil,
      reset_ask: now < s.resetUntil,
      login_ip: login ? '192.168.1.77' : '',
      login_who: login ? 'Firefox on Linux' : '',
    },
    sources: list.map((x) => ({
      kind: CTRL[x.kind as keyof typeof CTRL],
      via: VIA[x.via ?? 'server'],
      chosen: x.chosen,
      label: x.kind === 'server' ? x.host.replace(/^wss?:\/\//, '').split('/')[0] || x.host : x.name || x.host,
      id: x.via === 'satellite' ? 'satellite' : x.host,
    })),
    lan_cfg: lanOctets(s),
  }
}

/** Runs the firmware's device UI (deck-ui.ts) on this bridge's menu state. Opening, closing and the tick need no
 *  model but the time, as in db_ui.c. */
function ui<T>(s: Demo, now: number, run: Parameters<typeof withUi<T>>[3], m: UiModel = BARE): T {
  const [r, menu] = withUi(s.ui, m, now, run)
  s.ui = menu
  return r
}

// Every press in the demo comes from the web UI, so Neustart, Setup-WLAN, Speichern and the factory reset do
// nothing, as on a real bridge (db_ui screens.c, from_deck).
function uiInput(s: Demo, now: number, kind: number, index: number, value = 0, x = 0, y = 0, x2 = 0) {
  const m = uiModel(s, now)
  const [what, v] = ui(s, now, (u) => [DO[u.deck_input(kind, index, value, x, y, x2, 0)], u.deck_act_value()], m)
  const src = m.sources[v]
  if (what === 'source' && src) {
    s.cfg = {
      ...s.cfg,
      controller: (Object.keys(CTRL) as (keyof typeof CTRL)[])[src.kind] ?? 'companion',
      host: src.id,
      setup_done: true,
    }
  }
  if (what === 'answer' && s.request) dock(s, now, { accept: v === 1, ip: s.request.ip })
  if (what === 'brightness') s.cfg = { ...s.cfg, brightness: Math.max(0, Math.min(100, s.cfg.brightness + v)) }
  if (what === 'hide_password') s.pwUntil = 0
  if (what === 'cancel_reset') s.resetUntil = 0
  if (what === 'login' && s.loginReq) s.loginReq.state = v ? 'accepted' : 'denied'
  if (what === 'setup_done') s.cfg = { ...s.cfg, setup_done: true }
  if (what === 'lang') s.cfg = { ...s.cfg, lang: s.cfg.lang === 'en' ? 'de' : 'en' }
  if (what === 'show_password') s.pwUntil = now + 30_000
}

export const openMenu = (s: Demo, now: number) => ui(s, now, (u) => u.deck_open())

/** db_ui_close_menu: also hides the password, the reset question and a login request. */
export function closeMenu(s: Demo, now: number) {
  ui(s, now, (u) => u.deck_close())
  s.pwUntil = 0
  s.resetUntil = 0
  if (s.loginReq?.state === 'pending') s.loginReq.state = 'denied'
}

// What the deck shows: one face per key and one for the strip. deck.ts paints them; equal faces share a picture.
export interface KeyFace {
  t: string
  sub?: string
  bg: string
  fg?: string
  /** The website's key look (site/remotion/hw.tsx Face): this icon over t. */
  site?: SiteIcon
  /** Colour of the rec dot. */
  dot?: string
  /** t is the time, drawn large. */
  clock?: true
}
export type SiteIcon =
  | 'cam'
  | 'slides'
  | 'pause'
  | 'mic'
  | 'mic-off'
  | 'title'
  | 'rec'
  | 'next'
  | 'back'
  | 'light'
  | 'coffee'
  | 'palette'
  | 'power'
  | 'play'
  | 'skip'
  | 'camera'
  | 'moon'
  | 'sun'
export interface StripZone {
  t: string
  v: number
  text: string
  off?: boolean
  color: string
}
/** What a host draws, or `ui`: the bridge's own screen, a hash of what db_ui built. */
export type Face = { keys: KeyFace[]; strip: { zones: StripZone[] } } | { ui: number }

const pad = (n: number) => String(n).padStart(2, '0')

const OFF = '#26282c'
const GREY = '#d4d7dc'
const C = {
  live: { on: '#e5322b', fg: '#ff7b70', ink: '#fff' },
  audio: { on: '#1f9d55', fg: '#5fd08c', ink: '#fff' },
  light: { on: '#f5a524', fg: '#f5b748', ink: '#1a1200' },
  show: { on: '#3a78ee', fg: '#82a9ff', ink: '#fff' },
  nav: { on: '#3a3d43', ink: '#fff' },
}
const key = (t: string, site: SiteIcon, c: keyof typeof C, on = false): KeyFace =>
  on ? { t, site, bg: C[c].on, fg: C[c].ink } : { t, site, bg: OFF, fg: GREY }
const mic = (on: boolean) => (on ? key('Mic', 'mic', 'audio', true) : key('Muted', 'mic-off', 'live'))

function faceEn(s: Demo, now: number): Face {
  const f = s.faces
  const w = writer(s, now)
  const d = new Date(now)
  const clock = `${pad(d.getHours())}:${pad(d.getMinutes())}`
  // The bridge's own screen: the firmware's db_ui code draws it (deck-ui.ts).
  if (w === 'ui') return { ui: ui(s, now, (u) => u.deck_screen(s.ota ? 1 : 0), uiModel(s, now)) }
  if (w === 'satellite' || isCompDock(s, w)) {
    if (f.page === 0)
      return {
        keys: [
          ...(['CAM 1', 'CAM 2', 'Slides'] as const).map((t, i) =>
            key(t, i < 2 ? 'cam' : 'slides', 'live', f.scene === i),
          ),
          key('Pause', 'pause', 'show', f.pause),
          mic(f.mic),
          key('Title', 'title', 'show', f.title),
          { ...key('REC', 'rec', 'live', f.rec), dot: f.rec ? '#fff' : C.live.fg },
          key('Lights', 'next', 'nav'),
        ],
        strip: {
          zones: ['Master', 'Mic', 'Music', 'Room'].map((t, i) => ({
            t,
            v: f.vol[i] ?? 0,
            text: f.mute[i] ? 'muted' : String(f.vol[i]),
            off: f.mute[i],
            color: C.audio.fg,
          })),
        },
      }
    return {
      keys: [
        ...['Front', 'Fill', 'Rim'].map((t, i) => key(t, 'light', 'light', f.lights[i])),
        key('BRB', 'coffee', 'show', f.brb),
        key('Wall', 'palette', 'light', f.wall),
        { ...key('All off', 'power', 'nav'), fg: C.live.fg },
        { t: clock, sub: 'Clock', bg: OFF, fg: GREY, clock: true },
        key('Show', 'back', 'nav'),
      ],
      strip: {
        zones: [
          {
            t: 'Front',
            v: f.light[0] ?? 0,
            text: f.lights[0] ? `${f.light[0]}%` : 'off',
            off: !f.lights[0],
            color: C.light.fg,
          },
          {
            t: 'Fill',
            v: f.light[1] ?? 0,
            text: f.lights[1] ? `${f.light[1]}%` : 'off',
            off: !f.lights[1],
            color: C.light.fg,
          },
          { t: 'Colour', v: (((f.light[2] ?? 4800) - 2900) / 4100) * 100, text: `${f.light[2]} K`, color: '#9fd3ff' },
          {
            t: 'Rim',
            v: f.light[3] ?? 0,
            text: f.lights[2] ? `${f.light[3]}%` : 'off',
            off: !f.lights[2],
            color: C.light.fg,
          },
        ],
      },
    }
  }
  if (w.startsWith('dock'))
    return {
      keys: [
        f.play ? key('Pause', 'pause', 'show', true) : key('Play', 'play', 'show'),
        key('Next', 'skip', 'show'),
        key('Light', 'light', 'light', f.lights[0]),
        mic(!f.appMute[3]),
        key('Shot', 'camera', 'show'),
        key('Focus', 'moon', 'show', f.focus),
        key('Desk', 'sun', 'light', f.lights[1]),
        { t: clock, sub: 'Clock', bg: OFF, fg: GREY, clock: true },
      ],
      strip: {
        zones: ['System', 'Music', 'Browser', 'Mic'].map((t, i) => ({
          t,
          v: f.appVol[i] ?? 0,
          text: f.appMute[i] ? 'muted' : `${f.appVol[i]}%`,
          off: f.appMute[i],
          color: i === 3 ? C.audio.fg : C.show.fg,
        })),
      },
    }
  return {
    keys: f.presses.map((n, i) => ({ t: String(n), sub: `Key ${i + 1}`, bg: '#14262a', fg: '#7fe0d0' })),
    strip: {
      zones: f.dial.map((n, i) => ({
        t: `Dial ${i + 1}`,
        v: 50 + Math.max(-50, Math.min(50, n)),
        text: String(n),
        color: '#7fe0d0',
      })),
    },
  }
}

/** The language of the host pages on the virtual deck, set by the demo page. */
export const view = { lang: 'en' as 'en' | 'de' }

const DE: Record<string, string> = {
  Clock: 'Uhr',
  Mic: 'Mikro',
  Music: 'Musik',
  Master: 'Summe',
  Room: 'Saal',
  Slides: 'Folien',
  Title: 'Titel',
  muted: 'stumm',
  Front: 'Haupt',
  Rim: 'Hinten',
  Lights: 'Licht',
  Fill: 'Fülllicht',
  Colour: 'Farbe',
  off: 'aus',
  Next: 'Weiter',
  Wall: 'Wand',
  'All off': 'Alle aus',
  Light: 'Licht',
  Muted: 'Stumm',
  Shot: 'Foto',
  Focus: 'Fokus',
  Desk: 'Tisch',
}

const de = (x: string) => DE[x] ?? x.replace(/^Key (\d+)$/, 'Taste $1').replace(/^Dial (\d+)$/, 'Regler $1')

export function face(s: Demo, now: number): Face {
  const f = faceEn(s, now)
  if (view.lang === 'en' || 'ui' in f) return f
  return {
    keys: f.keys.map((k) => ({ ...k, t: de(k.t), sub: k.sub && de(k.sub) })),
    strip: { zones: f.strip.zones.map((z) => ({ ...z, t: de(z.t), text: de(z.text) })) },
  }
}

export const OTA_MS = 9000
export const otaPct = (s: Demo, now: number) =>
  s.ota ? Math.min(100, Math.floor(((now - s.ota.at) / OTA_MS) * 100)) : 0

export interface Out {
  ota?: { target: string; state: string; pct: number; error?: string }
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
  ui(s, now, (u) => u.deck_tick()) // closes an idle menu, ends a confirm
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
      event(s, now, 'update_restart', 'p4')
      s.ota = null
      restartSoon(s, now)
    } else out.ota = { target: 'p4', state: 'download', pct }
  }
  if (s.join && now >= s.join.until) {
    const j = s.join
    s.join = null
    const known = WIFI.find((w) => w.ssid === j.ssid)
    // A name the scan did not list is a hidden network: it joins like any other with a good password.
    const okPass = known?.open || j.pass.length >= 8
    // Neighbour is too far away to answer; every other network takes any password of 8 or more characters.
    if (okPass && j.pass !== 'wrongpass' && j.ssid !== 'Neighbour') {
      s.joinState = 'ok'
      s.joinError = ''
      s.cfg = { ...s.cfg, wifi_ssid: j.ssid, wifi_bssid: j.bssid, wifi_pass_set: !known?.open }
      event(s, now, 'wlan_joined', j.ssid, j.bssid || '02:00:00:00:01:01', '6')
      say(`I net: WLAN ${j.ssid} joined, ${WLAN_IP}`)
    } else {
      s.joinState = 'failed'
      s.joinError = j.ssid === 'Neighbour' ? 'no answer from the access point' : 'wrong password'
      say(`W net: WLAN ${j.ssid}: ${s.joinError}`)
    }
  }
  if (s.loginReq?.state === 'pending') {
    if (now - s.loginReq.at > 4000) s.loginReq.state = 'accepted'
  }
  if (s.resetUntil && now >= s.resetUntil) s.resetUntil = 0
  // During setup an Elgato app finds the bridge a few seconds into the source step. Pairing is open until the first
  // computer, so it pairs without a question, and it takes the deck while nobody else has it.
  const appAt = s.setupSrcAt
  if (!s.cfg.setup_done && appAt && now - appAt > 5000 && !s.docks.length) {
    s.docks = [{ ip: '192.168.1.31', mac: '02:00:5e:10:00:31', name: 'edit-pc', kind: 'app', connected: true }]
    if (!s.cfg.host) s.cfg = { ...s.cfg, controller: 'app', host: '192.168.1.31' }
    say('I dock: 192.168.1.31 paired, pairing was open')
  }
  // Like the firmware, an unanswered pairing request lapses after a minute.
  if (s.request && now - s.request.at > 60_000) s.request = null
  const l = s.later
  if (l && now >= l.at && !s.request) {
    s.later = null
    // While pairing is open a computer pairs without a question (dock_paired_only).
    if (now < s.pairOpenUntil) {
      s.docks = [...s.docks.filter((d) => d.ip !== l.ip), { ...l, mac: '02:00:5e:10:00:53', connected: true }]
      event(s, now, 'host_connected', 'app', l.ip)
      say(`I dock: ${l.ip} paired, pairing was open`)
    } else {
      s.request = { ip: l.ip, name: l.name, at: now }
      event(s, now, 'pair_asks', l.ip)
      say(`I dock: ${l.ip} asks to pair`)
    }
  }
  if (s.otaFail && now >= s.otaFail.at) {
    out.ota = { target: 'unknown', state: 'error', pct: -1, error: s.otaFail.why }
    say(`E ota: download failed: ${s.otaFail.why}`)
    s.otaFail = null
  }
  // Nobody stands at the demo's deck to tap "Zurücksetzen" twice, so after a few seconds someone does.
  if (s.resetUntil && now > s.resetUntil - 55_000) factoryReset(s, now)
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
    if (b.pairing > 0 && !s.later && !s.docks.some((d) => d.ip === '192.168.1.53'))
      s.later = { ip: '192.168.1.53', name: 'studio-mac', kind: 'app', at: now + 6000 }
    return ok()
  }
  if (b.forget !== undefined) {
    // A computer that is forgotten keeps looking for the dock, so it asks to pair again a few seconds later.
    const gone = s.docks.find((d) => d.ip === b.forget)
    if (gone && !s.later)
      s.later = { ip: gone.ip, name: gone.name, kind: gone.kind === 'companion' ? 'companion' : 'app', at: now + 5000 }
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
      event(s, now, 'pair_accepted', r.ip)
    } else {
      s.denied = [...s.denied, { ip: r.ip, name: r.name, since_s: up(s, now) }].slice(-4)
      event(s, now, 'pair_refused', r.ip)
    }
    return ok()
  }
  return bad(400, 'nothing to do')
}

export function makeToken(s: Demo, name: string, right: 'control' | 'full'): Reply {
  if (!/^[A-Za-z0-9_-]{1,23}$/.test(name)) return bad(400, 'name: 1 to 23 letters, digits, - or _')
  const rest = s.tokens.filter((t) => t.name !== name)
  if (rest.length >= 8) return bad(409, 'at most 8 tokens')
  const abc = 'abcdefghjkmnpqrstuvwxyz23456789'
  const tok = Array.from(crypto.getRandomValues(new Uint8Array(40)), (b) => abc[b % abc.length]).join('')
  s.tokens = [...rest, { name, right }]
  return res('makeToken', { name, right, token: `dc_${tok}` })
}

/** db_ui_factory_reset: every setting goes, the bridge restarts into setup with a new password on the deck. */
export function factoryReset(s: Demo, now: number) {
  const f = fresh(now, { setup: true })
  const abc = 'abcdefghjkmnpqrstuvwxyz23456789'
  const pass = Array.from(crypto.getRandomValues(new Uint8Array(8)), (b) => abc[b % abc.length]).join('')
  Object.assign(s, { cfg: f.cfg, pass, docks: [], tokens: [], denied: [], request: null, later: null })
  Object.assign(s, {
    resetUntil: 0,
    pwUntil: 0,
    authed: false,
    claimed: false,
    requested: true,
    tls: false,
    pagePartition: null,
    matter: f.matter,
    setupStep: 0,
  })
  restartSoon(s, now)
}

export function startOta(s: Demo, now: number, version = LATEST): Reply {
  if (s.ota) return bad(409, 'an update runs already', 'busy')
  const running = s.slots.find((x) => x.running)?.slot ?? 1
  const slot = (running % s.slots.length) + 1
  s.ota = { at: now, version, slot }
  return ok()
}

export const latest = LATEST
export const companion = COMPANION
