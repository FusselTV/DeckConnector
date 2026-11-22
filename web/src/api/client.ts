import type {
  Config,
  ConfigPatch,
  Coredump,
  EventPage,
  Found,
  Frontend,
  InputBody,
  InputLogEntry,
  Inspect,
  MatterCodes,
  Metrics,
  Passkey,
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
  /** Seconds until a locked login may try again. */
  readonly retry: number | undefined
  constructor(status: number, message: string, code?: string, retry?: number) {
    super(message)
    this.status = status
    this.code = code
    this.retry = retry
  }
}

type Listener = (status: number) => void
const authListeners = new Set<Listener>()
/** Called with 401 (session ended) or 0 (network gone) from any request. */
export const onAuthLost = (fn: Listener) => {
  authListeners.add(fn)
  return () => void authListeners.delete(fn)
}

interface Req {
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
  // A hung request ends with an error instead of an idle button. Uploads take as long as they take.
  const limit = o.raw ? 0 : 20_000
  let res: Response
  try {
    res = await fetch(path, {
      method,
      credentials: 'same-origin',
      headers: { 'X-DeckConnector': '1', ...(o.body !== undefined ? { 'Content-Type': 'application/json' } : {}) },
      body: o.raw ?? (o.body !== undefined ? JSON.stringify(o.body) : undefined),
      signal: o.signal ?? (limit ? AbortSignal.timeout(limit) : undefined),
    })
  } catch (e) {
    if (e instanceof DOMException && e.name === 'AbortError') throw e
    if (e instanceof DOMException && e.name === 'TimeoutError') throw new ApiError(0, '', 'timeout')
    if (!o.quiet) authListeners.forEach((f) => f(0))
    throw new ApiError(0, 'not reachable', 'offline')
  }
  if (res.status === 401 && !o.quiet) authListeners.forEach((f) => f(401))
  const type = res.headers.get('content-type') ?? ''
  const data: unknown = type.includes('json') ? await res.json().catch(() => null) : await res.text()
  if (!res.ok) {
    const d = (data && typeof data === 'object' ? data : {}) as { error?: string; code?: string; retry?: number }
    // A proxy's HTML error page is no message for a person; the page then words it from the status.
    const text = typeof data === 'string' && data.length < 200 && !data.trimStart().startsWith('<') ? data : ''
    throw new ApiError(res.status, d.error ?? text, d.code, d.retry)
  }
  if (type.includes('json') && data === null) throw new ApiError(502, '', 'badreply')
  return data as R
}

/** A list reply that is not a list (a cut reply, a proxy page) fails like any other error. */
const list = <T>(p: Promise<T[]>) =>
  p.then((x) => {
    if (!Array.isArray(x)) throw new ApiError(502, '', 'badreply')
    return x
  })

const enc = encodeURIComponent
const q = (deck?: string) => (deck ? `?deck=${enc(deck)}` : '')

/**
 * Presses from this page carry "real": true by design, so they reach Companion and the app.
 * Never call this from a test against the bench bridge; the UI only sends it while remote control is on.
 */
export const pressReal = (body: InputBody, deck?: string) =>
  request<{ ok: boolean }>('/api/input' + q(deck), { body: { ...body, real: true } })

