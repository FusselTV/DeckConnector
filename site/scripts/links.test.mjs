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
