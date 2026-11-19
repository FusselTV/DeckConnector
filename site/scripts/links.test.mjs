import assert from 'node:assert/strict'
import { existsSync, readFileSync, readdirSync } from 'node:fs'
import test from 'node:test'
import { site } from '../site.config.ts'

const out = new URL('../out/', import.meta.url)
const pages = readdirSync(out, { recursive: true }).filter((f) => f.endsWith('.html'))
const found = (path) =>
  [path, `${path}index.html`, `${path}.html`].some((p) => existsSync(new URL(`.${decodeURI(p)}`, out)))

test('every link, image and script on the site points to a file in out/', () => {
  const broken = []
  for (const page of pages) {
    const html = readFileSync(new URL(page, out), 'utf8')
    for (const [, url] of html.matchAll(/\s(?:href|src)="([^"]+)"/g)) {
      const path = url.replace(/&amp;/g, '&').replace(site, '').split(/[?#]/)[0]
      if (path.startsWith('/') && !path.startsWith('//') && !found(path)) broken.push(`${page}: ${url}`)
    }
  }
  assert.deepEqual(broken, [])
})

test('the language switch lands on a page: each page has its other-language twin or a redirect to it', () => {
  const rules = readFileSync(new URL('_redirects', out), 'utf8')
    .split('\n')
    .filter((l) => l.startsWith('/'))
    .map((l) => l.split(/\s+/)[0].replace('*', ''))
  const missing = pages
    .filter((p) => p.endsWith('index.html') && !/^(demo\/app|404|_)/.test(p))
    .map((p) => `/${p.replace(/index\.html$/, '')}`)
    .map((p) => (p.startsWith('/de/') ? p.slice(3) : `/de${p}`))
    .filter((p) => !found(p) && !rules.some((r) => p.startsWith(r)))
  assert.deepEqual(missing, [])
})

test('a table row sits in a tbody: the browser adds one the server did not render, and hydration fails', () => {
  const bare = pages.filter((p) => /<table[^>]*>\s*<tr/.test(readFileSync(new URL(p, out), 'utf8')))
  assert.deepEqual(bare, [])
})
