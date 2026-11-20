// node grab.mjs <demo url> <dir>: the demo's status and deck pictures for board.py.
import { mkdirSync, writeFileSync } from 'node:fs'
import { chromium } from './lib.mjs'

// what the pictures show differently from the demo
const STATUS = { volume: 40, screen_off_s: 1800, temp_c: 43 }

const [url, dir] = process.argv.slice(2)
mkdirSync(dir, { recursive: true })
const browser = await chromium.launch()
const page = await browser.newPage({ viewport: { width: 1280, height: 800 } })
await page.goto(url)
await page.waitForTimeout(2500)
const status = { ...(await page.evaluate(() => fetch('/api/status').then((r) => r.json()))), ...STATUS }
delete status.t
writeFileSync(`${dir}/status.json`, JSON.stringify(status, null, 1))

const picture = (i) =>
  page.evaluate(async ([w, h]) => {
    const c = Object.assign(document.createElement('canvas'), { width: w, height: h })
    const g = c.getContext('2d')
    g.fillRect(0, 0, w, h)
    for (const img of document.querySelectorAll('img[src^="data:"][style*="left"]')) {
      const s = img.style
      const [x, y, iw, ih] = [s.left, s.top, s.width, s.height].map((v) => parseFloat(v) / 100)
      g.drawImage(img, x * w, y * h, iw * w, ih * h)
    }
    return c.toDataURL('image/jpeg', 0.92)
  }, [status.decks[0].width, status.decks[0].height]).then((d) => writeFileSync(`${dir}/deck-${i}.jpg`, Buffer.from(d.split(',')[1], 'base64')))

// deck-0.jpg as the demo starts, then one picture after each of these keys
const KEYS = [1, 8]
await picture(0)
for (const [i, k] of KEYS.entries()) {
  await page.getByLabel(new RegExp(`^Key ${k}, row`)).click()
  await page.waitForTimeout(800)
  await picture(i + 1)
}
await browser.close()
