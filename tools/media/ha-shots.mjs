// node ha-shots.mjs <out dir> [en-dark ...]: Home Assistant pictures as PNG; webp.py turns them into the docs files.
import { mkdirSync } from 'node:fs'
import { chromium } from './lib.mjs'

export const HA = 'http://127.0.0.1:18140'
export const BOARD = 'http://127.0.0.1:18141'
const VARIANTS = ['en-dark', 'en-light', 'de-dark', 'de-light']
const DASHBOARD_NAMES = { en: ['Brightness', 'Screen', 'Source', 'Controls'], de: ['Helligkeit', 'Bildschirm', 'Quelle', 'Bedienung'] }

export async function login(browser) {
  const page = await browser.newPage({ viewport: { width: 1400, height: 1000 } })
  await page.goto(HA + '/')
  await page.getByLabel('Username').fill('test')
  await page.getByRole('textbox', { name: 'Password' }).fill('test-pass-1')
  await page.keyboard.press('Enter')
  await page.waitForURL(/lovelace|home/, { timeout: 30000 })
  await page.waitForTimeout(1500)
  return page
}

export const hass = (page, fn, ...args) => page.evaluate(fn, args)

export async function themed(browser, state, lang, dark, size = { viewport: { width: 1400, height: 2000 }, deviceScaleFactor: 2 }) {
  const ctx = await browser.newContext({ storageState: state, locale: lang, colorScheme: dark ? 'dark' : 'light', ...size })
  await ctx.addInitScript(([lang, dark]) => {
    localStorage.setItem('selectedTheme', JSON.stringify({ theme: 'default', dark }))
    localStorage.setItem('selectedLanguage', JSON.stringify(lang))
  }, [lang, dark])
  return ctx
}

export async function setLanguage(page, lang) {
  await hass(page, async ([lang]) => {
    const h = document.querySelector('home-assistant').hass
    await h.callWS({ type: 'config/core/update', language: lang })
    const [entry] = await h.callWS({ type: 'config_entries/get', domain: 'deckconnector' })
    await h.callApi('POST', `config/config_entries/entry/${entry.entry_id}/reload`)
  }, lang)
  await new Promise((r) => setTimeout(r, 6000))
}

// the theme can arrive after the first paint of a new context
export async function waitForTheme(page, dark) {
  await page.waitForFunction((dark) => {
    const [r, g, b] = getComputedStyle(document.documentElement).backgroundColor.match(/\d+/g).map(Number)
    return r + g + b < 384 === dark
  }, dark, { timeout: 20000 })
}

async function clip(page, boxes, path, margin = 16) {
  const x = Math.min(...boxes.map((b) => b.x)) - margin
  const y = Math.min(...boxes.map((b) => b.y)) - margin
  const width = Math.max(...boxes.map((b) => b.x + b.width)) + margin - x
  const height = Math.max(...boxes.map((b) => b.y + b.height)) + margin - y
  await page.screenshot({ path, clip: { x, y, width, height } })
}

// the cards in the device page's middle column, without "+n disabled entities" and a card that only had those
async function cards(page) {
  await page.evaluate(() => {
    const walk = (root) => {
      for (const el of root.querySelectorAll('*')) {
        if (el.shadowRoot) walk(el.shadowRoot)
        if (!el.children.length && /disabled entit|deaktivierte Entit/i.test(el.textContent)) (el.closest('button,a') ?? el).style.display = 'none'
      }
    }
    walk(document)
  })
  await page.waitForTimeout(400)
  const els = await page.locator('ha-config-device-page .column').nth(1).locator(':scope > *').all()
  const shown = []
  for (const el of els) {
    const entities = (await el.evaluate((e) => e.tagName)) === 'HA-DEVICE-ENTITIES-CARD'
    if (!entities || (await el.locator('ha-state-icon, state-badge').count())) shown.push(el)
  }
  return (await Promise.all(shown.map((el) => el.boundingBox()))).filter((b) => b && b.height > 0)
}

