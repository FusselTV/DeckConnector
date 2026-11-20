// node web-gif.mjs <demo url> <frame dir>: prints the crop of the deck picture dialog for gif.sh. One scene per line;
// edit SCENES to change the GIF. Also records <frame dir>/ha (connector page), /deck (deck page) and /screen (dialog).
import { center, chromium, CURSOR, deckJpeg, mouse, recorder } from './lib.mjs'
import { around, BOARD, cards, devices, HA, login, setLanguage, themed, waitForTheme } from './ha-shots.mjs'

const SCENES = [
  ['hold', 700],
  ['key', 1], // CAM 1 goes live
  ['dial', 1, 6], // Master up
  ['key', 8], // the Lights folder
  ['key', 3], // Rim on
  ['slider', 'Sounds', 0, 60],
  ['slider', 'Brightness', 80, 40],
  ['button', 'Restart'],
  ['dialog', 'Restart'],
  ['hold', 1100],
  ['fast', 30], // the rest of the countdown
  ['until', () => !document.querySelector('[role=alertdialog]')],
  ['fast', 1],
  ['hold', 400],
  ['nav', 'Deck'],
  ['point', '#speaker'],
  ['hold', 800],
]

const [url, dir] = process.argv.slice(2)
const browser = await chromium.launch()
const haBrowser = await chromium.launch() // its screenshot loops must not slow the web page down
const main = await login(haBrowser)
await setLanguage(main, 'en')
const haCtx = await themed(haBrowser, await main.context().storageState(), 'en', true, { viewport: { width: 1400, height: 1000 }, deviceScaleFactor: 2 })
const dev = await devices(main)
const ha = await haCtx.newPage()
await ha.goto(`${HA}/config/devices/device/${dev.board}`)
await waitForTheme(ha, true)
await ha.waitForTimeout(3500)
const clip = around((await cards(ha)).slice(0, 2))

const ctx = await browser.newContext({ viewport: { width: 1280, height: 800 }, colorScheme: 'dark', reducedMotion: 'reduce' })
const sent = {}
let failed // the page does not await put: its errors would vanish there
const put = async (cfg) => {
  try {
    const patch = Object.fromEntries(['volume', 'brightness'].filter((k) => cfg[k] !== sent[k]).map((k) => [k, cfg[k]]))
    if (!Object.keys(patch).length) return
    const r = await fetch(`${BOARD}/api/config`, { method: 'PUT', headers: { Authorization: `Bearer ${'s'.repeat(64)}` }, body: JSON.stringify(patch) })
    if (!r.ok) throw new Error(`board.py took no ${JSON.stringify(patch)}: ${r.status}`)
    Object.assign(sent, patch)
  } catch (e) {
    failed ??= e
  }
}
const post = async (path, body) => {
  const r = await fetch(`${BOARD}${path}`, { method: 'POST', body })
  if (!r.ok) throw new Error(`board.py: ${path} ${r.status}`)
}
let size, shown, seen, chain = Promise.resolve() // one queue keeps the pictures and inputs in order
const inputs = (log) => {
  if (seen !== undefined && (log.at(-1)?.seq ?? 0) < seen) seen = 0 // a restart starts the log over
  // the demo's first save (at load) predates the recording
  const fresh = log.filter((e) => e.seq > (seen ?? Infinity))
  seen = Math.max(seen ?? 0, ...log.map((e) => e.seq))
  if (fresh.some((e) => e.kind.startsWith('touch'))) failed ??= new Error('touch inputs are not sent to board.py')
  const body = JSON.stringify(fresh.map(({ kind, index, value }) => ({ kind, index, value: value ?? 0 })))
  if (fresh.length) chain = chain.then(() => post('/_input', body)).catch((e) => (failed ??= e))
}
const screen = () =>
  (chain = chain.then(async () => {
    if (!size) return
    const d = await page.evaluate(deckJpeg, size)
    if (!d || d === shown) return
    shown = d
    await post('/_screen', Buffer.from(d.split(',')[1], 'base64'))
  }).catch((e) => (failed ??= e)))
