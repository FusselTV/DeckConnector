import { readdirSync, readFileSync } from 'node:fs'
import { expect, test } from '@playwright/test'
import { en } from '../src/i18n/en.ts'

// Plain imports would make tsc follow the app's extensionless imports, which this tsconfig refuses.
const [modelFile, serverFile, uiFile, backupFile]: string[] = [
  '../src/demo/model.ts',
  '../src/demo/server.ts',
  '../src/demo/deck-ui.ts',
  '../src/lib/backup.ts',
]
await ((await import(uiFile)) as { deckUiReady: Promise<void> }).deckUiReady
type Demo = { bootAt: number }
const { boot, fresh, EV, LIVE } = (await import(modelFile)) as {
  fresh: (now: number) => Demo
  boot: (s: Demo, at: number) => void
  EV: Record<string, [string, string, string]>
  LIVE: string[]
}
const { WRITABLE, restarts } = (await import(backupFile)) as {
  WRITABLE: Set<string>
  restarts: (k: string) => boolean
}
const { handle } = (await import(serverFile)) as {
  handle: (
    s: Demo,
    now: number,
    method: string,
    url: string,
    body: undefined,
    paint: () => object[],
  ) => {
    status: number
    body: unknown
  }
}

// The same GET to the mock and to firmware/simdeck (the whole firmware on Linux) must answer
// with the same keys and value types. Run by the "contract" project in playwright.demo.config.ts.

const SIM = `http://127.0.0.1:${process.env.DC_SIM_PORT ?? 18230}`
const AUTH = 'Basic ' + Buffer.from('admin:simulator-pass').toString('base64')

const ROUTES = [
  '/api',
  '/api/status',
  '/api/config',
  '/api/screen',
  '/api/slots',
  '/api/frontend',
  '/api/events',
  '/api/metrics',
  '/api/wifi/scan',
  '/api/companion/found',
  '/api/tokens',
  '/api/update/check',
  '/api/profile',
  '/api/input/log',
  '/api/boot',
  '/api/backup',
  '/api/coredump',
]

// The simulator leaves out what needs hardware (network, memory, pairing). A key only the mock sends is fine when
// the firmware writes it somewhere: its sources name every JSON key it sends. An invented key fails.
const fw = new URL('../../firmware/', import.meta.url)
const FIRMWARE_KEYS = new Set(
  ['main/', ...readdirSync(new URL('components/', fw)).map((d) => `components/${d}/`)].flatMap((dir) =>
    readdirSync(new URL(dir, fw))
      .filter((f) => f.endsWith('.c'))
      .flatMap((f) =>
        [...readFileSync(new URL(dir + f, fw), 'utf8').matchAll(/\\?"([a-z_0-9]+)\\?"/g)].map((m) => m[1]),
      ),
  ),
)
// Where the demo shows more than the simulator can, on purpose.
const DEMO_ONLY: Record<string, string> = {
  '/api/update/check': 'the firmware has no release feed yet; the demo offers v1.0.1 so the update flow can be tried',
}

type Shape = string | { [k: string]: Shape }
const name = (x: Shape) => (typeof x === 'string' ? x : 'object')

/** Array items merge: every key any item has; a key with two types becomes "a|b" and fails the comparison. */
function merge(a: Shape, b: Shape): Shape {
  if (typeof a === 'string' || typeof b === 'string') return a === b ? a : `${name(a)}|${name(b)}`
  const out: Record<string, Shape> = { ...a }
  for (const [k, v] of Object.entries(b)) out[k] = k in out ? merge(out[k], v) : v
  return out
}

function shape(v: unknown): Shape {
  if (v === null) return 'null'
  if (Array.isArray(v)) return { '[]': v.length ? v.map(shape).reduce(merge) : 'empty' }
  if (typeof v === 'object')
    return Object.fromEntries(
      Object.entries(v)
        .filter(([, x]) => x !== undefined)
        .map(([k, x]) => [k, shape(x)]),
    )
  return typeof v
}

function diff(mock: Shape, sim: Shape, at: string, out: string[]) {
  if (typeof mock === 'string' || typeof sim === 'string') {
    if (mock !== sim && mock !== 'null' && sim !== 'null' && mock !== 'empty' && sim !== 'empty')
      out.push(`${at}: mock ${name(mock)}, simulator ${name(sim)}`)
    return
  }
  const m = mock as Record<string, Shape>
  const s = sim as Record<string, Shape>
  for (const k of Object.keys(s)) if (!(k in m)) out.push(`${at}.${k}: missing in the mock`)
  for (const k of Object.keys(m))
    if (!(k in s)) {
      if (!FIRMWARE_KEYS.has(k)) out.push(`${at}.${k}: only in the mock, and the firmware never sends it`)
    } else diff(m[k], s[k], `${at}.${k}`, out)
}

