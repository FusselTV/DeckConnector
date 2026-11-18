import type { Page } from '@playwright/test'
import { en, de, expect, openTab, TABS, test, type Dict } from './helpers.ts'

const keySrc = (page: Page, dict: Dict, n: number) =>
  page
    .getByRole('button', { name: dict['ov.key'].replace('{n}', String(n)).replace('{r}', '1') })
    .locator('img')
    .first()
    .getAttribute('src')

for (const [lang, dict] of [
  ['en', en],
  ['de', de],
] as const) {
  test(`every page works in ${lang}, and a press changes the virtual deck`, async ({ demo: page }) => {
    await page.goto(`?lang=${lang}`)
    await expect(page.locator('html')).toHaveAttribute('lang', lang)
    await expect(page.locator('.deckToggle')).toHaveAttribute('aria-expanded', 'true')
    // The deck is the first thing a visitor sees, on a phone too.
    expect((await page.locator('#card-deck').boundingBox())?.y).toBeLessThan(200)
    await expect.poll(() => keySrc(page, dict, 1)).toMatch(/^data:image\/png/)
    const before = await keySrc(page, dict, 1)
    await expect(page.locator('#arm')).toHaveAttribute('aria-checked', 'true')
    await page.getByRole('button', { name: dict['ov.key'].replace('{n}', '1').replace('{r}', '1') }).click()
    await expect.poll(() => keySrc(page, dict, 1)).not.toBe(before)
    for (const tab of TABS) await openTab(page, dict, tab)
    await expect(page.locator('.demo-badge')).toBeVisible()
  })
}

test('a reload keeps the demo state and Reset starts over', async ({ demo: page }) => {
  await page.goto('?lang=en')
  await openTab(page, en, 'access')
  await page.locator('#tok-add').click()
  await page.locator('#tname').fill('stream-pc')
  await page.getByRole('button', { name: 'Create' }).click()
  await expect(page.locator('#tokval')).toBeVisible()
  await page.keyboard.press('Escape')
  await page.reload()
  await openTab(page, en, 'access')
  await expect(page.getByText('stream-pc')).toBeVisible()
  await page.locator('.demo-badge button').click()
  await page.waitForLoadState()
  await openTab(page, en, 'access')
  await expect(page.getByText('home-assistant')).toBeVisible()
  await expect(page.getByText('stream-pc')).toHaveCount(0)
})

test('a restart comes back after a few seconds', async ({ demo: page }) => {
  await page.goto('?lang=en')
  await page
    .getByRole('button', { name: /Restart/ })
    .first()
    .click()
  await page
    .getByRole('alertdialog')
    .getByRole('button', { name: /Restart/ })
    .click()
  await expect(page.locator('#rs-t')).toBeVisible()
  await expect(page.locator('#rs-t')).toHaveCount(0, { timeout: 20_000 })
  await expect(page.locator('#tabpanel')).toBeVisible()
})

test('setup mode starts the wizard', async ({ demo: page }) => {
  await page.goto('?setup=1&lang=en')
  await expect(page.locator('#wz-step')).toBeVisible()
})

test('login mode wants the demo password, which the page shows next to the field', async ({ demo: page }) => {
  await page.goto('?login=1&lang=en')
  await page.locator('#lpw').fill('anything')
  await page.keyboard.press('Enter')
  await expect(page.getByText(en['login.wrong'])).toBeVisible()
  await page.getByRole('button', { name: 'Fill in' }).click()
  await page.keyboard.press('Enter')
  await expect(page.locator('#lpw')).toHaveCount(0)
})

test('login mode lets the deck accept the browser', async ({ demo: page }) => {
  await page.goto('?login=1&lang=en')
  await page.getByRole('button', { name: en['login.req'] }).click()
  await expect(page.locator('#lpw')).toHaveCount(0, { timeout: 10_000 })
})

test('Wi-Fi scan lists example networks', async ({ demo: page }) => {
  await page.goto('?lang=en')
  await openTab(page, en, 'network')
  await page
    .locator('summary')
    .filter({ hasText: new RegExp(`^${en['net.wlan']}`) })
    .click()
  await expect(page.getByText('Studio Guest').first()).toBeVisible({ timeout: 8000 })
})

test('an update installs with progress and restarts into the new version', async ({ demo: page }) => {
  test.setTimeout(60_000)
  await page.goto('?lang=en')
  await openTab(page, en, 'firmware')
  await page.locator('#upd-check').click()
  await expect(page.locator('#ntc-upd-new')).toBeVisible()
  await expect(page.locator('#fw-file'), 'one install button while an update is offered').toHaveCount(0)
  await page.locator('#ntc-upd-new').getByRole('button', { name: en['btn.install'] }).click()
  await page.getByRole('alertdialog').getByRole('button', { name: en['btn.install'] }).click()
  await expect(page.getByText(en['ota.download']).first()).toBeVisible()
  await expect(page.getByText('v1.0.1').first()).toBeVisible({ timeout: 30_000 })
  await expect
    .poll(
      () => page.evaluate(() => sessionStorage.getItem('dc.demo')?.includes('"firmware":"v1.0.1"')).catch(() => false),
      {
        timeout: 30_000,
      },
    )
    .toBe(true)
  await expect(page.locator('#ntc-upd-new'), 'the offer goes with the old version').toHaveCount(0)
  await expect(page.locator('#fw-file')).toHaveCount(1)
})
