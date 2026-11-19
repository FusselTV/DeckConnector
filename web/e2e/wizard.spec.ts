import { expect, login, sim, test } from './fixtures.ts'

test('a device nobody set up opens the wizard without the login page', async ({ page, errors: _errors }) => {
  // The simulator's one chance may be used up, so the claim is answered here, with a real session behind it.
  await page.route('**/api/setup/claim', async (r) => {
    await page.request.post('/api/login', {
      headers: { 'X-DeckConnector': '1' },
      data: { password: 'simulator-pass' },
    })
    await r.fulfill({ json: { ok: true } })
  })
  await sim('PUT', '/api/config', { setup_done: false })
  await page.goto('/')
  await expect(page.locator('#wz-step')).toBeVisible()
  await expect(page.locator('#lpw')).toHaveCount(0)
  await sim('PUT', '/api/setup', { step: 0 })
  await sim('PUT', '/api/config', { setup_done: true })
})

test('a browser that comes after a password was set during setup hears why it must log in', async ({
  page,
  errors: _errors,
}) => {
  await page.route('**/api/setup/claim', (r) => r.fulfill({ status: 409, json: { ok: false, error: 'claimed' } }))
  await page.addInitScript(() => localStorage.setItem('dc.lang', '"en"'))
  await sim('PUT', '/api/config', { setup_done: false })
  await page.goto('/')
  await expect(page.getByText('Another browser went on with the setup.')).toBeVisible()
  await sim('PUT', '/api/config', { setup_done: true })
})

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
  await page.goto('/')
  await login(page)
  await expect(page.locator('#wz-step')).toHaveText(/Welche Sprache|Which language/)
  await page.getByRole('radio', { name: 'English' }).click()
  await expect(page.locator('#w-speaker')).toHaveAttribute('aria-checked', 'false')
  await page.locator('#w-speaker').click()
  await page.getByRole('button', { name: 'Next' }).click()
  await expect(page.locator('#wz-step')).toHaveText('Set your own password')
  await expect.poll(() => sim('GET', '/api/config')).toMatchObject({ lang: 'en', speaker: true }) // the deck too
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
  await page.locator('#w-speaker').click()
  await page.getByRole('button', { name: 'Next' }).click()
  await expect.poll(() => sim('GET', '/api/config')).toMatchObject({ speaker: false }) // changed back, sent again
  // The wizard keeps its step on the device; close the page first so no late step write follows the reset.
  await page.waitForLoadState('networkidle')
  await page.close()
  await sim('PUT', '/api/setup', { step: 0 })
  await sim('PUT', '/api/config', { setup_done: true, lang: 'de', speaker: false })
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
      const body = r.request().postDataJSON() as Record<string, unknown>
      if (Object.keys(body).join() === 'lang') return r.fallback() // the deck's language, sent on its own
      puts.push(body)
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
    if ('setup_done' in body || Object.keys(body).join() === 'lang') return r.fallback()
    puts.push(body)
    await r.fulfill({ json: { ok: true, restart: false } })
  })
  await sim('PUT', '/api/config', { setup_done: false })
  await page.goto('/')
  await login(page)
  await page.getByRole('radio', { name: 'English' }).click()
  await page.locator('#wname').fill('studio-deck')
  await page.getByRole('button', { name: 'Next' }).click()
  await page.locator('#wpw').fill('newpass99')
  await page.getByRole('button', { name: 'Next' }).click()
  await expect(page.getByText('The network stays as it is')).toBeVisible()
  await page.getByRole('button', { name: 'Save and restart' }).click()
  await expect(page.locator('#wz-step')).toHaveText('Which source controls your deck?')
  expect(puts).toEqual([{ name: 'studio-deck', admin_pass: 'newpass99' }])
  await page.unrouteAll({ behavior: 'ignoreErrors' })
  await page.waitForLoadState('networkidle')
  await page.close()
  await sim('PUT', '/api/setup', { step: 0 })
  await sim('PUT', '/api/config', { setup_done: true })
})

