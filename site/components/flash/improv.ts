// Improv Wi-Fi Serial v1 with the DeckConnector vendor command 0x80. Plain TS without imports, so node runs the
// unit tests on it directly.

export const STATE = 1
export const ERROR = 2
export const RPC = 3
export const RESULT = 4

export const WIFI = 1
export const CURRENT_STATE = 2
export const INFO = 3
export const SCAN = 4
export const NAME = 6
export const NETWORK = 7
export const SETUP = 0x80

export const PROVISIONING = 3
export const PROVISIONED = 4

const HEADER = [0x49, 0x4d, 0x50, 0x52, 0x4f, 0x56] // "IMPROV"
const enc = new TextEncoder()
const dec = new TextDecoder()

export interface Packet {
  type: number
  data: Uint8Array
}

export class ImprovError extends Error {
  readonly code: number
  constructor(code: number) {
    super(`improv error ${code}`)
    this.code = code
  }
}

export function frame(type: number, data: Uint8Array): Uint8Array {
  const out = new Uint8Array(HEADER.length + 3 + data.length + 2)
  out.set(HEADER)
  out.set([1, type, data.length], HEADER.length)
  out.set(data, HEADER.length + 3)
  const end = out.length - 2
  out[end] = out.subarray(0, end).reduce((a, b) => a + b, 0) & 0xff
  out[end + 1] = 0x0a
  return out
}

export function rpc(cmd: number, ...strings: string[]): Uint8Array {
  const parts = strings.map((s) => {
    const b = enc.encode(s)
    if (b.length > 255 || b.includes(0)) throw new RangeError('improv string too long or has a NUL byte')
    return [b.length, ...b]
  })
  const payload = parts.flat()
  if (payload.length > 253) throw new RangeError('improv payload too long')
  return frame(RPC, new Uint8Array([cmd, payload.length, ...payload]))
}

/** The strings of an RPC result's data, after command and length. */
export function strings(data: Uint8Array): string[] {
  const out: string[] = []
  for (let pos = 0; pos < data.length;) {
    const n = data[pos] ?? 0
    out.push(dec.decode(data.subarray(pos + 1, pos + 1 + n)))
    pos += 1 + n
  }
  return out
}

/** Splits the console byte stream into Improv packets and text lines. */
export class Reader {
  private buf = new Uint8Array(0)
  private text = ''
  private readonly onLine: (line: string) => void

  constructor(onLine: (line: string) => void = () => undefined) {
    this.onLine = onLine
  }

  feed(chunk: Uint8Array): Packet[] {
    const joined = new Uint8Array(this.buf.length + chunk.length)
    joined.set(this.buf)
    joined.set(chunk, this.buf.length)
    this.buf = joined
    const packets: Packet[] = []
    for (;;) {
      const at = find(this.buf)
      if (at < 0) {
        const keep = Math.max(0, this.buf.length - HEADER.length + 1)
        this.addText(this.buf.subarray(0, keep))
        this.buf = this.buf.slice(keep)
        return packets
      }
      this.addText(this.buf.subarray(0, at))
      this.buf = this.buf.slice(at)
      if (this.buf.length < 9) return packets
      const end = 9 + (this.buf[8] ?? 0) + 1
      if (this.buf.length < end) return packets
      const pk = this.buf.subarray(0, end)
      const sum = pk.subarray(0, end - 1).reduce((a, b) => a + b, 0) & 0xff
      // A damaged frame gives up only its first byte: its length may reach into the next real frame.
      if (pk[6] !== 1 || sum !== pk[end - 1]) {
        this.buf = this.buf.slice(1)
        continue
      }
      packets.push({ type: pk[7] ?? 0, data: pk.slice(9, end - 1) })
      this.buf = this.buf.slice(end)
    }
  }

  private addText(b: Uint8Array) {
    if (!b.length) return
    const lines = (this.text + dec.decode(b)).split(/\r?\n/)
    this.text = lines.pop() ?? ''
    for (const l of lines) if (l.trim()) this.onLine(redact(l))
  }
}

function find(buf: Uint8Array): number {
  outer: for (let i = 0; i + HEADER.length <= buf.length; i++) {
    for (let j = 0; j < HEADER.length; j++) if (buf[i + j] !== HEADER[j]) continue outer
    return i
  }
  return -1
}

// The boot log prints the generated admin password once.
export const redact = (line: string) => line.replace(/(web login: \S+ \/ )\S+/, '$1******')

type Until = (got: Packet[]) => boolean

export const resultFor =
  (cmd: number): Until =>
  (got) =>
    got.some((p) => p.type === RESULT && p.data[0] === cmd)

const stateDone: Until = (got) => {
  const states = got.filter((p) => p.type === STATE)
  const last = states[states.length - 1]
  return !!last && (last.data[0] !== PROVISIONED || got.some((p) => p.type === RESULT))
}

