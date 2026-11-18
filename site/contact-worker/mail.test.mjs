import assert from 'node:assert/strict'
import test from 'node:test'
import { check, mime } from './mail.ts'

const ok = {
  name: ' Ada\r\nBcc: x@y.z ',
  email: 'ada@example.com',
  message: 'Hallo Ä',
  t: 5000,
}

test('check takes a good form and flattens the name', () => {
  assert.deepEqual(check(ok), {
    name: 'Ada Bcc: x@y.z',
    email: 'ada@example.com',
    message: 'Hallo Ä',
    bot: false,
  })
})

test('check refuses bad input', () => {
  for (const email of [
    '',
    'ada',
    'a@b',
    'a b@c.de',
    'a@b.de\r\nBcc: x@y.z',
    'jürgen@x.de',
    'a\x00b@c.de',
    `${'a'.repeat(250)}@b.de`,
  ])
    assert.deepEqual(check({ ...ok, email }), { problem: 'email' }, email)
  assert.deepEqual(check({ ...ok, message: '  ' }), { problem: 'message' })
  assert.deepEqual(check({ ...ok, message: 'x'.repeat(5001) }), {
    problem: 'message',
  })
  assert.deepEqual(check({ ...ok, t: 2999 }), { problem: 'fast' })
  assert.deepEqual(check({ ...ok, t: '9999' }), { problem: 'fast' })
  assert.equal(check({ ...ok, website: 'x' }).bot, true)
})

test('mime has one header per line and the text in base64', () => {
  const m = mime({
    from: 'f@d.com',
    to: 't@d.com',
    replyTo: 'ada@example.com',
    name: 'Ada Ä',
    message: 'Ä'.repeat(100),
    id: '<1@d.com>',
    date: new Date(0),
  })
  const [head, body] = m.split('\r\n\r\n')
  assert.match(head, /^Subject: =\?UTF-8\?B\?[A-Za-z0-9+/=]+\?=$/m)
  assert.match(head, /^Reply-To: <ada@example.com>$/m)
  assert.ok(body.split('\r\n').every((l) => l.length <= 76))
  assert.equal(
    Buffer.from(body.replace(/\r\n/g, ''), 'base64').toString(),
    `${'Ä'.repeat(100)}\r\n\r\n--\r\nAda Ä <ada@example.com>\r\n`,
  )
})

test('a long subject folds into encoded words of at most 75 characters', () => {
  const m = mime({
    from: 'f@d.com',
    to: 't@d.com',
    replyTo: 'a@b.de',
    name: '😀'.repeat(100),
    message: 'x',
    id: '<1@d.com>',
    date: new Date(0),
  })
  const subject = m.split('\r\n\r\n')[0].match(/^Subject: (.*(?:\r\n .*)*)/m)[1]
  const words = subject.split('\r\n ')
  assert.ok(words.length > 1 && words.every((w) => w.length <= 75))
  assert.equal(
    words.map((w) => Buffer.from(w.slice(10, -2), 'base64').toString()).join(''),
    `Kontakt: ${'😀'.repeat(100)}`,
  )
})
