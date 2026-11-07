import { expect, sim, test } from './fixtures.ts'

const TABS = ['Overview', 'Sources', 'Deck', 'Network', 'Access', 'Firmware', 'Diagnostics']

async function openTab(page: import('@playwright/test').Page, name: string) {
  if (await page.locator('.side').isVisible()) await page.getByRole('tab', { name: new RegExp(`^${name}`) }).click()
  else if (['Overview', 'Sources', 'Diagnostics'].includes(name))
    await page
      .locator('.tabbar')
      .getByRole('button', { name: new RegExp(name) })
      .click()
  else {
    await page.locator('.tabbar').getByRole('button', { name: /More/ }).click()
    await page.getByRole('dialog').getByRole('button', { name }).click()
  }
}

test('every tab renders in both languages and themes', async ({ app: page }) => {
  for (const name of TABS) {
    await openTab(page, name)
    await expect(page.locator('#tabpanel > .stack:not([hidden])').first()).toBeVisible()
  }
  await page.evaluate(() => {
    localStorage.setItem('dc.lang', '"de"')
    localStorage.setItem('dc.theme', '"light"')
  })
  await page.reload()
  await expect(page.locator('html')).toHaveAttribute('lang', 'de')
  await expect(page.locator('html')).toHaveAttribute('data-theme', 'light')
  await expect(page.getByText('Übersicht').filter({ visible: true }).first()).toBeVisible()
})

test('a setting goes into the save bar and can be discarded or saved', async ({ app: page }) => {
  await openTab(page, 'Deck')
  await page.getByRole('radio', { name: /Two keys/ }).click()
  const bar = page.getByRole('region', { name: 'Unsaved changes' })
  await expect(bar).toBeVisible()
  await openTab(page, 'Network')
  await expect(bar).toBeVisible()
  await bar.getByRole('button', { name: 'Discard' }).click()
  await expect(bar).toHaveCount(0)
  await openTab(page, 'Deck')
  await page.getByRole('radio', { name: /Two keys/ }).click()
  await bar.getByRole('button', { name: /^Save/ }).click()
  await expect(bar).toHaveCount(0)
  expect(await sim('GET', '/api/config')).toMatchObject({ menu_chord: 'keys' })
})

test('drafts from two tabs save in one request with only the changed fields', async ({ app: page }) => {
  const puts: unknown[] = []
  page.on('request', (r) => r.method() === 'PUT' && r.url().endsWith('/api/config') && puts.push(r.postDataJSON()))
  await page.route('**/api/config', async (r) => {
    if (r.request().method() === 'PUT') await new Promise((f) => setTimeout(f, 400))
    await r.fallback()
  })
  await openTab(page, 'Deck')
  await page.locator('#hold').fill('3')
  // Another client turns the input lock on while this draft is open; the save must not turn it back off.
  await sim('PUT', '/api/config', { input_lock: true })
  await openTab(page, 'Access')
  await page.locator('#cors-fold summary').click()
  await page.locator('#cors').fill('http://studio.local')
  const bar = page.getByRole('region', { name: 'Unsaved changes' })
  await bar.getByRole('button', { name: /^Save/ }).dblclick()
  await expect(bar).toHaveCount(0)
  expect(puts).toEqual([{ menu_hold_ms: 3000, cors_origins: 'http://studio.local' }])
  expect(await sim('GET', '/api/config')).toMatchObject({ input_lock: true, menu_hold_ms: 3000 })
})

test('closing the Companion dialog while it tests the address saves nothing', async ({ app: page }) => {
  const puts: unknown[] = []
  page.on('request', (r) => r.method() === 'PUT' && r.url().endsWith('/api/config') && puts.push(r.postDataJSON()))
  await page.route('**/api/sources/probe', async (r) => {
    await new Promise((f) => setTimeout(f, 1500))
    await r.fulfill({ json: { ok: true } })
  })
  await openTab(page, 'Sources')
  await page.locator('#src-add').click()
  await page.locator('#m-sat').fill('10.9.9.9')
  await page.getByRole('dialog').getByRole('button', { name: /^Save/ }).click()
  await page.keyboard.press('Escape')
  await expect(page.getByRole('dialog')).toHaveCount(0)
  await page.waitForTimeout(2500)
  expect(puts).toEqual([])
})

test('broken replies show a worded error and leave the rest of the page working', async ({ app: page, errors }) => {
  await page.route('**/api/tokens', (r) => r.fulfill({ json: null }))
  await page.route('**/api/config', (r) =>
    r.request().method() === 'PUT'
      ? r.fulfill({ status: 500, contentType: 'text/html', body: '<html><body><h1>500</h1></body></html>' })
      : r.fallback(),
  )
  await openTab(page, 'Access')
  await expect(page.getByText('The token list could not be loaded.')).toBeVisible()
  await openTab(page, 'Deck')
  await page.getByRole('radio', { name: /Two keys/ }).click()
  await page.getByRole('region', { name: 'Unsaved changes' }).getByRole('button', { name: /^Save/ }).click()
  await expect(page.getByText('The connector answered with an error (500). Try again.')).toBeVisible()
  await expect(page.getByText('<html>')).toHaveCount(0)
  await openTab(page, 'Overview')
  await expect(page.locator('#bri-ov')).toBeVisible()
  errors.splice(0, errors.length, ...errors.filter((e) => !e.includes('status of 500')))
})