async function dashboard(page, names) {
  await hass(page, async ([names]) => {
    const h = document.querySelector('home-assistant').hass
    const boards = await h.callWS({ type: 'lovelace/dashboards/list' })
    if (!boards.some((d) => d.url_path === 'dashboard-studio'))
      await h.callWS({ type: 'lovelace/dashboards/create', url_path: 'dashboard-studio', title: 'Studio', mode: 'storage', icon: 'mdi:view-grid' })
    await h.callWS({ type: 'lovelace/config/save', url_path: 'dashboard-studio', config: { views: [{ title: 'Studio', path: 'studio', type: 'sections', max_columns: 1, sections: [{ type: 'grid', cards: [
      { type: 'heading', heading: 'Stream Deck +' },
      { type: 'picture-entity', entity: 'camera.stream_deck_screen', show_name: false, show_state: false, grid_options: { columns: 12 } },
      { type: 'tile', entity: 'number.studio_deck_brightness', name: names[0], features: [{ type: 'numeric-input', style: 'slider' }], grid_options: { columns: 12 } },
      { type: 'tile', entity: 'switch.studio_deck_screen', name: names[1] },
      { type: 'tile', entity: 'sensor.stream_deck_source', name: names[2] },
      { type: 'tile', entity: 'event.stream_deck_controls', name: names[3], grid_options: { columns: 12 } },
    ] }] }] } })
  }, names)
}

async function devices(page) {
  return Object.fromEntries(await hass(page, () =>
    Object.values(document.querySelector('home-assistant').hass.devices)
      .filter((d) => d.identifiers.some(([domain]) => domain === 'deckconnector'))
      .map((d) => [d.via_device_id ? 'deck' : 'board', d.id])))
}

if (import.meta.url === `file://${process.argv[1]}`) {
  const [out, ...only] = process.argv.slice(2)
  mkdirSync(out, { recursive: true })
  const browser = await chromium.launch()
  const main = await login(browser)
  const dev = await devices(main)
  const state = await main.context().storageState()

  for (const lang of ['en', 'de']) {
    const wanted = VARIANTS.filter((v) => v.startsWith(lang) && (!only.length || only.includes(v)))
    if (!wanted.length) continue
    await setLanguage(main, lang)
    await dashboard(main, DASHBOARD_NAMES[lang])
    await fetch(`${BOARD}/_press?k=1&show=1`)
    for (const v of wanted) {
      const ctx = await themed(browser, state, lang, v.endsWith('dark'))
      const page = await ctx.newPage()
      for (const name of ['board', 'deck']) {
        await page.goto(`${HA}/config/devices/device/${dev[name]}`)
        await waitForTheme(page, v.endsWith('dark'))
        await page.waitForTimeout(3500)
        const boxes = await cards(page)
        if (name === 'board') boxes.pop() // Connected devices
        await clip(page, boxes, `${out}/ha-${name}-${v}.png`)
        if (v === 'en-dark') await clip(page, name === 'board' ? boxes.slice(0, 2) : boxes, `${out}/readme-${name}.png`)
      }
      if (v === 'en-dark') {
        await page.evaluate(() => document.querySelector('home-assistant').dispatchEvent(new CustomEvent('hass-more-info', { detail: { entityId: 'camera.stream_deck_screen' }, bubbles: true, composed: true })))
        await page.waitForTimeout(4000)
        await clip(page, [await page.locator('ha-more-info-dialog dialog').first().boundingBox()], `${out}/readme-screen.png`, 0)
        await page.keyboard.press('Escape')
      }
      await page.goto(`${HA}/dashboard-studio/studio`)
      await page.waitForTimeout(6000)
      await clip(page, [await page.locator('hui-grid-section').first().boundingBox()], `${out}/ha-dashboard-${v}.png`)
      await page.goto(`${HA}/config/integrations/dashboard/add?domain=deckconnector`)
      await page.waitForTimeout(3000)
      const confirm = page.getByRole('button', { name: /^ok$/i })
      if (await confirm.waitFor({ timeout: 5000 }).then(() => true, () => false)) await confirm.click()
      await page.waitForTimeout(2000)
      const input = page.locator('dialog-data-entry-flow input').first()
      await input.fill('studio-deck.local')
      await input.press('Enter')
      await page.mouse.move(0, 0)
      await page.waitForTimeout(2500)
      await clip(page, [await page.locator('dialog-data-entry-flow wa-dialog dialog').first().boundingBox()], `${out}/ha-flow-${v}.png`, 0)
      await ctx.close()
    }
  }
  await browser.close()
}
