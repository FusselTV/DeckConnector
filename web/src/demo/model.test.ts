import { beforeAll, describe, expect, it } from 'vitest'
import type { Config, EventPage, Source, Status } from '../api/types'
import { boot, face, fresh, input, OTA_MS, putConfig, RESTART_MS, status, tick, view, writer, type Demo } from './model'
import { deckUiReady } from './deck-ui'
import { handle, type Paint } from './server'

const T0 = Date.UTC(2026, 10, 8, 18, 0)
const paint: Paint = (f) => [{ x: 0, y: 0, w: 1, h: 1, url: 'ui' in f ? 'ui' : f.keys.map((k) => k.t).join() }]
beforeAll(() => deckUiReady)

function demo(o?: { setup?: boolean; login?: boolean }) {
  const s = fresh(T0, o)
  boot(s, s.bootAt)
  return s
}
const call = (s: Demo, method: string, url: string, body?: object, now = T0) =>
  handle(s, now, method, url, body as Record<string, unknown> | undefined, paint)
/** What the host draws: the keys and strip the demo paints itself. */
function host(s: Demo, now = T0) {
  const f = face(s, now)
  if ('ui' in f) throw new Error('the bridge draws')
  return f
}
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
    expect(host(s).keys.map((k) => k.t)).toEqual(['CAM 1', 'CAM 2', 'Slides', 'Pause', 'ON', 'Title', 'REC', 'Lights'])
    input(s, T0, { key: 4 })
    expect(host(s).keys[4]?.t).toBe('OFF')
    input(s, T0, { encoder: 0, action: 'turn', ticks: 50 })
    expect(s.faces.vol[0]).toBe(100)
    input(s, T0, { key: 7 })
    expect(host(s).keys[0]?.t).toBe('Front')
    expect(s.inputs.map((e) => e.kind)).toEqual(['key_down', 'key_up', 'encoder_turn', 'key_down', 'key_up'])
    input(s, T0, { key: 5 })
    expect(
      host(s)
        .keys.slice(0, 5)
        .map((k) => k.bg),
    ).toEqual(Array(5).fill('#2b2d31'))
  })

  it('mutes the Elgato App mic from its key and from the dial alike', () => {
    const s = demo()
    s.cfg = { ...s.cfg, controller: 'app', host: '192.168.1.31' }
    expect(writer(s, T0)).toBe('dock1')
    input(s, T0, { key: 3 })
    expect([host(s).keys[3]?.t, host(s).strip.zones[3]?.text]).toEqual(['Muted', 'muted'])
    input(s, T0, { encoder: 3 })
    expect(host(s).keys[3]?.t).toBe('Mic')
  })

  it('labels the host pages in German and lets a pairing request lapse after a minute', () => {
    const s = demo()
    view.lang = 'de'
    const f = host(s)
    view.lang = 'en'
    expect([f.keys[2]?.t, f.keys[4]?.t]).toEqual(['Folien', 'AN'])
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
    expect(s.faces.mute[2]).toBe(true)
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

  it('keeps the events through a restart, marked as an earlier boot, and pages them newest first', () => {
    const s = demo()
    const before = (call(s, 'GET', '/api/events').body as EventPage).events.length
    boot(s, T0 + 5000)
    const page = call(s, 'GET', '/api/events', undefined, T0 + 5000).body as EventPage
    expect(page.events.filter((e) => e.boot === 1).length).toBe(before)
    expect(page.events.find((e) => e.kind === 'boot')).toMatchObject({ boot: 0, code: 'started', level: 'info' })
    const seqs = page.events.map((e) => e.seq)
    expect(seqs).toEqual([...seqs].sort((a, b) => b - a))
    const older = call(s, 'GET', `/api/events?before=${seqs[0]}&n=1`).body as EventPage
    expect(older.events.map((e) => e.seq)).toEqual([seqs[1]])
  })

  it('refuses a bad name and keeps the old settings', () => {
    const s = demo()
    expect(putConfig(s, T0, { name: '-bad-' }).status).toBe(400)
    expect(s.cfg.name).toBe('studio-deck')
  })

  it('keeps the browser that changed the password logged in, then wants the new one and locks after 5 wrong', () => {
    const s = demo()
    putConfig(s, T0, { admin_pass: 'new-secret' })
    expect(call(s, 'GET', '/api/config').status).toBe(200)
    call(s, 'POST', '/api/logout')
    expect(call(s, 'GET', '/api/config').status).toBe(401)
    expect(call(s, 'POST', '/api/login', { password: 'studio-demo' }).status).toBe(401)
    expect(call(s, 'POST', '/api/login', { password: 'new-secret' }).status).toBe(200)
    call(s, 'POST', '/api/logout')
    for (let i = 0; i < 4; i++) call(s, 'POST', '/api/login', { password: 'wrong' })
    expect(call(s, 'POST', '/api/login', { password: 'wrong' }).status).toBe(429)
    expect(call(s, 'POST', '/api/login', { password: 'new-secret' }).status).toBe(429)
    expect(call(s, 'POST', '/api/login', { password: 'new-secret' }, T0 + 16_000).status).toBe(200)
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

  it('has a forgotten computer ask to pair again, then pairs or refuses it', () => {
    const a = demo()
    tick(a, T0 + 25_000)
    expect(status(a, T0 + 25_000).pairing?.request).toBeFalsy() // nothing asks by itself
    call(a, 'POST', '/api/dock', { forget: '192.168.1.31' })
    tick(a, T0 + 6000)
    expect(status(a, T0 + 6000).pairing?.request).toBe('192.168.1.31')
    expect(call(a, 'POST', '/api/dock', { accept: true, ip: '192.168.1.99' }).status).toBe(409)
    call(a, 'POST', '/api/dock', { accept: true, ip: '192.168.1.31' })
    expect(a.docks.map((d) => d.name)).toEqual(['edit-pc'])
    const b = demo()
    call(b, 'POST', '/api/dock', { forget: '192.168.1.31' })
    tick(b, T0 + 6000)
    call(b, 'POST', '/api/dock', { accept: false, ip: '192.168.1.31' })
    expect(status(b, T0 + 6000).pairing?.denied?.map((d) => d.ip)).toEqual(['192.168.1.31'])
    call(b, 'DELETE', '/api/pairing/denied/192.168.1.31')
    expect(b.denied).toEqual([])
    tick(b, T0 + 13_000)
    expect(b.request?.name).toBe('edit-pc') // and asks again
  })

  it("shows the firmware's menu over the host and switches the deck's language from it", () => {
    const s = demo()
    const before = writer(s, T0)
    call(s, 'POST', '/api/menu')
    expect(writer(s, T0)).toBe('ui')
    expect(status(s, T0).menu_open).toBe(true)
    expect(face(s, T0)).toHaveProperty('ui')
    input(s, T0, { key: 0 }) // top left: the other language; the hosts stay on the home screen
    expect(s.cfg.lang).toBe('en')
    input(s, T0, { key: 3 })
    expect(status(s, T0).menu_open).toBe(false)
    expect(writer(s, T0)).toBe(before)
  })

  it('acts on menu input as the firmware does from the web UI', () => {
    const s = demo()
    const open = () => call(s, 'POST', '/api/menu')
    const menuOpen = () => status(s, T0).menu_open
    open()
    const main = face(s, T0)
    input(s, T0, { encoder: 0, action: 'turn', ticks: 2 })
    expect(s.cfg.brightness).toBe(90)
    input(s, T0, { key: 7 })
    input(s, T0, { key: 5 })
    expect(menuOpen()).toBe(true) // Neustart and Setup-WLAN take only presses on the deck itself
    expect(s.downUntil).toBe(0)
    input(s, T0, { key: 4 })
    expect(face(s, T0)).not.toEqual(main) // the LAN editor
    input(s, T0, { key: 4 })
    input(s, T0, { key: 3 })
    expect(menuOpen()).toBe(false)
    open()
    input(s, T0, { touch: 'tap', x: 700, y: 430 })
    expect(menuOpen()).toBe(false)
    open()
    tick(s, T0 + 5 * 60_000)
    expect(menuOpen()).toBe(false) // an idle menu closes
  })

  it('shows the password on the deck until a key is pressed', () => {
    const s = demo()
    call(s, 'POST', '/api/password/show')
    expect(writer(s, T0)).toBe('ui')
    input(s, T0, { key: 0 })
    expect(s.pwUntil).toBe(0)
    expect(writer(s, T0)).toBe('satellite')
  })

  it('reads a firmware file the way the bridge does: header, target and version', () => {
    const s = demo()
    const head = (target: number, version: string) => [
      ...[68, 66, 70, 87, 1, target, 0, 0, 0, 16, 0, 0],
      ...Array.from({ length: 132 }, (_, i) => version.charCodeAt(i) || 0),
    ]
    const inspect = (h: number[], blob: number) => call(s, 'POST', '/api/ota/inspect', { blob, head: h }).body
    expect(
      inspect(
        Array.from({ length: 144 }, () => 0),
        4240,
      ),
    ).toMatchObject({ ok: false, code: 'ota_not_dbfw' })
    expect(inspect(head(1, 'v1.0.1'), 4240)).toMatchObject({ ok: true, target: 'p4', version: 'v1.0.1' })
    expect(inspect(head(6, 'v0.5.0'), 4240)).toMatchObject({ ok: false, code: 'ota_wrong_target' })
    expect(call(s, 'PUT', '/api/frontend', { blob: 4240, head: head(3, 'web demo') }).status).toBe(200)
    expect(call(s, 'GET', '/api/frontend').body).toMatchObject({ source: 'partition', version: 'web demo' })
  })

  it('pairs a computer without a question while pairing is open, and asks otherwise', () => {
    const s = demo()
    call(s, 'POST', '/api/dock', { pairing: 300 })
    tick(s, T0 + 7000)
    expect(s.docks.map((d) => d.name)).toContain('studio-mac')
    expect(s.request).toBeNull()
  })

  it('confirms a factory reset on the deck after a few seconds and starts over in setup', () => {
    const s = demo({ login: true })
    call(s, 'POST', '/api/password/reset')
    tick(s, T0 + 6000)
    tick(s, T0 + 6000 + 1000 + RESTART_MS)
    expect(s.cfg.setup_done).toBe(false)
    expect(s.pass).not.toBe('studio-demo')
    expect(call(s, 'POST', '/api/login', { password: s.pass }, T0 + 20_000).status).toBe(200)
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
      code: 'probe_refused',
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
    expect((call(s, 'GET', '/api/update/check').body as { latest: string }).latest).toBe('v1.0.1')
    const { url } = call(s, 'GET', '/api/update/check').body as { url: string }
    call(s, 'POST', '/api/ota/url', { url })
    expect(tick(s, T0 + OTA_MS / 2).ota).toMatchObject({ state: 'download', pct: 50 })
    expect(writer(s, T0 + OTA_MS / 2)).toBe('ui')
    expect(tick(s, T0 + OTA_MS).ota).toMatchObject({ state: 'done', pct: 100 })
    tick(s, T0 + OTA_MS + 1000 + RESTART_MS)
    expect(s.firmware).toBe('v1.0.1')
    expect(s.slots.find((x) => x.running)?.slot).toBe(3)
    expect(call(s, 'GET', '/api/update/check', undefined, T0 + OTA_MS + 1000 + RESTART_MS).body).toEqual({
      current: 'v1.0.1',
      latest: 'v1.0.1',
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

  it("serves the host's picture, or the bridge's own screen while it covers the deck", () => {
    const s = demo()
    const screen = () => call(s, 'GET', '/api/screen').body as { images: { url: string }[]; keys: unknown[] }
    expect(screen().keys).toHaveLength(8)
    expect(screen().images[0]?.url).toContain('CAM 2')
    call(s, 'POST', '/api/menu')
    expect(screen().images[0]?.url).toBe('ui')
  })

  it('finds Companion only where it runs, and an Elgato app pairs by itself in the setup source step', () => {
    const probe = (addr: string) => call(demo(), 'POST', '/api/sources/probe', { addr, pin: '' }).body
    expect(probe('192.168.1.20')).toMatchObject({ ok: true })
    expect(probe('192.168.1.99')).toMatchObject({ ok: false, code: 'probe_refused' })
    const s = demo({ setup: true })
    call(s, 'PUT', '/api/setup', { step: 5 })
    tick(s, T0 + 6000)
    expect(drawing(s, T0 + 6000)).toMatchObject({ kind: 'app', name: 'edit-pc' })
  })
})
