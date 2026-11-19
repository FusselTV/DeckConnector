import type { InputBody, Inspect, Rect, Route } from '../api/types'
import {
  closeMenu,
  dbfw,
  event,
  eventPage,
  reachable,
  openMenu,
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
  companionAt,
  restartSoon,
  startOta,
  status,
  STRIP,
  WIDTH,
  WIFI,
  type Demo,
  type Face,
  res,
  type Reply,
} from './model'

/** Turns a face into one image URL per key and one for the strip. */
export type Paint = (f: Face) => (Rect & { url: string })[]

/** A request body as the fetch shim hands it over: parsed JSON, or the size and first 144 bytes of an upload. */
export type Body = Record<string, unknown> | { blob: number; head: number[] } | undefined

const ok = (extra: object = {}): Reply => ({ status: 200, body: { ok: true, ...extra } })
const fail = (status: number, error: string, code?: string): Reply => ({ status, body: { ok: false, error, code } })

const ROUTES: [string, string, Route['access']][] = [
  ['GET', '/api', 'control'],
  ['GET', '/api/status', 'control'],
  ['GET', '/api/config', 'full'],
  ['PUT', '/api/config', 'full'],
  ['POST', '/api/config', 'open'],
  ['POST', '/api/ota', 'full'],
  ['POST', '/api/reboot', 'full'],
  ['POST', '/api/sound', 'control'],
  ['POST', '/api/sound/selftest', 'full'],
  ['DELETE', '/api/ap', 'full'],
  ['POST', '/api/reset', 'admin'],
  ['GET', '/api/log', 'full'],
  ['GET', '/api/screen', 'control'],
  ['GET', '/api/screen/*', 'control'],
  ['POST', '/api/input', 'control'],
  ['POST', '/api/menu', 'control'],
  ['DELETE', '/api/menu', 'control'],
  ['POST', '/api/screen', 'full'],
  ['POST', '/api/deck/reconnect', 'control'],
  ['POST', '/api/deck/stall', 'full'],
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
  ['DELETE', '/api/token', 'control'],
  ['GET', '/api/passkeys', 'admin'],
  ['POST', '/api/passkeys', 'admin'],
  ['PUT', '/api/passkeys/*', 'admin'],
  ['DELETE', '/api/passkeys/*', 'admin'],
  ['POST', '/api/passkeys/challenge', 'open'],
  ['POST', '/api/login/passkey', 'open'],
  ['GET', '/api/ws', 'control'],
  ['POST', '/api/login', 'open'],
  ['POST', '/api/logout', 'open'],
  ['POST', '/api/password/show', 'open'],
  ['POST', '/api/password/reset', 'open'],
  ['DELETE', '/api/password/reset', 'open'],
  ['POST', '/api/login/request', 'open'],
  ['POST', '/api/setup/claim', 'open'],
  ['GET', '/api/login/request/*', 'open'],
  ['DELETE', '/api/login/request/*', 'open'],
  ['GET', '/api/profile', 'full'],
  ['GET', '/api/metrics', 'control'],
  ['GET', '/api/frontend', 'control'],
  ['GET', '/api/companion/found', 'full'],
  ['GET', '/api/cert', 'open'],
  ['GET', '/api/ca', 'open'],
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
  ['GET', '/api/matter', 'full'],
  ['POST', '/api/matter', 'full'],
  ['DELETE', '/api/matter', 'full'],
  ['POST', '/api/matter/press', 'full'],
]

const OPEN = new Set(ROUTES.filter((r) => r[2] === 'open').map((r) => `${r[0]} ${r[1]}`))

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

const UPDATE_URL = `http://192.168.1.10/deckconnector/${latest}.dbfw`
const RELEASES = 'https://github.com/FusselTV/DeckConnector/releases'

/** The header of an uploaded file, as the bridge reads it (dbfw in model.ts). */
const file = (body: Body) => {
  const b = (body ?? { blob: 0 }) as { blob?: number; head?: number[] }
  return dbfw(b.head ?? [], typeof b.blob === 'number' ? b.blob : 0)
}

