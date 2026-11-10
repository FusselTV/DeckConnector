'use client'
import { useEffect, useLayoutEffect, useState } from 'react'
import { create, getByID, suggest, type AnyZBSearch } from 'zbsearch'
import type { SortedResult } from 'fumadocs-core/search'
import { experimental_useSearch as useSearch } from 'fumadocs-core/search/client'
import { staticClient, type StaticOptions } from 'fumadocs-core/search/client/orama-static'
import { SearchDialog, SearchDialogClose, SearchDialogContent, SearchDialogHeader, SearchDialogIcon, SearchDialogInput, SearchDialogList, SearchDialogOverlay, useSearch as useDialog } from 'fumadocs-ui/components/dialog/search'
import { useI18n } from 'fumadocs-ui/contexts/i18n'
import type { SharedProps } from 'fumadocs-ui/contexts/search'
import { prefix, type Lang } from '~/lib/i18n'
import { tokenizer, words } from '~/lib/search'

const popular = ['getting-started', 'flashing', 'companion', 'elgato-app', 'troubleshooting']
const dbs: Partial<Record<Lang, AnyZBSearch>> = {}

const client = (lang: Lang, search?: StaticOptions['search']) =>
  staticClient({
    from: `/api/search/${lang}`,
    initDB: () => (dbs[lang] = create({ schema: { _: 'string' }, components: { tokenizer: tokenizer[lang] } })),
    search,
  })

async function find(lang: Lang, query: string) {
  const run = async (q: string, search?: StaticOptions['search']) => client(lang, search).search(q)
  // A word the docs never use is a typo: take the closest word they do use.
  const fix = async (w: string) => {
    const db = dbs[lang]
    if (!db || (await run(w, { limit: 1 })).length) return w
    return suggest(db, { term: w, prefix: false, tolerance: w.length > 5 ? 2 : 1, limit: 1, properties: ['content'] }).suggestions[0]?.suggestion ?? w
  }
  await run('', { limit: 1 })
  const terms = await Promise.all(words(query, lang).map(fix))
  const q = terms.join(' ')
  const all = await run(q, { threshold: 0 })
  return shape(lang, rank(all.length ? all : await run(q), terms), terms)
}

