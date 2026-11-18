import { mkdirSync, writeFileSync } from 'node:fs'
import type { Locator, Page } from '@playwright/test'
import { calls, de, en, expect, openTab, SHOTS, TABS, test, type Call, type Dict, type Tab } from './helpers.ts'

// Clicks every control of every tab and records what each click did. A control that does nothing a visitor can see,
// or that the mock answers with an error it would not give on a device, fails the run.

const CONTROLS =
  ':is(button,[role=switch],[role=radio],[role=button],select,input[type=checkbox],summary,a[href]):not([aria-disabled=true]):not(:disabled)'
// Restart and log out work (demo.spec covers them) but would end the page under the crawler.
const SKIP = /^(Restart|Neu starten|Log out|Abmelden)$/
// Answers a device gives too: no crash report stored.
const EXPECTED = (c: Call) => c.url === '/api/coredump' && c.status === 404
// The page polls these on its own, so they say nothing about a click.
const POLL = /^GET \/api\/(status|metrics|events|input\/log|log)(\?|$)/
const DIALOG = 'dialog[open]:visible, [role=dialog]:visible, [role=alertdialog]:visible'
// English the mock could leak into the German page (an error text, a deck label). Code, logs and addresses stay English.
const ENGLISH = /\b(the|and|no|not|is|are|to|of|with|connection|answer|waiting|failed|press(es)?)\b/i

async function english(page: Page) {
  return page.evaluate((re) => {
    const roots = [
      document.querySelector('#tabpanel > .stack:not([hidden])'),
      ...document.querySelectorAll('.toast, .banners, dialog[open], [role=dialog], [role=alertdialog]'),
    ]
    const hits: string[] = []
    for (const root of roots) {
      if (!root) continue
      const w = document.createTreeWalker(root, NodeFilter.SHOW_TEXT)
      for (let n = w.nextNode(); n; n = w.nextNode()) {
        const el = n.parentElement
        const t = n.textContent?.trim() ?? ''
        if (t && el?.checkVisibility() && !el.closest('pre,code,textarea,.mono') && new RegExp(re, 'i').test(t))
          hits.push(t.slice(0, 80))
      }
    }
    return hits
  }, ENGLISH.source)
}

interface Row {
  tab: Tab
  control: string
  effects: string[]
  errors: string[]
  english?: string[]
}

async function signature(page: Page) {
  return page.evaluate(() => {
    const p = document.querySelector('#tabpanel > .stack:not([hidden])') ?? document.body
    const states = [...document.querySelectorAll('[aria-checked],[aria-expanded],[aria-pressed],details,input,select')]
      .map((e) =>
        [
          e.getAttribute('aria-checked'),
          e.getAttribute('aria-expanded'),
          e.getAttribute('aria-pressed'),
          (e as HTMLDetailsElement).open,
          (e as HTMLInputElement).value,
        ].join(),
      )
      .join('|')
    const imgs = [...document.querySelectorAll('img')].map((i) => i.src.slice(-40)).join()
    return `${(p as HTMLElement).innerText}#${states}#${imgs}`
  })
}

const nameOf = (l: Locator) =>
  l.evaluate((e) =>
    (e.getAttribute('aria-label') || e.textContent || (e as HTMLInputElement).placeholder || e.id || e.tagName)
      .replace(/\s+/g, ' ')
      .trim()
      .slice(0, 60),
  )

async function settle(page: Page) {
  await page.waitForTimeout(250)
  await expect
    .poll(() => page.evaluate(() => (window as unknown as { __demoBusy: { n: number } }).__demoBusy.n), {
      timeout: 6000,
    })
    .toBe(0)
  await page.waitForTimeout(150)
}

async function closeOverlays(page: Page, dict: Dict) {
  for (let i = 0; i < 3; i++) {
    const d = page.locator(DIALOG)
    if (!(await d.count())) return
    await page.keyboard.press('Escape')
    await page.waitForTimeout(200)
    if (await d.count())
      await d
        .last()
        .getByRole('button', { name: new RegExp(`^(${dict['c.cancel']}|${dict['c.close']})$`) })
        .first()
        .click({ timeout: 1000 })
        .catch(() => undefined)
  }
}

