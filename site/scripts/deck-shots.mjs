// Renders the deck pictures for the docs with the firmware's own db_ui code (deck-shots.c), shows them in the
// real web UI's live deck and saves that crop as webp in public/docs/.
// Run it with tools/media/deck.sh.
import { execFileSync } from 'node:child_process'
import { mkdtempSync, readFileSync, rmSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { createRequire } from 'node:module'

const root = new URL('../../', import.meta.url).pathname
const web = process.argv[2] ?? 'http://127.0.0.1:5470'
const out = join(root, 'site/public/docs')
const require = createRequire(join(root, 'web/package.json'))
const { chromium } = require('playwright')
const sharp = createRequire(join(root, 'site/package.json'))('sharp')

const work = mkdtempSync(join(process.env.DECK_SHOTS_TMP ?? tmpdir(), 'deck-shots-'))
const ui = join(root, 'firmware/components/db_ui')
const core = join(root, 'firmware/components/db_core')
const bin = join(work, 'deck-shots')
execFileSync('gcc', [
  '-std=gnu2x',
  '-O1',
  `-I${ui}`,
  `-I${core}/include`,
  join(root, 'site/scripts/deck-shots.c'),
  ...['screens.c', 'render.c', 'canvas.c', 'font_data.c'].map((f) => join(ui, f)),
  ...['db_driver.c', 'db_device.c', 'db_screen.c'].map((f) => join(core, f)),
  ...execFileSync('sh', ['-c', `ls ${core}/drivers/*.c`])
    .toString()
    .trim()
    .split('\n'),
  '-lm',
  '-o',
  bin,
])
execFileSync(bin, [work])

// Same geometry as the firmware's Stream Deck + canvas and the demo (web/src/demo/model.ts).
const keys = [12, 172].flatMap((y) => [13, 232, 451, 670].map((x) => ({ x, y, w: 120, h: 120 })))
const strip = { x: 0, y: 380, w: 800, h: 100 }

const sim = (process.env.DC_SIM ?? 'http://127.0.0.1:18470') + '/api/'
const headers = {
  Authorization: 'Basic ' + btoa('admin:simulator-pass'),
  'X-DeckConnector': '1',
  'Content-Type': 'application/json',
}
for (const [path, body] of [
  ['setup', '{"step":0}'],
  ['config', '{"setup_done":true,"brightness":100}'],
]) {
  const r = await fetch(sim + path, { method: 'PUT', headers, body })
  if (!r.ok) throw new Error(`PUT ${path}: ${r.status}`)
}

const browser = await chromium.launch()
const page = await browser.newPage({
  viewport: { width: 1280, height: 900 },
  deviceScaleFactor: 2,
  colorScheme: 'dark',
})
let url = ''
await page.route('**/api/input**', (r) => r.fulfill({ json: { ok: true } }))
await page.route('**/api/screen*', (r) =>
  r.fulfill({
    json: {
      writer: 'ui',
      width: 800,
      height: 480,
      keys,
      touch: [strip],
      encoders: 4,
      images: [{ x: 0, y: 0, w: 800, h: 480, url }],
    },
  }),
)
// The simulator draws nothing; its own screen events would blank the picture.
await page.routeWebSocket('**/api/ws', (ws) => {
  const server = ws.connectToServer()
  server.onMessage((m) => {
    if (!String(m).includes('"t":"screen"')) ws.send(m)
  })
})
await page.goto(web)
await page.locator('#lpw').fill('simulator-pass')
await page.keyboard.press('Enter')
await page.locator('.front').waitFor()

// The host switch only changes the top row, so those pictures keep just that row.
const raw = (name) => {
  const ppm = readFileSync(join(work, `${name}.ppm`))
  return sharp(ppm.subarray(ppm.indexOf('255\n') + 4), { raw: { width: 800, height: 480, channels: 3 } })
}
// The troubleshooting table: the touch strip of each status screen, without the brightness on its left.
for (const lang of ['en', 'de'])
  for (const [name, left] of [
    ['no-network', 200],
    ['companion', 200],
    ['companion-away', 200],
    ['app', 200],
    ['server', 200],
    ['server-off', 200],
    ['request', 0],
    ['offer', 0],
  ]) {
    const file = join(out, `deck-st-${name}-${lang}.webp`)
    await raw(`st-${name}-${lang}`)
      .extract({ left, top: 380, width: 800 - left, height: 100 })
      .webp({ quality: 90 })
      .toFile(file)
    console.log(file)
  }

for (const [name, row] of ['en', 'de'].flatMap((l) => [
  [`menu-${l}`, false],
  [`away-${l}`, true],
  [`asking-${l}`, true],
])) {
  const png = await raw(name).png().toBuffer()
  url = `data:image/png;base64,${png.toString('base64')}`
  await page.reload()
  await page.locator('.front .face img').first().waitFor()
  await page.addStyleTag({
    content:
      'html,body,.card,.deckcard,.fwrap{background:transparent!important;box-shadow:none!important}.fstrip .z{border:0!important}',
  })
  await page.waitForTimeout(500)
  const front = await page.locator('.front').boundingBox()
  const k0 = await page.locator('.fkey').nth(0).boundingBox()
  const k3 = await page.locator('.fkey').nth(3).boundingBox()
  const m = k0.height * 0.12
  const clip = row
    ? { x: k0.x - m, y: k0.y - m, width: k3.x + k3.width - k0.x + 2 * m, height: k0.height + 2 * m }
    : front
  const shot = await page.screenshot({ clip, omitBackground: true })
  const file = join(out, `deck-${name}.webp`)
  await sharp(shot).webp({ quality: 90 }).toFile(file)
  console.log(file)
}
await browser.close()
rmSync(work, { recursive: true })
