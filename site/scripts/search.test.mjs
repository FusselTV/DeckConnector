// The docs search in a real browser against the production build in out/: each query, typed into the dialog and
// sent with Enter, must land on one of its headings. node --test scripts/search.test.mjs
import assert from 'node:assert/strict'
import { createReadStream, existsSync, statSync } from 'node:fs'
import { createServer } from 'node:http'
import { extname, join } from 'node:path'
import { after, before, test } from 'node:test'

const OUT = new URL('../out/', import.meta.url).pathname
const { chromium } = await import(new URL('../../web/node_modules/playwright/index.mjs', import.meta.url).href)

const queries = {
  en: {
    'wifi password': [
      '/docs/network#wi-fi',
      '/docs/first-setup#over-the-setup-wi-fi',
      '/docs/first-setup#in-the-browser',
      '/docs/security#setup-wi-fi',
    ],
    16622: [
      '/docs/network#ports',
      '/docs/companion#satellite',
      '/docs/troubleshooting#companion-does-not-connect',
      '/docs/companion#address-forms',
    ],
    'companion satellite': ['/docs/companion#satellite', '/docs/companion'],
    'compnion satelite': ['/docs/companion#satellite', '/docs/companion'],
    'flash fails': ['/docs/flashing', '/docs/flash-troubleshooting'],
    'static ip': ['/docs/network#fixed-address'],
    'api key': ['/docs/security#api-keys', '/docs/home-assistant#api-key'],
    ota: ['/docs/updates'],
    'forgot password': ['/docs/security#lost-password'],
    'factory reset': ['/docs/first-setup#factory-reset', '/docs/security#lost-password'],
    'elgato app does not find deck': ['/docs/troubleshooting#the-elgato-app-does-not-find-the-deck'],
    websocket: ['/docs/api#websocket'],
    'mdns name': ['/docs/network#names'],
    'Authorization: Bearer': [
      '/docs/security#api-keys',
      '/docs/api#login',
      '/docs/home-assistant#api-key',
      '/docs/own-server#connection',
    ],
    'hidden network': ['/docs/network#wi-fi'],
    port: ['/docs/network#ports'],
    ports: ['/docs/network#ports'],
    wlan: ['/docs/network#wi-fi', '/docs/first-setup#over-the-setup-wi-fi'],
    'wi-fi': ['/docs/network#wi-fi', '/docs/first-setup#over-the-setup-wi-fi'],
    wifi: ['/docs/network#wi-fi', '/docs/first-setup#over-the-setup-wi-fi'],
  },
  de: {
    'passwort vergessen': ['/de/docs/security#passwort-vergessen'],
    'wlan passwort': [
      '/de/docs/network#wlan',
      '/de/docs/first-setup#über-das-setup-wlan',
      '/de/docs/first-setup#im-browser',
      '/de/docs/security#setup-wlan',
    ],
    'feste ip': ['/de/docs/network#feste-adresse'],
    'api schlüssel': ['/de/docs/security#api-schlüssel', '/de/docs/home-assistant#api-schlüssel'],
    werkseinstellungen: ['/de/docs/first-setup#werkszustand', '/de/docs/security#passwort-vergessen'],
    'companion verbindet nicht': ['/de/docs/troubleshooting#companion-verbindet-sich-nicht'],
    'flashen klappt nicht': ['/de/docs/flashing', '/de/docs/flash-troubleshooting'],
    zertifikat: ['/de/docs/security#https'],
    absturzbericht: ['/de/docs/troubleshooting#logs-und-absturzberichte'],
    satelite: ['/de/docs/companion#satellite', '/de/docs/companion'],
    16622: [
      '/de/docs/network#ports',
      '/de/docs/companion#satellite',
      '/de/docs/troubleshooting#companion-verbindet-sich-nicht',
      '/de/docs/companion#adressformen',
    ],
    'ota update': ['/de/docs/updates'],
    port: ['/de/docs/network#ports'],
    wlan: ['/de/docs/network#wlan', '/de/docs/first-setup#über-das-setup-wlan'],
    wifi: ['/de/docs/network#wlan', '/de/docs/first-setup#über-das-setup-wlan'],
  },
}

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
let page

before(async () => {
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
  page = await browser.newPage({ viewport: { width: 1280, height: 800 } })
  page.setDefaultTimeout(15000)
})

after(async () => {
  await browser?.close()
  server?.close()
})

/** Where Enter goes once the result list has stopped changing. */
async function land(lang, q) {
  await page.goto(`${base}${lang === 'de' ? '/de' : ''}/docs/`)
  const input = page.locator('[data-fd-search-dialog-input]')
  for (let i = 0; i < 10 && !(await input.isVisible()); i++) {
    await page.keyboard.press('Control+k')
    await page.waitForTimeout(300)
  }
  await input.fill(q)
  let last = ''
  for (let same = 0; same < 3;) {
    await page.waitForTimeout(150)
    const now = await page
      .locator('[role=option]')
      .evaluateAll((os) => os.map((o) => `${o.getAttribute('aria-selected')}${o.textContent}`).join('|'))
    same = now && now === last ? same + 1 : 0
    last = now
  }
  const long = await page
    .locator('[role=option]')
    .evaluateAll((os) => os.map((o) => o.textContent.trim()).filter((t) => t.length > 140))
  assert.deepEqual(long, [], `"${q}" shows long results`)
  const from = page.url()
  await page.keyboard.press('Enter')
  await page.waitForURL((u) => u.href !== from)
  const u = new URL(page.url())
  const hash = decodeURIComponent(u.hash.slice(1))
  if (hash) await page.waitForFunction((h) => document.getElementById(h), hash)
  return decodeURIComponent(u.pathname.replace(/\/$/, '') + u.hash)
}

for (const [lang, set] of Object.entries(queries))
  for (const [q, want] of Object.entries(set))
    test(`[${lang}] ${q}`, async () => {
      const got = await land(lang, q)
      assert.ok(
        want.some((w) => got === w || (!w.includes('#') && got.split('#')[0] === w)),
        `"${q}" lands on ${got}, want ${want.join(' or ')}`,
      )
    })
