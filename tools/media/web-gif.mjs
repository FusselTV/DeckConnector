// node web-gif.mjs <demo url> <frame dir>. One scene per line; edit SCENES to change the GIF.
import { center, chromium, CURSOR, mouse, recorder } from './lib.mjs'

const SCENES = [
  ['hold', 700],
  ['key', 1], // CAM 1 goes live
  ['dial', 1, 6], // Master up
  ['key', 8], // the Lights folder
  ['key', 3], // Rim on
  ['slider', 'Sounds', 0, 60],
  ['button', 'Restart'],
  ['dialog', 'Restart'],
  ['hold', 1500],
  ['fast', 30], // the rest of the countdown
  ['until', () => !document.querySelector('[role=alertdialog]')],
  ['fast', 1],
  ['hold', 400],
  ['nav', 'Deck'],
  ['point', '#speaker'],
  ['hold', 1000],
]

const [url, dir] = process.argv.slice(2)
const browser = await chromium.launch()
const ctx = await browser.newContext({ viewport: { width: 1280, height: 800 }, colorScheme: 'dark', reducedMotion: 'reduce' })
const page = await ctx.newPage()
await page.goto(url)
await page.waitForTimeout(2500)
await page.evaluate(CURSOR)
const m = mouse(page)
await m.park([1150, 720])
const rec = await recorder(ctx, page, dir)

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
  fast: (n) => rec.speed(n),
  nav: async (name) => m.click(await center(page.locator('aside.side').getByText(name, { exact: true })), 700),
  point: async (sel) => m.move(...(await center(page.locator(sel)))),
}

for (const [step, ...args] of SCENES) await STEPS[step](...args)
await rec.stop()
await browser.close()
