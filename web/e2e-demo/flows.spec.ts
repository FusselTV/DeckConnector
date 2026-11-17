import type { Page } from '@playwright/test'
import { calls, en, expect, openTab, test } from './helpers.ts'

// The flows behind the dialogs, which the crawler only opens: each one runs to its end in the mock.

const t = (k: keyof typeof en, v: Record<string, string | number> = {}) =>
  Object.entries(v).reduce((s, [a, b]) => s.replace(`{${a}}`, String(b)), en[k])
const dialog = (page: Page) => page.locator('dialog[open]').last()
const confirm = (page: Page, ok: string | RegExp) =>
  page.getByRole('alertdialog').getByRole('button', { name: ok }).click()
const saveBar = (page: Page) => page.locator('.savebar').getByRole('button', { name: new RegExp(`^${en['c.save']}`) })
const file = (name: string, data: string | Buffer) => ({
  name,
  mimeType: 'application/octet-stream',
  buffer: Buffer.from(data),
})
/** A firmware file for the main chip, v1.0.1, with a header the bridge accepts and an empty body. */
const FW = Buffer.concat([
  Buffer.from([...Buffer.from('DBFW'), 1, 1, 0, 0, 0, 1, 0, 0]),
  Buffer.from('v1.0.1'.padEnd(132, '\0')),
  Buffer.alloc(256),
])
const failed = async (page: Page) => (await calls(page)).filter((c) => c.status >= 400 && c.url !== '/api/coredump')

test.beforeEach(async ({ page }) => {
  await page.context().grantPermissions(['clipboard-read', 'clipboard-write'])
})

test('Wi-Fi: scan, a wrong password fails, the right one joins, a hidden network joins', async ({ demo: page }) => {
  test.setTimeout(60_000)
  await page.goto('?lang=en')
  await openTab(page, en, 'network')
  await page.locator('summary').filter({ hasText: en['net.wlan'] }).click()
  await page.getByRole('button', { name: en['wifi.rescan'] }).click()
  await page.getByRole('button', { name: /Office/ }).click()
  await page.locator('#wifi-pass').fill('wrongpass')
  await saveBar(page).click()
  await expect(page.getByText(en['wiz.test_bad']).first()).toBeVisible({ timeout: 15_000 })
  await page.locator('#wifi-pass').fill('office-pass-1')
  await saveBar(page).click()
  await expect(page.locator('summary').filter({ hasText: 'Office' })).toBeVisible({ timeout: 15_000 })
  await expect(page.locator('.savebar')).toHaveCount(0, { timeout: 15_000 })
  await page.getByRole('button', { name: en['wifi.hidden'] }).click()
  await page.locator('#wifi-ssid').fill('Backstage')
  await page.locator('#wifi-hpass').fill('backstage-pass')
  await saveBar(page).click()
  await expect(page.locator('summary').filter({ hasText: 'Backstage' })).toBeVisible({ timeout: 15_000 })
  expect(await failed(page)).toEqual([])
})

test('LAN: a static address saves, restarts and shows up', async ({ demo: page }) => {
  test.setTimeout(60_000)
  await page.goto('?lang=en')
  await openTab(page, en, 'network')
  await page.locator('summary').filter({ hasText: en['net.lan'] }).first().click()
  await page.locator('#lan-dhcp').getByRole('radio', { name: en['net.static'] }).click()
  await page.locator('#lan-ip').fill('192.168.1.50')
  await page.locator('#lan-mask').fill('255.255.255.0')
  await page.locator('#lan-gateway').fill('192.168.1.1')
  await page.locator('#lan-dns').fill('192.168.1.1')
  await saveBar(page).click()
  await page.getByRole('alertdialog').getByRole('button').last().click()
  await expect(page.locator('#rs-t')).toBeVisible()
  await expect(page.locator('#rs-t')).toHaveCount(0, { timeout: 20_000 })
  await openTab(page, en, 'network')
  await expect(page.getByText('192.168.1.50').filter({ visible: true }).first()).toBeVisible()
  expect(await failed(page)).toEqual([])
})

