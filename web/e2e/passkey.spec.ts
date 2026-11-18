import { expect, sim, test } from './fixtures.ts'
import { SIM_PORT } from './ports.ts'

// Passkeys need a secure context and a name: the simulator's own page on http://localhost is both, the dev server on
// 127.0.0.1 is neither. Chromium's virtual authenticator stands in for a phone or a security key.

test.beforeEach(async ({ page }, info) => {
  test.skip(info.project.name !== 'desktop', 'one layout is enough')
  await sim('PUT', '/api/config', { setup_done: true })
  for (const p of (await sim('GET', '/api/passkeys')) as { id: number }[]) await sim('DELETE', `/api/passkeys/${p.id}`)
  await page.addInitScript(() => localStorage.setItem('dc.lang', '"en"'))
})

async function logIn(page: import('@playwright/test').Page, host: string) {
  await page.goto(`http://${host}:${SIM_PORT}/`)
  await page.locator('#lpw').fill('simulator-pass')
  await page.keyboard.press('Enter')
  await page.getByRole('tab', { name: /^Access/ }).click()
}

test('a passkey made on the Access page logs in', async ({ page, errors: _errors }) => {
  const cdp = await page.context().newCDPSession(page)
  await cdp.send('WebAuthn.enable')
  const { authenticatorId } = await cdp.send('WebAuthn.addVirtualAuthenticator', {
    options: {
      protocol: 'ctap2',
      transport: 'internal',
      hasResidentKey: true,
      hasUserVerification: true,
      isUserVerified: true,
      automaticPresenceSimulation: true,
    },
  })
  await logIn(page, 'localhost')
  await page.locator('#pk-add').click()
  await page.locator('#pkname').fill('Virtual')
  await page.getByRole('dialog').getByRole('button', { name: 'Add passkey' }).click()
  await expect(page.locator('#card-pk .li')).toContainText(['Virtual'])
  await expect(page.locator('#card-pk .li')).toContainText(['localhost'])
  expect((await cdp.send('WebAuthn.getCredentials', { authenticatorId })).credentials).toHaveLength(1)

  await page.context().clearCookies()
  await page.reload()
  await page.locator('#pk-login').click()
  await expect(page.getByRole('tab', { name: /^Access/ })).toBeVisible()
  const cred = (await cdp.send('WebAuthn.getCredentials', { authenticatorId })).credentials.at(0)
  expect(cred?.signCount).toBeGreaterThan(0)
})

test('passkeys are not offered by IP address', async ({ page, errors: _errors }) => {
  await page.goto(`http://127.0.0.1:${SIM_PORT}/`)
  await expect(page.locator('#lpw')).toBeVisible()
  await expect(page.locator('#pk-login')).toHaveCount(0)
  await logIn(page, '127.0.0.1')
  await expect(page.locator('#card-pk')).toContainText('not its IP address')
  await expect(page.locator('#pk-add')).toHaveCount(0)
})
