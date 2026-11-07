// Field checks shared by the tabs and the wizard. Each returns an i18n key for the error, or null when fine.
import type { Key } from '../i18n/types'

const OCTET = '(25[0-5]|2[0-4]\\d|1\\d\\d|[1-9]?\\d)'
const IP_RE = new RegExp(`^${OCTET}(\\.${OCTET}){3}$`)

export const isIP = (v: string) => IP_RE.test(v.trim())
export const ipNum = (v: string) => v.split('.').reduce((a, b) => a * 256 + Number(b), 0)

/** "/24" style prefix to a dotted netmask. */
export const cidrMask = (n: number) =>
  [0, 1, 2, 3].map((i) => 256 - 2 ** (8 - Math.max(0, Math.min(8, n - 8 * i)))).join('.')

export const isMask = (v: string) => {
  if (!isIP(v)) return false
  const n = ipNum(v)
  const inv = ~n >>> 0
  return (inv & (inv + 1)) === 0
}

/** Splits "10.0.0.5/24" into the address and its mask. */
export function splitCidr(v: string): { ip: string; mask?: string } {
  const m = /^\s*([\d.]+)\s*\/\s*(\d{1,2})\s*$/.exec(v)
  if (!m?.[1] || !m[2] || Number(m[2]) > 32) return { ip: v.trim() }
  return { ip: m[1], mask: cidrMask(Number(m[2])) }
}

export const sameNet = (a: string, b: string, mask: string) =>
  (ipNum(a) & ipNum(mask)) >>> 0 === (ipNum(b) & ipNum(mask)) >>> 0

export interface IpForm {
  dhcp: boolean
  ip: string
  mask: string
  gateway: string
  dns: string
}
export type IpErrors = Partial<Record<keyof IpForm, Key>>

export function ipErrors(v: IpForm): IpErrors {
  if (v.dhcp) return {}
  const e: IpErrors = {}
  if (!v.ip) e.ip = 'net.need'
  else if (!isIP(v.ip)) e.ip = 'net.bad_ip'
  if (!v.mask) e.mask = 'net.need'
  else if (!isMask(v.mask)) e.mask = 'net.bad_ip'
  if (v.gateway && !isIP(v.gateway)) e.gateway = 'net.bad_ip'
  else if (v.gateway && !e.ip && !e.mask && !sameNet(v.ip, v.gateway, v.mask)) e.gateway = 'net.gw_out'
  if (v.dns && !isIP(v.dns)) e.dns = 'net.bad_ip'
  return e
}

/** SHA-256 certificate pin: empty, or 64 hex digits with optional colons and spaces. */
export const pinError = (v: string): Key | null =>
  !v.trim() || /^[0-9a-f]{64}$/i.test(v.replace(/[:\s]/g, '')) ? null : 'src.pin_bad'
export const normPin = (v: string) => v.replace(/[:\s]/g, '').toLowerCase()

export const wsUrlError = (v: string): Key | null => {
  try {
    const u = new URL(v.trim())
    return (u.protocol === 'ws:' || u.protocol === 'wss:') && u.hostname ? null : 'src.bad_url'
  } catch {
    return 'src.bad_url'
  }
}

/** Companion address: IP, host, host:port, or a ws(s):// URL. */
export const companionError = (v: string): Key | null => {
  const s = v.trim()
  if (!s) return null
  if (/^wss?:\/\//i.test(s)) return wsUrlError(s) ? 'wiz.addr_bad' : null
  const m = /^([a-z0-9.-]+)(?::(\d{1,5}))?$/i.exec(s)
  if (!m?.[1] || (m[2] && Number(m[2]) > 65535)) return 'wiz.addr_bad'
  if (/^[\d.]+$/.test(m[1]) && !isIP(m[1])) return 'wiz.addr_bad'
  return null
}

export const httpUrlError = (v: string): Key | null => {
  try {
    const u = new URL(v.trim())
    return u.protocol === 'http:' || u.protocol === 'https:' ? null : 'fw.not_dbfw'
  } catch {
    return 'fw.not_dbfw'
  }
}

export const passwordError = (pw: string, rep: string): { pw?: Key; rep?: Key } => {
  if (!pw && !rep) return {}
  if (pw.length < 6) return { pw: 'access.pw_short' }
  if (pw !== rep) return { rep: 'access.pw_diff' }
  return {}
}

export const tokenNameError = (v: string): Key | null => (/^[A-Za-z0-9_-]{1,23}$/.test(v) ? null : 'access.tbad')

export const corsError = (v: string): Key | null =>
  v
    .split(',')
    .map((s) => s.trim())
    .filter(Boolean)
    .every((s) => /^https?:\/\/[^/\s]+$/.test(s))
    ? null
    : 'src.bad_url'
