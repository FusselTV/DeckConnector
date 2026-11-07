import { test as base, expect, type Page } from '@playwright/test'

export const SIM = 'http://127.0.0.1:18210'
const AUTH = 'Basic ' + Buffer.from('admin:simulator-pass').toString('base64')

export async function sim(method: string, path: string, body?: unknown) {
  const r = await fetch(SIM + path, {
    method,
    headers: { Authorization: AUTH, 'X-DeckConnector': '1', 'Content-Type': 'application/json' },
    body: body === undefined ? undefined : JSON.stringify(body),
  })
  return r.json() as Promise<unknown>
}

// A fresh page has no session, so its first status call answers 401, and a device without a crash answers the
// coredump call with 404. Both are expected; anything else in the console fails the test.
const EXPECTED = /Failed to load resource: the server responded with a status of (401|404)/

export const test = base.extend<{ errors: string[]; app: Page }>({
  errors: async ({ page }, use) => {
    const errors: string[] = []
    page.on('console', (m) => m.type() === 'error' && !EXPECTED.test(m.text()) && errors.push(m.text()))
    page.on('pageerror', (e) => errors.push(e.message))
    await use(errors)
    expect(errors, 'console errors').toEqual([])
  },
  app: async ({ page, errors: _errors }, use) => {
    // Presses from the page carry "real": true by design. A test never lets one reach a device, not even the simulator.
    await page.route('**/api/input**', (r) => r.fulfill({ json: { ok: true } }))
    await sim('PUT', '/api/setup', { step: 0 })
    await sim('PUT', '/api/config', {
      setup_done: true,
      brightness: 80,
      menu_chord: 'auto',
      menu_hold_ms: 2000,
      cors_origins: '',
    })
    await page.addInitScript(() => localStorage.getItem('dc.lang') ?? localStorage.setItem('dc.lang', '"en"'))
    await page.goto('/')
    await login(page)
    await use(page)
  },
})

export async function login(page: Page) {
  await page.locator('#lpw').fill('simulator-pass')
  await page.keyboard.press('Enter')
  await expect(page.locator('#lpw')).toHaveCount(0)
}

export { expect }
