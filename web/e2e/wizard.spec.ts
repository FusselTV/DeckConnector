import { expect, login, sim, test } from './fixtures.ts'

test('the setup wizard goes forward and back and keeps what was entered', async ({ page, errors: _errors }) => {
  await page.route('**/api/input**', (r) => r.fulfill({ json: { ok: true } }))
  // The simulator has no cable and its Wi-Fi join never finishes, so the join test is answered here too.
  let joined = false
  page.on('response', (r) =>
    r.url().includes('/api/wifi') || (r.url().includes('/api/status') && joined)
      ? console.log('DBG', r.request().method(), r.url(), r.status())
      : undefined,
  )
  await page.route('**/api/wifi/join', (r) => ((joined = true), r.fulfill({ json: { ok: true } })))
  await page.route('**/api/status', async (r) => {
    const res = await r.fetch()
    const j = (await res.json()) as { network?: { wlan?: object } }
    if (joined) j.network = { ...j.network, wlan: { ...j.network?.wlan, join: 'ok' } }
    await r.fulfill({ response: res, json: j })
  })
  await sim('PUT', '/api/config', { setup_done: false })
  await page.addInitScript(() => sessionStorage.removeItem('dc.wiz'))
  await page.goto('/')
  await login(page)
  await expect(page.locator('#wz-step')).toHaveText(/Welche Sprache|Which language/)
  await page.getByRole('radio', { name: 'English' }).click()
  await page.getByRole('button', { name: 'Next' }).click()
  await expect(page.locator('#wz-step')).toHaveText('Set your own password')
  await page.locator('#wpw').fill('abc')
  await page.getByRole('button', { name: 'Next' }).click()
  await expect(page.getByText('At least 6 characters.')).toBeVisible()
  await page.locator('#wpw').fill('abcdef1')
  await page.getByRole('button', { name: 'Next' }).click()
  await expect(page.locator('#wz-step')).toHaveText('How does the connector get online?')
  await page.locator('#w-back').click()
  await expect(page.locator('#wpw')).toHaveValue('abcdef1')
  await page.goBack()
  await expect(page.locator('#wz-step')).toHaveText('Which language?')
  // The wizard keeps its step on the device; close the page first so no late step write follows the reset.
  await page.waitForLoadState('networkidle')
  await page.close()
  await sim('PUT', '/api/setup', { step: 0 })
  await sim('PUT', '/api/config', { setup_done: true })
})

test('the wizard offers the stored name and asks for a new password again after a reload', async ({
  page,
  errors: _errors,
}) => {
  const { name } = (await sim('GET', '/api/config')) as { name: string }
  const puts: Record<string, unknown>[] = []
  await page.route('**/api/input**', (r) => r.fulfill({ json: { ok: true } }))
  // The settings answer late, as on a busy device; the save itself is answered here so the simulator keeps running.
  await page.route('**/api/config', async (r) => {
    if (r.request().method() === 'PUT') {
      puts.push(r.request().postDataJSON() as Record<string, unknown>)
      return r.fulfill({ json: { ok: true, restart: false } })
    }
    await new Promise((f) => setTimeout(f, 1500))
    await r.fallback()
  })
  // The simulator's Wi-Fi join never finishes, so the join test is answered here too.
  let joined = false
  await page.route('**/api/wifi/join', (r) => ((joined = true), r.fulfill({ json: { ok: true } })))
  await page.route('**/api/status', async (r) => {
    const res = await r.fetch()
    const j = (await res.json()) as { network?: { wlan?: object } }
    if (joined) j.network = { ...j.network, wlan: { ...j.network?.wlan, join: 'ok' } }
    await r.fulfill({ response: res, json: j })
  })
  await sim('PUT', '/api/config', { setup_done: false })
  await page.addInitScript(
    () => sessionStorage.getItem('dc.init') ?? (sessionStorage.clear(), sessionStorage.setItem('dc.init', '1')),
  )
  await page.goto('/')
  await login(page)
  await page.getByRole('radio', { name: 'English' }).click()
  await expect(page.locator('#wname')).toHaveValue(name)
  await page.getByRole('button', { name: 'Next' }).click()
  await page.locator('#wpw').fill('newpass99')
  await page.getByRole('button', { name: 'Next' }).click()
  await expect(page.locator('#wz-step')).toHaveText('How does the connector get online?')
  await page.reload()
  await expect(page.locator('#wz-step')).toHaveText('Set your own password')
  await expect(page.getByText('forgot the new password')).toBeVisible()
  await page.locator('#wpw').fill('newpass99')
  await page.getByRole('button', { name: 'Next' }).click()
  await page.getByRole('button', { name: /Gast/ }).first().click()
  await page.getByRole('button', { name: 'Save and restart' }).click()
  await expect.poll(() => puts.length).toBe(1)
  expect(puts[0]).toMatchObject({ name, admin_pass: 'newpass99' })
  await page.unrouteAll({ behavior: 'ignoreErrors' })
  await page.waitForLoadState('networkidle')
  await page.close()
  await sim('PUT', '/api/setup', { step: 0 })
  await sim('PUT', '/api/config', { setup_done: true })
})

test('a re-run over the network keeps it and still saves a new name and password', async ({
  page,
  errors: _errors,
}) => {
  const puts: Record<string, unknown>[] = []
  await page.route('**/api/input**', (r) => r.fulfill({ json: { ok: true } }))
  // A device already on Wi-Fi; the save is answered here so the simulator does not restart.
  await page.route('**/api/config', async (r) => {
    if (r.request().method() === 'GET') {
      const res = await r.fetch()
      return r.fulfill({ response: res, json: { ...((await res.json()) as object), wifi_ssid: 'Zuhause' } })
    }
    const body = r.request().postDataJSON() as Record<string, unknown>
    if ('setup_done' in body) return r.fallback()
    puts.push(body)
    await r.fulfill({ json: { ok: true, restart: false } })
  })
  await sim('PUT', '/api/config', { setup_done: false })
  await page.addInitScript(() => sessionStorage.removeItem('dc.wiz'))
  await page.goto('/')
  await login(page)
  await page.getByRole('radio', { name: 'English' }).click()
  await page.locator('#wname').fill('studio-deck')
  await page.getByRole('button', { name: 'Next' }).click()
  await page.locator('#wpw').fill('newpass99')
  await page.getByRole('button', { name: 'Next' }).click()
  await expect(page.getByText('The network stays as it is')).toBeVisible()
  await page.getByRole('button', { name: 'Save and restart' }).click()
  await expect(page.locator('#wz-step')).toHaveText('Which program controls your deck?')
  expect(puts).toEqual([{ name: 'studio-deck', admin_pass: 'newpass99' }])
  await page.unrouteAll({ behavior: 'ignoreErrors' })
  await page.waitForLoadState('networkidle')
  await page.close()
  await sim('PUT', '/api/setup', { step: 0 })
  await sim('PUT', '/api/config', { setup_done: true })
})
