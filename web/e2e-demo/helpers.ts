import { test as base, expect, type Page } from '@playwright/test'

import { en } from '../src/i18n/en.ts'

export type Dict = typeof en
// A plain import would make tsc follow de.ts into the app's extensionless imports, which this tsconfig refuses.
const deFile: string = '../src/i18n/de.ts'
export const de = ((await import(deFile)) as { de: Dict }).de
export { en }

export const TABS = ['overview', 'sources', 'deck', 'network', 'access', 'firmware', 'diagnose'] as const
export type Tab = (typeof TABS)[number]
const MOBILE = ['overview', 'sources', 'network']

/** What the fetch shim in src/demo/install.ts answered. */
export interface Call {
  method: string
  url: string
  status: number
}
export const calls = (page: Page) => page.evaluate(() => (window as unknown as { __demoCalls: Call[] }).__demoCalls)

// Every console error fails the test, and no request may leave the page: the demo answers everything itself.
export const test = base.extend<{ demo: Page }>({
  demo: async ({ page, baseURL }, use) => {
    const errors: string[] = []
    const out: string[] = []
    page.on('console', (m) => m.type() === 'error' && errors.push(m.text()))
    page.on('pageerror', (e) => errors.push(e.message))
    page.on('request', (r) => {
      const u = r.url()
      if (!u.startsWith('data:') && !u.startsWith('blob:') && !u.startsWith(baseURL ?? '')) out.push(u)
      if (u.includes('/api/')) out.push(u)
    })
    await use(page)
    expect(errors, 'console errors').toEqual([])
    expect(out, 'requests that left the page').toEqual([])
  },
})

export async function openTab(page: Page, dict: Dict, tab: Tab) {
  const name = dict[`tab.${tab}`]
  await page.locator('#tabpanel').waitFor()
  if (await page.locator('.side').isVisible()) await page.getByRole('tab', { name: new RegExp(`^${name}`) }).click()
  else if (MOBILE.includes(tab))
    await page
      .locator('.tabbar')
      .getByRole('button', { name: new RegExp(name) })
      .click()
  else {
    await page.locator('.tabbar').getByRole('button', { name: dict['tab.more'] }).click()
    await page.getByRole('dialog').getByRole('button', { name }).click()
  }
  await expect(page.locator('#tabpanel > .stack:not([hidden])').first()).toBeVisible()
}

export const SHOTS = process.env.DEMO_SHOTS

export { expect }
