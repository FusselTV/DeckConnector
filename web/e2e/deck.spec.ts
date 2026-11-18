import type { Page } from '@playwright/test'
import { expect, test } from './fixtures.ts'

async function armedDeck(page: Page) {
  const sent: Record<string, unknown>[] = []
  page.on('request', (r) => {
    if (r.method() === 'POST' && r.url().includes('/api/input')) sent.push(r.postDataJSON() as Record<string, unknown>)
  })
  const toggle = page.locator('.deckToggle')
  if ((await toggle.isVisible()) && (await toggle.getAttribute('aria-expanded')) === 'false') await toggle.click()
  await page.locator('#arm').click()
  await expect(page.locator('#arm')).toHaveAttribute('aria-checked', 'true')
  return { sent, key: page.getByRole('button', { name: 'Key 1, row 1' }) }
}

// Synthetic touch pointer events, so the desktop project can drive the touch path too.
async function touch(page: Page, name: string, type: string, dy = 0) {
  await page.getByRole('button', { name }).evaluate(
    (el, [type, dy]) => {
      const r = el.getBoundingClientRect()
      const o = { bubbles: true, pointerType: 'touch', pointerId: 7, clientX: r.x + 10, clientY: r.y + 10 + dy }
      el.dispatchEvent(new PointerEvent(type, o))
    },
    [type, dy] as const,
  )
}

test('a touch scroll that starts on a key presses nothing, even after a short rest', async ({ app: page }) => {
  const { sent } = await armedDeck(page)
  await touch(page, 'Key 1, row 1', 'pointerdown')
  await page.waitForTimeout(150)
  await touch(page, 'Key 1, row 1', 'pointermove', 40)
  await touch(page, 'Key 1, row 1', 'pointercancel', 40)
  await page.waitForTimeout(600)
  expect(sent).toEqual([])
})

test('a touch tap sends one press on lift, a still hold sends down then up', async ({ app: page }) => {
  const { sent } = await armedDeck(page)
  await touch(page, 'Key 1, row 1', 'pointerdown')
  await page.waitForTimeout(150)
  expect(sent).toEqual([])
  await touch(page, 'Key 1, row 1', 'pointerup')
  await expect.poll(() => sent.length).toBe(1)
  expect(sent[0]).toEqual({ key: 0, real: true })
  await touch(page, 'Key 2, row 1', 'pointerdown')
  await expect.poll(() => sent.length).toBe(2)
  expect(sent[1]).toMatchObject({ key: 1, action: 'down' })
  await touch(page, 'Key 2, row 1', 'pointerup')
  await expect.poll(() => sent.length).toBe(3)
  expect(sent[2]).toMatchObject({ key: 1, action: 'up', real: true })
})

test('a key held by keyboard is released when focus moves away', async ({ app: page }) => {
  const { sent, key } = await armedDeck(page)
  await key.focus()
  await page.keyboard.down(' ')
  await expect.poll(() => sent.length).toBe(1)
  await page.keyboard.press('Tab')
  await page.keyboard.up(' ')
  await expect.poll(() => sent.length).toBe(2)
  expect(sent.map((b) => b.action)).toEqual(['down', 'up'])
})

test('a key held by mouse is released when remote control turns off', async ({ app: page }) => {
  const { sent, key } = await armedDeck(page)
  await key.hover()
  await page.mouse.down()
  await expect.poll(() => sent.length).toBe(1)
  await page.locator('#arm').evaluate((el: HTMLElement) => el.click())
  await expect.poll(() => sent.length).toBe(2)
  expect(sent[1]).toMatchObject({ key: 0, action: 'up', real: true })
  await page.mouse.up()
  await page.waitForTimeout(300)
  expect(sent).toHaveLength(2)
})

test('a failed release is retried', async ({ app: page, errors }) => {
  const { sent, key } = await armedDeck(page)
  let fails = 1
  await page.route('**/api/input**', (r) =>
    (r.request().postDataJSON() as { action?: string }).action === 'up' && fails-- > 0
      ? r.fulfill({ status: 503, body: 'busy' })
      : r.fulfill({ json: { ok: true } }),
  )
  await key.hover()
  await page.mouse.down()
  await page.mouse.up()
  await expect.poll(() => sent.filter((b) => b.action === 'up').length).toBe(2)
  errors.splice(0, errors.length, ...errors.filter((e) => !e.includes('503')))
})