test('with four servers stored, the wizard refuses a fifth instead of replacing the first', async ({
  page,
  errors: _errors,
}) => {
  const puts: unknown[] = []
  const servers = ['ws://a:1/x', 'ws://b:1/x', 'ws://c:1/x', 'ws://d:1/x']
  await page.route('**/api/input**', (r) => r.fulfill({ json: { ok: true } }))
  await page.route('**/api/config', async (r) => {
    if (r.request().method() !== 'GET') {
      const body = r.request().postDataJSON() as object
      if (Object.keys(body).join() !== 'lang') puts.push(body)
      return r.fallback()
    }
    const res = await r.fetch()
    await r.fulfill({ response: res, json: { ...((await res.json()) as object), servers } })
  })
  await sim('PUT', '/api/setup', { step: 5 })
  await sim('PUT', '/api/config', { setup_done: false })
  await page.goto('/')
  await login(page)
  await expect(page.locator('#wz-step')).toHaveText('Which source controls your deck?')
  await expect(page.locator('.wizfoot .btn.pri')).toBeDisabled() // no source yet: only Later goes on
  await page.getByRole('radio', { name: /Own program/ }).click()
  await page.locator('#w-srv').fill('ws://e:1/x')
  await page.getByRole('button', { name: 'Connect' }).click()
  await expect(page.getByText('4 servers at most.')).toBeVisible()
  expect(puts).toEqual([])
  await page.unrouteAll({ behavior: 'ignoreErrors' })
  await page.waitForLoadState('networkidle')
  await page.close()
  await sim('PUT', '/api/setup', { step: 0 })
  await sim('PUT', '/api/config', { setup_done: true })
})

test('on the setup Wi-Fi the wizard only joins the Wi-Fi, stores no step, and closes the setup Wi-Fi', async ({
  page,
  errors: _errors,
}) => {
  const puts: unknown[] = []
  let closed = false
  await page.route('**/api/input**', (r) => r.fulfill({ json: { ok: true } }))
  await page.route('**/api/config', async (r) => {
    if (r.request().method() !== 'GET') puts.push(r.request().postDataJSON())
    await r.fallback()
  })
  await page.route('**/api/ap', (r) => ((closed = r.request().method() === 'DELETE'), r.fallback()))
  const steps: unknown[] = []
  page.on('request', (r) => r.method() === 'PUT' && r.url().endsWith('/api/setup') && steps.push(r.postDataJSON()))
  // The simulator is not on its setup Wi-Fi here, and its join never finishes: status and socket are patched.
  let joined = false
  type St = { t?: string; via_ap?: boolean; network?: Record<string, object> }
  const patch = (j: St) => {
    j.via_ap = true
    j.network = { ...j.network, ap: { ...j.network?.ap, on: true } }
    if (joined) j.network.wlan = { ...j.network.wlan, join: 'ok', ip: '192.168.1.50', ssid: 'Gast' }
    return j
  }
  await page.route('**/api/wifi/join', (r) => ((joined = true), r.fulfill({ json: { ok: true } })))
  await page.route('**/api/status', async (r) => {
    const res = await r.fetch()
    await r.fulfill({ response: res, json: patch((await res.json()) as St) })
  })
  await page.routeWebSocket('**/api/ws', (ws) => {
    const server = ws.connectToServer()
    server.onMessage((m) => {
      const j = JSON.parse(String(m)) as St
      ws.send(JSON.stringify(j.t === 'state' ? patch(j) : j))
    })
  })
  await page.addInitScript(() => localStorage.setItem('dc.lang', '"en"'))
  await sim('PUT', '/api/setup', { step: 0 })
  await sim('PUT', '/api/config', { setup_done: false })
  await page.goto('/')
  await login(page)
  await expect(page.locator('#wz-step')).toHaveText('How does the connector get online?')
  await expect(page.locator('#w-back')).toHaveCount(0)
  await page.getByRole('button', { name: /Gast/ }).first().click()
  await page.getByRole('button', { name: 'Next' }).click()
  await expect(page.locator('#wz-step')).toHaveText('Done')
  await expect(page.locator('a[href="http://192.168.1.50"]')).toBeVisible()
  await page.getByRole('button', { name: 'Close setup Wi-Fi' }).click()
  await expect.poll(() => closed).toBe(true)
  expect(puts).toEqual([])
  // No step stored: the browser on the user's network starts at the beginning, with name and password.
  expect(steps).toEqual([])
  await page.unrouteAll({ behavior: 'ignoreErrors' })
  await page.close()
  await sim('PUT', '/api/setup', { step: 0 })
  await sim('PUT', '/api/config', { setup_done: true })
})