test('a dialog closed with Escape gives focus back to the button that opened it', async ({ app: page }) => {
  const btn = page.getByRole('button', { name: /Restart/ }).first()
  await btn.focus()
  await page.keyboard.press('Enter')
  await expect(page.getByRole('alertdialog')).toBeVisible()
  await page.keyboard.press('Escape')
  await expect(page.getByRole('alertdialog')).toHaveCount(0)
  await expect(btn).toBeFocused()
})

test('dialogs confirm, cancel and create', async ({ app: page }) => {
  await page
    .getByRole('button', { name: /Restart/ })
    .first()
    .click()
  const dlg = page.getByRole('alertdialog')
  await expect(dlg).toContainText('Restart the connector?')
  await dlg.getByRole('button', { name: 'Cancel' }).click()
  await expect(dlg).toHaveCount(0)
  await openTab(page, 'Access')
  await page.locator('#tok-add').click()
  await page.locator('#tname').fill('e2e-token')
  await page.getByRole('button', { name: 'Create' }).click()
  await expect(page.locator('#tokval')).toBeVisible()
  await page.getByRole('button', { name: 'Done' }).click()
  await expect(page.getByText('e2e-token')).toBeVisible()
  await page.getByRole('button', { name: 'Delete: e2e-token' }).click()
  await page.getByRole('alertdialog').getByRole('button', { name: 'Delete' }).click()
  await expect(page.getByText('e2e-token')).toHaveCount(0)
})

test('connection rows form an accordion', async ({ app: page }) => {
  await openTab(page, 'Network')
  const lan = page.locator('#row-lan')
  const wlan = page.locator('#row-wlan')
  await lan.locator('summary').click()
  await expect(lan).toHaveAttribute('open', '')
  await wlan.locator('summary').click()
  await expect(wlan).toHaveAttribute('open', '')
  await expect(lan).not.toHaveAttribute('open', '')
})

test('the live deck is locked until remote control is on', async ({ app: page }) => {
  const sent: unknown[] = []
  page.on('request', (r) => r.method() === 'POST' && r.url().includes('/api/input') && sent.push(r.postDataJSON()))
  const toggle = page.locator('.deckToggle')
  if ((await toggle.isVisible()) && (await toggle.getAttribute('aria-expanded')) === 'false') await toggle.click()
  const key = page.getByRole('button', { name: 'Key 1, row 1' })
  // A locked key is aria-disabled but still clickable, as it is for a person.
  await key.click({ force: true })
  await expect(page.locator('#arm')).toHaveClass(/nudge/)
  expect(sent).toEqual([])
  await page.locator('#arm').click()
  await key.click({ force: true })
  await expect.poll(() => sent.length).toBeGreaterThan(0)
  expect(sent[0]).toMatchObject({ key: 0, real: true })
})

test('search finds settings and opens them', async ({ app: page }) => {
  await page.keyboard.press('Control+k')
  const box = page.getByRole('combobox', { name: 'Search' })
  await box.fill('wifi')
  await expect(page.getByRole('option').first()).toContainText(/Wi-Fi|Networks/)
  await page.keyboard.press('Enter')
  await expect(page.locator('#row-wlan')).toHaveAttribute('open', '')
})

test('long lists scroll inside their own box', async ({ app: page }) => {
  await page.route('**/api/wifi/scan', (r) =>
    r.fulfill({
      json: Array.from({ length: 60 }, (_, i) => ({
        ssid: `Net-${i}`,
        bssid: `aa:bb:cc:00:00:${i.toString(16).padStart(2, '0')}`,
        rssi: -50 - (i % 40),
        channel: 1 + (i % 11),
        open: i % 5 === 0,
      })),
    }),
  )
  await openTab(page, 'Network')
  await page.locator('#row-wlan summary').click()
  const list = page.locator('.wlist')
  await expect(list.locator('.wgrp')).toHaveCount(60)
  const box = await list.evaluate((el) => ({ h: el.clientHeight, sh: el.scrollHeight }))
  expect(box.h).toBeLessThanOrEqual(420)
  expect(box.sh).toBeGreaterThan(box.h)
  await openTab(page, 'Diagnostics')
  await expect(page.locator('pre.log')).toHaveCSS('height', '300px')
})

test('with input lock on the device, arming asks once and presses carry force', async ({ app: page }) => {
  await sim('PUT', '/api/config', { input_lock: true })
  await page.reload()
  // The arm asks only once the page knows the setting: the brightness slider waits for the same config.
  await expect(page.locator('#bri-ov')).toBeEnabled()
  const sent: unknown[] = []
  page.on('request', (r) => r.method() === 'POST' && r.url().includes('/api/input') && sent.push(r.postDataJSON()))
  const toggle = page.locator('.deckToggle')
  if ((await toggle.isVisible()) && (await toggle.getAttribute('aria-expanded')) === 'false') await toggle.click()
  await page.locator('#arm').click()
  const dlg = page.getByRole('alertdialog')
  await expect(dlg).toContainText('Presses are locked on the device')
  await dlg.getByRole('button', { name: 'Send anyway' }).click()
  await page.getByRole('button', { name: 'Key 1, row 1' }).click({ force: true })
  await expect.poll(() => sent.length).toBeGreaterThan(0)
  expect(sent[0]).toMatchObject({ key: 0, real: true, force: true })
  await sim('PUT', '/api/config', { input_lock: false })
})

test('logging out says so on the login page', async ({ app: page }) => {
  if (await page.locator('.side').isVisible()) await page.getByRole('button', { name: 'Log out' }).click()
  else {
    await page.locator('.tabbar').getByRole('button', { name: /More/ }).click()
    await page.getByRole('dialog').getByRole('button', { name: 'Log out' }).click()
  }
  await expect(page.getByText('You logged out.')).toBeVisible()
})
