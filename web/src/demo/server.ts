import type { InputBody, Route } from '../api/types'
import {
  companion,
  config,
  dock,
  face,
  HEIGHT,
  input,
  KEYS,
  latest,
  makeToken,
  putConfig,
  reachable,
  restartSoon,
  startOta,
  status,
  STRIP,
  WIDTH,
  WIFI,
  type Demo,
  type Face,
  type Reply,
} from './model'

/** Turns a face into one image URL per key and one for the strip. */
export type Paint = (f: Face) => { keys: string[]; strip: string }

/** A request body as the fetch shim hands it over: parsed JSON, or the size of an upload. */
export type Body = Record<string, unknown> | { blob: number } | undefined

const ok = (extra: object = {}): Reply => ({ status: 200, body: { ok: true, ...extra } })
const fail = (status: number, error: string, code?: string): Reply => ({ status, body: { ok: false, error, code } })

const ROUTES: [string, string, Route['access']][] = [
  ['GET', '/api', 'control'],
  ['GET', '/api/status', 'control'],
  ['GET', '/api/config', 'full'],
  ['PUT', '/api/config', 'full'],
  ['POST', '/api/ota', 'full'],
  ['POST', '/api/reboot', 'full'],
  ['GET', '/api/log', 'full'],
  ['GET', '/api/screen', 'control'],
  ['GET', '/api/screen/*', 'control'],
  ['POST', '/api/input', 'control'],
  ['POST', '/api/menu', 'control'],
  ['DELETE', '/api/menu', 'control'],
  ['POST', '/api/deck/reconnect', 'control'],
  ['POST', '/api/dock', 'full'],
  ['DELETE', '/api/pairing/denied/*', 'full'],
  ['GET', '/api/screen.jpg', 'control'],
  ['POST', '/api/ota/url', 'full'],
  ['GET', '/api/slots', 'full'],
  ['POST', '/api/slots/*', 'full'],
  ['GET', '/api/tokens', 'admin'],
  ['GET', '/api/backup', 'admin'],
  ['POST', '/api/tokens', 'admin'],
  ['DELETE', '/api/tokens/*', 'admin'],
  ['GET', '/api/ws', 'control'],
  ['POST', '/api/login', 'open'],
  ['POST', '/api/logout', 'open'],
  ['POST', '/api/password/show', 'open'],
  ['POST', '/api/password/reset', 'open'],
  ['DELETE', '/api/password/reset', 'open'],
  ['POST', '/api/login/request', 'open'],
  ['GET', '/api/login/request/*', 'open'],
  ['DELETE', '/api/login/request/*', 'open'],
  ['GET', '/api/profile', 'full'],
  ['GET', '/api/metrics', 'control'],
  ['GET', '/api/frontend', 'control'],
  ['GET', '/api/companion/found', 'full'],
  ['GET', '/api/cert', 'open'],
  ['PUT', '/api/tls', 'admin'],
  ['DELETE', '/api/tls', 'full'],
  ['GET', '/api/input/log', 'control'],
  ['GET', '/api/events', 'control'],
  ['POST', '/api/sources/retry', 'control'],
  ['POST', '/api/sources/probe', 'full'],
  ['POST', '/api/ota/inspect', 'full'],
  ['PUT', '/api/setup', 'full'],
  ['GET', '/api/update/check', 'full'],
  ['GET', '/api/wifi/scan', 'full'],
  ['POST', '/api/wifi/join', 'full'],
  ['PUT', '/api/frontend', 'full'],
  ['DELETE', '/api/frontend', 'full'],
  ['GET', '/api/coredump', 'admin'],
  ['GET', '/api/boot', 'full'],
  ['DELETE', '/api/coredump', 'full'],
]

const OPEN = new Set(ROUTES.filter((r) => r[2] === 'open').map((r) => r[1]))

export const CERT = `-----BEGIN CERTIFICATE-----
MIIBszCCAVmgAwIBAgIUDemoDemoDemoDemoDemoDemoDemo0wCgYIKoZIzj0EAwIw
HzEdMBsGA1UEAwwUZGVja2Nvbm5lY3Rvci1hMWIyYzMwHhcNMjYxMDAxMDAwMDAw
WhcNMzYwOTI4MDAwMDAwWjAfMR0wGwYDVQQDDBRkZWNrY29ubmVjdG9yLWExYjJj
MzBZMBMGByqGSM49AgEGCCqGSM49AwEHA0IABERlbW8gY2VydGlmaWNhdGUsIG5v
dCBhIHJlYWwga2V5LiBEZW1vIGNlcnRpZmljYXRlLCBub3QgYSByZWFsIGtleS4u
-----END CERTIFICATE-----
`

