// node web-gif.mjs <demo url> <frame dir>: prints the crop of the deck picture dialog for readme.py. One scene per line;
// edit SCENES to change the GIF. Also records <frame dir>/ha (connector page), /deck (deck page) and /screen (dialog).
import { writeFileSync } from 'node:fs'
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
// the screencast sends CSS pixels; the flag makes them device pixels. Not for Home Assistant: its screenshots fall behind
const browser = await chromium.launch({ args: ['--force-device-scale-factor=2'] })
const haBrowser = await chromium.launch() // its screenshot loops must not slow the web page down
const main = await login(haBrowser)
await setLanguage(main, 'en')
const haCtx = await themed(haBrowser, await main.context().storageState(), 'en', true, { viewport: { width: 1400, height: 1000 }, deviceScaleFactor: 2 })
const dev = await devices(main)
const ha = await haCtx.newPage()
await ha.goto(`${HA}/config/devices/device/${dev.board}`)
await waitForTheme(ha, true)
await ha.waitForTimeout(3500)
const clip = around((await cards(ha)).slice(0, 1)) // Controls

const ctx = await browser.newContext({ viewport: { width: 1280, height: 800 }, deviceScaleFactor: 2, colorScheme: 'dark', reducedMotion: 'reduce' })
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
const timeline = [] // [Date.now() seconds, path, body] for readme.py
const post = async (path, body, t = Date.now() / 1000) => {
  timeline.push([t, path, body])
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
  const t = Date.now() / 1000 // when the demo took them, not when the queue gets to them
  if (fresh.length) chain = chain.then(() => post('/_input', body, t)).catch((e) => (failed ??= e))
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
const deckClip = around((await cards(haDeck)).slice(-1)) // Events: the Screen sensor thumbnail loads once
const haScreen = await haCtx.newPage()
await haScreen.goto(`${HA}/config/devices/device/${dev.deck}`)
await waitForTheme(haScreen, true)
// HA ignores the event until its frontend has loaded the dialog, so it is sent until the dialog opens
await haScreen.waitForFunction(() => {
  document.querySelector('home-assistant').dispatchEvent(new CustomEvent('hass-more-info', { detail: { entityId: 'camera.stream_deck_screen' }, bubbles: true, composed: true }))
  return document.querySelector('home-assistant')?.shadowRoot?.querySelector('ha-more-info-dialog')
}, null, { polling: 1000, timeout: 20000 })
await haScreen.waitForTimeout(4000)
const dialog = haScreen.locator('ha-more-info-dialog dialog').first()
const screenClip = around([await dialog.boundingBox()], 0)
const radius = await dialog.evaluate((d) => parseFloat(getComputedStyle(d).borderTopLeftRadius))
const box = (r) => [r.x, r.y, r.width, r.height].map(Math.round)
const d = await page.evaluate(() => document.querySelector('img[src^="data:"][style*="left"]').parentElement.getBoundingClientRect().toJSON())
const deckBox = [d.x, d.y, d.width, d.height] // not rounded: at 2x half a CSS pixel shifts the crop enough to mix up dial values
const camBox = await haScreen.evaluate(() => {
  const walk = (n) => n.src?.includes('camera_proxy_stream') ? n : [...(n.shadowRoot?.children ?? []), ...n.children].reduce((f, c) => f ?? walk(c), null)
  return walk(document.body)?.getBoundingClientRect().toJSON()
})
if (!camBox) throw new Error('no camera stream in the dialog')
// a first real press, so the Events card starts with this run's input and not one from the docs pictures
await page.getByLabel(/^Key 2, row/).click() // CAM 2, already live
await page.waitForTimeout(300)
await chain
if (!timeline.some(([, path]) => path === '/_input')) throw failed ?? new Error('the first press did not reach board.py')
await page.evaluate(CURSOR)
const m = mouse(page)
await m.park([1150, 720])
await ha.waitForTimeout(1500)
const t0 = Date.now() / 1000
const rec = await recorder(ctx, page, dir, undefined, 'jpeg')
const haRecs = [
  await recorder(haCtx, ha, `${dir}/ha`, clip),
  await recorder(haCtx, haDeck, `${dir}/deck`, deckClip),
  await recorder(haCtx, haScreen, `${dir}/screen`), // no clip: screenshots are too slow here; readme.py crops the screencast
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
      await page.waitForTimeout(120) // each value lasts a GIF frame; the 2x screencast misses shorter ones
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
const out = await rec.stop(t0, t1)
for (const r of haRecs) await r.stop(t0, t1)
const bar = await (await themed(haBrowser, await main.context().storageState(), 'en', true, { viewport: { width: 1000, height: 300 }, deviceScaleFactor: 2 })).newPage()
await bar.goto(`${HA}/config/devices/device/${dev.deck}`)
await waitForTheme(bar, true)
const toolbar = await bar.locator('hass-subpage .toolbar').first().boundingBox()
await bar.getByText('Stream Deck +').first().waitFor()
await bar.waitForTimeout(1000)
await bar.screenshot({ path: `${dir}/ha-bar.png`, clip: { x: 0, y: 0, width: 1000, height: toolbar.y + toolbar.height } })
const pictures = timeline.filter(([, path]) => path === '/_screen').map(([, , body]) => body)
pictures.forEach((b, i) => writeFileSync(`${dir}/screen-${i}.jpg`, b))
writeFileSync(`${dir}/timeline.json`, JSON.stringify({ deck: deckBox, camera: box(camBox), radius, events: timeline.filter(([t, path]) => t >= t0 || path === '/_screen').map(([t, path, body]) => [out(t), path === '/_screen' ? pictures.indexOf(body) : JSON.parse(body)]) }))
await Promise.all([browser.close(), haBrowser.close()])
console.log(`crop ${[screenClip.width, screenClip.height, screenClip.x, screenClip.y].map(Math.round).join(':')}`)
