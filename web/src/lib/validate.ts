// Field checks shared by the tabs and the wizard. Each returns an i18n key for the error, or null when fine.
import type { Key } from '../i18n/types'

const OCTET = '(25[0-5]|2[0-4]\\d|1\\d\\d|[1-9]?\\d)'
const IP_RE = new RegExp(`^${OCTET}(\\.${OCTET}){3}$`)

export const isIP = (v: string) => IP_RE.test(v)
const ipNum = (v: string) => v.split('.').reduce((a, b) => a * 256 + Number(b), 0)

/** "/24" style prefix to a dotted netmask. */
export const cidrMask = (n: number) =>
  [0, 1, 2, 3].map((i) => 256 - 2 ** (8 - Math.max(0, Math.min(8, n - 8 * i)))).join('.')

/** A netmask from /1 to /30: anything wider or narrower leaves no usable host address. */
export const isMask = (v: string) => {
  if (!isIP(v)) return false
  const inv = ~ipNum(v) >>> 0
  return (inv & (inv + 1)) === 0 && inv >= 3 && inv < 0x80000000
}

/** Splits "10.0.0.5/24" into the address and its mask. */
export function splitCidr(v: string): { ip: string; mask?: string } {
  const m = /^\s*([\d.]+)\s*\/\s*(\d{1,2})\s*$/.exec(v)
  if (!m?.[1] || !m[2] || Number(m[2]) < 1 || Number(m[2]) > 30) return { ip: v.trim() }
  return { ip: m[1], mask: cidrMask(Number(m[2])) }
}

const sameNet = (a: string, b: string, mask: string) =>
  (ipNum(a) & ipNum(mask)) >>> 0 === (ipNum(b) & ipNum(mask)) >>> 0

interface IpForm {
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
  else if (!isMask(v.mask)) e.mask = 'net.bad_mask'
  if (!e.ip && !e.mask) {
    const host = ipNum(v.ip) & ~ipNum(v.mask)
    if (host === 0 || host >>> 0 === ~ipNum(v.mask) >>> 0) e.ip = 'net.ip_edge'
  }
  if (v.gateway && !isIP(v.gateway)) e.gateway = 'net.bad_ip'
  else if (v.gateway && !e.ip && !e.mask && !sameNet(v.ip, v.gateway, v.mask)) e.gateway = 'net.gw_out'
  else if (v.gateway && v.gateway === v.ip) e.gateway = 'net.gw_self'
  if (v.dns && !isIP(v.dns)) e.dns = 'net.bad_ip'
  return e
}

/** SHA-256 certificate pin: empty, or 64 hex digits with optional colons and spaces. */
export const pinError = (v: string): Key | null =>
  !v.trim() || /^[0-9a-f]{64}$/i.test(v.replace(/[:\s]/g, '')) ? null : 'src.pin_bad'
export const normPin = (v: string) => v.replace(/[:\s]/g, '').toLowerCase()

const bytes = (v: string) => new TextEncoder().encode(v).length

/** A ws:// or wss:// URL as the firmware takes it: lower-case scheme, no spaces, host up to 63 characters. */
export const wsUrlError = (v: string): Key | null => {
  if (!/^wss?:\/\/[^\s"']+$/.test(v) || v.length > 127) return 'src.bad_url'
  try {
    const u = new URL(v)
    return u.hostname && u.hostname.length <= 63 ? null : 'src.bad_url'
  } catch {
    return 'src.bad_url'
  }
}

export const NAME_RE = /^[A-Za-z0-9](?:[A-Za-z0-9\x2d]{0,29}[A-Za-z0-9])?$/ // \x2d: a bare "-" is invalid in the v-mode pattern attribute
export const nameError = (v: string): Key | null => (NAME_RE.test(v) ? null : 'net.bad_name')

/** Companion address: IP, host, host:port, or a ws(s):// URL. */
export const companionError = (v: string): Key | null => {
  const s = v.trim()
  if (!s) return null
  if (s.length > 63) return 'wiz.addr_bad'
  if (/^wss?:\/\//i.test(s)) return wsUrlError(s) ? 'wiz.addr_bad' : null
  const m = /^([a-z0-9.-]+)(?::(\d{1,5}))?$/i.exec(s)
  if (!m?.[1] || (m[2] && Number(m[2]) > 65535)) return 'wiz.addr_bad'
  if (/^[\d.]+$/.test(m[1]) && !isIP(m[1])) return 'wiz.addr_bad'
  return null
}

export const httpUrlError = (v: string): Key | null => {
  try {
    return new URL(v.trim()).protocol === 'http:' ? null : 'fw.bad_url'
  } catch {
    return 'fw.bad_url'
  }
}

export const passwordError = (pw: string): Key | null =>
  !pw ? null : bytes(pw) < 6 ? 'access.pw_short' : bytes(pw) > 23 ? 'access.pw_long' : null

export const tokenNameError = (v: string): Key | null => (/^[A-Za-z0-9_-]{1,23}$/.test(v) ? null : 'access.tbad')

export const corsError = (v: string): Key | null =>
  v.length <= 127 &&
  v
    .split(',')
    .map((s) => s.trim())
    .filter(Boolean)
    .every((s) => /^https?:\/\/[^/\s]+$/.test(s))
    ? null
    : 'access.cors_bad'

/** What the firmware takes: SSID 1 to 32 bytes, not only spaces; password empty (open), 8 to 63 characters, or
 *  64 hex digits. `secured`: the scan saw this name with encryption, so an empty password can only fail. */
export const wifiError = (ssid: string, pass: string, secured = false): Key | null =>
  !ssid.trim() || bytes(ssid) > 32
    ? 'wifi.pick'
    : !pass && secured
      ? 'wifi.pass_need'
      : pass && !(pass.length >= 8 && pass.length <= 63) && !/^[0-9a-f]{64}$/i.test(pass)
        ? 'wifi.pass_len'
        : null

export const securedWifi = (scan: { ssid: string; open: boolean }[], ssid: string) =>
  scan.some((a) => a.ssid === ssid && !a.open)
