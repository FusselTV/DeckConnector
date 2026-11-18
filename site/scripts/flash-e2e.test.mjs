// The flasher in a real browser against the production build in out/, with a fake navigator.serial bridged to
// scripts/flash-board.mjs. Needs `npm run build` with public/firmware/ and the built firmware/sim.
// node --test scripts/flash-e2e.test.mjs   (SHOTS=<dir> also saves screenshots, light and dark, 1280 and 390)
import assert from 'node:assert/strict'
import { createReadStream, existsSync, mkdirSync, readFileSync, statSync } from 'node:fs'
import { createServer } from 'node:http'
import { extname, join } from 'node:path'
import { after, before, describe, test } from 'node:test'
import { Board, FW, SIM } from './flash-board.mjs'

const OUT = new URL('../out/', import.meta.url).pathname
const { chromium, devices } = await import(new URL('../../web/node_modules/playwright/index.mjs', import.meta.url).href)
const SHOTS = process.env.SHOTS
const skip = !existsSync(join(OUT, 'flash/index.html')) || !existsSync(join(FW, 'manifest.json')) || !existsSync(SIM)
const factory = readFileSync(join(FW, 'deckconnector-factory.bin'))
const app = readFileSync(join(FW, 'deckconnector.bin'))

const TYPES = {
  '.html': 'text/html',
  '.js': 'text/javascript',
  '.css': 'text/css',
  '.json': 'application/json',
  '.svg': 'image/svg+xml',
  '.woff2': 'font/woff2',
  '.txt': 'text/plain',
}
let server
let base
let browser

before(async () => {
  if (skip) return
  server = createServer((req, res) => {
    let p = join(OUT, decodeURIComponent(new URL(req.url, 'http://x').pathname))
    if (existsSync(p) && statSync(p).isDirectory()) p = join(p, 'index.html')
    if (!existsSync(p)) return void res.writeHead(404).end()
    res.writeHead(200, { 'content-type': TYPES[extname(p)] ?? 'application/octet-stream' })
    createReadStream(p).pipe(res)
  })
  await new Promise((r) => server.listen(0, '127.0.0.1', r))
  base = `http://127.0.0.1:${server.address().port}`
  browser = await chromium.launch()
})

after(async () => {
  await browser?.close()
  server?.close()
})

