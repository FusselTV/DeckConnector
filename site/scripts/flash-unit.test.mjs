// node --test scripts/flash-unit.test.mjs
import assert from 'node:assert/strict'
import { createHash, randomBytes } from 'node:crypto'
import { test } from 'node:test'
import * as im from '../components/flash/improv.ts'
import { md5 } from '../components/flash/md5.ts'

const hex = (s) => Uint8Array.from(s.split(' '), (x) => parseInt(x, 16))
const enc = (s) => new TextEncoder().encode(s)
const cat = (...a) => Uint8Array.from(a.flatMap((x) => [...x]))
const result = (cmd, ...strings) => {
  const body = strings.flatMap((s) => [enc(s).length, ...enc(s)])
  return im.frame(im.RESULT, Uint8Array.from([cmd, body.length, ...body]))
}

test('frames match IMPROV.md', () => {
  assert.deepEqual(im.rpc(im.CURRENT_STATE), hex('49 4D 50 52 4F 56 01 03 02 02 00 E5 0A'))
  assert.deepEqual(new im.Reader().feed(hex('0A 49 4D 50 52 4F 56 01 01 01 02 E2 0A')), [{ type: im.STATE, data: hex('02') }])
})

test('rpc refuses what the firmware refuses', () => {
  assert.throws(() => im.rpc(im.WIFI, 'a\0b', 'x'))
  assert.throws(() => im.rpc(im.WIFI, 'x'.repeat(256)))
  assert.deepEqual(im.strings(im.rpc(im.WIFI, 'Zuhause', 'pässwort').slice(11, -2)), ['Zuhause', 'pässwort'])
})

test('packets among log lines, fed one byte at a time', () => {
  const lines = []
  const r = new im.Reader((l) => lines.push(l))
  const stream = cat(
    enc('I (312) boot: ESP-IDF v6.0\r\nI (400) web: web login: admin / Secret123\n'),
    im.frame(im.STATE, hex('02')),
    enc('W (500) wifi: half a li'),
    im.frame(im.ERROR, hex('03')),
    enc('ne\nIMPRO not a frame\n'),
    result(im.INFO, 'DeckConnector', 'v1', 'ESP32-P4', 'deck-1'),
  )
  const got = []
  for (const b of stream) got.push(...r.feed(Uint8Array.of(b)))
  assert.deepEqual(
    got.map((p) => p.type),
    [im.STATE, im.ERROR, im.RESULT],
  )
  assert.deepEqual(im.strings(got[2].data.subarray(2)), ['DeckConnector', 'v1', 'ESP32-P4', 'deck-1'])
  assert.ok(lines.includes('I (400) web: web login: admin / ******'), lines.join('|'))
  assert.ok(!lines.join('\n').includes('Secret123'))
  assert.ok(lines.includes('W (500) wifi: half a li') && lines.includes('ne'), lines.join('|'))
  assert.ok(lines.includes('IMPRO not a frame'))
})

test('a bad checksum is dropped, the next frame still parses', () => {
  const bad = im.frame(im.STATE, hex('04'))
  bad[bad.length - 2] ^= 1
  const got = new im.Reader().feed(cat(bad, im.frame(im.STATE, hex('02'))))
  assert.deepEqual(got, [{ type: im.STATE, data: hex('02') }])
})

/** An Improv client wired to a scripted bridge: `answer(cmd, data)` returns the bytes the bridge sends back. */
function rig(answer) {
  const sent = []
  const client = new im.Improv(async (b) => {
    const [p] = new im.Reader().feed(b)
    const cmd = p.data[0]
    sent.push([cmd, im.strings(p.data.subarray(2))])
    const out = answer(cmd, im.strings(p.data.subarray(2)), sent.length)
    if (out) setTimeout(() => client.feed(out), 5)
  })
  return { client, sent }
}

