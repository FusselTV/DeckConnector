import type {
  BootInfo,
  Config,
  ConfigPatch,
  Coredump,
  Found,
  Frontend,
  InputBody,
  Inspect,
  InputLogEntry,
  Metrics,
  Profile,
  PutReply,
  Route,
  Screen,
  Slot,
  Status,
  TlsInfo,
  Token,
  UpdateCheck,
  WifiAp,
} from './types'

declare const __DC_READONLY__: boolean
/** True in `npm run dev` against anything but the local simulator: nothing that changes the device is sent. */
export const READONLY = typeof __DC_READONLY__ !== 'undefined' && __DC_READONLY__

export class ApiError extends Error {
  readonly status: number
  readonly code: string | undefined
  constructor(status: number, message: string, code?: string) {
    super(message)
    this.status = status
    this.code = code
  }
  /** The firmware does not know this route yet: the caller hides the feature instead of showing an error. */
  /** input_lock on the device refused it; a person may confirm and send it again with force. */
  get locked() {
    return this.status === 423
  }
  get missing() {
    return this.status === 404 || this.status === 405 || this.status === 501
  }
}

type Listener = (status: number) => void
const authListeners = new Set<Listener>()
/** Called with 401 (session ended) or 0 (network gone) from any request. */
export const onAuthLost = (fn: Listener) => {
  authListeners.add(fn)
  return () => void authListeners.delete(fn)
}

export interface Req {
  method?: 'GET' | 'POST' | 'PUT' | 'DELETE'
  body?: unknown
  raw?: BodyInit
  signal?: AbortSignal
  quiet?: boolean
}

export async function request<R>(path: string, o: Req = {}): Promise<R> {
  const method = o.method ?? (o.body !== undefined || o.raw !== undefined ? 'POST' : 'GET')
  if (READONLY && method !== 'GET' && !/^\/api\/log(in|out)$/.test(path))
    throw new ApiError(403, 'read-only dev target', 'readonly')
  let res: Response
  try {
    res = await fetch(path, {
      method,
      credentials: 'same-origin',
      headers: { 'X-DeckConnector': '1', ...(o.body !== undefined ? { 'Content-Type': 'application/json' } : {}) },
      body: o.raw ?? (o.body !== undefined ? JSON.stringify(o.body) : undefined),
      signal: o.signal,
    })
  } catch (e) {
    if (e instanceof DOMException && e.name === 'AbortError') throw e
    if (!o.quiet) authListeners.forEach((f) => f(0))
    throw new ApiError(0, 'not reachable', 'offline')
  }
  if (res.status === 401 && !o.quiet) authListeners.forEach((f) => f(401))
  const type = res.headers.get('content-type') ?? ''
  const data: unknown = type.includes('json') ? await res.json().catch(() => null) : await res.text()
  if (!res.ok) {
    const d = (data && typeof data === 'object' ? data : {}) as { error?: string; code?: string }
    throw new ApiError(res.status, d.error ?? (typeof data === 'string' && data ? data : res.statusText), d.code)
  }
  return data as R
}

/** Resolves to null when the firmware lacks the route, so a card can say "needs newer firmware". */
export async function optional<R>(p: Promise<R>): Promise<R | null> {
  try {
    return await p
  } catch (e) {
    if (e instanceof ApiError && e.missing) return null
    throw e
  }
}

const enc = encodeURIComponent
const q = (deck?: string) => (deck ? `?deck=${enc(deck)}` : '')

/**
 * Presses from this page carry "real": true by design, so they reach Companion and the app.
 * Never call this from a test against the bench bridge; the UI only sends it while remote control is on.
 */
export const pressReal = (body: InputBody, deck?: string, force = false) =>
  request<{ ok: boolean }>('/api/input' + q(deck), { body: { ...body, real: true, ...(force ? { force } : {}) } })

