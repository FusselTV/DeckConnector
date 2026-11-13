import { api } from '../api/client'
import type { Key } from '../i18n/types'
import { isIP } from './validate'

const TCP = 16622 // Companion's Satellite API, plain TCP
const WS = 16623 // the same over WebSocket
const BUDGET_MS = 15_000 // each probe holds the bridge's web lock: a wrong address must not starve the page
const PROBE_MS = 6_000 // about the longest one probe takes; none starts with less of the budget left

// The address of Companion's web page (http://host:8000): its port is the page's, Satellite is on the host.
const webPage = (v: string) => /^https?:\/\/[^/\s]+\/?$/i.test(v.trim())

/**
 * The addresses to try for what a person typed, best guess first: the input as given, the other of ws and wss,
 * then Companion's own ports on the same host and the usual proxy path. [] when it is not an address at all.
 */
export function candidates(input: string): string[] {
  const m = /^(?:([a-z]+):\/\/)?([a-z0-9.-]+)(?::(\d{1,5}))?(\/[^\s"]*)?$/i.exec(input.trim())
  if (!m?.[2]) return []
  const scheme = m[1]?.toLowerCase()
  const host = m[2]
  const port = m[3] ? Number(m[3]) : 0
  const path = m[4] && m[4] !== '/' ? m[4] : ''
  if ((scheme && !['tcp', 'http', 'https', 'ws', 'wss'].includes(scheme)) || port > 65535) return []
  if (/^[\d.]+$/.test(host) && !isIP(host)) return []
  const at = (secure: boolean, rest: string) => `${secure ? 'wss' : 'ws'}://${host}${rest}`
  const p = port ? `:${port}` : ''
  const out: string[] = []
  if ((!scheme || scheme === 'tcp') && !path) {
    if (port === WS) out.push(at(false, p))
    out.push(port && port !== TCP ? `${host}${p}` : host)
    if (port && port !== TCP) out.push(at(false, p))
  } else {
    const secure = scheme === 'https' || scheme === 'wss' || !scheme
    out.push(at(secure, p + path), at(!secure, p + path))
    if (!path) out.push(at(secure, `${p}/satellite`))
  }
  out.push(host, at(false, `:${WS}`), at(true, '/satellite'))
  return [...new Set(out)].filter((a) => a.length < 64)
}

/** What a save keeps when no candidate answered: the web page's address would never be Satellite. */
export const fallback = (input: string) =>
  webPage(input) ? (/^https?:\/\/([^/:]+)/i.exec(input.trim())?.[1] ?? input) : (candidates(input)[0] ?? input)

export const companionError = (v: string): Key | null => (!v.trim() || candidates(v).length ? null : 'wiz.addr_bad')

export type Tried = { addr: string; code?: string; error: string }

// Something answered: these say more about the address than a closed port or a silent one.
const RANK = ['probe_not_companion', 'probe_not_ws', 'probe_cert', 'probe_tls', 'probe_no_answer', 'probe_refused']

/** The most telling failure of a search, for the one line the page shows. */
export function worst(tried: Tried[], input: string): Tried | undefined {
  // Behind the web page's address, Satellite's own port says why: a page answering "not Satellite" does not.
  const bare = webPage(input) ? tried.find((x) => !x.addr.includes(':')) : undefined
  if (bare) return bare
  const rank = (x: Tried) => {
    const i = RANK.indexOf(x.code ?? '')
    return i < 0 ? RANK.length : i
  }
  return [...tried].sort((a, b) => rank(a) - rank(b))[0]
}

/** Tries the candidates in turn on the bridge and stops at the first Companion. */
export async function findCompanion(
  input: string,
  pin: string,
  onTry: (addr: string) => void,
): Promise<{ addr: string; version: string } | { tried: Tried[] }> {
  const tried: Tried[] = []
  const end = Date.now() + BUDGET_MS
  for (const addr of candidates(input)) {
    if (Date.now() > end - PROBE_MS) break
    onTry(addr)
    const r = await api.probe(addr, addr.startsWith('wss://') ? pin : '')
    if (r.ok) return { addr, version: r.version ?? '' }
    tried.push({ addr, code: r.code, error: r.error ?? '' })
    if (r.code === 'probe_no_name') break // every candidate has the same host
  }
  return { tried }
}