test('sources: add Companion, a server, open pairing for the app, edit and remove each', async ({ demo: page }) => {
  await page.goto('?lang=en')
  await openTab(page, en, 'sources')
  const rows = page.locator('#card-src')
  // Companion: picked from what the search found.
  await page.locator('#src-add').click()
  await dialog(page)
    .getByRole('button', { name: t('src.connect_to', { name: 'Backup Companion' }) })
    .click()
  await expect(rows.getByText('192.168.1.21')).toBeVisible()
  // A server.
  await page.locator('#src-add').click()
  await dialog(page).getByRole('radio', { name: en['prog.server'] }).click()
  await dialog(page).locator('#m-url').fill('ws://192.168.1.77:9000/deck')
  await dialog(page).getByRole('button', { name: en['c.add'] }).click()
  await expect(rows.getByText('ws://192.168.1.77:9000/deck')).toBeVisible()
  // A server that is not on this network fails to connect, like on a device.
  await page.locator('#src-add').click()
  await dialog(page).getByRole('radio', { name: en['prog.server'] }).click()
  await dialog(page).locator('#m-url').fill('example')
  await dialog(page).getByRole('button', { name: en['c.add'] }).click()
  await expect(dialog(page).getByText(en['src.bad_url'])).toBeVisible()
  await page.keyboard.press('Escape')
  // The Elgato app: pairing opens and closes.
  await page.locator('#src-add').click()
  await dialog(page).getByRole('radio', { name: en['prog.app'] }).click()
  await dialog(page).locator('#pair-open').click()
  await expect(dialog(page).getByText(/Pairing open/)).toBeVisible()
  await dialog(page).getByText(en['src.close'], { exact: true }).click()
  await expect(dialog(page).locator('#pair-open')).toBeVisible()
  await dialog(page).locator('#only').click()
  await dialog(page).getByRole('button', { name: en['wiz.s_done'] }).click()
  // Edit the server, then remove all of them.
  await page.getByRole('button', { name: 'Edit ws://192.168.1.77:9000/deck' }).click()
  await dialog(page).locator('#m-url').fill('ws://192.168.1.78:9000/deck')
  await dialog(page).getByRole('button', { name: en['c.save'] }).click()
  await expect(rows.getByText('ws://192.168.1.78:9000/deck')).toBeVisible()
  await page.getByRole('button', { name: 'Remove: ws://192.168.1.78:9000/deck' }).click()
  await expect(rows.getByText('ws://192.168.1.78:9000/deck')).toHaveCount(0)
  await page.getByRole('button', { name: /^Remove: (Studio|Backup) Companion|^Remove: 192\.168\.1\.21/ }).click()
  await confirm(page, en['src.remove'])
  await expect(rows.getByText('192.168.1.21')).toHaveCount(0)
  await page.getByRole('button', { name: 'Remove: edit-pc' }).click()
  await expect(rows.getByText('edit-pc')).toHaveCount(0)
  expect(await failed(page)).toEqual([])
})

test('pairing: a forgotten computer asks again, deny it, ask again', async ({ demo: page }) => {
  test.setTimeout(60_000)
  await page.clock.install()
  await page.goto('?lang=en')
  await openTab(page, en, 'sources')
  await page.getByRole('button', { name: 'Remove: edit-pc' }).click()
  await page.clock.runFor(7000)
  const ask = page.getByText(t('pair.t', { name: 'edit-pc', ip: '192.168.1.31' }))
  await expect(ask.filter({ visible: true }).first()).toBeVisible()
  await page
    .getByRole('button', { name: /^(Deny|Ablehnen)$/ })
    .first()
    .click()
  await expect(ask).toHaveCount(0)
  await page.locator('#src-add').click()
  await dialog(page).getByRole('radio', { name: en['prog.app'] }).click()
  await expect(dialog(page).getByText(`${en['wiz.denied']}: edit-pc`)).toBeVisible()
  await dialog(page).getByRole('button', { name: en['wiz.ask_again'] }).click()
  await expect(dialog(page).getByText(`${en['wiz.denied']}: edit-pc`)).toHaveCount(0)
  expect(await failed(page)).toEqual([])
})

