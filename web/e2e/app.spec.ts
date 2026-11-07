import { expect, login, sim, test } from './fixtures.ts'

const TABS = ['Overview', 'Sources', 'Deck', 'Network', 'Access', 'Firmware', 'Diagnostics']

async function openTab(page: import('@playwright/test').Page, name: string) {
  if (await page.locator('.side').isVisible()) await page.getByRole('tab', { name: new RegExp(`^${name}`) }).click()
  else if (['Overview', 'Sources', 'Network'].includes(name))
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
  // Another client changes the brightness while this draft is open; the save must not set it back.
  await sim('PUT', '/api/config', { brightness: 55 })
  await openTab(page, 'Access')
  await page.locator('#cors-fold summary').click()
  await page.locator('#cors').fill('http://studio.local')
  const bar = page.getByRole('region', { name: 'Unsaved changes' })
  await bar.getByRole('button', { name: /^Save/ }).dblclick()
  await expect(bar).toHaveCount(0)
  expect(puts).toEqual([{ menu_hold_ms: 3000, cors_origins: 'http://studio.local' }])
  expect(await sim('GET', '/api/config')).toMatchObject({ brightness: 55, menu_hold_ms: 3000 })
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

test('the menu chord cards are one tab stop and arrow keys move the choice', async ({ app: page }) => {
  await openTab(page, 'Deck')
  const auto = page.locator('#chord').getByRole('radio', { name: /Automatic/ })
  await auto.focus()
  await page.keyboard.press('ArrowRight')
  const keys = page.locator('#chord').getByRole('radio', { name: /Two keys/ })
  await expect(keys).toBeFocused()
  await expect(keys).toHaveAttribute('aria-checked', 'true')
  await expect(auto).toHaveAttribute('tabindex', '-1')
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
  await page.keyboard.press('Escape')
  await page.mouse.click(5, 5)
  await expect(page.locator('#tokval')).toBeVisible()
  await expect(page.getByRole('dialog').getByRole('button', { name: 'Close' })).toHaveCount(0)
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

test('a pairing answer names the host that asked', async ({ app: page }) => {
  const docks: unknown[] = []
  await page.route('**/api/status', async (r) => {
    const res = await r.fetch()
    const j = (await res.json()) as Record<string, unknown>
    j.pairing = {
      request: '192.168.1.50',
      request_name: 'studio-pc',
      hosts: [],
      denied: [{ ip: '10.0.0.9', since_s: 0 }],
    }
    await r.fulfill({ response: res, json: j })
  })
  await page.route('**/api/dock', (r) => (docks.push(r.request().postDataJSON()), r.fulfill({ json: { ok: true } })))
  await page.reload()
  await page.locator('#ntc-pair').getByRole('button', { name: 'Accept' }).first().click()
  await expect.poll(() => docks).toEqual([{ accept: true, ip: '192.168.1.50' }])
  await page.unrouteAll({ behavior: 'ignoreErrors' })
})

test('a restart shows the addresses the device names in its reply', async ({ app: page }) => {
  await page.route('**/api/config', (r) =>
    r.request().method() === 'PUT'
      ? r.fulfill({ json: { ok: true, restart: true, reach: ['http://192.168.1.77', 'http://studio.local'] } })
      : r.fallback(),
  )
  await openTab(page, 'Deck')
  await page.getByRole('radio', { name: /Full speed/ }).click()
  await page.getByRole('region', { name: 'Unsaved changes' }).getByRole('button', { name: /^Save/ }).click()
  await page.getByRole('alertdialog').getByRole('button', { name: 'Save and restart' }).click()
  await expect(page.getByRole('link', { name: 'http://studio.local' })).toBeVisible()
  await expect(page.getByRole('link', { name: 'http://192.168.1.77' })).toBeVisible()
})

test('the device timeline shows in Diagnostics', async ({ app: page }) => {
  await openTab(page, 'Diagnostics')
  await page.locator('#dia-events summary').click()
  await expect(page.locator('#dia-events')).toContainText('started')
})

test('logging out says so on the login page', async ({ app: page }) => {
  if (await page.locator('.side').isVisible()) await page.getByRole('button', { name: 'Log out' }).click()
  else {
    await page.locator('.tabbar').getByRole('button', { name: /More/ }).click()
    await page.getByRole('dialog').getByRole('button', { name: 'Log out' }).click()
  }
  await expect(page.getByText('You logged out.')).toBeVisible()
})

test('the plus on the overview opens the add dialog, and the certificate pin waits behind a link', async ({
  app: page,
}) => {
  await page.getByRole('button', { name: 'Add source' }).first().click()
  const dlg = page.getByRole('dialog')
  await expect(dlg).toBeVisible()
  await dlg.locator('#m-sat').fill('wss://companion.example/satellite')
  await expect(dlg.locator('#m-satpin')).toHaveCount(0)
  await dlg.getByRole('button', { name: 'Pin a certificate (optional)' }).click()
  await expect(dlg.locator('#m-satpin')).toBeVisible()
})

test('saving a backup asks whether passwords go in', async ({ app: page }) => {
  await openTab(page, 'Firmware')
  await page.locator('#bak-dl').click()
  const dlg = page.getByRole('dialog')
  await expect(dlg.getByRole('radio', { name: /Settings only/ })).toHaveAttribute('aria-checked', 'true')
  await dlg.getByRole('radio', { name: /With passwords/ }).click()
  const [file] = await Promise.all([
    page.waitForEvent('download'),
    dlg.getByRole('button', { name: 'Save backup' }).click(),
  ])
  expect(file.suggestedFilename()).toMatch(/-backup\.json$/)
})

test('without the password, the login page shows it on the deck or asks the deck for a reset', async ({ page }) => {
  const calls: string[] = []
  await page.route('**/api/password/**', (r) => {
    calls.push(`${r.request().method()} ${new URL(r.request().url()).pathname}`)
    return r.fulfill({ json: { ok: true, s: 60 } })
  })
  await page.addInitScript(() => localStorage.setItem('dc.lang', '"en"'))
  await page.goto('/')
  await page.getByRole('button', { name: 'Show password on the deck' }).click()
  await expect(page.getByRole('button', { name: /On the deck for/ })).toBeDisabled()
  await page.getByRole('button', { name: 'Forgot your password?' }).click()
  await page.getByRole('alertdialog').getByRole('button', { name: 'Confirm on the deck' }).click()
  await expect(page.getByText(/Confirm on the deck\. \d+ s left/)).toBeVisible()
  await page.getByRole('button', { name: 'Cancel' }).click()
  expect(calls).toEqual(['POST /api/password/show', 'POST /api/password/reset', 'DELETE /api/password/reset'])
})

test('the deck menu button closes the menu it opened', async ({ app: page }) => {
  const btn = page.locator('#card-ctl .btnrow3 button').first()
  await expect(btn).toHaveText('Deck menu')
  await btn.click()
  await expect(btn).toHaveText('Close menu')
  await expect(btn).toHaveAttribute('aria-pressed', 'true')
  await btn.click()
  await expect(btn).toHaveText('Deck menu')
})

test('the deck can let a browser in without the password', async ({ page }) => {
  // The simulator accepts every login request after a second (DB_SIM_LOGIN_ACCEPT).
  await page.addInitScript(() => localStorage.setItem('dc.lang', '"en"'))
  await page.goto('/')
  await page.getByRole('button', { name: 'Ask the deck to let me in' }).click()
  await expect(page.getByText(/Accept on the deck\. \d+ s left/)).toBeVisible()
  await expect(page.locator('#lpw')).toHaveCount(0, { timeout: 8000 })
  await expect(page.getByRole('heading', { name: 'Overview' }).first()).toBeAttached()
})

test('an error toast shows above an open dialog and can be read and used there', async ({ app: page, errors }) => {
  await openTab(page, 'Access')
  await page.route('**/api/tokens', (r) =>
    r.request().method() === 'POST'
      ? r.fulfill({ status: 500, json: { ok: false, error: 'flash full' } })
      : r.fallback(),
  )
  await page.locator('#tok-add').first().click()
  await page.locator('#tname').fill('e2e-toast')
  await page.getByRole('button', { name: 'Create' }).click()
  const alert = page.getByRole('alert').filter({ hasText: 'flash full' })
  await expect(alert).toBeVisible()
  await expect(page.getByRole('dialog')).toBeVisible()
  const onTop = await alert.evaluate((el) => {
    const r = el.getBoundingClientRect()
    return el.contains(document.elementFromPoint(r.x + r.width / 2, r.y + r.height / 2)) && !el.closest('[inert]')
  })
  expect(onTop).toBe(true)
  await page.screenshot({ path: 'test-results/toast-over-dialog.png' })
  errors.splice(0, errors.length, ...errors.filter((e) => !e.includes('500')))
})

test('settings that fail to load disable editing, say so, and load on a later try', async ({ page, errors }) => {
  await page.route('**/api/input**', (r) => r.fulfill({ json: { ok: true } }))
  await sim('PUT', '/api/setup', { step: 0 })
  await sim('PUT', '/api/config', { setup_done: true })
  let fail = 2
  await page.route('**/api/config', (r) =>
    r.request().method() === 'GET' && fail-- > 0 ? r.fulfill({ status: 503, json: { ok: false } }) : r.fallback(),
  )
  await page.addInitScript(() => localStorage.setItem('dc.lang', '"en"'))
  await page.goto('/#sources')
  await login(page)
  const notice = page.getByText('Settings could not be loaded.').first()
  await expect(notice).toBeVisible()
  await expect(page.locator('#src-add')).toBeDisabled()
  await expect(notice).toHaveCount(0, { timeout: 15_000 })
  await expect(page.locator('#src-add')).toBeEnabled()
  errors.splice(0, errors.length, ...errors.filter((e) => !e.includes('status of 503')))
})

test('a failed logout keeps the page and says the session may still be valid', async ({ app: page, errors }) => {
  await page.route('**/api/logout', (r) => r.fulfill({ status: 500, json: { ok: false, error: 'busy' } }))
  if (await page.locator('.side').isVisible()) await page.getByRole('button', { name: 'Log out' }).click()
  else {
    await page.locator('.tabbar').getByRole('button', { name: /More/ }).click()
    await page.getByRole('dialog').getByRole('button', { name: 'Log out' }).click()
  }
  await expect(page.getByRole('alert').filter({ hasText: 'may still be valid' })).toBeVisible()
  await expect(page.locator('#lpw')).toHaveCount(0)
  errors.splice(0, errors.length, ...errors.filter((e) => !e.includes('status of 500')))
})

test('a login that fails for another reason than the password says why', async ({ page, errors }) => {
  await sim('PUT', '/api/setup', { step: 0 })
  await sim('PUT', '/api/config', { setup_done: true })
  await page.addInitScript(() => localStorage.setItem('dc.lang', '"en"'))
  await page.goto('/')
  await page.route('**/api/login', (r) => r.fulfill({ status: 500, json: { ok: false, error: 'flash busy' } }))
  await page.locator('#lpw').fill('anything')
  await page.keyboard.press('Enter')
  await expect(page.getByText('The connector refused it: flash busy')).toBeVisible()
  await expect(page.getByText('Wrong password.')).toHaveCount(0)
  await page.unroute('**/api/login')
  await page.locator('#lpw').fill('not-the-password')
  await page.keyboard.press('Enter')
  await expect(page.getByText('Wrong password.')).toBeVisible()
  errors.splice(0, errors.length, ...errors.filter((e) => !/status of (500|403)/.test(e)))
})

test('a status reply with an error is shown and does not count as offline', async ({ app: page, errors }) => {
  await page.route('**/api/status', (r) => r.fulfill({ status: 500, json: { ok: false, error: 'oops' } }))
  await page.evaluate(() => document.dispatchEvent(new Event('visibilitychange')))
  await expect(page.getByText('The status could not be read').first()).toBeVisible()
  await expect(page.getByText(/not reachable|Reconnecting/i)).toHaveCount(0)
  errors.splice(0, errors.length, ...errors.filter((e) => !e.includes('status of 500')))
})