test('a dial held on its arrow stops repeating after a few seconds', async ({ app: page }) => {
  const { sent } = await armedDeck(page)
  const dial = page.getByRole('button', { name: /^Dial 1\./ })
  test.skip(!(await dial.count()), 'this deck has no dials')
  await dial.scrollIntoViewIfNeeded()
  const b = await dial.boundingBox()
  if (!b) throw new Error('no dial box')
  await page.mouse.move(b.x + b.width * 0.9, b.y + b.height / 2)
  await page.mouse.down()
  await page.waitForTimeout(7000)
  const n = sent.length
  expect(n).toBeGreaterThan(5)
  await page.waitForTimeout(500)
  expect(sent.length).toBe(n)
  await page.mouse.up()
})

test('a key up waits for its down, and a release that fails for good says so', async ({ app: page, errors }) => {
  const { key } = await armedDeck(page)
  const order: string[] = []
  await page.route('**/api/input**', async (r) => {
    const action = (r.request().postDataJSON() as { action?: string }).action ?? ''
    order.push(action)
    if (action === 'down') {
      await new Promise((f) => setTimeout(f, 400))
      order.push('down done')
      return r.fulfill({ json: { ok: true } })
    }
    return r.fulfill({ status: 409, json: { ok: false, error: 'no' } })
  })
  await key.hover()
  await page.mouse.down()
  await page.mouse.up()
  await expect(page.getByRole('alert').filter({ hasText: 'Key 1 may still be held' })).toBeVisible()
  expect(order).toEqual(['down', 'down done', 'up'])
  errors.splice(0, errors.length, ...errors.filter((e) => !e.includes('409')))
})

test('wheel events on a focused dial add up by distance into one turn per frame', async ({ app: page }) => {
  const { sent } = await armedDeck(page)
  const dial = page.getByRole('button', { name: /^Dial 1\./ })
  test.skip(!(await dial.count()), 'this deck has no dials')
  await dial.focus()
  const wheel = (dys: number[]) =>
    dial.evaluate((el, dys) => {
      for (const deltaY of dys) el.dispatchEvent(new WheelEvent('wheel', { deltaY, bubbles: true, cancelable: true }))
    }, dys)
  await wheel(Array<number>(9).fill(10))
  await page.waitForTimeout(200)
  expect(sent).toEqual([])
  await wheel([10, 100, 100])
  await expect.poll(() => sent.length).toBe(1)
  await page.waitForTimeout(200)
  expect(sent).toEqual([{ encoder: 0, action: 'turn', ticks: 3, real: true }])
})

test('a locked deck takes the PIN from the page, digits in order', async ({ app: page }) => {
  const sent: unknown[] = []
  page.on('request', (r) => {
    if (r.method() === 'POST' && r.url().includes('/api/input')) sent.push(r.postDataJSON())
  })
  await page.routeWebSocket('**/api/ws', () => {}) // its state would replace the stubbed deck
  await page.route('**/api/status', async (r) => {
    const res = await r.fetch()
    const j = (await res.json()) as { decks: Record<string, unknown>[] }
    Object.assign(j.decks[0], { locked: true, pin_count: 2 })
    await r.fulfill({ response: res, json: j })
  })
  await page.reload()
  const toggle = page.locator('.deckToggle')
  await page.locator('#card-deck').waitFor()
  if ((await toggle.isVisible()) && (await toggle.getAttribute('aria-expanded')) === 'false') await toggle.click()
  const pad = page.getByRole('group', { name: 'Companion locked this deck. Enter the PIN.' })
  await expect(pad.getByRole('status')).toHaveText('••')
  await pad.getByRole('button', { name: '4', exact: true }).click()
  await pad.getByRole('button', { name: '0', exact: true }).click()
  await expect
    .poll(() => sent)
    .toEqual([
      { pin: 4, real: true },
      { pin: 0, real: true },
    ])
  await page.unrouteAll({ behavior: 'ignoreErrors' })
})
