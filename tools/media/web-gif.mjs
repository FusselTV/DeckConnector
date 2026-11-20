// node web-gif.mjs <demo url> <frame dir>. One scene per line; edit SCENES to change the GIF.
// Records Home Assistant's connector page on the same timeline to <frame dir>/ha. The demo's volume and brightness
// go to board.py, which pushes them to Home Assistant like the board does.
import { center, chromium, CURSOR, mouse, recorder } from './lib.mjs'
import { BOARD, cards, devices, HA, login, setLanguage, themed, waitForTheme } from './ha-shots.mjs'

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
const main = await login(browser)
await setLanguage(main, 'en')
const haCtx = await themed(browser, await main.context().storageState(), 'en', true, { viewport: { width: 1400, height: 1000 }, deviceScaleFactor: 2 })
const ha = await haCtx.newPage()
await ha.goto(`${HA}/config/devices/device/${(await devices(main)).board}`)
await waitForTheme(ha, true)
await ha.waitForTimeout(3500)
const [x, y, r, b] = (await cards(ha)).slice(0, 2).reduce(([x, y, r, b], c) => [Math.min(x, c.x), Math.min(y, c.y), Math.max(r, c.x + c.width), Math.max(b, c.y + c.height)], [1e9, 1e9, 0, 0])
const clip = { x: x - 16, y: y - 16, width: r - x + 32, height: b - y + 32 } // ha-shots' margin

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
await ctx.exposeFunction('__put', (raw) => put(JSON.parse(raw).cfg))
await ctx.addInitScript(() => {
  const set = Storage.prototype.setItem
  Storage.prototype.setItem = function (k, v) {
    if (k === 'dc.demo') window.__put(v)
    return set.call(this, k, v)
  }
})
const page = await ctx.newPage()
await page.goto(url)
await page.waitForTimeout(2500)
await put(await page.evaluate(() => fetch('/api/status').then((r) => r.json())))
await page.evaluate(CURSOR)
const m = mouse(page)
await m.park([1150, 720])
await ha.waitForTimeout(1500)
const rec = await recorder(ctx, page, dir)
const haRec = await recorder(haCtx, ha, `${dir}/ha`, clip)

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
  fast: (n) => [rec, haRec].forEach((r) => r.speed(n)),
  nav: async (name) => m.click(await center(page.locator('aside.side').getByText(name, { exact: true })), 700),
  point: async (sel) => m.move(...(await center(page.locator(sel)))),
}

for (const [step, ...args] of SCENES) await STEPS[step](...args)
if (failed) throw failed
const [t0, t1] = [Math.min(rec.first(), haRec.first()), Date.now() / 1000]
for (const r of [rec, haRec]) await r.stop(t0, t1)
await browser.close()