async function crawl(page: Page, dict: Dict, tab: Tab, shot: string): Promise<Row[]> {
  const rows: Row[] = []
  const done = new Set<string>()
  const panel = () => page.locator('#tabpanel > .stack:not([hidden])').first()
  await openTab(page, dict, tab)
  await settle(page)
  await page.evaluate(() => {
    const w = window as unknown as { __toasts: number }
    w.__toasts = 0
    new MutationObserver(
      (m) =>
        (w.__toasts += m.reduce(
          (n, r) => n + [...r.addedNodes].filter((x) => x instanceof Element && x.matches('.toast')).length,
          0,
        )),
    ).observe(document.body, {
      subtree: true,
      childList: true,
    })
  })
  for (let i = 0; i < 200; i++) {
    const all = panel().locator(CONTROLS).filter({ visible: true })
    const n = await all.count()
    let el: Locator | null = null
    let name = ''
    for (let k = 0; k < n; k++) {
      const c = all.nth(k)
      const nm = await nameOf(c)
      const key = `${await c.evaluate((e) => e.tagName)}:${nm}`
      if (done.has(key)) continue
      done.add(key)
      if (SKIP.test(nm) || (await c.getAttribute('id')) === 'arm') continue
      el = c
      name = nm
      break
    }
    if (!el) break
    const before = await signature(page)
    const nCalls = (await calls(page)).length
    const tag = await el.evaluate((e) => e.tagName)
    const got = { download: false, picker: false, popup: false }
    const onDownload = () => (got.download = true)
    const onPicker = () => (got.picker = true)
    const onPopup = (p: Page) => ((got.popup = true), void p.close())
    page.on('download', onDownload)
    page.on('filechooser', onPicker)
    page.on('popup', onPopup)
    const toastCount = () =>
      page.evaluate(
        () => document.querySelectorAll('.toast').length + (window as unknown as { __toasts: number }).__toasts,
      )
    const toasts = await toastCount()
    const wasOn =
      (await el.getAttribute('aria-checked')) === 'true' || (await el.getAttribute('aria-pressed')) === 'true'
    const effects: string[] = []
    try {
      if (tag === 'SELECT') {
        const opts = await el.locator('option').evaluateAll((o) => o.map((x) => (x as HTMLOptionElement).value))
        await el.selectOption(opts[opts.length - 1] ?? '')
      } else if (tag === 'SUMMARY') {
        const open = () => el.evaluate((e) => (e.parentElement as HTMLDetailsElement).open)
        const was = await open()
        await el.click()
        if ((await open()) !== was) effects.push(was ? 'folds' : 'opens')
        if (was) await el.click()
      } else if (
        tag === 'A' &&
        (await el.evaluate(
          (e) => !(e as HTMLAnchorElement).target && (e as HTMLAnchorElement).origin !== location.origin,
        ))
      ) {
        // a same-tab link to another host would only show whether that host resolves in time
        effects.push(`links to ${await el.evaluate((e) => (e as HTMLAnchorElement).host)}`)
      } else await el.click({ timeout: 2000 })
    } catch {
      effects.push('not clickable')
    }
    await settle(page)
    const changed = (await signature(page)) !== before
    const fresh = (await calls(page)).slice(nCalls)
    const errors = fresh.filter((c) => c.status >= 400 && !EXPECTED(c)).map((c) => `${c.method} ${c.url} ${c.status}`)
    effects.push(
      ...fresh
        .filter((c) => c.status < 400)
        .map((c) => `${c.method} ${c.url}`)
        .filter((c) => !POLL.test(c) || name.match(/restart|Neustart/i))
        .map((c) => c.split('?')[0] ?? c),
    )
    const leaks = dict === de ? await english(page) : []
    const dlg = page.locator(DIALOG).last()
    if (await dlg.count()) {
      const title =
        (await dlg
          .locator('h1,h2,h3')
          .first()
          .textContent()
          .catch(() => null)) ?? 'dialog'
      effects.push(`dialog "${title.trim()}"`)
      if (SHOTS) await dlg.screenshot({ path: `${shot}-dlg-${rows.length}.png` }).catch(() => undefined)
      await closeOverlays(page, dict)
    }
    page.off('download', onDownload)
    page.off('filechooser', onPicker)
    page.off('popup', onPopup)
    if (got.popup) effects.push('opens a page')
    if (got.download) effects.push('download')
    if (got.picker) effects.push('file picker')
    const bar = page.locator('.savebar:visible').filter({ hasText: dict['save.discard'] })
    if (await bar.count()) {
      effects.push('save bar')
      await bar.getByRole('button', { name: dict['save.discard'] }).click()
    }
    if ((await toastCount()) > toasts)
      effects.push(`toast "${((await page.locator('.toast').allInnerTexts()).at(-1) ?? '').slice(0, 60)}"`)
    // A help tip or menu that is not a dialog closes with Escape too.
    await page.keyboard.press('Escape')
    if (!(await page.locator(`#tabpanel[aria-labelledby=tab-${tab}]`).count())) {
      effects.push('navigates')
      if (await page.locator('#tabpanel').isVisible()) await openTab(page, dict, tab)
      else break
    } else if (!effects.length && changed) effects.push('view changes')
    else if (!effects.length && wasOn && tag !== 'SUMMARY') effects.push('already selected')
    rows.push({ tab, control: name, effects, errors, ...(leaks.length ? { english: leaks } : {}) })
  }
  if (SHOTS) await page.screenshot({ path: `${shot}.png`, fullPage: true })
  return rows
}

for (const [lang, dict] of [
  ['en', en],
  ['de', de],
] as const) {
  for (const tab of TABS) {
    test(`crawl ${lang} ${tab}`, async ({ demo: page }, info) => {
      test.setTimeout(240_000)
      await page.context().grantPermissions(['clipboard-read', 'clipboard-write'])
      await page.goto(`?lang=${lang}`)
      const dir = SHOTS ?? info.outputPath()
      mkdirSync(dir, { recursive: true })
      const rows = await crawl(page, dict, tab, `${dir}/${info.project.name}-${lang}-${tab}`)
      if (SHOTS) writeFileSync(`${dir}/${info.project.name}-${lang}-${tab}.json`, JSON.stringify(rows, null, 1))
      expect(rows.length, 'controls found').toBeGreaterThan(0)
      expect(rows.filter((r) => r.errors.length)).toEqual([])
      expect(rows.filter((r) => r.english).map((r) => `${r.control}: ${r.english?.join(' | ')}`)).toEqual([])
      expect(rows.filter((r) => !r.effects.length).map((r) => r.control)).toEqual([])
    })
  }
}