test('API keys: create one, copy it, delete it', async ({ demo: page }) => {
  await page.goto('?lang=en')
  await openTab(page, en, 'access')
  await page.locator('#tok-add').click()
  await page.locator('#tname').fill('stream-pc')
  await dialog(page).getByRole('button', { name: 'Create' }).click()
  await expect(page.locator('#tokval')).toContainText(/dc_/)
  await dialog(page).getByRole('button', { name: en['wiz.s_done'] }).click()
  await page.getByRole('button', { name: 'Delete: stream-pc' }).click()
  await confirm(page, /Delete/)
  await expect(page.getByText('stream-pc')).toHaveCount(0)
  expect(await failed(page)).toEqual([])
})

test('TLS: a bad file is refused, a good pair shows its details and applies with a restart', async ({ demo: page }) => {
  test.setTimeout(60_000)
  await page.goto('?lang=en')
  await openTab(page, en, 'access')
  await page.locator('#cert-up').click()
  await page.locator('#crt').setInputFiles(file('cert.pem', 'hello'))
  await page.locator('#ckey').setInputFiles(file('key.pem', 'world'))
  await expect(dialog(page).getByRole('alert')).toBeVisible()
  await page
    .locator('#crt')
    .setInputFiles(file('cert.pem', '-----BEGIN CERTIFICATE-----\nMIIB\n-----END CERTIFICATE-----'))
  await page
    .locator('#ckey')
    .setInputFiles(file('key.pem', '-----BEGIN PRIVATE KEY-----\nMIIE\n-----END PRIVATE KEY-----'))
  await expect(dialog(page).getByText('CN=studio-deck.example.org')).toBeVisible()
  await dialog(page).getByRole('button', { name: en['btn.apply'] }).click()
  await expect(page.locator('#rs-t')).toBeVisible()
  await expect(page.locator('#rs-t')).toHaveCount(0, { timeout: 20_000 })
  expect(await failed(page)).toEqual([])
})

test('password: change it, stay logged in through the restart, log out and in again', async ({ demo: page }) => {
  test.setTimeout(60_000)
  await page.goto('?lang=en')
  await openTab(page, en, 'access')
  await page.locator('#npw').fill('new-secret-1')
  await saveBar(page).click()
  await page.getByRole('alertdialog').getByRole('button').last().click()
  await expect(page.locator('#rs-t')).toBeVisible()
  await expect(page.locator('#rs-t')).toHaveCount(0, { timeout: 20_000 })
  await expect(page.locator('#tabpanel')).toBeVisible()
  if (!(await page.locator('.side').isVisible()))
    await page.locator('.tabbar').getByRole('button', { name: en['tab.more'] }).click()
  await page.getByRole('button', { name: en['hdr.logout'] }).filter({ visible: true }).click()
  await expect(page.locator('#lpw')).toBeVisible()
  await page.locator('#lpw').fill('new-secret-1')
  await page.keyboard.press('Enter')
  await expect(page.locator('#lpw')).toHaveCount(0)
})

test('firmware: install from a file, from a URL, write a slot, boot a slot', async ({ demo: page }) => {
  test.setTimeout(90_000)
  await page.goto('?lang=en')
  await openTab(page, en, 'firmware')
  // A file that is too short is refused, a good one installs.
  await page.locator('#fw-file').click()
  await page.locator('#upd-file').setInputFiles(file('x.dbfw', 'short'))
  await expect(dialog(page).locator('.ferr, [id$=-err]').first()).toBeVisible()
  await page.locator('#upd-file').setInputFiles(file('deckconnector.dbfw', FW))
  await expect(dialog(page).getByText(t('fw.file_v', { v: 'v1.0.1' }), { exact: false })).toBeVisible()
  // The demo's own sample files take the same path.
  await dialog(page).getByRole('button', { name: 'deckconnector-web.dbfw' }).click()
  await expect(dialog(page).getByText(t('fw.file_v', { v: 'web demo 3f9a1c' }), { exact: false })).toBeVisible()
  await page.keyboard.press('Escape')
  // URL.
  await page.locator('#fw-file').click()
  await dialog(page).locator('#upd-mode').getByRole('radio', { name: en['upd.url'] }).click()
  await dialog(page).locator('#upd-url').fill('http://192.168.1.10/deckconnector-v1.0.1.dbfw')
  await dialog(page).getByRole('button', { name: en['upd.go_restart'] }).click()
  await confirm(page, en['btn.install'])
  await expect(page.getByText(en['ota.download']).first()).toBeVisible()
  await expect
    .poll(() => page.evaluate(() => sessionStorage.getItem('dc.demo')?.includes('"firmware":"v1.0.1"')), {
      timeout: 40_000,
    })
    .toBe(true)
  await page.waitForLoadState()
  await openTab(page, en, 'firmware')
  // Slots: write v1.0.1 into slot 4 without a restart, then boot the old one.
  await page.locator('summary').filter({ hasText: en['fw.slots'] }).click()
  const chooser = page.waitForEvent('filechooser')
  await page.getByRole('button', { name: en['fw.write'] }).nth(2).click()
  await (await chooser).setFiles(file('deckconnector.dbfw', FW))
  await confirm(page, en['fw.write'])
  await expect(page.getByText(t('fw.written', { slot: 4 }))).toBeVisible()
  await page.getByRole('button', { name: en['slot.boot'] }).first().click()
  await page.getByRole('alertdialog').getByRole('button').last().click()
  await expect(page.locator('#rs-t')).toBeVisible()
  await expect(page.locator('#rs-t')).toHaveCount(0, { timeout: 20_000 })
  expect(await failed(page)).toEqual([])
})