await ctx.exposeFunction('__put', (raw) => {
  const d = JSON.parse(raw)
  inputs(d.inputs)
  return put(d.cfg)
})
await ctx.exposeFunction('__screen', screen)
await ctx.addInitScript(() => {
  let t
  new MutationObserver(() => {
    clearTimeout(t)
    t = setTimeout(() => window.__screen(), 30)
  }).observe(document, { subtree: true, childList: true, attributes: true, attributeFilter: ['src'] })
  const set = Storage.prototype.setItem
  Storage.prototype.setItem = function (k, v) {
    if (k === 'dc.demo') window.__put(v)
    return set.call(this, k, v)
  }
})
const page = await ctx.newPage()
await page.goto(url)
await page.waitForTimeout(2500)
const status = await page.evaluate(() => fetch('/api/status').then((r) => r.json()))
await put(status)
size = [status.decks[0].width, status.decks[0].height]
await screen()
if (!shown) throw failed ?? new Error('no live deck in the demo')
// after the first picture: the deck page loads its Screen thumbnail once
const haDeck = await haCtx.newPage()
await haDeck.goto(`${HA}/config/devices/device/${dev.deck}`)
await waitForTheme(haDeck, true)
await haDeck.waitForTimeout(3500)
const deckClip = around(await cards(haDeck))
const haScreen = await haCtx.newPage()
await haScreen.goto(`${HA}/config/devices/device/${dev.deck}`)
await waitForTheme(haScreen, true)
await haScreen.evaluate(() => document.querySelector('home-assistant').dispatchEvent(new CustomEvent('hass-more-info', { detail: { entityId: 'camera.stream_deck_screen' }, bubbles: true, composed: true })))
await haScreen.waitForTimeout(4000)
const screenClip = around([await haScreen.locator('ha-more-info-dialog dialog').first().boundingBox()], 0)
await page.evaluate(CURSOR)
const m = mouse(page)
await m.park([1150, 720])
await ha.waitForTimeout(1500)
const t0 = Date.now() / 1000
const rec = await recorder(ctx, page, dir)
const haRecs = [
  await recorder(haCtx, ha, `${dir}/ha`, clip),
  await recorder(haCtx, haDeck, `${dir}/deck`, deckClip),
  await recorder(haCtx, haScreen, `${dir}/screen`), // no clip: screenshots are too slow here; ha.sh crops the screencast
]

const STEPS = {
  hold: (ms) => page.waitForTimeout(ms),
  key: async (n) => m.click(await center(page.getByLabel(new RegExp(`^Key ${n}, row`))), 450),
  dial: async (n, clicks) => {
    const dial = page.getByLabel(new RegExp(`^Dial ${n}\\.`))
    await m.move(...(await center(dial)))
    await dial.focus()
    for (let i = 0; i < clicks; i++) {
      await page.mouse.wheel(0, 120)
      await page.waitForTimeout(70)
    }
    await dial.blur()
    await page.waitForTimeout(350)
  },
  slider: async (label, from, to) => {
    const b = await page.getByRole('slider', { name: label }).boundingBox()
    const at = (v) => [b.x + (b.width * v) / 100, b.y + b.height / 2]
    await m.drag(at(from), at(to), 300)
  },
  button: async (name) => m.click(await center(page.getByRole('button', { name, exact: true })), 500),
  dialog: async (name) => m.click(await center(page.locator('dialog[open]').getByRole('button', { name })), 300),
  until: (fn) => page.waitForFunction(fn, null, { timeout: 30000 }),
  fast: (n) => [rec, ...haRecs].forEach((r) => r.speed(n)),
  nav: async (name) => m.click(await center(page.locator('aside.side').getByText(name, { exact: true })), 700),
  point: async (sel) => m.move(...(await center(page.locator(sel)))),
}

for (const [step, ...args] of SCENES) await STEPS[step](...args)
await chain
if (failed) throw failed
const t1 = Date.now() / 1000
for (const r of [rec, ...haRecs]) await r.stop(t0, t1)
await Promise.all([browser.close(), haBrowser.close()])
console.log(`crop ${[screenClip.width, screenClip.height, screenClip.x, screenClip.y].map(Math.round).join(':')}`)
