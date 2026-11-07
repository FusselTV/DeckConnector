import { readFileSync } from 'node:fs'
import { gzipSync } from 'node:zlib'

const BUDGET = 200 * 1024
const raw = readFileSync(new URL('../dist/index.html', import.meta.url))
const gz = gzipSync(raw, { level: 9 }).length
const kb = (n) => (n / 1024).toFixed(1) + ' KB'
console.log(`dist/index.html: ${kb(raw.length)} raw, ${kb(gz)} gzip (budget ${kb(BUDGET)})`)
if (gz > BUDGET) {
  console.error('over the gzip budget')
  process.exit(1)
}