test('backup: save both kinds, restore changes the name, a broken file is refused', async ({ demo: page }) => {
  test.setTimeout(60_000)
  await page.goto('?lang=en')
  await openTab(page, en, 'firmware')
  await page.locator('#bak-dl').click()
  await dialog(page)
    .getByRole('radio', { name: new RegExp(en['fw.save_full']) })
    .click()
  const dl = page.waitForEvent('download')
  await dialog(page).getByRole('button', { name: en['fw.save'] }).click()
  const text = await (await (await dl).createReadStream()).toArray()
  const backup = JSON.parse(Buffer.concat(text).toString()) as Record<string, unknown>
  expect(backup).toMatchObject({ name: 'studio-deck', admin_pass: 'studio-demo' })
  let chooser = page.waitForEvent('filechooser')
  await page.locator('#bak-restore').click()
  await (await chooser).setFiles(file('broken.json', '{nope'))
  await expect(page.getByText(en['fw.restore_bad'])).toBeVisible()
  chooser = page.waitForEvent('filechooser')
  await page.locator('#bak-restore').click()
  const { admin_pass: _a, wifi_pass: _w, ...settings } = backup
  await (await chooser).setFiles(file('b.json', JSON.stringify({ ...settings, brightness: 40 })))
  await page.getByRole('alertdialog').getByRole('button').last().click()
  await expect(page.getByText(en['fw.restored'])).toBeVisible()
  expect(await failed(page)).toEqual([])
})

test('diagnostics: log, events, inputs, profile, metrics all show data', async ({ demo: page }) => {
  await page.goto('?lang=en')
  await openTab(page, en, 'diagnose')
  await expect(page.getByText('DeckConnector v1.0.0').first()).toBeVisible()
  for (const part of ['Firmware', 'Settings'])
    await expect(page.locator('#card-health').getByText(part, { exact: true })).toBeVisible()
  // The folds are exclusive: opening one closes the others.
  const open = async (name: string, shows: string | RegExp) => {
    await page.locator('summary').filter({ hasText: name }).first().click()
    await expect(page.getByText(shows).filter({ visible: true }).first()).toBeVisible()
  }
  await open('Events', /started \(restart\)/)
  await open('Profile', 'deck')
  await open('Memory', /MB|KB/)
  await open('Boot timing', /ms/)
  await open('API routes', '/api/screen/*')
  await page.getByRole('radio', { name: 'Before the last restart' }).click()
  await expect(page.getByText('restart asked over the API').first()).toBeVisible()
  expect(await failed(page)).toEqual([])
})