/** The CPU, heap and PSRAM curves: smooth, a little noisy, the same for the same time. */
function series(now: number, base: number, swing: number, seed: number) {
  const out: number[] = []
  for (let i = 119; i >= 0; i--) {
    const t = Math.floor(now / 5000) - i
    out.push(Math.round((base + swing * Math.sin(t / 7 + seed) + (swing / 3) * Math.sin(t * 1.7 + seed * 3)) * 10) / 10)
  }
  return out
}

function prevLog(s: Demo) {
  return [
    `00:00:00.021 I (21) boot: DeckConnector ${s.firmware}, ESP32-P4 rev v3.1`,
    '00:00:00.412 I (412) deck: Stream Deck + EL00K1A00001 at high speed',
    '00:00:06.692 I (6692) net: LAN 192.168.1.214/24 via DHCP',
    '03:14:27.180 I web: POST /api/reboot',
    '03:14:27.190 W main: restart asked over the API',
  ].join('\n')
}

export function handle(s: Demo, now: number, method: string, url: string, body: Body, paint: Paint): Reply {
  const u = new URL(url, 'http://demo.invalid')
  const path = u.pathname.replace(/\/$/, '') || '/'
  const q = u.searchParams
  const b = body && !('blob' in body) ? body : {}
  const str = (k: string) => (typeof b[k] === 'string' ? b[k] : '')
  const route = `${method} ${path}`

  if (route === 'POST /api/login') {
    if (!str('password')) return fail(401, 'wrong password', 'wrong_password')
    s.authed = true
    return ok()
  }
  if (route === 'POST /api/logout') {
    s.authed = false
    return ok()
  }
  if (route === 'POST /api/password/show') {
    s.pwUntil = now + 30_000
    return ok({ decks: 1 })
  }
  if (route === 'POST /api/password/reset') {
    s.resetUntil = now + 60_000
    return ok({ s: 60 })
  }
  if (route === 'DELETE /api/password/reset') {
    s.resetUntil = 0
    return ok()
  }
  if (route === 'POST /api/login/request') {
    s.loginReq = { id: 'd3m0'.padEnd(32, '0'), at: now, state: 'pending' }
    return { status: 200, body: { id: s.loginReq.id, s: 60 } }
  }
  if (path.startsWith('/api/login/request/')) {
    const r = s.loginReq
    if (!r || r.id !== path.slice(19)) return fail(404, 'no such request')
    if (method === 'DELETE') {
      s.loginReq = null
      return ok()
    }
    const left = Math.max(0, 60 - Math.floor((now - r.at) / 1000))
    const state = r.state
    if (state === 'accepted') {
      s.authed = true
      r.state = 'used'
    }
    return { status: 200, body: { state, s: left } }
  }
  if (route === 'GET /api/cert') return { status: 200, body: CERT }
  if (!OPEN.has(path) && !s.authed) return fail(401, 'login required')

  switch (route) {
    case 'GET /api':
      return { status: 200, body: ROUTES.map(([m, p, access]) => ({ method: m, path: p, access })) }
    case 'GET /api/status':
      return { status: 200, body: status(s, now) }
    case 'GET /api/config':
      return { status: 200, body: config(s) }
    case 'GET /api/backup':
      return {
        status: 200,
        body: {
          ...config(s),
          ...(q.get('secrets') ? { admin_pass: s.pass, wifi_pass: s.cfg.wifi_pass_set ? 'studio-wifi-pass' : '' } : {}),
        },
      }
    case 'PUT /api/config':
      return putConfig(s, now, b)
    case 'GET /api/screen': {
      const f = face(s, now)
      const p = paint(f)
      return {
        status: 200,
        body: {
          writer: status(s, now).writer,
          width: WIDTH,
          height: HEIGHT,
          keys: KEYS,
          touch: [STRIP],
          encoders: 4,
          menu_chord: {
            on: s.cfg.menu_chord === 'auto' ? 'encoders' : s.cfg.menu_chord,
            first: 0,
            last: s.cfg.menu_chord === 'keys' ? 7 : 3,
            hold_ms: s.cfg.menu_hold_ms,
          },
          images: [...KEYS.map((r, i) => ({ ...r, url: p.keys[i] ?? '' })), { ...STRIP, url: p.strip }],
        },
      }
    }
    case 'POST /api/input':
      return input(s, now, b as unknown as InputBody)
    case 'POST /api/menu':
      s.menu = true
      return ok()
    case 'DELETE /api/menu':
      s.menu = false
      s.pwUntil = 0
      s.resetUntil = 0
      return ok()
    case 'POST /api/deck/reconnect':
    case 'POST /api/deck/stall':
      return ok()
    case 'POST /api/reboot':
      restartSoon(s, now)
      return ok()
    case 'POST /api/dock':
      return dock(s, now, b)
    case 'GET /api/log':
      return { status: 200, body: q.get('boot') === 'prev' ? prevLog(s) : s.log.join('\n') + '\n' }
    case 'GET /api/events':
      return { status: 200, body: s.events }
    case 'GET /api/coredump':
      return fail(404, 'no coredump')
    case 'DELETE /api/coredump':
      return ok()
    case 'GET /api/slots':
      return { status: 200, body: s.slots }
    case 'GET /api/boot':
      return {
        status: 200,
        body: { version: 'v1.0.0', idf: 'v5.5.1', built: '2026-09-01', sha256: 'demo', partitions: [], md5: 'demo' },
      }
    case 'POST /api/ota': {
      const size = typeof body?.blob === 'number' ? body.blob : 0
      if (size < 144) return fail(400, 'file too short', 'ota_too_short')
      const n = Number(q.get('slot') ?? 0)
      const into = n || ((s.slots.find((x) => x.running)?.slot ?? 1) % s.slots.length) + 1
      const keep = q.get('boot') === '0'
      s.slots = s.slots.map((x) =>
        x.slot === into
          ? {
              ...x,
              state: 'valid',
              version: latest,
              built: '2026-11-04',
              level: 1,
              bootable: true,
              why_not: undefined,
              next: !keep,
            }
          : { ...x, next: keep ? x.next : false },
      )
      if (!keep) restartSoon(s, now)
      return ok(keep ? {} : { restart: true })
    }
    case 'POST /api/ota/inspect': {
      const size = typeof body?.blob === 'number' ? body.blob : 0
      if (size < 144) return { status: 200, body: { ok: false, error: 'file too short', code: 'ota_too_short' } }
      return {
        status: 200,
        body: {
          ok: true,
          target: 'p4',
          version: latest,
          length: 1_835_008,
          sha256: '9f2c41e07b3d58a6c1e2f04d7a96b3c8e5d21f0a4b7c6e9d8f1a2b3c4d5e6f70',
        },
      }
    }
    case 'POST /api/ota/url': {
      const url = str('url')
      if (!/^https?:\/\//.test(url)) return fail(400, 'http or https URL', 'url_http_only')
      return startOta(s, now)
    }
    case 'GET /api/update/check':
      return {
        status: 200,
        body:
          s.firmware === latest
            ? { current: s.firmware, latest }
            : { current: s.firmware, latest, url: `https://updates.example.org/deckconnector/${latest}.dbfw` },
      }
    case 'GET /api/frontend':
      return {
        status: 200,
        body: s.pagePartition
          ? { source: 'partition', api: 2, version: s.pagePartition, api_min: 2, size: 168_440 }
          : { source: 'builtin', api: 2 },
      }
    case 'PUT /api/frontend':
      s.pagePartition = latest
      return ok()
    case 'DELETE /api/frontend':
      s.pagePartition = null
      return ok()
    case 'GET /api/companion/found':
      return {
        status: 200,
        body: [
          { name: companion.name, host: '192.168.1.20', port: 16622, version: companion.version, surfaces: 1 },
          { name: 'Backup Companion', host: '192.168.1.21', port: 16622, version: companion.version, surfaces: 0 },
        ],
      }
    case 'GET /api/wifi/scan':
      return { status: 200, body: WIFI }
    case 'POST /api/wifi/join': {
      s.join = { ssid: str('ssid'), bssid: str('bssid'), pass: str('pass'), until: now + 3000 }
      s.joinState = 'testing'
      s.joinError = ''
      return { status: 202, body: { ok: true } }
    }
    case 'GET /api/metrics':
      return {
        status: 200,
        body: {
          every_s: 5,
          cpu: series(now, 14, 6, 1),
          heap: series(now, 63, 2, 2),
          psram: series(now, 8, 0.6, 3),
          flash: {
            nvs: { used: 9_216, size: 24_576 },
            ota: { used: 3_670_016, size: 16_777_216 },
            web: { used: 168_440, size: 1_048_576 },
          },
        },
      }
    case 'GET /api/profile':
      return {
        status: 200,
        body: {
          routes: [
            { method: 'GET', uri: '/api/status', count: 6, avg_us: 1830, max_us: 4210 },
            { method: 'GET', uri: '/api/screen', count: 4, avg_us: 920, max_us: 1600 },
            { method: 'GET', uri: '/api/screen/*', count: 36, avg_us: 410, max_us: 1220 },
            { method: 'POST', uri: '/api/input', count: 3, avg_us: 640, max_us: 880 },
          ],
          cpu_permille: { deck: 38, link: 21, web: 17, net: 9, IDLE0: 860, IDLE1: 912 },
          stack_free: { deck: 2140, link: 3012, web: 4420, net: 1880, main: 2600 },
        },
      }
    case 'GET /api/tokens':
      return { status: 200, body: s.tokens }
    case 'POST /api/tokens':
      return makeToken(s, str('name'), str('right') === 'full' ? 'full' : 'control', now)
    case 'GET /api/input/log': {
      const since = Number(q.get('since') ?? 0)
      return { status: 200, body: s.inputs.filter((e) => e.seq > since) }
    }
    case 'POST /api/sources/retry': {
      const h = str('host')
      if (h !== s.cfg.satellite && !s.cfg.servers.includes(h)) return fail(404, 'no such host')
      s.since[h === s.cfg.satellite ? 'satellite' : h] = now
      return ok()
    }
    case 'POST /api/sources/probe': {
      const a = str('addr')
      if (!reachable(a))
        return { status: 200, body: { ok: false, error: 'no connection', code: 'probe_no_connection' } }
      return { status: 200, body: { ok: true, version: companion.version, surfaces: 1 } }
    }
    case 'PUT /api/setup':
      s.setupStep = Math.max(0, Math.min(6, Number(b.step) || 0))
      return ok()
    case 'PUT /api/tls': {
      const cert = str('cert')
      const key = str('key')
      if (!cert.includes('BEGIN CERTIFICATE')) return fail(400, 'not a PEM certificate')
      if (!/BEGIN (EC |RSA )?PRIVATE KEY/.test(key)) return fail(400, 'not a PEM key')
      if (b.apply) {
        s.tls = true
        restartSoon(s, now)
      }
      return {
        status: 200,
        body: {
          subject: 'CN=studio-deck.example.org',
          issuer: 'CN=Example Studio CA',
          not_after: '2027-11-08',
          sha256: '6E:B4:09:C3:A7:58:F2:1D',
        },
      }
    }
    case 'DELETE /api/tls':
      s.tls = false
      restartSoon(s, now)
      return ok()
  }
  if (method === 'POST' && /^\/api\/slots\/\d+\/boot$/.test(path)) {
    const n = Number(path.split('/')[3])
    const slot = s.slots.find((x) => x.slot === n)
    if (!slot?.bootable || slot.running) return fail(409, slot?.why_not ?? 'not bootable')
    s.slots = s.slots.map((x) => ({ ...x, next: x.slot === n }))
    restartSoon(s, now)
    return ok()
  }
  if (method === 'DELETE' && path.startsWith('/api/tokens/')) {
    const n = decodeURIComponent(path.slice(12))
    if (!s.tokens.some((t) => t.name === n)) return fail(404, 'no such token')
    s.tokens = s.tokens.filter((t) => t.name !== n)
    return ok()
  }
  if (method === 'DELETE' && path.startsWith('/api/pairing/denied/')) {
    const ip = decodeURIComponent(path.slice(20))
    s.denied = s.denied.filter((d) => d.ip !== ip)
    return ok()
  }
  return fail(404, 'no such endpoint')
}