const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms))

export interface State {
  state: number
  url?: string
}

export interface Network {
  online: boolean
  wlan: boolean
  lan: boolean
  urls: string[]
}

export interface AccessPoint {
  ssid: string
  rssi: number
  secured: boolean
}

export type SetupReply = { ok: true; restart: boolean } | { ok: false; message: string }

export class Improv {
  private waiters = new Set<(p: Packet) => void>()
  readonly reader: Reader
  private readonly write: (b: Uint8Array) => Promise<void>

  constructor(write: (b: Uint8Array) => Promise<void>, onLine: (line: string) => void = () => undefined) {
    this.write = write
    this.reader = new Reader(onLine)
  }

  feed(chunk: Uint8Array) {
    for (const p of this.reader.feed(chunk)) for (const w of [...this.waiters]) w(p)
  }

  /** Sends `packet` and collects packets until `until` holds. An error other than 0 rejects. */
  async ask(packet: Uint8Array, until: Until, timeoutMs = 5000): Promise<Packet[]> {
    const got: Packet[] = []
    let waiter: (p: Packet) => void = () => undefined
    let timer: ReturnType<typeof setTimeout> | undefined
    const done = new Promise<Packet[]>((resolve, reject) => {
      timer = setTimeout(() => reject(new ImprovError(-1)), timeoutMs)
      waiter = (p) => {
        got.push(p)
        if (p.type === ERROR && p.data[0]) {
          clearTimeout(timer)
          reject(new ImprovError(p.data[0]))
        } else if (until(got)) {
          clearTimeout(timer)
          resolve(got)
        }
      }
      this.waiters.add(waiter)
    })
    try {
      await this.write(packet)
      return await done
    } finally {
      clearTimeout(timer)
      this.waiters.delete(waiter)
    }
  }

  private async result(cmd: number, args: string[] = [], timeoutMs?: number) {
    const got = await this.ask(rpc(cmd, ...args), resultFor(cmd), timeoutMs)
    const r = got.filter((p) => p.type === RESULT && p.data[0] === cmd).pop()
    return r ? strings(r.data.subarray(2)) : []
  }

  async state(timeoutMs = 2000): Promise<State> {
    const got = await this.ask(rpc(CURRENT_STATE), stateDone, timeoutMs)
    const st = got.filter((p) => p.type === STATE).pop()?.data[0] ?? 0
    const url = got.filter((p) => p.type === RESULT).pop()
    return { state: st, url: url ? strings(url.data.subarray(2))[0] : undefined }
  }

  /** Asks for the state every second until the bridge answers, as it does after a reset. */
  async waitReady(timeoutMs = 15000, stop: () => boolean = () => false): Promise<State> {
    const end = Date.now() + timeoutMs
    for (;;) {
      try {
        return await this.state(1000)
      } catch (e) {
        if (!(e instanceof ImprovError && e.code === -1) || Date.now() >= end || stop()) throw e
      }
      await sleep(0)
    }
  }

  async info() {
    const [product = '', version = '', chip = '', name = ''] = await this.result(INFO)
    return { product, version, chip, name }
  }

  async scan(): Promise<AccessPoint[]> {
    const got = await this.ask(
      rpc(SCAN),
      (g) => g.some((p) => p.type === RESULT && p.data[0] === SCAN && p.data[1] === 0),
      10000,
    )
    const aps = got
      .filter((p) => p.type === RESULT && p.data[0] === SCAN && p.data[1])
      .map((p) => strings(p.data.subarray(2)))
      .map(([ssid = '', rssi = '0', auth = 'YES']) => ({ ssid, rssi: Number(rssi), secured: auth === 'YES' }))
    const seen = new Set<string>()
    return aps.filter((a) => a.ssid && !seen.has(a.ssid) && seen.add(a.ssid))
  }

  /** Joins a Wi-Fi network. Resolves with the bridge's URL; error 3 means the join failed. */
  async wifi(ssid: string, password: string): Promise<string | undefined> {
    return (await this.result(WIFI, [ssid, password], 25000))[0]
  }

  async network(): Promise<Network> {
    const [flags = '0', lanUrl = '', wlanUrl = ''] = await this.result(NETWORK)
    const f = Number(flags)
    return { online: !!(f & 1), wlan: !!(f & 2), lan: !!(f & 4), urls: [lanUrl, wlanUrl].filter(Boolean) }
  }

  async setup(adminPassword: string, name: string, done: boolean, lang = ''): Promise<SetupReply> {
    const [r = '', message = ''] = await this.result(SETUP, [
      adminPassword,
      name,
      done ? '1' : '',
      ...(lang ? [lang] : []),
    ])
    if (r === 'ok' || r === 'restart') return { ok: true, restart: r === 'restart' }
    return { ok: false, message: message || r }
  }
}
