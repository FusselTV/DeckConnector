// Every field the firmware may leave out is optional: the host simulator sends less.

type Kind = 'companion' | 'app' | 'server' | 'unknown'
export type Right = 'control' | 'full'

export interface DeckInfo {
  model: string
  serial: string
  firmware?: string
  /** False when the Elgato app and Companion's Elgato plugin cannot use the deck. */
  dock?: boolean
  /** The menu controls per menu_chord choice, for decks with controls that cannot be held. */
  chords?: Record<'auto' | 'keys' | 'encoders', { on: 'keys' | 'encoders' | 'off'; last: number }>
  keys?: number
  encoders?: number
  width?: number
  height?: number
  usb_speed?: 'high' | 'full'
  hub?: boolean
  surface?: number
  /** In `decks`: 0 to 3, kept per deck over replugs. It fixes the deck's Network Dock port and Satellite order. */
  slot?: number
  /** In `decks`: slot + 1. */
  number?: number
  /** In `decks`: what the deck shows now, 0 while the screen is off. */
  brightness?: number
}

export interface Source {
  kind: Kind
  host: string
  name?: string
  via?: 'satellite' | 'dock'
  connected: boolean
  chosen: boolean
  drawing: boolean
  connecting?: boolean
  error?: string
  /** When this state began, in seconds after the start (compare with uptime_s). */
  since_s?: number
  iface?: 'lan' | 'wlan'
  client?: string
}

interface PairHost {
  ip: string
  mac?: string
  name?: string
  kind?: Kind
  connected?: boolean
}

export interface Pairing {
  only_paired?: boolean
  request?: string | null
  request_name?: string | null
  open?: boolean
  open_s?: number
  auto_open?: boolean
  /** Hosts refused since the start; since_s is when, in seconds after the start. */
  denied?: { ip: string; name?: string; since_s?: number }[]
  hosts?: PairHost[]
}

export interface Network {
  lan?: { cable?: boolean; dead?: boolean; ip?: string }
  wlan?: {
    ssid?: string
    ip?: string
    bssid?: string
    channel?: number
    lock?: string
    /** `ssid` is the backup Wi-Fi, wifi2_ssid. */
    backup?: boolean
    rssi?: number
    join?: '' | 'testing' | 'ok' | 'failed'
    join_error?: string
  }
  ap?: { on?: boolean; ssid?: string; clients?: number }
}

export interface Counters {
  images?: number
  superseded?: number
  rejected?: number
  usb_errors?: number
  usb_recoveries?: number
  usb_speed?: 'high' | 'full'
  inputs?: number
  inputs_dropped?: number
}

export interface Status {
  id?: string
  name?: string
  firmware?: string
  wlan_firmware?: string
  api?: number
  simulated_input?: string
  https_fingerprint?: string
  uptime_s?: number
  controller?: string
  writer?: string
  /** The deck menu covers the deck. */
  menu_open?: boolean
  /** Every deck is dark after `screen_off_s` without input; the next input wakes them. */
  screen_off?: boolean
  /** This page reached the device over its setup Wi-Fi, which needs no password. */
  via_ap?: boolean
  device?: DeckInfo | null
  decks?: DeckInfo[]
  /** Decks plugged in but left dark by `max_decks`. */
  decks_over_limit?: number
  network?: Network
  sources?: Source[]
  pairing?: Pairing
  counters?: Counters
  boot_ms?: Record<string, number>
  memory?: Record<string, number>
  /** The chip's temperature in °C, missing until the first sample. */
  temp_c?: number
  setup?: { active?: boolean; step?: number; rerun?: boolean }
  /** HTTPS uses a certificate uploaded with PUT /api/tls instead of the device's own. */
  https_custom?: boolean
  /** The device CA's short fingerprint, also shown on the deck when the CA is downloaded. */
  https_ca?: string
  boot?: { reason?: string; at?: number | null }
}

export interface IpConf {
  dhcp: boolean
  ip: string
  mask: string
  gateway: string
  dns: string
}

export interface Config {
  name: string
  controller: string
  host: string
  brightness: number
  lan: IpConf
  wlan: IpConf
  wifi_ssid: string
  wifi_pass_set: boolean
  wifi_bssid: string
  /** Joined when the first Wi-Fi gives no link; the bridge goes back to the first once it is in reach. */
  wifi2_ssid: string
  wifi2_pass_set: boolean
  servers: string[]
  server_pins: string[]
  satellite: string
  /** False after a person removed `satellite` or said no on the deck: Companion is not moved there by itself. */
  satellite_auto: boolean
  satellite_pin: string
  https_only: boolean
  /** The one-time login code the deck shows as a QR code until setup is done. */
  qr_login: boolean
  ntp_server: string
  cors_origins: string
  /** A reverse proxy's IPv4 address: behind it, its X-Forwarded-For names the client. Empty: none. */
  trusted_proxy: string
  menu_chord: 'auto' | 'keys' | 'encoders' | 'off'
  menu_hold_ms: number
  /** What the deck's own screens speak. */
  lang: 'de' | 'en'
  usb_speed: 'auto' | 'full'
  max_decks: number
  /** 0: never, else 10 to 86400 seconds without input until the decks go dark. */
  screen_off_s: number
  dock_paired_only: boolean
  setup_done: boolean
  token_set: boolean
}

