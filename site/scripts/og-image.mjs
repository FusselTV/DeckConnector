// Writes public/og.png from the landing hero. Run against a served build: node scripts/og-image.mjs http://localhost:4600/
const { chromium } = await import(new URL('../../web/node_modules/playwright/index.mjs', import.meta.url).href)

const b = await chromium.launch()
const p = await b.newPage({ viewport: { width: 1200, height: 630 }, colorScheme: 'dark', reducedMotion: 'reduce' })
await p.goto(process.argv[2] ?? 'http://localhost:4600/', { waitUntil: 'networkidle' })
await p.addStyleTag({ content: 'header, main section:first-of-type .flex.flex-wrap { display: none !important }' })
await p.waitForTimeout(1500)
await p.screenshot({ path: new URL('../public/og.png', import.meta.url).pathname })
await b.close()