export const api = {
  routes: () => request<Route[]>('/api'),
  status: () => request<Status>('/api/status'),
  login: (password: string, remember = false) =>
    request<{ ok: boolean; retry?: number }>('/api/login', {
      body: remember ? { password, remember } : { password },
      quiet: true,
    }),
  logout: () => request<{ ok: boolean }>('/api/logout', { method: 'POST', quiet: true }),
  config: () => request<Config>('/api/config'),
  putConfig: (patch: ConfigPatch) => request<PutReply>('/api/config', { method: 'PUT', body: patch }),
  screen: (deck?: string) => request<Screen>('/api/screen' + q(deck)),
  menu: (deck?: string) => request<{ ok: boolean }>('/api/menu' + q(deck), { method: 'POST' }),
  reconnect: () => request<{ ok: boolean }>('/api/deck/reconnect', { method: 'POST' }),
  reboot: () => request<{ ok: boolean }>('/api/reboot', { method: 'POST' }),
  dock: (body: { accept: boolean } | { pairing: number } | { forget: string | true }) =>
    request<{ ok: boolean }>('/api/dock', { body }),
  log: (prev = false) => request<string>('/api/log' + (prev ? '?boot=prev' : '')),
  coredump: () => optional(request<Coredump>('/api/coredump')),
  clearCoredump: () => request<{ ok: boolean }>('/api/coredump', { method: 'DELETE' }),
  boot: () => request<BootInfo>('/api/boot'),
  slots: () => request<Slot[]>('/api/slots'),
  bootSlot: (n: number) => request<{ ok: boolean }>(`/api/slots/${n}/boot`, { method: 'POST' }),
  ota: (file: Blob, slot?: number, boot = true) =>
    request<{ ok: boolean }>('/api/ota' + (slot ? `?slot=${slot}${boot ? '' : '&boot=0'}` : boot ? '' : '?boot=0'), {
      method: 'POST',
      raw: file,
    }),
  inspect: (file: Blob) => request<Inspect>('/api/ota/inspect', { method: 'POST', raw: file.slice(0, 144) }),
  otaUrl: (url: string) => request<{ ok: boolean }>('/api/ota/url', { body: { url } }),
  frontend: () => request<Frontend>('/api/frontend'),
  putFrontend: (file: Blob) => request<{ ok: boolean }>('/api/frontend', { method: 'PUT', raw: file }),
  resetFrontend: () => request<{ ok: boolean }>('/api/frontend', { method: 'DELETE' }),
  found: () => request<Found[]>('/api/companion/found'),
  wifiScan: () => request<WifiAp[]>('/api/wifi/scan'),
  wifiJoin: (ssid: string, pass: string, bssid = '') =>
    request<{ ok: boolean }>('/api/wifi/join', { body: { ssid, pass, bssid } }),
  metrics: () => request<Metrics>('/api/metrics'),
  profile: () => request<Profile>('/api/profile'),
  tokens: () => request<Token[]>('/api/tokens'),
  makeToken: (name: string, right: 'control' | 'full') =>
    request<Token & { token: string }>('/api/tokens', { body: { name, right } }),
  dropToken: (name: string) => request<{ ok: boolean }>('/api/tokens/' + enc(name), { method: 'DELETE' }),
  // Newer routes: callers check useRoute() first, or use optional().
  inputLog: (since = 0) => request<InputLogEntry[]>('/api/input/log' + (since ? `?since=${since}` : '')),
  retrySource: (host: string) => request<{ ok: boolean }>('/api/sources/retry', { body: { host } }),
  probe: (addr: string, pin: string) =>
    request<{ ok: boolean; error?: string; version?: string; surfaces?: number }>('/api/sources/probe', {
      body: { addr, pin },
    }),
  updateCheck: () => request<UpdateCheck>('/api/update/check'),
  tlsCheck: (cert: string, key: string) => request<TlsInfo>('/api/tls', { method: 'PUT', body: { cert, key } }),
  putTls: (cert: string, key: string) =>
    request<TlsInfo>('/api/tls', { method: 'PUT', body: { cert, key, apply: true } }),
  resetTls: () => request<{ ok: boolean }>('/api/tls', { method: 'DELETE' }),
  backup: () => request<unknown>('/api/backup?secrets=1'),
  setup: (step: number) => request<{ ok: boolean }>('/api/setup', { method: 'PUT', body: { step } }),
}

export const certUrl = '/api/cert'
export const coredumpRawUrl = '/api/coredump?raw=1'