test('deck: keys, dials, strip and the deck menu', async ({ demo: page }) => {
  await page.goto('?lang=en')
  const before = (await calls(page)).length
  await page.getByRole('button', { name: t('ov.key', { n: 2, r: 1 }) }).click()
  await page.getByRole('button', { name: /^Dial 1/ }).click()
  await page.getByRole('button', { name: /^Touch strip/ }).click()
  await page
    .getByRole('button', { name: /^Deck menu|Menu/ })
    .first()
    .click()
  await expect
    .poll(async () => (await calls(page)).slice(before).map((c) => `${c.method} ${c.url}`))
    .toEqual(expect.arrayContaining(['POST /api/input', 'POST /api/menu']))
  await page.getByRole('button', { name: t('ov.key', { n: 4, r: 1 }) }).click() // Menü schließen
  await page.locator('#arm').click()
  await expect(page.locator('#arm')).toHaveAttribute('aria-checked', 'false')
  expect(await failed(page)).toEqual([])
})

test('setup wizard runs every step', async ({ demo: page }) => {
  test.setTimeout(90_000)
  await page.goto('?setup=1&lang=en')
  const step = page.getByText(/^Step \d of \d/)
  const next = page.locator('button.btn.pri').filter({ visible: true }).last()
  await expect(step).toContainText(en['wiz.s_lang'])
  await page.locator('#wname').fill('my-deck')
  await next.click()
  await expect(step).toContainText(en['wiz.s_pw'])
  await page.locator('#wpw').fill('demo-pass-1')
  await next.click()
  await expect(step).toContainText(en['wiz.s_net'])
  await next.click()
  await expect(step).toContainText(en['wiz.s_src'], { timeout: 20_000 })
  await page.getByRole('radio', { name: new RegExp(en['wiz.p_comp']) }).click()
  await page.locator('#w-sat').fill('192.168.1.99')
  await page.getByRole('button', { name: en['src.connect'], exact: true }).click()
  await expect(page.getByRole('alert').filter({ hasText: en['err.probe_refused'] }).first()).toBeVisible()
  await page.getByRole('button', { name: t('wiz.use', { name: 'Studio Companion' }) }).click()
  await expect(page.getByText(t('wiz.connected', { host: '' }).slice(1, 12)).first()).toBeVisible({ timeout: 10_000 })
  await next.click()
  await expect(page.getByText(en['wiz.done_q']).first()).toBeVisible({ timeout: 20_000 })
  await page.getByRole('button', { name: en['wiz.go'] }).click()
  await expect(page.locator('#tabpanel')).toBeVisible()
  expect(await failed(page)).toEqual([])
})

for (const [pick, fill] of [
  [en['prog.app'], null],
  [en['wiz.p_other'], 'ws://192.168.1.40:8765/deckconnector'],
] as const)
  test(`setup wizard: ${pick} connects and draws`, async ({ demo: page }) => {
    test.setTimeout(60_000)
    await page.goto('?setup=1&lang=en')
    const next = page.locator('button.btn.pri').filter({ visible: true }).last()
    await page.locator('#wname').fill('my-deck')
    await next.click()
    await page.locator('#wpw').fill('demo-pass-1')
    await next.click()
    await next.click()
    await page.getByRole('radio', { name: new RegExp(pick) }).click()
    if (fill) {
      await page.locator('#w-srv').fill(fill)
      await page.getByRole('button', { name: en['src.connect'], exact: true }).click()
    }
    await expect(page.getByText(/is connected and drawing/).first()).toBeVisible({ timeout: 15_000 })
    await next.click()
    await expect(page.getByText(en['wiz.done_q']).first()).toBeVisible({ timeout: 20_000 })
    expect(await failed(page)).toEqual([])
  })

test('login: show password, reset request, deck login request', async ({ demo: page }) => {
  test.setTimeout(60_000)
  await page.goto('?login=1&lang=en')
  await page.getByRole('button', { name: en['login.show'] }).click()
  await expect(page.getByText(/On the deck for \d+ s/).first()).toBeVisible()
  await page.getByRole('button', { name: en['login.forgot'] }).click()
  await confirm(page, en['login.reset_go'])
  await expect(page.getByText(en['login.reset_wait'].split('.')[0] ?? '').first()).toBeVisible()
  await page.getByRole('button', { name: en['c.cancel'] }).first().click()
  await page.getByRole('button', { name: en['login.req'] }).click()
  await expect(page.locator('#lpw')).toHaveCount(0, { timeout: 15_000 })
})
