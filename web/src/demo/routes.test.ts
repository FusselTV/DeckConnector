import { afterEach, beforeAll, describe, expect, it, vi } from 'vitest'
import http from '../../../firmware/components/db_web/http.c?raw'
import { api, certUrl, coredumpRawUrl } from '../api/client'
import { deckUiReady } from './deck-ui'
import { boot, fresh } from './model'
import mockSrc from './server.ts?raw'
import { handle } from './server'

// The mock must speak the firmware's API: the route table in http.c is the truth.

beforeAll(() => deckUiReady)

const ACCESS = { WEB_OPEN: 'open', WEB_CONTROL: 'control', WEB_FULL: 'full', WEB_ADMIN: 'admin' } as const

/** method, path and access of every route; `extra`: behind #ifdef DB_DEBUG_CRASH or #if CONFIG_DB_MATTER, not in the demo's firmware. */
function firmwareRoutes() {
  const table = http.slice(http.indexOf('} list[] = {'), http.indexOf('_Static_assert(sizeof(list)'))
  const out: { method: string; path: string; access: string; extra: boolean }[] = []
  let extra = false
  for (const chunk of table.split(/(?=#if|#endif|\{\{\.uri)/)) {
    if (chunk.startsWith('#ifdef DB_DEBUG') || chunk.startsWith('#if CONFIG_DB_MATTER')) extra = true
    if (chunk.startsWith('#endif')) extra = false
    const [, path, method, level] =
      /^\{\{\.uri = "([^"]+)",\s*\.method = HTTP_(\w+)[\s\S]*?\},\s*(WEB_\w+)\}/.exec(chunk) ?? []
    if (path && method) out.push({ path, method, access: ACCESS[level as keyof typeof ACCESS], extra })
  }
  return out
}

const FW = firmwareRoutes().filter((r) => r.path.startsWith('/api') && r.method !== 'OPTIONS')
const key = (r: { method: string; path: string }) => `${r.method} ${r.path}`
/** The firmware route a request lands on: httpd matches a trailing * as any rest of the path. */
const routeOf = (method: string, path: string) =>
  FW.find(
    (r) => r.method === method && (r.path === path || (r.path.endsWith('/*') && path.startsWith(r.path.slice(0, -1)))),
  )

const blob = new Blob(['x'.repeat(200)])
const ARGS: Partial<Record<keyof typeof api, unknown[]>> = {
  ota: [blob],
  inspect: [blob],
  putFrontend: [blob],
  dock: [{ pairing: 0 }],
  bootSlot: [2],
}

/** Every request the page can make: each api.* call, the links it renders, the socket and the deck pictures. */
async function uiRequests() {
  const seen: { method: string; path: string }[] = []
  vi.stubGlobal('fetch', (url: string, init?: RequestInit) => {
    seen.push({ method: init?.method ?? 'GET', path: new URL(url, 'http://x').pathname })
    return Promise.resolve(new Response('[]', { headers: { 'content-type': 'application/json' } }))
  })
  for (const [name, fn] of Object.entries(api))
    await (fn as (...a: unknown[]) => Promise<unknown>)(...(ARGS[name as keyof typeof api] ?? ['x'])).catch(() => 0)
  for (const u of [certUrl, coredumpRawUrl, '/api/ws', '/api/screen/7.jpg'])
    seen.push({ method: 'GET', path: new URL(u, 'http://x').pathname })
  return seen
}

afterEach(() => void vi.unstubAllGlobals())

describe('the mock against the firmware route table', () => {
  it('parses the route table', () => {
    expect(FW.length).toBeGreaterThan(50)
    expect(FW.find((r) => r.path === '/api/ws')).toMatchObject({ method: 'GET', access: 'control' })
  })

  it('every request of the page goes to a firmware route', async () => {
    const lost = (await uiRequests()).filter((r) => !routeOf(r.method, r.path)).map(key)
    expect(lost).toEqual([])
  })

  it('the mock answers every route the page calls', async () => {
    const missing: string[] = []
    for (const r of await uiRequests()) {
      // The socket is the DemoSocket in install.ts; deck pictures come as data: URLs in the /api/screen reply.
      if (r.path === '/api/ws' || r.path.startsWith('/api/screen/') || routeOf(r.method, r.path)?.extra) continue
      const s = fresh(0)
      boot(s, 0)
      const reply = handle(s, 1000, r.method, r.path, r.method === 'GET' ? undefined : {}, () => [])
      if ((reply.body as { error?: string } | null)?.error === 'no such endpoint') missing.push(key(r))
    }
    expect(missing).toEqual([])
  })

  it('the route list of the mock (GET /api) is the firmware table, access levels included', () => {
    const s = fresh(0)
    const mine = (handle(s, 0, 'GET', '/api', undefined, () => []).body as typeof FW)
      .map((r) => `${key(r)} ${r.access}`)
      .sort()
    const theirs = FW.filter((r) => !r.extra)
      .map((r) => `${key(r)} ${r.access}`)
      .sort()
    expect(mine).toEqual(theirs)
  })

  it('every route the mock handles exists in the firmware', () => {
    const named = [...mockSrc.matchAll(/'(GET|POST|PUT|DELETE) (\/api[^' ]*)'/g)].map(([, method = '', path = '']) => ({
      method,
      path,
    }))
    expect(named.length).toBeGreaterThan(40)
    expect(named.filter((r) => !routeOf(r.method, r.path.replace(/\*$/, 'x'))).map(key)).toEqual([])
  })
})