function prevLog(s: Demo) {
  if (s.prev) return s.prev.join('\n') + '\n'
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
    // Like the firmware: the password counts, and five wrong ones in a row lock the login for a while.
    if (now < (s.lockUntil ?? 0))
      return {
        status: 429,
        body: { ok: false, error: 'locked', code: 'locked', retry: Math.ceil(((s.lockUntil ?? 0) - now) / 1000) },
      }
    if (str('password') !== s.pass) {
      s.loginFails = (s.loginFails ?? 0) + 1
      if (s.loginFails >= 5) {
        s.loginFails = 0
        s.lockUntil = now + 15_000
        return { status: 429, body: { ok: false, error: 'locked', code: 'locked', retry: 15 } }
      }
      return fail(401, 'wrong password', 'wrong_password')
    }
    s.loginFails = 0
    s.authed = true
    return ok()
  }
  if (route === 'POST /api/logout') {
    s.authed = false
    return ok()
  }
  if (route === 'POST /api/password/show') {
    s.pwUntil = now + 30_000
    return res('showPassword', { ok: true }, { decks: 1 })
  }
  if (route === 'POST /api/password/reset') {
    s.resetUntil = now + 60_000
    return res('askReset', { ok: true, s: 60 })
  }
  if (route === 'DELETE /api/password/reset') {
    s.resetUntil = 0
    return ok()
  }
  if (route === 'POST /api/setup/claim') {
    if (s.cfg.setup_done) return fail(409, 'set up')
    if (s.claimed) return fail(409, 'claimed')
    s.authed = true
    return ok()
  }
  if (route === 'POST /api/login/request') {
    s.loginReq = { id: 'd3m0'.padEnd(32, '0'), at: now, state: 'pending' }
    return res('askLogin', { id: s.loginReq.id, s: 60 })
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
    return res('loginState', { state, s: left })
  }
  if (route === 'GET /api/cert' || route === 'GET /api/ca') return { status: 200, body: CERT }
  // the demo has no device to keep a passkey: the browser would make a real one for this site
  if (route === 'POST /api/login/passkey' || (method !== 'GET' && path.startsWith('/api/passkeys')))
    return fail(403, 'passkeys need HTTPS and a name, not an address', 'passkey_host')
  if (!OPEN.has(route) && !s.authed) return fail(401, 'login required')

  switch (route) {
    case 'GET /api':
      return {
        status: 200,
        body: ROUTES.map(([m, p, access]) => ({
          method: m,
          path: p,
          access,
        })),
      }
    case 'GET /api/status':
      return res('status', status(s, now), { t: 'status' })
    case 'GET /api/config':
      return res('config', config(s))
    case 'GET /api/backup':
      return {
        status: 200,
        body: {
          ...config(s),
          ...(q.get('secrets')
            ? {
                admin_pass: s.pass,
                wifi_pass: s.cfg.wifi_pass_set ? 'studio-wifi-pass' : '',
                wifi2_pass: s.cfg.wifi2_pass_set ? 'backup-wifi-pass' : '',
              }
            : {}),
        },
      }
    case 'PUT /api/config':
      return putConfig(s, now, b)
    case 'POST /api/config': // Companion's Discover: a set-up deck asks first
      return ok({ confirm: 'deck' })
    case 'GET /api/screen': {
      return res('screen', {
        writer: status(s, now).writer,
        width: WIDTH,
        height: HEIGHT,
        keys: KEYS,
        touch: [STRIP],
        strips: [STRIP],
        encoders: 4,
        menu_chord: {
          on: s.cfg.menu_chord === 'auto' ? 'encoders' : s.cfg.menu_chord,
          first: 0,
          last: s.cfg.menu_chord === 'keys' ? 7 : 3,
          hold_ms: s.cfg.menu_hold_ms,
        },
        images: paint(face(s, now)),
      })
    }
    case 'POST /api/input':
      return input(s, now, b as unknown as InputBody)
    case 'POST /api/menu':
      openMenu(s, now)
      return ok()
    case 'DELETE /api/menu':
      closeMenu(s, now)
      return ok()
    case 'POST /api/deck/reconnect':
      s.recoveries = (s.recoveries ?? 0) + 1
      return ok()
    case 'POST /api/screen':
    case 'POST /api/deck/stall':
      return ok()
    case 'DELETE /api/ap':
      return ok()
    case 'POST /api/sound': // the demo has no speaker to play it on
      return ok()
    case 'POST /api/sound/selftest':
      return fail(409, 'this board has no speaker')
    case 'POST /api/reboot':
    case 'POST /api/reset':
      restartSoon(s, now)
      return ok()
    case 'POST /api/dock':
      return dock(s, now, b)
    case 'GET /api/matter':
      if (!s.matter.running) return fail(409, 'Matter is off')
      // Matter's test code, as firmware/sim gives it
      return res('matter', {
        qr: 'MT:-24J0AFN00KA0648G00',
        code: '34970112332',
        qr_bits:
          '21:7fd33f08097615ddaea9dbf5748380e05ff50770004f2597f251cfc3958dc94b3dcd01b6dd1f290bc2e15d6ea1eb55754d8ea092f357a600',
      })
    case 'POST /api/matter':
      if (typeof b.on === 'boolean') {
        s.matter.on = b.on
        if (b.on) s.matter.running = true
      } else if (b.pair !== true && b.show !== true)
        return fail(400, 'on: true or false, pair: true or show: true expected')
      return s.matter.running || typeof b.on === 'boolean' ? ok() : fail(409, 'Matter is off')
    case 'DELETE /api/matter':
      restartSoon(s, now)
      return ok()
    case 'POST /api/matter/press':
      return s.matter.running ? ok() : fail(409, 'Matter is off')
    case 'GET /api/log':
      return res('log', q.get('boot') === 'prev' ? prevLog(s) : s.log.join('\n') + '\n')
    case 'GET /api/events':
      return res('events', eventPage(s, q))
    case 'GET /api/coredump':
      return fail(404, 'no coredump')
    case 'DELETE /api/coredump':
      return ok()
    case 'GET /api/slots':
      return res('slots', s.slots)
    case 'GET /api/boot':
      return {
        status: 200,
        body: {
          bootloader: { version: 1, idf: 'v6.0.2', built: 'Oct 21 2026', sha256: '5c1e0a7d93b4f2e6a8c0d1b7e4f39a2c' },
          ptable: {
            partitions: 9,
            md5: 'b2f4e81c06d3a7953e0c4f1a2d8b6e70',
            sha256: '0d7a4c19e3b5f8a2c6e1d9b4a7f03c58',
          },
        },
      }
    case 'POST /api/ota': {
      const h = file(body)
      if (h.error) return fail(400, h.error, h.code)
      if (h.target === 'c6') {
        event(s, now, 'update_restart', 'c6')
        return ok()
      }
      if (h.target !== 'p4') return fail(400, 'not for this device', 'ota_wrong_target')
      const n = Number(q.get('slot') ?? 0)
      const into = n || ((s.slots.find((x) => x.running)?.slot ?? 1) % s.slots.length) + 1
      const keep = q.get('boot') === '0'
      s.slots = s.slots.map((x) =>
        x.slot === into
          ? {
              ...x,
              state: 'valid',
              version: h.version,
              built: '2026-11-04',
              level: 1,
              bootable: true,
              why_not: undefined,
              next: !keep,
            }
          : { ...x, next: keep ? x.next : false },
      )
      if (!keep) restartSoon(s, now)
      return res('ota', { ok: true }, keep ? {} : { restart: true })
    }
    case 'POST /api/ota/inspect': {
      const h = file(body)
      const sha256 = '9f2c41e07b3d58a6c1e2f04d7a96b3c8e5d21f0a4b7c6e9d8f1a2b3c4d5e6f70'
      return res('inspect', { ok: !h.error, ...h, target: h.target as Inspect['target'], sha256 })
    }
    case 'POST /api/ota/url': {
      const url = str('url')
      if (!/^https?:\/\//.test(url))
        return fail(400, 'url: http(s)://… to a signed .dbfw file expected', 'url_http_only')
      // The download runs on the bridge; an address that does not answer fails there, a moment later.
      if (url !== UPDATE_URL && !url.startsWith(`${RELEASES}/download/`) && !reachable(url.replace(/^http:\/\//, ''))) {
        s.otaFail = { at: now + 2500, why: 'ESP_ERR_HTTP_CONNECT' }
        return ok()
      }
      return startOta(s, now)
    }
    case 'GET /api/update/check':
      return res(
        'updateCheck',
        s.firmware === latest
          ? { current: s.firmware, latest }
          : { current: s.firmware, latest, url: UPDATE_URL, notes: `${RELEASES}/tag/${latest}` },
      )
    case 'GET /api/frontend':
      return res(
        'frontend',
        s.pagePartition
          ? { source: 'partition', api: 2, version: s.pagePartition, api_min: 2, size: 168_440 }
          : { source: 'builtin', api: 2 },
      )
    case 'PUT /api/frontend': {
      const h = file(body)
      if (h.error) return fail(400, h.error, h.code)
      if (h.target !== 'web') return fail(400, 'not a web page file', 'ota_wrong_target')
      s.pagePartition = h.version
      return ok()
    }
    case 'DELETE /api/frontend':
      s.pagePartition = null
      return ok()
    case 'GET /api/companion/found':
      return res('found', [
        { name: companion.name, host: '192.168.1.20', port: 16622 },
        { name: 'Backup Companion', host: '192.168.1.21', port: 16622 },
      ])
    case 'GET /api/wifi/scan':
      return res('wifiScan', WIFI)
    case 'POST /api/wifi/join': {
      s.join = { ssid: str('ssid'), bssid: str('bssid'), pass: str('pass'), until: now + 3000 }
      s.joinState = 'testing'
      s.joinError = ''
      return { ...res('wifiJoin', { ok: true }), status: 202 }
    }
    case 'GET /api/metrics':
      return {
        status: 200,
        body: {
          every_s: 5,
          heap: series(now, 63, 2, 2),
          cpu: series(now, 14, 6, 1),
          psram: series(now, 8, 0.6, 3),
          temp: series(now, 47, 1.5, 4).map(Math.round),
          flash: {
            firmware: { used: 1_835_008, size: 4_194_304 },
            settings: { used: 6_784, size: 24_192 },
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
    case 'GET /api/passkeys':
      return res('passkeys', [])
    case 'GET /api/tokens':
      return res('tokens', s.tokens)
    case 'POST /api/tokens':
      return makeToken(s, str('name'), str('right') === 'full' ? 'full' : 'control')
    case 'GET /api/input/log': {
      const since = Number(q.get('since') ?? 0)
      const before = Number(q.get('before')) || Infinity
      return res('inputLog', s.inputs.filter((e) => e.seq > since && e.seq < before).slice(-400))
    }
    case 'POST /api/sources/retry': {
      const h = str('host')
      if (h !== s.cfg.satellite && !s.cfg.servers.includes(h)) return fail(404, 'no such host')
      s.since[h === s.cfg.satellite ? 'satellite' : h] = now
      return ok()
    }
    case 'POST /api/sources/probe': {
      const a = str('addr')
      if (!companionAt(a))
        return res('probe', { ok: false, error: 'connection refused', code: 'probe_refused', version: '' })
      return res('probe', { ok: true, error: '', version: companion.version })
    }
    case 'PUT /api/setup':
      s.setupStep = Math.max(0, Math.min(6, Number(b.step) || 0))
      if (s.setupStep === 5) s.setupSrcAt ??= now
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
      return res('tlsCheck', {
        subject: 'CN=studio-deck.example.org',
        issuer: 'CN=Example Studio CA',
        not_after: '2027-11-08',
        sha256: '6E:B4:09:C3:A7:58:F2:1D',
      })
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
  // the demo's visitor is logged in with the password, never with a token
  if (route === 'DELETE /api/token') return fail(400, 'only a token can end itself')
  if (method === 'DELETE' && path.startsWith('/api/tokens/')) {
    const n = decodeURIComponent(path.slice(12))
    if (!s.tokens.some((t) => t.name === n)) return fail(404, 'no such token')
    s.tokens = s.tokens.filter((t) => t.name !== n)
    return ok()
  }
  if (method === 'DELETE' && path.startsWith('/api/pairing/denied/')) {
    const ip = decodeURIComponent(path.slice(20))
    const d = s.denied.find((x) => x.ip === ip)
    s.denied = s.denied.filter((x) => x.ip !== ip)
    // A real computer keeps asking: once forgiven, it asks again a few seconds later.
    if (d && !s.later) s.later = { ip, name: d.name ?? ip, kind: 'companion', at: now + 6000 }
    return ok()
  }
  return fail(404, 'no such endpoint')
}