function fakeSerial() {
  const b64 = (u8) => btoa(Array.from(u8, (x) => String.fromCharCode(x)).join(''))
  const unb64 = (s) => Uint8Array.from(atob(s), (c) => c.charCodeAt(0))
  class FakePort {
    #open = false
    #r = null
    #ctrl = null
    #w = null
    getInfo() {
      return { usbVendorId: 0x1a86, usbProductId: 0x55d3 }
    }
    get readable() {
      if (!this.#open) return null
      this.#r ??= new ReadableStream({
        start: (c) => void (this.#ctrl = c),
        cancel: () => void ((this.#r = null), (this.#ctrl = null)),
      })
      return this.#r
    }
    get writable() {
      if (!this.#open) return null
      this.#w ??= new WritableStream({ write: (chunk) => window.__board({ t: 'write', b: b64(chunk) }) })
      return this.#w
    }
    async open(o) {
      if (this.#open) throw new DOMException('The port is already open.', 'InvalidStateError')
      this.#open = true
      await window.__board({ t: 'open', baud: o.baudRate })
    }
    async close() {
      try {
        this.#ctrl?.close()
      } catch {
        // already closed by a cancelled reader
      }
      this.#r = this.#ctrl = this.#w = null
      this.#open = false
    }
    async setSignals(s) {
      await window.__board({ t: 'sig', dtr: s.dataTerminalReady, rts: s.requestToSend })
    }
    push(u8) {
      if (!this.#open) return
      void this.readable
      try {
        this.#ctrl?.enqueue(u8)
      } catch {
        // the reader went away between the open check and here
      }
    }
  }
  const port = new FakePort()
  window.__boardData = (s) => port.push(unb64(s))
  Object.defineProperty(Navigator.prototype, 'serial', {
    configurable: true,
    get: () => ({ requestPort: async () => port, getPorts: async () => [port] }),
  })
}

/** A page whose serial port is `board`. */
async function open(t, { board, width = 1280, lang = '', ctx = {} } = {}) {
  const context = await browser.newContext({ viewport: { width, height: 900 }, ...ctx })
  t.after(async () => {
    if (process.env.DEBUG_FLASH)
      for (const p of context.pages())
        console.log(await p.locator('main').innerText(), await p.locator('pre').textContent())
    board?.stop()
    await context.close()
  })
  if (board) {
    let chain = Promise.resolve()
    let pending = []
    let timer = null
    let page = null
    board.onOutput((b) => {
      pending.push(b)
      timer ??= setTimeout(() => {
        timer = null
        const data = Buffer.concat(pending).toString('base64')
        pending = []
        chain = chain.then(() => page?.evaluate((s) => window.__boardData(s), data).catch(() => undefined))
      }, 3)
    })
    await context.exposeBinding('__board', ({ page: p }, m) => {
      page = p
      if (m.t === 'write') board.write(Buffer.from(m.b, 'base64'))
      if (m.t === 'sig') board.signals(m)
    })
    await context.addInitScript(fakeSerial)
  }
  const page = await context.newPage()
  page.setDefaultTimeout(30000)

  await page.goto(`${base}${lang}/flash/`)
  return page
}

let n = 0
async function shot(page, name) {
  const over = await page.evaluate(() => document.documentElement.scrollWidth - document.documentElement.clientWidth)
  // negative: the stable scrollbar gutter, which is no sideways scroll
  assert.ok(over <= 0, `${name} scrolls sideways by ${over}px`)
  if (!SHOTS) return
  mkdirSync(SHOTS, { recursive: true })
  const w = page.viewportSize().width
  const id = String(++n).padStart(2, '0')
  for (const theme of ['dark', 'light']) {
    await page.evaluate((t) => {
      document.documentElement.classList.remove('dark', 'light')
      document.documentElement.classList.add(t)
      document.documentElement.style.colorScheme = t
    }, theme)
    await page.evaluate(() => window.scrollTo(0, 0))
    await page.waitForTimeout(700)
    await page.screenshot({ path: join(SHOTS, `${w}-${theme}-${id}-${name}.png`), fullPage: true })
  }
  await page.evaluate(() => {
    document.documentElement.classList.remove('light')
    document.documentElement.classList.add('dark')
    document.documentElement.style.colorScheme = 'dark'
  })
}

async function fix(page, id, lang = '') {
  const href = await page
    .getByRole('link', { name: lang ? 'So behebst du das' : 'How to fix this' })
    .getAttribute('href')
  assert.equal(href, `${lang}/docs/flash-troubleshooting/#${id}`)
}

const widths = SHOTS ? [1280, 390] : [1280]

for (const width of widths)
  describe(`flasher at ${width}`, { skip: skip && 'needs out/, public/firmware and firmware/sim' }, () => {
    test('a browser without Web Serial', async (t) => {
      const context = await browser.newContext({ viewport: { width, height: 900 } })
      t.after(() => context.close())
      await context.addInitScript(() => Object.defineProperty(Navigator.prototype, 'serial', { get: () => undefined }))
      const page = await context.newPage()
      await page.goto(`${base}/flash/`)
      await page.getByText('This browser cannot flash').waitFor()
      await fix(page, 'browser')
      await shot(page, 'no-web-serial')
    })

    if (width === 390)
      test('a phone', async (t) => {
        const context = await browser.newContext({ ...devices['iPhone 13'] })
        t.after(() => context.close())
        const page = await context.newPage()
        await page.goto(`${base}/flash/`)
        await page.getByText('Flash from a computer').waitFor()
        await shot(page, 'phone')
        await page.goto(`${base}/de/flash/`)
        await page.getByText('Flashen geht nur am Computer').waitFor()
        await shot(page, 'phone-de')
      })

    test('a board that does not answer', async (t) => {
      const page = await open(t, { board: new Board({ deaf: true }), width })
      await shot(page, 'connect')
      await page.getByRole('button', { name: 'Connect', exact: true }).click()
      await page.getByText('The board did not answer.').waitFor({ timeout: 90000 })
      assert.ok(await page.getByText('Hold the BOOT button on the board.').isVisible())
      assert.ok(await page.getByText('Press and release RST.').isVisible())
      await fix(page, 'no-answer')
      assert.equal(
        await page.getByRole('link', { name: 'USB driver' }).getAttribute('href'),
        '/docs/flash-troubleshooting/#driver',
      )
      await shot(page, 'no-answer')
    })

    test('another chip', async (t) => {
      const page = await open(t, { board: new Board({ chip: 'c6' }), width })
      await page.getByRole('button', { name: 'Connect', exact: true }).click()
      await page.getByText('This board has an ESP32-C6').waitFor()
      await fix(page, 'wrong-chip')
      await shot(page, 'wrong-chip')
    })

    test('an old chip revision', async (t) => {
      const page = await open(t, { board: new Board({ rev: 100 }), width })
      await page.getByRole('button', { name: 'Connect', exact: true }).click()
      await page.getByText('This chip revision is not supported').waitFor()
      assert.ok(await page.getByText('revision v1.0. DeckConnector needs v3.1 to v3.99').isVisible())
      await fix(page, 'chip-revision')
      await shot(page, 'wrong-revision')
    })

    test('full install on a blank board, then setup over Improv', async (t) => {
      const board = new Board({
        slow: SHOTS ? 120 : 0,
        eraseMs: SHOTS ? 2500 : 300,
        bootMs: SHOTS ? 4000 : 0,
        simPort: 18390 + (width % 7),
      })
      const page = await open(t, { board, width })
      await page.getByRole('button', { name: 'Connect', exact: true }).click()
      await page.getByText('Choose how to install').waitFor()
      await page.getByText('Not possible on this board').waitFor()
      const go = page.getByRole('button', { name: /^Install v/ })
      assert.ok(await page.getByRole('radio', { name: /Update/ }).isDisabled())
      assert.ok(await go.isDisabled(), 'a full install needs the confirmation')
      await shot(page, 'install-blank')
      await page.getByLabel('I understand that all settings on this board are erased.').check()
      await shot(page, 'install-confirmed')
      await go.click()
      if (SHOTS) {
        await page.getByText('Erase the board').waitFor()
        await shot(page, 'erasing')
        await page.getByText(/Writing \d+%/).waitFor()
        await page.waitForTimeout(1500)
        await shot(page, 'writing')
      }
      if (SHOTS) {
        await page.getByText('Waiting for the board to start').waitFor({ timeout: 120000 })
        await shot(page, 'booting')
      }
      await page.getByText('Set up Wi-Fi').waitFor({ timeout: 120000 })
      assert.equal((await board.config()).lang, 'en', 'the deck speaks the language of the flasher')
      assert.equal(
        board.md5(0, factory.length),
        (await import('node:crypto')).createHash('md5').update(factory).digest('hex'),
      )

      await page.getByLabel('Join a Wi-Fi network').check()
      await page.getByRole('radio', { name: /Zuhause/ }).waitFor()
      await shot(page, 'setup-form')
      await page.locator('summary', { hasText: 'Log' }).click()
      await shot(page, 'setup-log')
      await page.locator('summary', { hasText: 'Log' }).click()

      await page.getByRole('radio', { name: 'Other network' }).check()
      await page.getByLabel('Network name (SSID)').fill('Elsewhere')
      await page.getByLabel('Wi-Fi password').fill('password1')
      await page.getByRole('button', { name: 'Save' }).click()
      await page.getByText('Could not join "Elsewhere"').waitFor()
      assert.equal(await page.getByLabel('Wi-Fi password').inputValue(), '', 'the sent password is gone')
      await fix(page, 'wifi-join')
      await shot(page, 'setup-wifi-failed')

      await page.getByRole('radio', { name: /Zuhause/ }).check()
      await page.getByLabel('Wi-Fi password').fill('password1')
      await page.getByRole('button', { name: 'Save' }).click()
      await page.getByText('Your connector is ready').waitFor({ timeout: 60000 })
      assert.ok(await page.getByRole('link', { name: /^deckconnector-\w+\.local$/ }).isVisible())
      assert.ok(await page.getByRole('link', { name: 'Open web UI' }).isVisible())
      const html = await page.content()
      assert.ok(!html.includes('password1'), 'no password on the page')
      await shot(page, 'ready')
    })

    test('update keeps the layout', async (t) => {
      const board = new Board({ contents: 'deckconnector', simPort: 18380 + (width % 7) })
      const page = await open(t, { board, width, lang: '/de' })
      await page.getByRole('button', { name: 'Verbinden', exact: true }).click()
      await page.getByText('Empfohlen').waitFor()
      assert.ok(await page.getByRole('radio', { name: /Update/ }).isChecked())
      await shot(page, 'install-update-de')
      await page.getByRole('button', { name: /installieren$/ }).click()
      await page.getByText('Mit einem WLAN verbinden').waitFor({ timeout: 60000 })
      assert.equal(
        board.md5(0x20000, app.length),
        (await import('node:crypto')).createHash('md5').update(app).digest('hex'),
      )
      await shot(page, 'setup-online-de')
      await page.getByRole('button', { name: 'Später in der Weboberfläche' }).click()
      await page.getByText('Dein Connector ist bereit').waitFor()
      await shot(page, 'ready-de')
    })

    test('settings closed', async (t) => {
      const board = new Board({
        contents: 'deckconnector',
        setupDone: true,
        improvOpenS: 0,
        simPort: 18370 + (width % 7),
      })
      const page = await open(t, { board, width })
      await page.getByRole('button', { name: 'Connect', exact: true }).click()
      await page.getByRole('button', { name: /^Install v/ }).click()
      await page.getByLabel('Join a Wi-Fi network').check()
      await page.getByRole('radio', { name: 'Other network' }).check()
      await page.getByLabel('Network name (SSID)').fill('Zuhause')
      await page.getByLabel('Wi-Fi password').fill('password1')
      await page.getByRole('button', { name: 'Save' }).click()
      await page.getByText('Press RST on the board, then save again.').waitFor()
      await fix(page, 'setup-no-answer')
      await shot(page, 'setup-closed')
    })

    test('verify fails', async (t) => {
      const page = await open(t, { board: new Board({ contents: 'deckconnector', md5Bad: true }), width })
      await page.getByRole('button', { name: 'Connect', exact: true }).click()
      await page.getByRole('button', { name: /^Install v/ }).click()
      await page.getByText('What the board read back does not match the file.').waitFor()
      await fix(page, 'read-back')
      await shot(page, 'verify-failed')
      await page.getByRole('button', { name: 'Start again' }).click()
      await page.getByRole('button', { name: 'Connect', exact: true }).waitFor()
    })

    if (width === 1280)
      test('the cable goes away while writing', async (t) => {
        const page = await open(t, { board: new Board({ contents: 'deckconnector', dropAfter: 3 }), width })
        await page.getByRole('button', { name: 'Connect', exact: true }).click()
        await page.getByRole('button', { name: /^Install v/ }).click()
        await page.getByText('The install stopped').waitFor({ timeout: 60000 })
        assert.ok(await page.getByText('Check the cable and start again.').isVisible())
        await fix(page, 'flash-stopped')
      })
  })
