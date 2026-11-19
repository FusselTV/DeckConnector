import { spawn, type ChildProcess } from 'node:child_process'
import { copyFileSync, readFileSync, rmSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { expect, test, type Browser, type Page } from '@playwright/test'
import { SIM_PORT } from './ports.ts'

// A fresh simulator of its own: the shared one was set up long ago, and only a fresh start lets browsers in. A new
// password restarts it; it comes back on its flash file, but unlike a board it keeps no sessions over the restart.
const PORT = SIM_PORT + 7
const BASE = `http://127.0.0.1:${PORT}`
const ELF = join(tmpdir(), `deckconnector-claim-${PORT}.elf`)
const FLASH = join(tmpdir(), `deckconnector-claim-${PORT}.flash`)
let sim: ChildProcess | null = null
let password = 'simulator' // main.c writes DB_SIM_PASSWORD over the stored one at each start

function start() {
  sim = spawn(ELF, [], {
    env: { ...process.env, DB_SIM_PORT: String(PORT), DB_SIM_PASSWORD: password, DB_SIM_FLASH: FLASH },
    stdio: 'ignore',
  })
  sim.on('exit', () => sim && start())
}

const up = () =>
  expect
    .poll(
      () =>
        fetch(BASE).then(
          (r) => r.ok,
          () => false,
        ),
      { timeout: 10_000 },
    )
    .toBe(true)

test.beforeAll(async () => {
  copyFileSync(new URL('../../firmware/sim/build/deckbridge_sim.elf', import.meta.url).pathname, ELF)
  const image = Buffer.alloc(0x90000, 0xff) // nvs and evlog as in firmware/sim/partitions.csv
  readFileSync(new URL('../../firmware/sim/build/partition_table/partition-table.bin', import.meta.url)).copy(
    image,
    0x8000,
  )
  writeFileSync(FLASH, image)
  start()
  await up()
})

test.afterAll(() => {
  const s = sim
  sim = null
  s?.kill()
  rmSync(FLASH, { force: true })
})

const errors: string[] = []
test.afterEach(() => expect(errors).toEqual([]))

async function open(browser: Browser): Promise<Page> {
  const page = await (await browser.newContext()).newPage()
  page.on('pageerror', (e) => errors.push(e.message))
  await page.addInitScript(() => localStorage.setItem('dc.lang', '"en"'))
  await page.goto(BASE)
  return page
}

test('during setup a second browser gets in too, until the first one sets a password', async ({ browser }) => {
  test.skip(test.info().project.name !== 'desktop', 'one fresh simulator per run')
  const a = await open(browser)
  await expect(a.locator('#wz-step')).toBeVisible()
  const b = await open(browser)
  await expect(b.locator('#wz-step')).toBeVisible()
  await expect(b.locator('#lpw')).toHaveCount(0)

  // What the wizard's network step sends with a new password, from the first browser's session.
  password = 'chosen-in-a'
  const put = await a.request.put(`${BASE}/api/config`, {
    headers: { 'X-DeckConnector': '1' },
    data: { admin_pass: 'chosen-in-a' },
  })
  expect(put.status()).toBe(200)

  await expect(b.getByText('Another browser went on with the setup.')).toBeVisible({ timeout: 15_000 })
  await b.locator('#lpw').fill('chosen-in-a')
  await b.getByRole('button', { name: 'Log in' }).click()
  await expect(b.locator('#wz-step')).toBeVisible()

  const c = await open(browser)
  await expect(c.getByText('Another browser went on with the setup.')).toBeVisible()
})