test('state, info, network and scan', async () => {
  const { client } = rig((cmd) => {
    if (cmd === im.CURRENT_STATE) return cat(im.frame(im.STATE, hex('04')), enc('log\n'), result(im.CURRENT_STATE, 'http://10.0.0.5'))
    if (cmd === im.INFO) return result(im.INFO, 'DeckConnector', 'v0.2.0', 'ESP32-P4', 'deck-a')
    if (cmd === im.NETWORK) return result(im.NETWORK, '7', 'http://10.0.0.5', 'http://10.0.0.9')
    if (cmd === im.SCAN)
      return cat(result(im.SCAN, 'Home', '-48', 'YES'), result(im.SCAN, 'Guest', '-70', 'NO'), result(im.SCAN, 'Home', '-80', 'YES'), result(im.SCAN))
  })
  assert.deepEqual(await client.state(), { state: 4, url: 'http://10.0.0.5' })
  assert.equal((await client.info()).name, 'deck-a')
  assert.deepEqual(await client.network(), { online: true, wlan: true, lan: true, urls: ['http://10.0.0.5', 'http://10.0.0.9'] })
  assert.deepEqual(await client.scan(), [
    { ssid: 'Home', rssi: -48, secured: true },
    { ssid: 'Guest', rssi: -70, secured: false },
  ])
})

test('errors reject with their code, a fresh command still works', async () => {
  const { client } = rig((cmd) =>
    cmd === im.WIFI ? cat(im.frame(im.STATE, hex('03')), im.frame(im.ERROR, hex('03')), im.frame(im.STATE, hex('02'))) : result(im.INFO, 'DeckConnector'),
  )
  await assert.rejects(client.wifi('Home', 'wrongpass'), (e) => e instanceof im.ImprovError && e.code === 3)
  assert.equal((await client.info()).product, 'DeckConnector')
})

test('setup answers', async () => {
  const replies = [['ok'], ['restart'], ['error', 'admin_pass: 6 to 23 characters']]
  const { client, sent } = rig((cmd, _s, n) => result(cmd, ...replies[n - 1]))
  assert.deepEqual(await client.setup('', '', true), { ok: true, restart: false })
  assert.deepEqual(await client.setup('new-pass', 'deck-b', false), { ok: true, restart: true })
  assert.deepEqual(await client.setup('abc', '', false), { ok: false, message: 'admin_pass: 6 to 23 characters' })
  assert.deepEqual(sent[0], [im.SETUP, ['', '', '1']])
  assert.deepEqual(sent[1], [im.SETUP, ['new-pass', 'deck-b', '']])
})

test('waitReady polls until the booting bridge answers', async () => {
  const { client, sent } = rig((cmd, _s, n) => (n >= 3 ? im.frame(im.STATE, hex('02')) : null))
  const t0 = Date.now()
  assert.deepEqual(await client.waitReady(5000), { state: 2, url: undefined })
  assert.equal(sent.length, 3)
  assert.ok(Date.now() - t0 >= 1900)
})

test('waitReady gives up', async () => {
  const { client } = rig(() => null)
  await assert.rejects(client.waitReady(1500), (e) => e instanceof im.ImprovError && e.code === -1)
})

test('md5 matches node', () => {
  for (const n of [0, 1, 55, 56, 63, 64, 65, 1000, 100000]) {
    const b = randomBytes(n)
    assert.equal(md5(new Uint8Array(b)), createHash('md5').update(b).digest('hex'), `length ${n}`)
  }
})

test('every fix link in the flasher has its heading on the troubleshooting page', async () => {
  const { readFileSync } = await import('node:fs')
  const src = ['Flasher.tsx', 'Setup.tsx'].map((f) => readFileSync(new URL(`../components/flash/${f}`, import.meta.url), 'utf8')).join('\n')
  const ids = ['browser', 'https', 'no-port', 'driver', 'port-busy', 'no-answer', 'wrong-chip', 'chip-revision', 'flash-stopped', 'read-back', 'firmware-download', 'setup-no-answer', 'wifi-join', 'find-board']
  for (const id of ids) assert.ok(src.includes(`'${id}'`) || src.includes(`"${id}"`) || src.includes(`#${id}\``), `the flasher links #${id}`)
  for (const page of ['flash-troubleshooting.mdx', 'flash-troubleshooting.de.mdx']) {
    const md = readFileSync(new URL(`../content/docs/${page}`, import.meta.url), 'utf8')
    for (const id of ids) assert.match(md, new RegExp(`^#+ .*\\[#${id}\\]$`, 'm'), `${page} has #${id}`)
  }
})