export const api = {
  routes: () => list(request<Route[]>('/api')),
  status: () => request<Status>('/api/status'),
  login: (password: string) =>
    request<{ ok: boolean; retry?: number }>('/api/login', {
      body: { password },
      quiet: true,
    }),
  logout: () => request<{ ok: boolean }>('/api/logout', { method: 'POST', quiet: true }),
  showPassword: () => request<{ ok: boolean }>('/api/password/show', { method: 'POST', quiet: true }),
  askReset: () => request<{ ok: boolean; s: number }>('/api/password/reset', { method: 'POST', quiet: true }),
  cancelReset: () => request<{ ok: boolean }>('/api/password/reset', { method: 'DELETE', quiet: true }),
  askLogin: (who: string) =>
    request<{ id: string; s: number }>('/api/login/request', { method: 'POST', body: { who }, quiet: true }),
  loginState: (id: string) =>
    request<{ state: 'pending' | 'accepted' | 'denied' | 'expired' | 'used'; s: number }>(`/api/login/request/${id}`, {
      quiet: true,
    }),
  cancelLogin: (id: string) => request<{ ok: boolean }>(`/api/login/request/${id}`, { method: 'DELETE', quiet: true }),
  config: () => request<Config>('/api/config'),
  putConfig: (patch: ConfigPatch) => request<PutReply>('/api/config', { method: 'PUT', body: patch }),
  screen: (deck?: string) => request<Screen>('/api/screen' + q(deck)),
  menu: (deck?: string) => request<{ ok: boolean }>('/api/menu' + q(deck), { method: 'POST' }),
  closeMenu: (deck?: string) => request<{ ok: boolean }>('/api/menu' + q(deck), { method: 'DELETE' }),
  reconnect: () => request<{ ok: boolean }>('/api/deck/reconnect', { method: 'POST' }),
  reboot: () => request<{ ok: boolean }>('/api/reboot', { method: 'POST' }),
  sound: (sound: string) => request<{ ok: boolean }>('/api/sound', { body: { sound } }),
  closeAp: () => request<{ ok: boolean }>('/api/ap', { method: 'DELETE' }),
  reset: () => request<{ ok: boolean }>('/api/reset', { method: 'POST' }),
  dock: (body: { accept: boolean; ip: string } | { pairing: number } | { forget: string | true }) =>
    request<{ ok: boolean }>('/api/dock', { body }),
  log: (prev = false) => request<string>('/api/log' + (prev ? '?boot=prev' : '')),
  /** 404 when there is no crash report. */
  coredump: () => request<Coredump>('/api/coredump'),
  clearCoredump: () => request<{ ok: boolean }>('/api/coredump', { method: 'DELETE' }),
  slots: () => list(request<Slot[]>('/api/slots')),
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
  found: () => list(request<Found[]>('/api/companion/found')),
  wifiScan: () => list(request<WifiAp[]>('/api/wifi/scan')),
  wifiJoin: (ssid: string, pass: string, bssid = '') =>
    request<{ ok: boolean }>('/api/wifi/join', { body: { ssid, pass, bssid } }),
  metrics: () => request<Metrics>('/api/metrics'),
  profile: () => request<Profile>('/api/profile'),
  tokens: () => list(request<Token[]>('/api/tokens')),
  makeToken: (name: string, right: 'control' | 'full') =>
    request<Token & { token: string }>('/api/tokens', { body: { name, right } }),
  dropToken: (name: string) => request<{ ok: boolean }>('/api/tokens/' + enc(name), { method: 'DELETE' }),
  passkeys: () => list(request<Passkey[]>('/api/passkeys')),
  passkeyChallenge: (login: boolean) =>
    request<{ challenge: string }>('/api/passkeys/challenge', { body: { login }, quiet: login }),
  addPasskey: (body: Record<string, string | number>) => request<{ id: number }>('/api/passkeys', { body }),
  renamePasskey: (id: number, name: string) =>
    request<{ ok: boolean }>(`/api/passkeys/${id}`, { method: 'PUT', body: { name } }),
  dropPasskey: (id: number) => request<{ ok: boolean }>(`/api/passkeys/${id}`, { method: 'DELETE' }),
  passkeyLogin: (body: Record<string, string>) => request<{ ok: boolean }>('/api/login/passkey', { body, quiet: true }),
  inputLog: (since = 0, before = 0) =>
    list(request<InputLogEntry[]>('/api/input/log' + (since ? `?since=${since}` : before ? `?before=${before}` : ''))),
  retrySource: (host: string) => request<{ ok: boolean }>('/api/sources/retry', { body: { host } }),
  probe: (addr: string, pin: string) =>
    request<{ ok: boolean; error?: string; code?: string; version?: string; surfaces?: number }>('/api/sources/probe', {
      body: { addr, pin },
    }),
  updateCheck: (auto = false) => request<UpdateCheck>('/api/update/check' + (auto ? '?auto=1' : '')),
  tlsCheck: (cert: string, key: string) => request<TlsInfo>('/api/tls', { method: 'PUT', body: { cert, key } }),
  putTls: (cert: string, key: string) =>
    request<TlsInfo>('/api/tls', { method: 'PUT', body: { cert, key, apply: true } }),
  resetTls: () => request<{ ok: boolean }>('/api/tls', { method: 'DELETE' }),
  backup: () => request<unknown>('/api/backup?secrets=1'),
  undeny: (ip: string) => request<{ ok: boolean }>('/api/pairing/denied/' + enc(ip), { method: 'DELETE' }),
  setup: (step: number) => request<{ ok: boolean }>('/api/setup', { method: 'PUT', body: { step } }),
  claimSetup: () => request<{ ok: boolean }>('/api/setup/claim', { method: 'POST' }),
  events: (q: { before?: number; since?: number; n?: number } = {}) =>
    request<EventPage>(
      '/api/events' + (Object.keys(q).length ? '?' + new URLSearchParams(q as Record<string, string>).toString() : ''),
    ).then((p) => {
      if (!Array.isArray(p.events)) throw new ApiError(502, '', 'badreply') // a firmware before event codes
      return p
    }),
  matter: () => request<MatterCodes>('/api/matter'),
  setMatter: (body: { on: boolean } | { pair: true } | { show: true }) =>
    request<{ ok: boolean }>('/api/matter', { body }),
  resetMatter: () => request<{ ok: boolean }>('/api/matter', { method: 'DELETE' }),
}

export const certUrl = '/api/cert'
export const caUrl = '/api/ca'
export const coredumpRawUrl = '/api/coredump?raw=1'
