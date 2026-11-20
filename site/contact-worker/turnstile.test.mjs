import assert from 'node:assert/strict'
import test from 'node:test'
import { human } from './turnstile.ts'

const answer = (body, status = 200) => {
  const calls = []
  globalThis.fetch = async (url, init) => (calls.push({ url, body: init.body }), Response.json(body, { status }))
  return calls
}

test('human sends secret, token and IP to siteverify and checks the hostname', async () => {
  const calls = answer({ success: true, hostname: 'deckconnector.com' })
  assert.equal(await human('s', 'tok', '1.2.3.4', 'deckconnector.com'), true)
  assert.equal(calls[0].url, 'https://challenges.cloudflare.com/turnstile/v0/siteverify')
  assert.deepEqual(Object.fromEntries(calls[0].body), { secret: 's', response: 'tok', remoteip: '1.2.3.4' })
})

test('human refuses a failed check', async () => {
  answer({ success: false, 'error-codes': ['invalid-input-response'] })
  assert.equal(await human('s', 'tok', '', 'deckconnector.com'), false)
  answer({}, 500)
  assert.equal(await human('s', 'tok', '', 'deckconnector.com'), false)
})

test('human refuses a token solved on another host', async () => {
  answer({ success: true, hostname: 'example.com' })
  assert.equal(await human('s', 'tok', '', 'deckconnector.com'), false)
})

test('human refuses a missing token without asking Cloudflare', async () => {
  const calls = answer({ success: true, hostname: 'deckconnector.com' })
  for (const t of [undefined, '', 5, 'x'.repeat(2049)])
    assert.equal(await human('s', t, '', 'deckconnector.com'), false)
  assert.equal(calls.length, 0)
})