// Pages whose title or headings name the query come first, then exact phrases and prose, so a long API table or code block that mentions every word does not.
// A heading that is the query itself ("Ports" for "port") beats headings that only contain it ("Port busy").
function rank(list: SortedResult[], terms: string[]) {
  const re = terms.map((t) => new RegExp(`(^|[^\\p{L}\\p{N}])${t.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')}`, 'iu'))
  const phrase = terms.join(' ')
  const label = (x: SortedResult) => (x.type === 'text' ? decodeURIComponent(x.url.split('#')[1] ?? '') : x.content)
  const named = (x: SortedResult) => re.filter((r) => r.test(label(x))).length
  const exact = (x: SortedResult) => x.type === 'heading' && [phrase, `${phrase}s`].includes(x.content.toLowerCase())
  const prose = (x: SortedResult) => !x.table && !x.content.startsWith('```')
  const groups: { page: SortedResult; hits: SortedResult[] }[] = []
  for (const x of list) {
    const last = groups.at(-1)
    if (x.type === 'page' || !last) groups.push({ page: x, hits: [] })
    else last.hits.push(x)
  }
  const score = ({ page, hits }: (typeof groups)[number]) =>
    2 * re.filter((r) => [page, ...hits].some((x) => r.test(label(x)))).length +
    (hits.some(exact) ? 3 : 0) +
    (terms.length > 1 && hits.some((x) => x.content.toLowerCase().replace(/[*`_]/g, '').includes(phrase)) ? 2 : 0) +
    (named(page) ? 1 : 0) +
    (hits.some(prose) ? 1 : 0)
  return groups
    .map((g, i) => ({ g, i, s: score(g) }))
    .sort((a, b) => b.s - a.s || b.g.hits.length - a.g.hits.length || a.i - b.i)
    .map(({ g }) => ({ page: g.page, hits: g.hits.sort((a, b) => +exact(b) - +exact(a) || named(b) - named(a) || +prose(b) - +prose(a)) }))
}

const MAX = 100

// One line of context: the matching table cell or sentence, cut at a word, never a whole paragraph.
function line(md: string, terms: string[]) {
  const has = (x: string) => terms.some((t) => x.toLowerCase().includes(t))
  let s = md.trim()
  if (s.startsWith('```')) s = s.split('\n').slice(1).find((l) => has(l) && !l.startsWith('```')) ?? ''
  else if (s.startsWith('|')) {
    const cells = (s.split('\n').at(-1) ?? '').split('|').slice(1, -1).map((c) => c.trim())
    const hit = cells.find(has) ?? cells[0] ?? ''
    s = hit === cells[0] || `${cells[0]}: ${hit}`.length > MAX ? hit : `${cells[0]}: ${hit}`
  }
  s = s.replace(/\*\*|__/g, '').replace(/\[([^\]]+)\]\([^)]*\)/g, '$1').replace(/\s+/g, ' ').trim()
  if (s.length <= MAX) return s
  const lower = s.toLowerCase()
  const at = Math.min(s.length, ...terms.map((t) => lower.indexOf(t)).filter((i) => i >= 0))
  let dot = s.lastIndexOf('. ', at)
  const stop = s.indexOf('. ', at) + 1 || s.length
  // a short sentence like "Then it is Wi-Fi." needs the one before it
  if (dot > 0 && stop - dot < 40) dot = s.lastIndexOf('. ', dot - 1)
  s = s.slice(dot < 0 ? 0 : dot + 2, stop)
  if (s.length <= MAX) return s
  const i = Math.max(0, s.toLowerCase().search(new RegExp(terms.map((t) => t.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')).join('|'))))
  const from = Math.max(0, Math.min(i - 40, s.length - MAX))
  const start = from ? s.indexOf(' ', from) + 1 : 0
  const cut = s.slice(start, start + MAX)
  const end = start + MAX < s.length
  return `${start ? '…' : ''}${end ? cut.slice(0, cut.lastIndexOf(' ')) : cut}${end ? '…' : ''}`
}

type Outline = Map<string, { heading?: string; text?: string }>
const outlines = new Map<string, Outline>()

// Every heading and the first line under it, read from the loaded index: a hit only carries its own text.
function outline(db: AnyZBSearch, page: string) {
  let o = outlines.get(page)
  if (o) return o
  o = new Map()
  for (let i = 0; ; i++) {
    const d = getByID(db, `${page}-${i}`) as { type: string; url: string; content: string } | undefined
    if (!d) break
    const e = o.get(d.url) ?? {}
    if (d.type === 'heading') e.heading ??= d.content
    else e.text ??= d.content
    o.set(d.url, e)
  }
  outlines.set(page, o)
  return o
}

// Each page shows its title, then at most two sections as heading + one line.
function shape(lang: Lang, groups: ReturnType<typeof rank>, terms: string[]): SortedResult[] {
  const db = dbs[lang]
  return groups.flatMap(({ page, hits }) => {
    const o = db ? outline(db, page.id) : new Map() as Outline
    const seen = new Set<string>()
    const rows: SortedResult[] = []
    // the description repeats under the title; it only counts when nothing else on the page matched
    for (const h of hits.filter((x, _, all) => x.url.includes('#') || all.every((y) => !y.url.includes('#')))) {
      if (seen.has(h.url) || rows.length === 2) continue
      seen.add(h.url)
      const e = o.get(h.url)
      const text = h.type === 'heading' ? e?.text : h.content
      const heading = h.type === 'heading' ? h.content : e?.heading
      rows.push({ id: h.id, url: h.url, type: 'text', content: text ? line(text, terms) : '', breadcrumbs: heading ? [heading] : undefined })
    }
    return [page, ...rows.filter((r) => r.content || r.breadcrumbs)]
  })
}

export default function Search(props: SharedProps) {
  const lang = useI18n().locale as Lang
  const [run] = useState(() => (q: string) => find(lang, q))
  const search = useSearch(run)
  const [start, setStart] = useState<SortedResult[] | null>(null)
  useEffect(() => {
    if (!props.open || start) return
    void (async () => {
      await client(lang).search('')
      const db = dbs[lang]
      if (!db) return
      setStart(
        popular.flatMap((slug) => {
          const url = `${prefix(lang)}/docs/${slug}`
          const page = getByID(db, url) as { content: string } | undefined
          return page ? [{ id: url, type: 'page' as const, content: page.content, url }] : []
        }),
      )
    })()
  }, [props.open, start, lang])
  return (
    <SearchDialog {...search} {...props}>
      <SearchDialogOverlay />
      <SearchDialogContent>
        <SearchDialogHeader>
          <SearchDialogIcon />
          {/* Without these, password managers take the bare text field in a dialog for a login field. */}
          <SearchDialogInput type="search" name="docs-query" autoComplete="off" autoCorrect="off" spellCheck={false} enterKeyHint="search" data-1p-ignore="" data-lpignore="true" data-bwignore="" data-form-type="other" />
          <SearchDialogClose />
        </SearchDialogHeader>
        <SearchDialogList defaultItems={start} />
        <BestHit />
      </SearchDialogContent>
    </SearchDialog>
  )
}

// The list makes its first row active, which is the page. Enter should open the best matching section of it.
function BestHit() {
  const { data, setActive } = useDialog()
  useLayoutEffect(() => {
    const next = data?.items[1]
    if (data?.items[0]?.type !== 'page' || !next || next.type === 'page') return
    setActive(next.type === 'table' ? (next.rows[0]?.id ?? null) : next.id)
  }, [data, setActive])
  return null
}
