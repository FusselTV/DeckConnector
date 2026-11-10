// Markdown table of a Lighthouse CI run, with the change against main when its reports are given.
// node lighthouse-report.mjs <reports dir> [<main reports dir>]
import { existsSync, readFileSync } from 'node:fs'
import { join } from 'node:path'

const load = (dir) => {
  if (!dir || !existsSync(join(dir, 'manifest.json'))) return null
  const runs = JSON.parse(readFileSync(join(dir, 'manifest.json'), 'utf8')).filter((r) => r.isRepresentativeRun)
  return new Map(
    runs.map((r) => {
      const lhr = JSON.parse(readFileSync(join(dir, r.jsonPath.split('/').pop()), 'utf8'))
      const size = (type) => lhr.audits['resource-summary'].details.items.find((i) => i.resourceType === type).transferSize
      const page = new URL(r.url).pathname
      return [
        page,
        {
          Performance: r.summary.performance * 100,
          SEO: r.summary.seo * 100,
          Accessibility: r.summary.accessibility * 100,
          'Best practices': r.summary['best-practices'] * 100,
          'LCP s': lhr.audits['largest-contentful-paint'].numericValue / 1000,
          'JS KB': size('script') / 1000,
          'Total KB': size('total') / 1000,
        },
      ]
    }),
  )
}

const [now, before] = [load(process.argv[2]), load(process.argv[3])]
if (!now) throw new Error(`no Lighthouse reports in ${process.argv[2]}`)
const cols = Object.keys(now.values().next().value)
const fmt = (v, col) => (col === 'LCP s' ? v.toFixed(1) : Math.round(v).toString())
const cell = (page, col) => {
  const v = now.get(page)[col]
  const old = before?.get(page)?.[col]
  if (old === undefined || fmt(v, col) === fmt(old, col)) return fmt(v, col)
  const d = v - old
  return `${fmt(v, col)} (${d > 0 ? '+' : '-'}${fmt(Math.abs(d), col)})`
}
console.log(`| Page | ${cols.join(' | ')} |`)
console.log(`|---|${cols.map(() => '---').join('|')}|`)
for (const page of now.keys()) console.log(`| ${page} | ${cols.map((c) => cell(page, c)).join(' | ')} |`)
console.log(before ? '\nIn brackets: the change against main.' : '\nNo Lighthouse run of main to compare with.')