/** Keys PUT /api/config takes besides those of Config. */
interface ConfigSecrets {
  wifi_pass?: string
  wifi2_pass?: string
  admin_pass?: string
  token?: string
}
export type ConfigPatch = Partial<Omit<Config, 'lan' | 'wlan'>> &
  ConfigSecrets & {
    lan?: Partial<IpConf>
    wlan?: Partial<IpConf>
  }

export interface DeviceEvent {
  /** Seconds after the start of its boot. */
  since_s: number
  /** 0 this boot, 1 the one before, ... */
  boot?: number
  /** Unix time, null before the clock is set. */
  at: number | null
  kind: string
  text: string
}

export interface PutReply {
  ok: boolean
  restart?: boolean
  reach?: string[]
}

export interface Inspect {
  ok: boolean
  error?: string
  code?: string
  target?: 'p4' | 'c6' | 'web' | 'page' | 'bootloader' | 'ptable'
  version?: string
  length?: number
  sha256?: string
}

export interface Rect {
  x: number
  y: number
  w: number
  h: number
}
export interface Screen {
  writer?: string
  width?: number
  height?: number
  keys: Rect[]
  touch: Rect[]
  encoders?: number
  menu_chord?: { on: 'encoders' | 'keys' | 'off'; first: number; last: number; hold_ms: number }
  images: (Rect & { url: string })[]
}

export type InputBody =
  | { key: number; action?: 'press' | 'down' | 'up' }
  | { encoder: number; action?: 'press' | 'down' | 'up' | 'turn'; ticks?: number }
  | { touch: 'tap' | 'long' | 'swipe'; x: number; y: number; x2?: number; y2?: number }

export interface Slot {
  slot: number
  label: string
  state: 'valid' | 'pending' | 'invalid' | 'empty' | 'unknown'
  version?: string
  built?: string
  running: boolean
  next: boolean
  level?: number
  bootable?: boolean
  why_not?: string
}

export interface Frontend {
  source: 'builtin' | 'partition'
  api: number
  version?: string
  api_min?: number
  size?: number
}

export interface Route {
  method: string
  path: string
  access: 'open' | 'control' | 'full' | 'admin'
}

export interface Token {
  name: string
  right?: Right
  created?: string
  last_used?: string
}

export interface Passkey {
  id: number
  name: string
  /** The host name it was made on: it works only there. */
  rp: string
}

export interface Found {
  name: string
  host: string
  port: number
  version?: string
  surfaces?: number
}

export interface WifiAp {
  ssid: string
  bssid: string
  rssi: number
  channel: number
  open: boolean
}

export interface Metrics {
  every_s: number
  cpu: number[]
  heap: number[]
  psram: number[]
  /** The chip's temperature in °C; 0 where the sensor gave nothing. */
  temp?: number[]
  flash?: Record<string, { used: number; size: number }>
}

export interface Profile {
  routes?: { method: string; uri: string; count: number; avg_us: number; max_us: number }[]
  cpu_permille?: Record<string, number>
  stack_free?: Record<string, number>
}

/** The summary fields are missing (or `summary` is false) when the firmware cannot read the dump. */
export interface Coredump {
  size: number
  summary?: boolean
  task?: string
  pc?: string
  ra?: string
  sp?: string
  mcause?: number
  mtval?: string
  elf_sha256?: string
  firmware?: string
}

/** One entry of GET /api/input/log as the firmware sends it. */
export interface InputLogEntry {
  seq: number
  at_ms: number
  origin: 'deck' | 'web' | 'tool'
  kind: string
  index?: number
  value?: number
  writer?: string
  /** The deck's serial. */
  deck?: string
}

export interface InputEvent {
  at: number
  origin: 'deck' | 'web' | 'api'
  client?: string
  deck?: string
  kind: string
  key?: number
  delta?: number
  source?: string
}

export interface UpdateCheck {
  current: string
  latest: string | null
  url?: string
  notes?: string
  error?: string
  code?: string
}

export interface TlsInfo {
  subject: string
  issuer: string
  not_after: string
  sha256: string
}

export type WsMsg =
  | ({ t: 'state' } & Status)
  | { t: 'screen'; deck?: string; screen?: Screen }
  | { t: 'input'; kind: string; index?: number; value?: number; writer?: string; simulated?: boolean; deck?: string }
  | { t: 'ota'; target: string; state: string; pct: number; error?: string; code?: string }
  | { t: 'ping' }
  | { t: 'log'; text: string }
  | { t: 'result'; id: number; status: number; reply: unknown }
