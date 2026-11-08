// A pretend ESP32-P4 board for the flasher tests. DTR and RTS drive it like the board's auto-reset circuit: RTS
// released while DTR is set starts the ROM download mode (enough of the ROM and stub protocol for esptool-js),
// released while DTR is clear starts the app, which is firmware/sim speaking Improv on stdin and stdout.
import { spawn } from 'node:child_process'
import { createHash } from 'node:crypto'
import { mkdtempSync, readFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { inflateSync } from 'node:zlib'

export const SIM = new URL('../../firmware/sim/build/deckbridge_sim.elf', import.meta.url).pathname
export const FW = new URL('../public/firmware/', import.meta.url).pathname

const SIZE = 32 << 20
const EFUSE_BLOCK1_W2 = 0x5012d044 + 8
const sleep = (ms) => new Promise((r) => setTimeout(r, ms))

/**
 * opts: chip 'p4' | 'c6', rev (major * 100 + minor), contents 'blank' | 'deckconnector', deaf (never answers sync),
 * bootMs (app start delay), md5Bad, dropAfter (stop answering after this many flash blocks), slow (ms per flash block), eraseMs,
 * improvOpenS and setupDone for the simulator, simPort.
 */
export class Board {
  constructor(opts = {}) {
    this.o = { chip: 'p4', rev: 301, contents: 'blank', slow: 0, eraseMs: 300, simPort: 18390, ...opts }
    this.flash = Buffer.alloc(SIZE, 0xff)
    if (this.o.contents === 'deckconnector') readFileSync(join(FW, 'deckconnector-factory.bin')).copy(this.flash, 0)
    this.dir = mkdtempSync(join(process.env.FLASH_TMP ?? tmpdir(), 'flash-sim-'))
    this.mode = 'off'
    this.dtr = false
    this.rts = false
    this.rx = []
    this.esc = false
    this.blocks = 0
    this.out = () => undefined
    this.queue = Promise.resolve()
  }

  /** Bytes the board sends go here. */
  onOutput(fn) {
    this.out = fn
  }

  signals({ dtr, rts }) {
    if (dtr !== undefined) this.dtr = dtr
    if (rts === undefined) return
    const was = this.rts
    this.rts = rts
    if (rts && !was) this.power('off')
    if (!rts && was) this.power(this.dtr ? 'download' : 'app')
  }

  power(mode) {
    this.mode = mode
    this.stub = false
    this.rx = []
    this.sim?.kill()
    this.sim = null
    if (mode === 'download') this.out(Buffer.from('ESP-ROM:esp32p4-eco2-20240710\r\nboot:0x20f (DOWNLOAD(USB/UART0))\r\nwaiting for download\r\n'))
    if (mode === 'app') setTimeout(() => this.mode === mode && !this.sim && this.startSim(), this.o.bootMs ?? 0)
  }

  startSim() {
    const sim = spawn(SIM, [], {
      cwd: this.dir,
      env: {
        ...process.env,
        DB_SIM_PORT: String(this.o.simPort),
        DB_SIM_PASSWORD: 'simulator',
        ...(this.o.improvOpenS !== undefined && { DB_SIM_IMPROV_OPEN_S: String(this.o.improvOpenS) }),
      },
      stdio: ['pipe', 'pipe', 'pipe'],
    })
    this.sim = sim
    sim.stdout.on('data', (b) => this.sim === sim && this.out(b))
    sim.stderr.on('data', (b) => this.sim === sim && this.out(b))
    sim.stdin.on('error', () => undefined)
    // The simulator forgets its settings on exit, so a finished setup is set again at every start.
    if (this.o.setupDone) {
      const body = [...Buffer.from('IMPROV'), 1, 3, 6, 0x80, 4, 0, 0, 1, 0x31]
      sim.stdin.write(Buffer.from([...body, body.reduce((a, b) => a + b, 0) & 0xff, 10]))
    }
    // The simulator exits where the firmware restarts.
    sim.on('exit', () => this.sim === sim && this.mode === 'app' && setTimeout(() => this.sim === sim && this.startSim(), 300))
  }

  write(bytes) {
    if (this.mode === 'app') return void this.sim?.stdin.write(bytes)
    if (this.mode !== 'download' || this.o.deaf) return
    for (const b of bytes) {
      if (b === 0xc0) {
        if (this.rx.length) {
          const pkt = Buffer.from(this.rx)
          this.queue = this.queue.then(() => this.command(pkt))
        }
        this.rx = []
      } else if (this.esc) {
        this.rx.push(b === 0xdc ? 0xc0 : 0xdd === b ? 0xdb : b)
        this.esc = false
      } else if (b === 0xdb) this.esc = true
      else this.rx.push(b)
    }
  }

  slip(data) {
    const out = [0xc0]
    for (const b of data) out.push(...(b === 0xc0 ? [0xdb, 0xdc] : b === 0xdb ? [0xdb, 0xdd] : [b]))
    out.push(0xc0)
    this.out(Buffer.from(out))
  }

  reply(op, val = 0, data = Buffer.alloc(0)) {
    const h = Buffer.alloc(8)
    h.writeUInt8(1, 0)
    h.writeUInt8(op, 1)
    h.writeUInt16LE(data.length + 2, 2)
    h.writeUInt32LE(val >>> 0, 4)
    this.slip(Buffer.concat([h, data, Buffer.from([0, 0])]))
  }

  reg(addr) {
    if (addr === EFUSE_BLOCK1_W2) {
      const major = Math.floor(this.o.rev / 100)
      const minor = this.o.rev % 100
      return ((major & 4) << 21) | ((major & 3) << 4) | (minor & 0xf)
    }
    return 0
  }

  async command(pkt) {
    if (this.mode !== 'download' || pkt[0] !== 0) return
    const op = pkt[1]
    const d = pkt.subarray(8)
    switch (op) {
      case 0x08: // sync
        for (let i = 0; i < 8; i++) this.reply(op, this.stub ? 0 : 0x12345678)
        return
      case 0x14: {
        const info = Buffer.alloc(20)
        info.writeUInt32LE(this.o.chip === 'p4' ? 18 : 13, 12)
        return this.reply(op, 0, info)
      }
      case 0x0a:
        return this.reply(op, this.reg(d.readUInt32LE(0)))
      case 0x06: // mem end: the stub starts and says hello
        this.reply(op)
        this.stub = true
        await sleep(20)
        return this.slip(Buffer.from('OHAI'))
      case 0xd0:
        await sleep(this.o.eraseMs)
        this.flash.fill(0xff)
        return this.reply(op)
      case 0x10:
        this.target = { offset: d.readUInt32LE(12), size: d.readUInt32LE(0), parts: [] }
        return this.reply(op)
      case 0x11:
        if (this.o.dropAfter !== undefined && ++this.blocks > this.o.dropAfter) return
        this.target.parts.push(Buffer.from(d.subarray(16)))
        if (this.o.slow) await sleep(this.o.slow)
        return this.reply(op)
      case 0x12: {
        const raw = inflateSync(Buffer.concat(this.target.parts))
        raw.copy(this.flash, this.target.offset)
        return this.reply(op)
      }
      case 0x13: {
        const at = d.readUInt32LE(0)
        const sum = createHash('md5').update(this.flash.subarray(at, at + d.readUInt32LE(4))).digest()
        if (this.o.md5Bad && at !== 0x8000) sum[0] ^= 1
        return this.reply(op, 0, sum)
      }
      default: // write_reg, mem_begin, mem_data, change_baud, spi_attach and the rest only need an ok
        return this.reply(op)
    }
  }

  md5(offset, size) {
    return createHash('md5').update(this.flash.subarray(offset, offset + size)).digest('hex')
  }

  stop() {
    this.mode = 'off'
    this.sim?.kill()
    this.sim = null
  }
}