test('the mock answers the main GET routes with the shape the simulator gives', async () => {
  const demo = fresh(Date.now())
  boot(demo, demo.bootAt)
  const out: string[] = []
  const empty: string[] = []
  const headers = { Authorization: AUTH, 'X-DeckConnector': '1', 'Content-Type': 'application/json' }
  await fetch(SIM + '/api/tokens', {
    method: 'POST',
    headers,
    body: JSON.stringify({ name: 'contract', right: 'full' }),
  })
  // simdeck answers before its emulated deck is attached, and an empty deck list compares with anything
  await expect
    .poll(async () => ((await (await fetch(SIM + '/api/status', { headers })).json()) as { decks: [] }).decks.length)
    .toBeGreaterThan(0)
  for (const path of ROUTES) {
    const r = await fetch(SIM + path, { headers })
    const text = await r.text()
    const mine = handle(demo, Date.now(), 'GET', path, undefined, () => [{ x: 0, y: 0, w: 1, h: 1, url: 'u' }])
    if (mine.status !== r.status) out.push(`${path}: status mock ${mine.status}, simulator ${r.status}`)
    const theirs: unknown = r.headers.get('content-type')?.includes('json') ? JSON.parse(text) : text
    if (Array.isArray(theirs) && !theirs.length) empty.push(path)
    if (!DEMO_ONLY[path]) diff(shape(mine.body), shape(theirs), path, out)
  }
  // The simulator has no data there, so only the types (src/demo/server.ts) guard these.
  test.info().annotations.push({ type: 'not compared', description: empty.join(', ') })
  expect(out).toEqual([])
})

// The page translates events by code: the mock must log the firmware's codes with the same kind, level and text.
test('the mock knows every firmware event code as the firmware defines it', () => {
  const src = readFileSync(new URL('components/db_log/evlog.c', fw), 'utf8')
  const codes = Object.fromEntries(
    [...src.matchAll(/\[DB_EV_\w+\]\s*=\s*\{\s*"(\w+)",\s*K_(\w+),\s*L_(\w+),\s*((?:"[^"]*"\s*)+)\}/g)].map((m) => [
      m[1],
      [m[2]?.toLowerCase(), m[3]?.toLowerCase(), [...m[4].matchAll(/"([^"]*)"/g)].map((x) => x[1]).join('')],
    ]),
  )
  const enumSrc = readFileSync(new URL('components/db_log/include/db_log.h', fw), 'utf8')
  expect(Object.keys(codes).length, 'a code without its row in the table').toBe(
    [...enumSrc.matchAll(/^\s+DB_EV_(?!COUNT)\w+,/gm)].length,
  )
  expect(EV).toEqual(codes)
  for (const code of Object.keys(codes).filter((c) => !['text', 'more', 'crashed'].includes(c)))
    expect(en, `the page has no words for ${code}`).toHaveProperty([`ev.${code}`])
})

// db_config_t field -> its JSON key, where they differ
const JSON_KEY: Record<string, string> = {
  servers_first: 'servers',
  servers_more: 'servers',
  qr_login_off: 'qr_login',
  satellite_auto_off: 'satellite_auto',
}

// The backup is the settings JSON: a field of db_config_t outside it would be lost on a restore.
test('every field of db_config_t is in the JSON', () => {
  const h = readFileSync(new URL('components/db_config/include/db_config.h', fw), 'utf8')
  const struct = h.slice(h.indexOf('/* Saved as one NVS blob'), h.indexOf('} db_config_t;'))
  const fields = [...struct.matchAll(/^\s+(?:const )?[a-z_0-9]+\s+([a-z_0-9]+)\s*[[;]/gm)].map((m) => m[1])
  const notSettings = ['version', 'input_lock', 'unused_token', 'server_tokens'] // pairings stay out of a backup
  const src = readFileSync(new URL('components/db_config/db_config.c', fw), 'utf8')
  expect(fields.length).toBeGreaterThan(30)
  expect(fields.filter((f) => !notSettings.includes(f) && !src.includes(`"${JSON_KEY[f] ?? f}"`))).toEqual([])
})
test('the firmware reads back every key it writes', () => {
  const src = readFileSync(new URL('components/db_config/db_config.c', fw), 'utf8')
  const [, write, read] = src.split(/^size_t db_config_to_json|^\/\* Copy a string member/m)
  const keys = (s = '') => new Set([...s.matchAll(/"([a-z_0-9]+)"/g)].map((m) => m[1]))
  const taken = keys(read)
  expect([...keys(write)].filter((k) => !k.endsWith('_set') && k !== 'setup_done' && !taken.has(k))).toEqual([])
})

// A field missing from api.c's list restarts the bridge on every change of it; the demo and the restore page say so.
test('the demo and the restore page know which settings apply without a restart', () => {
  const src = readFileSync(new URL('components/db_web/api.c', fw), 'utf8')
  const fields = [...src.matchAll(/live\.(\w+) = old\.\1;|memcpy\(live\.(\w+), old\.\2,/g)]
    .map((m) => m[1] || m[2])
    .filter((f) => f !== 'server_tokens') // not a setting: a new URL drops it
  const live = new Set([...fields.map((f) => JSON_KEY[f] ?? f), 'matter']) // api.c turns Matter on live, off restarts
  expect(live.size).toBeGreaterThan(20)
  expect(Object.keys(JSON_KEY).filter((f) => live.has(JSON_KEY[f]) !== fields.includes(f))).toEqual([])
  expect([...new Set(LIVE)].sort()).toEqual([...live].sort())
  expect([...WRITABLE].filter((k) => restarts(k) === live.has(k))).toEqual([])
})
