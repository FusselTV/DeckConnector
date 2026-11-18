// Writes public/og.jpg and og-de.jpg from the landing hero. Run against a served build: node scripts/og-image.mjs http://localhost:4600/
const { chromium } = await import(new URL('../../web/node_modules/playwright/index.mjs', import.meta.url).href)

const b = await chromium.launch()
const p = await b.newPage({ viewport: { width: 1200, height: 630 }, colorScheme: 'dark', reducedMotion: 'reduce' })
const base = process.argv[2] ?? 'http://localhost:4600/'
for (const [path, file] of [
  ['', 'og.jpg'],
  ['de/', 'og-de.jpg'],
]) {
  await p.goto(base + path, { waitUntil: 'networkidle' })
  await p.addStyleTag({
    content:
      'body > header > div > :not(:first-child), main section:first-of-type .flex.flex-wrap { visibility: hidden !important }',
  })
  await p.waitForTimeout(1500)
  await p.screenshot({ path: new URL(`../public/${file}`, import.meta.url).pathname, type: 'jpeg', quality: 85 })
}
await b.close()
