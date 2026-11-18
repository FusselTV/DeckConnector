import { readdirSync, readFileSync } from 'node:fs'
import { expect, test } from '@playwright/test'

// Plain imports would make tsc follow the app's extensionless imports, which this tsconfig refuses.
const [modelFile, serverFile, uiFile]: string[] = [
  '../src/demo/model.ts',
  '../src/demo/server.ts',
  '../src/demo/deck-ui.ts',
]
await ((await import(uiFile)) as { deckUiReady: Promise<void> }).deckUiReady
type Demo = { bootAt: number }
const { boot, fresh } = (await import(modelFile)) as {
  fresh: (now: number) => Demo
  boot: (s: Demo, at: number) => void
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

// The same GET to the mock and to the firmware simulator (firmware/sim, the real handlers on Linux) must answer
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
