import type { ESPLoader, Transport } from 'esptool-js'
import type { Image } from './firmware'
import { md5 } from './md5'

// Only what the flasher uses of Web Serial; the DOM lib has no types for it.
export interface WebSerialPort {
  open(o: { baudRate: number }): Promise<void>
  close(): Promise<void>
  readonly readable: ReadableStream<Uint8Array> | null
  readonly writable: WritableStream<Uint8Array> | null
  setSignals(s: { dataTerminalReady?: boolean; requestToSend?: boolean }): Promise<void>
  getInfo(): { usbVendorId?: number; usbProductId?: number }
}

interface Serial {
  requestPort(o?: { filters: { usbVendorId: number }[] }): Promise<WebSerialPort>
}

export const serial = (): Serial | undefined => (navigator as Navigator & { serial?: Serial }).serial

/** The WCH USB serial chip behind the board's USB-C port. */
const WCH = 0x1a86

/** Null when the person closed the port picker. */
export async function pickPort(all: boolean): Promise<WebSerialPort | null> {
  try {
    return (await serial()?.requestPort(all ? undefined : { filters: [{ usbVendorId: WCH }] })) ?? null
  } catch (e) {
    if (e instanceof DOMException && e.name === 'NotFoundError') return null
    throw e
  }
}

const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms))

export interface Chip {
  id: number
  name: string
  revision: number
  mac: string
}

export type Progress =
  | { phase: 'erase' }
  | { phase: 'write' | 'verify'; part: number; done: number; total: number }
  | { phase: 'verified'; part: number }

/** A board in download mode, talking to esptool-js. */
export class Esp {
  readonly port: WebSerialPort
  readonly chip: Chip
  private readonly loader: ESPLoader
  private readonly transport: Transport

  private constructor(port: WebSerialPort, loader: ESPLoader, transport: Transport, chip: Chip) {
    this.port = port
    this.loader = loader
    this.transport = transport
    this.chip = chip
  }

  /** Resets the board into download mode and reads chip and revision. The stub only starts when `accept` says yes. */
  static async connect(port: WebSerialPort, baud: number, log: (line: string) => void, accept: (c: Chip) => boolean) {
    const { ESPLoader, Transport } = await import('esptool-js')
    const transport = new Transport(port, false)
    let partial = ''
    const terminal = {
      clean: () => undefined,
      writeLine: (s: string) => {
        log(partial + s)
        partial = ''
      },
      write: (s: string) => {
        const lines = (partial + s).split(/\r?\n/)
        partial = lines.pop() ?? ''
        lines.filter((l) => l.trim()).forEach(log)
      },
    }
    const loader = new ESPLoader({ transport, baudrate: baud, romBaudrate: 115200, terminal })
    try {
      await loader.connect('default_reset', 7, true)
      const rom = loader.chip
      const chip: Chip = {
        id: rom.IMAGE_CHIP_ID ?? -1,
        name: rom.CHIP_NAME,
        revision: (await rom.getChipRevision?.(loader)) ?? -1,
        mac: await rom.readMac(loader),
      }
      log(`Chip ${chip.name}, revision v${Math.floor(chip.revision / 100)}.${chip.revision % 100}, MAC ${chip.mac}`)
      const esp = new Esp(port, loader, transport, chip)
      if (!accept(chip)) return esp
      await rom.postConnect?.(loader)
      await loader.runStub()
      if (baud !== 115200) await loader.changeBaud()
      return esp
    } catch (e) {
      await transport.disconnect().catch(() => undefined)
      throw e
    }
  }

  /** True when the flash holds the same partition table as `factory`, so an update keeps a working layout. */
  async sameLayout(factory: Image): Promise<boolean> {
    const at = 0x8000 - factory.offset
    const table = factory.data.subarray(at, at + 0xc00)
    if (table.length < 0xc00) return false
    return (await this.loader.flashMd5sum(0x8000, 0xc00)) === md5(table)
  }

  async flash(images: Image[], eraseAll: boolean, on: (p: Progress) => void) {
    if (eraseAll) on({ phase: 'erase' })
    let current = -1
    await this.loader.writeFlash({
      fileArray: images.map((i) => ({ data: i.data, address: i.offset })),
      flashMode: 'keep',
      flashFreq: 'keep',
      flashSize: 'keep',
      eraseAll,
      compress: true,
      calculateMD5Hash: md5,
      // `written` and `total` count compressed bytes; people care about the real size.
      reportProgress: (i, written, total) => {
        if (i !== current && current >= 0) on({ phase: 'verified', part: current })
        current = i
        const size = images[i]?.data.length ?? 0
        const done = total ? Math.round((written / total) * size) : 0
        on({ phase: written >= total ? 'verify' : 'write', part: i, done, total: size })
      },
    })
    if (current >= 0) on({ phase: 'verified', part: current })
  }

  /** Starts the new firmware and hands the port back at the console speed. */
  async boot(baud: number): Promise<WebSerialPort> {
    await this.transport.disconnect().catch(() => undefined)
    await this.port.open({ baudRate: baud })
    await this.port.setSignals({ dataTerminalReady: false, requestToSend: true })
    await sleep(100)
    await this.port.setSignals({ dataTerminalReady: false, requestToSend: false })
    return this.port
  }

  async close() {
    await this.transport.disconnect().catch(() => undefined)
  }
}

/** The board's console: reads until closed, writes Improv frames. */
export class Console {
  private reader: ReadableStreamDefaultReader<Uint8Array> | null = null
  private closed = false
  private isClosed = () => this.closed
  private readonly port: WebSerialPort

  constructor(port: WebSerialPort, onData: (b: Uint8Array) => void, onLost: () => void) {
    this.port = port
    void this.loop(onData, onLost)
  }

  private async loop(onData: (b: Uint8Array) => void, onLost: () => void) {
    while (!this.closed && this.port.readable) {
      const reader = this.port.readable.getReader()
      this.reader = reader
      try {
        for (;;) {
          const { value, done } = await reader.read()
          if (done) break
          onData(value)
        }
      } catch {
        // close() flips `closed` while the read is pending, which TS's narrowing cannot see.
        if (!this.isClosed()) onLost()
        return
      } finally {
        reader.releaseLock()
      }
    }
  }

  async write(b: Uint8Array) {
    const w = this.port.writable?.getWriter()
    if (!w) throw new Error('port closed')
    try {
      await w.write(b)
    } finally {
      w.releaseLock()
    }
  }

  async close() {
    this.closed = true
    await this.reader?.cancel().catch(() => undefined)
    await this.port.close().catch(() => undefined)
  }
}
