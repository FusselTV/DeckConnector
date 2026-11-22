export type Form = {
  name?: unknown
  email?: unknown
  message?: unknown
  website?: unknown
  t?: unknown
  turnstile?: unknown
}
export type Problem = 'email' | 'message' | 'fast'

export const MAX = { name: 100, email: 254, message: 5000 }

const str = (v: unknown) => (typeof v === 'string' ? v.trim() : '')
// Printable ASCII only: the address goes raw into the Reply-To header.
export const EMAIL = /^[!#-'*+\-./0-9=?A-Z^-~]+@[A-Za-z0-9.-]+\.[A-Za-z0-9-]+$/

/** The cleaned form, or what is wrong with it. `t` is the milliseconds the page was open before sending. */
export function check(f: Form): { problem: Problem } | { name: string; email: string; message: string; bot: boolean } {
  const name = str(f.name).replace(/\s+/g, ' ').slice(0, MAX.name)
  const email = str(f.email)
  const message = str(f.message)
  if (email.length > MAX.email || !EMAIL.test(email)) return { problem: 'email' }
  if (!message || message.length > MAX.message) return { problem: 'message' }
  if (typeof f.t !== 'number' || f.t < 3000) return { problem: 'fast' }
  return { name, email, message, bot: str(f.website) !== '' }
}

/** The rate limit's key: an IPv4 address, or the /64 of an IPv6 one, which its host owns whole. */
export function limitKey(ip: string) {
  if (!ip.includes(':')) return ip
  const [head = '', tail] = ip.split('::')
  const h = head ? head.split(':') : []
  const t = tail ? tail.split(':') : []
  const groups = tail === undefined ? h : [...h, ...Array<string>(8 - h.length - t.length).fill('0'), ...t]
  return groups
    .slice(0, 4)
    .map((g) => parseInt(g, 16).toString(16))
    .join(':')
}

const b64 = (s: string) => {
  let bin = ''
  for (const b of new TextEncoder().encode(s)) bin += String.fromCharCode(b)
  return btoa(bin)
}
// RFC 2047 caps an encoded word at 75 characters: 45 bytes each, never splitting a character.
const words = (s: string) => {
  const parts = ['']
  for (const ch of s) {
    if (new TextEncoder().encode(parts.at(-1) + ch).length > 45) parts.push('')
    parts[parts.length - 1] += ch
  }
  return parts.map((w) => `=?UTF-8?B?${b64(w)}?=`).join('\r\n ')
}

/** A plain text mail as RFC 5322 text. Every header value comes from check() or from us. */
export function mime(m: {
  from: string
  to: string
  replyTo: string
  name: string
  message: string
  id: string
  date: Date
}) {
  const who = m.name ? `${m.name} <${m.replyTo}>` : m.replyTo
  const body = b64(`${m.message}\n\n--\n${who}\n`.replace(/\r?\n/g, '\r\n')).replace(/.{76}/g, '$&\r\n')
  return [
    `From: Deck Connector <${m.from}>`,
    `To: <${m.to}>`,
    `Reply-To: <${m.replyTo}>`,
    `Subject: ${words(`Kontakt: ${m.name || m.replyTo}`)}`,
    `Message-ID: ${m.id}`,
    `Date: ${m.date.toUTCString()}`,
    'MIME-Version: 1.0',
    'Content-Type: text/plain; charset=UTF-8',
    'Content-Transfer-Encoding: base64',
    '',
    body,
  ].join('\r\n')
}
