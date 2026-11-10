import assert from 'node:assert/strict'
import { existsSync, readFileSync, readdirSync } from 'node:fs'
import test from 'node:test'
import { site } from '../site.config.ts'

const out = new URL('../out/', import.meta.url)
const md = readdirSync(new URL('md/', out), { recursive: true }).filter((f) => f.endsWith('.md')).map((f) => `md/${f}`)
const files = ['llms.txt', 'llms-full.txt', ...md]

test('every docs page has a Markdown copy in both languages', () => {
  const html = readdirSync(new URL('docs/', out), { recursive: true }).filter((f) => f.endsWith('index.html'))
  assert.equal(md.filter((f) => !f.startsWith('md/de/')).length, html.length)
  assert.equal(md.filter((f) => f.startsWith('md/de/')).length, html.length)
})

for (const f of files) {
  test(`${f} is clean Markdown with working links`, () => {
    const text = readFileSync(new URL(f, out), 'utf8')
    assert.equal(text.split('```').length % 2, 1, 'unbalanced code fence')
    const prose = text.replace(/```[\s\S]*?```/g, '')
    assert.doesNotMatch(prose, /^\s*<\/?[A-Z]/m, 'MDX component left over')
    assert.doesNotMatch(prose, /\]\(\//, 'relative link')
    for (const [, path] of prose.matchAll(new RegExp(`${site.replaceAll('.', '\\.')}(/[^\\s)?#]*)`, 'g'))) {
      const local = new URL(`.${path}${path.endsWith('/') ? 'index.html' : ''}`, out)
      assert.ok(existsSync(local), `${path} is not in out/`)
    }
  })
}
