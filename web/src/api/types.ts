// Every field the firmware may leave out is optional: old firmware and the host simulator send less.

export type Kind = 'companion' | 'app' | 'server' | 'unknown'
export type Right = 'control' | 'full'

export interface DeckInfo {
  model: string
  serial: string
  firmware?: string
  keys?: number
  encoders?: number
  width?: number
  height?: number
  usb_speed?: 'high' | 'full'
  hub?: boolean
  surface?: number
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

export interface PairHost {
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
  /** Hosts refused since the start (api-gaps #17), on firmware that lists them. */
  /** since_s: seconds after the start when the host was refused. */
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
  /** This page reached the device over its setup Wi-Fi, which needs no password. */
  via_ap?: boolean
  device?: DeckInfo | null
  decks?: DeckInfo[]
  network?: Network
  sources?: Source[]
  pairing?: Pairing
  counters?: Counters
  boot_ms?: Record<string, number>
  memory?: Record<string, number>
  setup?: { active?: boolean; step?: number; rerun?: boolean }
  /** Seconds the input lock stays open after PUT /api/input/arm. Only in GET /api/status. */
  https_custom?: boolean
  boot?: { reason?: string; at?: number | null }
  update?: { latest?: string; at?: string }
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
  wifi_bssid?: string
  servers: string[]
  server_pins: string[]
  satellite: string
  satellite_pin: string
  https_only: boolean
  /** The one-time login code the deck shows as a QR code until setup is done. */
  qr_login?: boolean
  cors_origins: string
  menu_chord: 'auto' | 'keys' | 'encoders' | 'off'
  menu_hold_ms: number
  usb_speed: 'auto' | 'full'
  dock_paired_only?: boolean
  setup_done?: boolean
  token_set?: boolean
}

/** Keys PUT /api/config takes besides those of Config. */
export interface ConfigSecrets {
  wifi_pass?: string
  admin_pass?: string
}
export type ConfigPatch = Partial<Omit<Config, 'lan' | 'wlan'>> &
  ConfigSecrets & {
    lan?: Partial<IpConf>
    wlan?: Partial<IpConf>
  }

export interface DeviceEvent {
  since_s: number
  /** Unix time, null before the clock is set. */
  at: number | null
  kind: string
  text: string
}

export interface PutReply {
  ok: boolean
  restart?: boolean
  /** The settings as stored, from firmware that echoes them. */
  config?: Config
  reach?: string[]
}

export interface Inspect {
  ok: boolean
  error?: string
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
  flash?: Record<string, { used: number; size: number }>
}

export interface Profile {
  routes?: { method: string; uri: string; count: number; avg_us: number; max_us: number }[]
  cpu_permille?: Record<string, number>
  stack_free?: Record<string, number>
}

export interface Coredump {
  task: string
  pc: string
  ra: string
  sp: string
  mcause: number
  mtval: string
  elf_sha256: string
  size: number
  firmware?: string
  at?: string
}

export interface BootInfo {
  bootloader: { version: number; idf: string; built: string; sha256: string }
  ptable: { partitions: number; md5: string; sha256: string }
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
}

export interface InputEvent {
  at: number
  origin: 'deck' | 'web' | 'api'
  client?: string
  deck?: number
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
  | { t: 'ota'; target: string; state: string; pct: number; error?: string }
  | { t: 'ping' }
  | { t: 'log'; text: string }
  | { t: 'result'; id: number; status: number; reply: unknown }
