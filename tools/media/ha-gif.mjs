// node ha-gif.mjs <en|de> <frame dir>: a dashboard while the deck is pressed; prints the crop for gif.sh.
import { chromium, CURSOR, mouse, recorder } from './lib.mjs'
import { BOARD, HA, hass, login, setLanguage, themed, waitForTheme } from './ha-shots.mjs'

const SCENES = [
  ['hold', 1200],
  ['press', 1, 1], // CAM 1 goes live in the picture, Controls says key 1
  ['hold', 2000],
  ['press', 8, 2], // the Lights folder
  ['hold', 2000],
  ['slider', 80, 40], // the brightness
  ['hold', 1800],
]
const NAMES = { en: ['Brightness', 'Screen', 'Controls'], de: ['Helligkeit', 'Bildschirm', 'Bedienung'] }

const [lang, dir] = process.argv.slice(2)
const names = NAMES[lang]
const browser = await chromium.launch()
const main = await login(browser)
await setLanguage(main, lang)
await hass(main, async ([names]) => {
  const h = document.querySelector('home-assistant').hass
  const boards = await h.callWS({ type: 'lovelace/dashboards/list' })
  if (!boards.some((d) => d.url_path === 'dashboard-live'))
    await h.callWS({ type: 'lovelace/dashboards/create', url_path: 'dashboard-live', title: 'Live', mode: 'storage' })
  const full = { columns: 'full' }
  await h.callWS({ type: 'lovelace/config/save', url_path: 'dashboard-live', config: { views: [{ path: 'live', type: 'sections', max_columns: 2, sections: [
    { type: 'grid', cards: [{ type: 'picture-entity', entity: 'camera.stream_deck_screen', camera_view: 'live', show_name: false, show_state: false, grid_options: full }] },
    { type: 'grid', cards: [
      { type: 'tile', entity: 'number.studio_deck_brightness', name: names[0], features: [{ type: 'numeric-input', style: 'slider' }], grid_options: full },
      { type: 'tile', entity: 'switch.studio_deck_screen', name: names[1], grid_options: full },
      { type: 'tile', entity: 'event.stream_deck_controls', name: names[2], state_content: 'event_type', grid_options: full },
    ] },
  ] }] } })
  await h.callService('number', 'set_value', { entity_id: 'number.studio_deck_brightness', value: 80 })
}, names)
await fetch(`${BOARD}/_press?show=0`)
const state = await main.context().storageState()

const ctx = await themed(browser, state, lang, true, { viewport: { width: 1100, height: 460 } })
await ctx.addInitScript(CURSOR)
const page = await ctx.newPage()
await page.goto(`${HA}/dashboard-live/live`)
await waitForTheme(page, true)
const picture = page.locator('hui-picture-entity-card img').first()
await page.waitForFunction((img) => img.complete && img.naturalWidth > 0, await picture.elementHandle())
await page.waitForTimeout(1000)
const crop = await page.evaluate(() => {
  const found = []
  const walk = (n) => {
    if (n.matches?.('hui-picture-entity-card, hui-tile-card')) found.push(n.getBoundingClientRect())
    ;[...(n.shadowRoot?.children ?? []), ...n.children].forEach(walk)
  }
  walk(document.body)
  const [x, y] = [Math.min(...found.map((r) => r.left)), Math.min(...found.map((r) => r.top))]
  return [Math.max(...found.map((r) => r.right)) - x, Math.max(...found.map((r) => r.bottom)) - y, x, y].map(Math.round)
})
const m = mouse(page)
await m.park([300, 440])
const rec = await recorder(ctx, page, dir)
const STEPS = {
  hold: (ms) => page.waitForTimeout(ms),
  press: async (k, show) => {
    if (!(await fetch(`${BOARD}/_press?k=${k}&show=${show}`)).ok) throw new Error('the board did not take the press')
  },
  slider: async (from, to) => {
    const b = await page.locator('ha-control-slider').first().boundingBox()
    const at = (v) => [b.x + (b.width * v) / 100, b.y + b.height / 2]
    await m.drag(at(from), at(to), 900)
  },
}
for (const [step, ...args] of SCENES) await STEPS[step](...args)
await rec.stop()
await browser.close()
console.log(`crop ${crop.join(':')}`)
