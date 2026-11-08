import { describe, expect, it } from 'vitest'
import type { Config, Source, Status } from '../api/types'
import { boot, face, fresh, input, OTA_MS, putConfig, RESTART_MS, status, tick, view, writer, type Demo } from './model'
import { handle, type Paint } from './server'

const T0 = Date.UTC(2026, 10, 8, 18, 0)
const paint: Paint = (f) => ({ keys: f.keys.map((k) => `k:${k.t}`), strip: 'strip' })

function demo(o?: { setup?: boolean; login?: boolean }) {
  const s = fresh(T0, o)
  boot(s, s.bootAt)
  return s
}
const call = (s: Demo, method: string, url: string, body?: object, now = T0) =>
  handle(s, now, method, url, body as Record<string, unknown> | undefined, paint)
const drawing = (s: Demo, now = T0) => status(s, now).sources?.find((x: Source) => x.drawing)

describe('demo bridge', () => {
  it('starts with Companion over Satellite drawing and an Elgato App dock host in the background', () => {
    const s = demo()
    expect(writer(s, T0)).toBe('satellite')
    expect(drawing(s)?.name).toBe('Studio Companion')
    expect(status(s, T0).sources?.find((x) => x.kind === 'app')).toMatchObject({ connected: true, drawing: false })
  })

  it('presses toggle keys, page over and turn dials within 0 to 100', () => {
    const s = demo()
    expect(face(s, T0).keys[1]?.t).toBe('Mic on')
    input(s, T0, { key: 1 })
    expect(face(s, T0).keys[1]?.t).toBe('Muted')
    input(s, T0, { encoder: 0, action: 'turn', ticks: 50 })
    expect(s.faces.vol[0]).toBe(100)
    input(s, T0, { key: 7 })
    expect(face(s, T0).keys[0]?.t).toBe('Key')
    expect(s.inputs.map((e) => e.kind)).toEqual(['key_down', 'key_up', 'encoder_turn', 'key_down', 'key_up'])
  })

  it('labels the host pages in German and lets a pairing request lapse after a minute', () => {
    const s = demo()
    view.lang = 'de'
    const f = face(s, T0)
    view.lang = 'en'
    expect([f.keys[1]?.t, f.keys[0]?.sub]).toEqual(['Mikro an', 'Uhr'])
    s.requested = true
    s.request = { ip: '192.168.1.52', name: 'stage-laptop', at: T0 }
    tick(s, T0 + 59_000)
    expect(s.request).not.toBeNull()
    tick(s, T0 + 61_000)
    expect(s.request).toBeNull()
  })

  it('maps a strip tap to the dial above it and refuses a touch outside the strip', () => {
    const s = demo()
    input(s, T0, { touch: 'tap', x: 450, y: 430 })
    expect(s.faces.mute[2]).toBe(false)
    expect(input(s, T0, { touch: 'tap', x: 10, y: 10 })).toMatchObject({ status: 400, body: { code: 'strip_outside' } })
  })

  it('changes a live setting at once and restarts for the others, then boots fresh', () => {
    const s = demo()
    expect(putConfig(s, T0, { brightness: 40 }).body).toMatchObject({ restart: false })
    const r = putConfig(s, T0, { name: 'stage-deck' })
    expect(r.body).toMatchObject({ restart: true, reach: ['http://192.168.1.214', 'http://stage-deck.local'] })
    expect(call(s, 'GET', '/api/status', undefined, T0)).toMatchObject({ status: 200 })
    tick(s, T0 + 1000 + RESTART_MS + 1)
    expect(s.downUntil).toBe(0)
    expect((call(s, 'GET', '/api/status', undefined, T0 + 1000 + RESTART_MS + 2).body as Status).uptime_s).toBe(0)
    expect(s.cfg.name).toBe('stage-deck')
  })

  it('refuses a bad name and keeps the old settings', () => {
    const s = demo()
    expect(putConfig(s, T0, { name: '-bad-' }).status).toBe(400)
    expect(s.cfg.name).toBe('studio-deck')
  })

  it('ends the session when the password changes and takes any password at the login', () => {
    const s = demo()
    putConfig(s, T0, { admin_pass: 'new-secret' })
    expect(call(s, 'GET', '/api/config').status).toBe(401)
    expect(call(s, 'POST', '/api/login', { password: '' }).status).toBe(401)
    expect(call(s, 'POST', '/api/login', { password: 'whatever' }).status).toBe(200)
    expect((call(s, 'GET', '/api/config').body as Config).name).toBe('studio-deck')
  })

  it('lets the deck accept a login request after a few seconds, once', () => {
    const s = demo({ login: true })
    const { id } = call(s, 'POST', '/api/login/request', { who: 'Firefox' }).body as { id: string }
    expect(call(s, 'GET', `/api/login/request/${id}`).body).toMatchObject({ state: 'pending' })
    tick(s, T0 + 5000)
    expect(call(s, 'GET', `/api/login/request/${id}`, undefined, T0 + 5000).body).toMatchObject({ state: 'accepted' })
    expect(s.authed).toBe(true)
    expect(call(s, 'GET', `/api/login/request/${id}`, undefined, T0 + 6000).body).toMatchObject({ state: 'used' })
  })

  it('has a host ask to pair, then pairs or refuses it', () => {
    const a = demo()
    tick(a, T0 + 26_000)
    expect(status(a, T0 + 26_000).pairing?.request).toBe('192.168.1.52')
    expect(call(a, 'POST', '/api/dock', { accept: true, ip: '192.168.1.99' }).status).toBe(409)
    call(a, 'POST', '/api/dock', { accept: true, ip: '192.168.1.52' })
    expect(a.docks.map((d) => d.name)).toEqual(['edit-pc', 'stage-laptop'])
    const b = demo()
    tick(b, T0 + 26_000)
    call(b, 'POST', '/api/dock', { accept: false, ip: '192.168.1.52' })
    expect(status(b, T0 + 26_000).pairing?.denied?.map((d) => d.ip)).toEqual(['192.168.1.52'])
    call(b, 'DELETE', '/api/pairing/denied/192.168.1.52')
    expect(b.denied).toEqual([])
  })

  it('shows the menu over the host and picks a host from it', () => {
    const s = demo()
    call(s, 'POST', '/api/menu')
    expect(writer(s, T0)).toBe('ui')
    expect(status(s, T0).menu_open).toBe(true)
    const app = face(s, T0).keys.findIndex((k) => k.t === 'edit-pc')
    input(s, T0, { key: app })
    expect(s.menu).toBe(false)
    expect(s.cfg).toMatchObject({ controller: 'app', host: '192.168.1.31' })
    expect(writer(s, T0)).toBe('dock1')
  })

  it('connects to example LAN hosts only, after a short wait', () => {
    const s = demo()
    putConfig(s, T0, { satellite: '10.0.0.5' })
    expect(drawing(s, T0 + 100)).toBeUndefined()
    expect(status(s, T0 + 100).sources?.find((x) => x.via === 'satellite')).toMatchObject({ connecting: true })
    expect(status(s, T0 + 2000).sources?.find((x) => x.via === 'satellite')).toMatchObject({ connected: false })
    expect(writer(s, T0 + 2000)).toBe('ui')
    putConfig(s, T0 + 2000, { satellite: '192.168.1.21' })
    expect(writer(s, T0 + 4000)).toBe('satellite')
    expect(call(s, 'POST', '/api/sources/probe', { addr: 'example.org' }).body).toMatchObject({
      ok: false,
      code: 'probe_no_connection',
    })
  })

  it('tests a Wi-Fi network and keeps it only when the password fits', () => {
    const s = demo()
    expect(call(s, 'POST', '/api/wifi/join', { ssid: 'Office', pass: 'short', bssid: '' }).status).toBe(202)
    expect(status(s, T0).network?.wlan?.join).toBe('testing')
    tick(s, T0 + 3500)
    expect(status(s, T0 + 3500).network?.wlan).toMatchObject({ join: 'failed', ssid: 'Studio' })
    call(s, 'POST', '/api/wifi/join', { ssid: 'Office', pass: 'long-enough', bssid: '' }, T0 + 4000)
    tick(s, T0 + 7500)
    expect(status(s, T0 + 7500).network?.wlan).toMatchObject({ join: 'ok', ssid: 'Office' })
  })

  it('installs an update with progress and starts the new version from the next slot', () => {
    const s = demo()
    expect((call(s, 'GET', '/api/update/check').body as { latest: string }).latest).toBe('v1.3.0')
    call(s, 'POST', '/api/ota/url', { url: 'https://updates.example.org/x.dbfw' })
    expect(tick(s, T0 + OTA_MS / 2).ota).toMatchObject({ state: 'download', pct: 50 })
    expect(writer(s, T0 + OTA_MS / 2)).toBe('ui')
    expect(tick(s, T0 + OTA_MS).ota).toMatchObject({ state: 'done', pct: 100 })
    tick(s, T0 + OTA_MS + 1000 + RESTART_MS)
    expect(s.firmware).toBe('v1.3.0')
    expect(s.slots.find((x) => x.running)?.slot).toBe(3)
    expect(call(s, 'GET', '/api/update/check', undefined, T0 + OTA_MS + 1000 + RESTART_MS).body).toEqual({
      current: 'v1.3.0',
      latest: 'v1.3.0',
    })
  })

  it('makes, lists and ends API keys', () => {
    const s = demo()
    const r = call(s, 'POST', '/api/tokens', { name: 'stream-pc', right: 'control' })
    expect((r.body as { token: string }).token).toMatch(/^dc_/)
    expect(call(s, 'POST', '/api/tokens', { name: 'bad name' }).status).toBe(400)
    call(s, 'DELETE', '/api/tokens/home-assistant')
    expect(call(s, 'GET', '/api/tokens').body).toEqual([expect.objectContaining({ name: 'stream-pc' })])
  })

  it('starts the setup in setup mode and ends it when done', () => {
    const s = demo({ setup: true })
    expect(status(s, T0).setup).toMatchObject({ active: true })
    call(s, 'PUT', '/api/setup', { step: 3 })
    expect(status(s, T0).setup?.step).toBe(3)
    putConfig(s, T0, { setup_done: true })
    call(s, 'PUT', '/api/setup', { step: 0 })
    expect(status(s, T0).setup).toMatchObject({ active: false })
  })

  it('serves a screen with one picture per key and one for the strip', () => {
    const s = demo()
    const sc = call(s, 'GET', '/api/screen').body as { images: { url: string }[]; keys: unknown[] }
    expect(sc.keys).toHaveLength(8)
    expect(sc.images).toHaveLength(9)
    expect(sc.images[1]?.url).toBe('k:Mic on')
  })
})
