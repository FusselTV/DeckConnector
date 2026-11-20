// node sync-gif.mjs <demo url> <frame dir>: the web UI's controls and Home Assistant on one timeline. The demo's
// volume and brightness go to board.py, which pushes them to Home Assistant like the board does.
// Prints the ffmpeg filter that puts both sides together for ha.sh.
import { chromium, CURSOR, mouse, recorder } from './lib.mjs'
import { BOARD, cardBox, HA, hass, login, themed, waitForTheme } from './ha-shots.mjs'

const SCENES = [
  ['hold', 800],
  ['slider', 'Sounds', 0, 60],
  ['hold', 1200],
  ['slider', 'Brightness', 80, 30],
  ['hold', 2200],
]

const [url, dir] = process.argv.slice(2)
const browser = await chromium.launch()
const main = await login(browser)
await hass(main, async () => {
  const h = document.querySelector('home-assistant').hass
  const boards = await h.callWS({ type: 'lovelace/dashboards/list' })
  if (!boards.some((d) => d.url_path === 'dashboard-sync'))
    await h.callWS({ type: 'lovelace/dashboards/create', url_path: 'dashboard-sync', title: 'Sync', mode: 'storage' })
  const full = { columns: 'full' }
  await h.callWS({ type: 'lovelace/config/save', url_path: 'dashboard-sync', config: { views: [{ path: 'sync', type: 'sections', max_columns: 1, sections: [
    { type: 'grid', cards: [
      { type: 'tile', entity: 'number.studio_deck_brightness', name: 'Brightness', features: [{ type: 'numeric-input', style: 'slider' }], grid_options: full },
      { type: 'tile', entity: 'media_player.studio_deck_speaker', name: 'Speaker', state_content: 'volume_level', features: [{ type: 'media-player-volume-slider' }], grid_options: full },
    ] },
  ] }] } })
})
const state = await main.context().storageState()

const web = await browser.newContext({ viewport: { width: 1280, height: 800 }, colorScheme: 'dark', reducedMotion: 'reduce' })
const sent = {}
let failed // the page does not await __relay: its errors would vanish there
await web.exposeFunction('__relay', async (raw) => {
  try {
    const cfg = JSON.parse(raw).cfg
    const patch = Object.fromEntries(['volume', 'brightness'].filter((k) => cfg[k] !== sent[k]).map((k) => [k, cfg[k]]))
    if (!Object.keys(patch).length) return
    const r = await fetch(`${BOARD}/api/config`, { method: 'PUT', headers: { Authorization: `Bearer ${'s'.repeat(64)}` }, body: JSON.stringify(patch) })
    if (!r.ok) throw new Error(`board.py took no ${JSON.stringify(patch)}: ${r.status}`)
    Object.assign(sent, patch)
  } catch (e) {
    failed ??= e
  }
})
await web.addInitScript(() => {
  const set = Storage.prototype.setItem
  Storage.prototype.setItem = function (k, v) {
    if (k === 'dc.demo') window.__relay(v)
    return set.call(this, k, v)
  }
})
await web.addInitScript(CURSOR)
const page = await web.newPage()
await page.goto(url)
await page.waitForTimeout(2500)
const webCrop = await page.getByRole('slider', { name: 'Brightness' }).evaluate((el) => {
  const r = el.closest('section.card').getBoundingClientRect()
  return [r.width, r.height, r.left, r.top].map(Math.round)
})

const ctx = await themed(browser, state, 'en', true, { viewport: { width: 420, height: 400 } })
const ha = await ctx.newPage()
await ha.goto(`${HA}/dashboard-sync/sync`)
await waitForTheme(ha, true)
await ha.waitForTimeout(1500)
const haCrop = await cardBox(ha, 'hui-tile-card')

const slider = async (label, v) => {
  const b = await page.getByRole('slider', { name: label }).boundingBox()
  return [b.x + (b.width * v) / 100, b.y + b.height / 2]
}
const m = mouse(page)
const [, label, from] = SCENES.find(([step]) => step === 'slider')
const [px, py] = await slider(label, from)
await m.park([px + 16, py + 24])
const recs = [await recorder(web, page, `${dir}/web`), await recorder(ctx, ha, `${dir}/ha`)]
const STEPS = {
  hold: (ms) => page.waitForTimeout(ms),
  slider: async (label, from, to) => m.drag(await slider(label, from), await slider(label, to), 300),
}
for (const [step, ...args] of SCENES) await STEPS[step](...args)
if (failed) throw failed
const t0 = Math.min(...recs.map((r) => r.first()))
for (const r of recs) await r.stop(t0)
await browser.close()
// the cards top-aligned on Home Assistant's background: the two pages' own backgrounds differ
const h = Math.max(webCrop[1], haCrop[1])
const side = (i, [w, ch, x, y], gap) => `[${i}]fps=12,crop=${w}:${ch}:${x}:${y},pad=${w + gap}:${h}:0:0:0x111111[s${i}]`
console.log(`${side(0, webCrop, 16)};${side(1, haCrop, 0)};[s0][s1]hstack=shortest=1,pad=iw+16:ih+16:8:8:0x111111`)
