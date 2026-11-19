import {
  Activity,
  Calendar,
  ChevronDown,
  CircleCheck,
  CircleX,
  Download,
  Hand,
  KeyRound,
  LayoutGrid,
  Link2,
  ListFilter,
  MousePointer2,
  Network,
  Pause,
  Play,
  Power,
  Radio,
  Search,
  Terminal,
  TriangleAlert,
  X,
  type LucideIcon,
} from 'lucide-react'
import { Fragment, useEffect, useMemo, useRef, useState } from 'react'
import { api } from '../api/client'
import type { DeviceEvent, EventPage, InputEvent } from '../api/types'
import { followLog, onDeviceEvent } from '../app/connection'
import { deckName, decksOf } from '../app/derive'
import { logFocusAtom, tabAtom, toast } from '../app/state'
import { useStore } from '../app/store'
import { errText, useUi } from '../app/ui'
import { Card } from '../components/Card'
import { Dialog } from '../components/Dialog'
import { Btn, IconBtn, Seg } from '../components/controls'
import type { T, TKey } from '../i18n'
import { cx, download, fold, useLatest } from '../lib/util'
import { inputText } from './Overview'

type Group = 'source' | 'net' | 'pairing' | 'deck' | 'input' | 'start' | 'tech'
type Level = 'info' | 'ok' | 'warn' | 'err' | 'debug'
const GROUPS: Group[] = ['source', 'net', 'pairing', 'deck', 'input', 'start', 'tech']
const DEFAULT_ON: Group[] = ['source', 'net', 'pairing', 'deck', 'start']
const GROUP_OF: Record<string, Group> = {
  source: 'source',
  companion: 'source',
  lan: 'net',
  wlan: 'net',
  pairing: 'pairing',
  matter: 'pairing',
  deck: 'deck',
  boot: 'start',
  update: 'start',
}
const ICON: Record<Group, LucideIcon> = {
  source: Radio,
  net: Network,
  pairing: Link2,
  deck: LayoutGrid,
  input: Hand,
  start: Power,
  tech: Terminal,
}
const IN_ICON = { deck: Hand, web: MousePointer2, api: KeyRound }
const DAY = 864e5
const DM = { day: 'numeric', month: 'numeric' } as const
const PAGE = 200
const LINES = 1000
const RESTART = ['started', 'crashed']

interface Row {
  key: string
  t: number
  g: Group
  level: Level
  text: string
  detail?: string
  ev?: DeviceEvent
  input?: InputEvent
  n?: number
}

/** The event in the page's language, from its code and args; the firmware's English when the page has no words. */
function eventText(e: DeviceEvent, t: T): string {
  const a = e.args.map((x, i) => {
    if (e.code === 'host_connected' || e.code === 'host_left') return i === 0 ? t(`prog.${x}` as TKey) : x
    if (RESTART.includes(e.code)) return t(`boot.${x}` as TKey)
    return x === 'the default route' ? t('ev.default_route') : x
  })
  const key = `ev.${e.code === 'crashed' ? 'started' : e.code}` as TKey
  const s = t(key, Object.fromEntries(a.map((x, i) => [i, x])))
  return s === key ? e.text : s
}

const kindsOf = (f: { kinds: string[] } | null) => (f ? GROUPS.filter((g) => f.kinds.includes(g)) : undefined)

/** The time, a minute at most behind. */
function useNow() {
  const [now, setNow] = useState(() => Date.now())
  useEffect(() => {
    const id = setInterval(() => setNow(Date.now()), 60_000)
    return () => clearInterval(id)
  }, [])
  return now
}

const startOfDay = (ms: number) => new Date(new Date(ms).toDateString()).getTime()

/** Every event the page has loaded, newest first; older pages come on scrolling, new ones over the socket. */
function useEvents(on: boolean) {
  const { t } = useUi()
  const [list, setList] = useState<DeviceEvent[]>([])
  const [page, setPage] = useState<EventPage | null>(null)
  const [error, setError] = useState('')
  const busy = useRef(false)
  // a new start: `boot` of every event changes, and without evlog the numbers start again
  const boot = useStore((s) => s.status.boot?.at ?? 0)
  const live = useStore((s) => s.conn === 'live')
  // a fresh copy wins: `boot` counts back from the current start
  const merge = (add: DeviceEvent[], fresh = false) =>
    setList((x) =>
      [...new Map([...(fresh ? [] : x), ...add].map((e) => [e.seq, e])).values()].sort((p, q) => q.seq - p.seq),
    )
  const load = async (before?: number, fresh = false) => {
    if (busy.current) return null
    busy.current = true
    try {
      const p = await api.events(before ? { before, n: PAGE } : { n: PAGE })
      setPage(p)
      setError('')
      merge(p.events, fresh)
      return p
    } catch (e) {
      setError(errText(e, t))
      return null
    } finally {
      busy.current = false
    }
  }
  // The last 7 days for the summary, a few pages at most; the rest on scrolling.
  useEffect(() => {
    if (!on || !live) return
    const run = { off: false }
    void (async () => {
      let p = await load(undefined, true)
      for (let i = 0; i < 4 && p && !run.off; i++) {
        const last = p.events.at(-1)
        if (!last || last.seq <= p.oldest || (last.at && last.at * 1000 < Date.now() - 7 * DAY)) break
        p = await load(last.seq)
      }
    })()
    return () => {
      run.off = true
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps -- a new start or a reconnect reads again
  }, [on, live, boot])
  useEffect(
    () =>
      onDeviceEvent((e) => {
        merge([e])
        if (page && e.seq > page.newest + 1) void load() // some went missing on the way
      }),
    // eslint-disable-next-line react-hooks/exhaustive-deps -- `page` is read when an event comes
    [page],
  )
  const oldest = list.at(-1)?.seq ?? 0
  return {
    list,
    page,
    error,
    loadMore: () => {
      if (page && oldest > page.oldest) void load(oldest)
    },
  }
}

/** The raw log while Technisch is on: this boot's lines live, the boot before's from GET /api/log?prev=1. */
function useLines(on: boolean, paused: boolean) {
  const [lines, setLines] = useState<string[]>([])
  const [prev, setPrev] = useState<string[]>([])
  const pausedRef = useLatest(paused)
  useEffect(() => {
    if (!on) return
    const stop = followLog((text, replace) => {
      if (pausedRef.current) return
      const more = text.split('\n').filter(Boolean)
      setLines((x) => [...(replace ? [] : x), ...more].slice(-LINES))
    })
    void api.log(true).then(
      (x) => setPrev(x.split('\n').filter(Boolean)),
      () => undefined,
    )
    return stop
  }, [on, pausedRef])
  return { lines, prev }
}

// "I (12:34:56.789) tag: text": UTC once the clock is set, the time since the start before that
const LINE = /^([EWIDV]) \((\d+):(\d+):(\d+)\.(\d+)\)/
function lineTime(m: RegExpExecArray, lo: number, hi: number, up: number): number | null {
  const x = ((Number(m[2]) * 60 + Number(m[3])) * 60 + Number(m[4])) * 1000 + Number(m[5])
  let wall = Math.floor(hi / DAY) * DAY + x
  if (wall > hi) wall -= DAY
  return wall >= lo && wall <= hi ? wall : up ? up + x : null
}
const LINE_LEVEL: Record<string, Level> = { E: 'err', W: 'warn' }

export function Log() {
  const { t, fmt, lang, jump } = useUi()
  const visible = tabAtom.use() === 'log'
  const st = useStore((s) => s.status)
  const bootAt = useStore((s) => s.bootAt)
  const inputs = useStore((s) => s.inputs)
  const decks = decksOf(st)
  const [on, setOn] = useState<Group[]>(() => kindsOf(logFocusAtom.get()) ?? DEFAULT_ON)
  const [want, setWant] = useState<string[] | null>(() => logFocusAtom.get()?.codes ?? null)
  const now = useNow()
  const [prob, setProb] = useState<'all' | 'prob'>('all')
  const [q, setQ] = useState('')
  const [day, setDay] = useState<number | null>(null)
  const [open, setOpen] = useState<string | null>(null)
  const [sheet, setSheet] = useState(false)
  const [unfold, setUnfold] = useState(false)
  const [paused, setPaused] = useState(false)
  const [who, setWho] = useState<'all' | 'deck' | 'web'>('all')
  const [dk, setDk] = useState('all')
  const box = useRef<HTMLDivElement>(null)
  const ev = useEvents(visible)
  const tech = useLines(visible && on.includes('tech'), paused)

  const rows = useMemo(() => {
    const out: Row[] = []
    let newer = Infinity
    for (const e of ev.list) {
      let at = e.at ? e.at * 1000 : e.boot === 0 && bootAt ? bootAt + e.since_s * 1000 : newer
      at = Math.min(at, newer) // in the firmware's order, also where a time is missing or the clock jumped
      newer = at
      if (e.code === 'more') continue
      out.push({
        key: `e${e.seq}`,
        t: at,
        g: GROUP_OF[e.kind] ?? 'start',
        level: e.level,
        text: eventText(e, t),
        ev: e,
      })
    }
    inputs.forEach((x, i) => {
      const origin =
        x.origin === 'deck' ? t('tab.deck') : x.origin === 'api' ? t('in.o_api', { c: x.client ?? '' }) : t('in.o_web')
      out.push({
        key: `i${x.at}-${i}`,
        t: x.at,
        g: 'input',
        level: 'info',
        text: inputText(x, t),
        detail: x.source ? `${origin} → ${x.source}` : origin,
        input: x,
      })
    })
    const started = ev.list.find((e) => e.boot === 1 && RESTART.includes(e.code) && e.at)
    const prevAt = started?.at ? started.at * 1000 - started.since_s * 1000 : 0
    const addLines = (ls: string[], lo: number, hi: number, up: number, tag: string) => {
      let last = lo
      ls.forEach((l, i) => {
        const m = LINE.exec(l)
        last = m ? (lineTime(m, lo, hi, up) ?? hi - (ls.length - i)) : last
        out.push({ key: `${tag}${i}`, t: last, g: 'tech', level: (m && LINE_LEVEL[m[1] ?? '']) ?? 'debug', text: l })
      })
    }
    if (on.includes('tech')) {
      const boot = bootAt || now
      addLines(tech.lines, boot - 1000, now + 60_000, boot, 'l')
      addLines(tech.prev, prevAt ? prevAt - 1000 : boot - 1, boot, prevAt, 'p')
    }
    return out.sort((a, b) => b.t - a.t)
  }, [ev.list, inputs, tech.lines, tech.prev, on, bootAt, now, t])

  const counts = useMemo(() => {
    const c: Partial<Record<Group, number>> = {}
    rows.forEach((r) => (c[r.g] = (c[r.g] ?? 0) + 1))
    return c
  }, [rows])

  const onlyInput = on.length === 1 && on[0] === 'input'
  const shown = useMemo(() => {
    const fq = fold(q)
    const keep = rows.filter(
      (r) =>
        on.includes(r.g) &&
        (prob === 'all' || r.level === 'warn' || r.level === 'err') &&
        (day === null || (r.t >= day && r.t < day + DAY)) &&
        (!fq || fold(`${r.text} ${r.detail ?? ''}`).includes(fq)) &&
        (!onlyInput ||
          !r.input ||
          ((who === 'all' || (who === 'deck') === (r.input.origin === 'deck')) &&
            (dk === 'all' || r.input.deck === dk))),
    )
    // the same thing again and again ("Companion lost" 40 times) is one row with a count
    const out: Row[] = []
    for (const r of keep) {
      const last = out.at(-1)
      if (
        last &&
        !(r.ev && RESTART.includes(r.ev.code)) &&
        last.g === r.g &&
        last.text === r.text &&
        last.ev?.boot === r.ev?.boot
      )
        last.n = (last.n ?? 1) + 1
      else out.push({ ...r })
    }
    return out
  }, [rows, on, prob, day, q, onlyInput, who, dk])

  // A link from another place: its kinds, and the newest entry it means open.
  useEffect(() => {
    const take = () => {
      const f = logFocusAtom.get()
      if (!f) return
      setOn(kindsOf(f) ?? DEFAULT_ON)
      setProb('all')
      setDay(null)
      setQ('')
      setOpen(null)
      setWant(f.codes ?? null)
      logFocusAtom.set(null)
    }
    if (logFocusAtom.get()) logFocusAtom.set(null) // taken by the first render
    return logFocusAtom.subscribe(take)
  }, [])
  const target = open ?? (want ? (shown.find((r) => r.ev && want.includes(r.ev.code))?.key ?? null) : null)
  useEffect(() => {
    if (want && target) document.getElementById(`lg-${target}`)?.scrollIntoView({ block: 'center' })
  }, [want, target])

  // 7 days, 8 bins of 3 hours each, coloured by the worst level; problems counted by kind
  const today = startOfDay(now)
  const week = useMemo(() => {
    const evs = rows.filter((r) => r.ev && r.t >= today - 6 * DAY)
    const problems: Partial<Record<Group, number>> = {}
    evs.forEach((r) => {
      if (r.level === 'warn' || r.level === 'err') problems[r.g] = (problems[r.g] ?? 0) + 1
    })
    const days = Array.from({ length: 7 }, (_, i) => {
      const start = today - (6 - i) * DAY
      const bins = Array.from({ length: 8 }, (_, b) => {
        const rs = evs.filter((r) => r.t >= start + b * 108e5 && r.t < start + (b + 1) * 108e5)
        return {
          n: rs.length,
          lv: rs.some((r) => r.level === 'err') ? 'err' : rs.some((r) => r.level === 'warn') ? 'warn' : '',
        }
      })
      return { start, bins }
    })
    return { problems, days, total: Object.values(problems).reduce((a, b) => a + b, 0) }
  }, [rows, today])

  const dayName = (ms: number) => {
    const d = startOfDay(ms)
    return d === today
      ? t('log.today')
      : d === today - DAY
        ? t('log.yesterday')
        : new Date(ms).toLocaleDateString(lang, { weekday: 'long', ...DM })
  }
  const kindLabel = (g: Group) => t(`log.k_${g}`)
  const toggle = (g: Group) =>
    setOn((x) => (x.includes(g) ? x.filter((y) => y !== g) : GROUPS.filter((y) => y === g || x.includes(y))))
  const onScroll = () => {
    const el = box.current
    if (el && el.scrollHeight - el.scrollTop - el.clientHeight < 200) ev.loadMore()
  }
  const save = () => {
    const name = `${st.name ?? 'deckconnector'}-log.txt`
    const text = shown
      .map((r) => `${new Date(r.t).toISOString()} ${r.level} ${r.g} ${r.ev?.text ?? r.text}${r.n ? ` (x${r.n})` : ''}`)
      .join('\n')
    download(name, text, 'text/plain')
    toast(t('c.downloaded', { file: name }))
  }

  const sev = (l: Level) =>
    l === 'warn' ? (
      <span className="sev warn">
        <TriangleAlert size={12} aria-hidden />
        {t('log.warn')}
      </span>
    ) : l === 'err' ? (
      <span className="sev err">
        <CircleX size={12} aria-hidden />
        {t('log.err')}
      </span>
    ) : null

  const details = (r: Row) => {
    const e = r.ev
    const crash =
      e && (e.code === 'crashed' || (e.boot === 1 && st.boot?.reason === 'crash' && RESTART.includes(e.code)))
    return (
      <dl className="lg-det">
        <dt>{t('log.d_time')}</dt>
        <dd className="mono">{new Date(r.t).toLocaleString(lang, { dateStyle: 'short', timeStyle: 'medium' })}</dd>
        {e && (
          <>
            <dt>{t('meter.uptime')}</dt>
            <dd>{fmt.up(e.since_s)}</dd>
            <dt>{t('log.d_boot')}</dt>
            <dd>{e.boot ? t('log.d_boot_n', { n: e.boot }) : t('log.d_boot_0')}</dd>
            {e.boot === 0 && st.firmware && (
              <>
                <dt>{t('log.d_fw')}</dt>
                <dd className="mono">{st.firmware}</dd>
              </>
            )}
          </>
        )}
        {r.input?.deck && (
          <>
            <dt>{t('tab.deck')}</dt>
            <dd className="mono">{r.input.deck}</dd>
          </>
        )}
        {crash && (
          <>
            <dt>{t('log.d_report')}</dt>
            <dd>
              <button type="button" className="linkbtn" onClick={() => jump('diagnose', 'dia-crash')}>
                {t('tab.diagnose')} › {t('dia.crash')}
              </button>
            </dd>
          </>
        )}
      </dl>
    )
  }

  let lastDay = 0
  const list = shown.map((r) => {
    const d = startOfDay(r.t)
    const head = d !== lastDay ? <div className="lg-day">{dayName(r.t)}</div> : null
    lastDay = d
    const isOpen = target === r.key
    const click = () => {
      setWant(null)
      setOpen(isOpen ? null : r.key)
    }
    if (r.ev && RESTART.includes(r.ev.code))
      return (
        <Fragment key={r.key}>
          {head}
          <div className={cx('lg-bootdiv', isOpen && 'lg-open')} id={`lg-${r.key}`}>
            <button type="button" aria-expanded={isOpen} onClick={click}>
              <Power size={14} className={`lv-${r.level}`} aria-hidden />
              <span>
                {t('log.restart', { time: fmt.time(r.t), why: t(`boot.${r.ev.args[0] ?? 'other'}` as TKey) })}
              </span>
              {sev(r.level)}
            </button>
            {isOpen && details(r)}
          </div>
        </Fragment>
      )
    const I = r.input ? IN_ICON[r.input.origin] : ICON[r.g]
    const w = sev(r.level)
    return (
      <Fragment key={r.key}>
        {head}
        <div
          className={cx(
            'lg-row',
            r.level === 'err' && 'err',
            r.level === 'warn' && 'w',
            r.g === 'tech' && 'tech',
            isOpen && 'lg-open',
          )}
          id={`lg-${r.key}`}
        >
          <button type="button" aria-expanded={isOpen} onClick={click}>
            <time>{fmt.time(r.t)}</time>
            <I size={16} className={`ic lv-${r.level}`} aria-hidden />
            <span className="lg-t">
              {r.text}
              {r.n && <em className="lg-n">×{r.n}</em>}
              {r.g !== 'tech' && (
                <small>
                  {w}
                  {w && ' · '}
                  <span className="kd">{kindLabel(r.g)}</span>
                  {r.detail && ` · ${r.detail}`}
                </small>
              )}
            </span>
          </button>
          {isOpen && details(r)}
        </div>
      </Fragment>
    )
  })

  const problemsLine = t('log.problems', { n: week.total })
  const lastAt = ev.list.at(-1)?.at
  return (
    <div className={cx('cards lg', /[a-z]/i.test(fmt.time(0)) && 'h12')}>
      <Card
        id="lg-sum"
        live
        icon={Activity}
        className={cx(!unfold && 'fold')}
        title={
          <>
            {t('log.week')}
            <span className="sumline">· {problemsLine}</span>
          </>
        }
        right={
          <>
            <span className="small muted hide-phone">{t('log.tap')}</span>
            <span className="only-phone">
              <IconBtn
                icon={ChevronDown}
                label={t(unfold ? 'log.fold' : 'log.unfold')}
                onClick={() => setUnfold(!unfold)}
              />
            </span>
          </>
        }
      >
        <div className="lg-sum">
          {week.total ? (
            GROUPS.filter((g) => week.problems[g]).map((g) => {
              const I = ICON[g]
              return (
                <button
                  key={g}
                  type="button"
                  onClick={() => {
                    setProb('prob')
                    setOn([g])
                  }}
                >
                  <I size={14} className="lv-warn" aria-hidden />
                  <b>{week.problems[g]}</b> {kindLabel(g)}
                </button>
              )
            })
          ) : (
            <span className="lg-none">
              <CircleCheck size={14} className="lv-ok" aria-hidden />
              {t('log.no_problems')}
            </span>
          )}
        </div>
        <div className="days">
          {week.days.map(({ start, bins }) => {
            const label =
              start === today ? t('log.today') : new Date(start).toLocaleDateString(lang, { weekday: 'short' })
            return (
              <button
                key={start}
                type="button"
                aria-pressed={day === start}
                aria-label={new Date(start).toLocaleDateString(lang, { weekday: 'long', ...DM })}
                onClick={() => setDay(day === start ? null : start)}
              >
                <span className="dbar">
                  {bins.map((b, i) => (
                    <i key={i} className={b.lv} style={{ height: `${Math.min(100, b.n * 30)}%` }} />
                  ))}
                </span>
                {label}
              </button>
            )
          })}
        </div>
      </Card>
      <section className="card" id="log" aria-label={t('tab.log')}>
        <div className="body">
          <div className="lg-tools">
            <Seg
              id="lg-prob"
              label={t('log.show')}
              value={prob}
              onChange={setProb}
              options={[
                ['all', t('log.all')],
                ['prob', t('log.prob')],
              ]}
            />
            <span className="only-phone">
              <Btn sm icon={ListFilter} onClick={() => setSheet(true)}>
                {t('log.kinds', { a: on.length, b: GROUPS.length })}
              </Btn>
            </span>
            <label className="lg-search">
              <Search size={16} className="ic" aria-hidden />
              <input
                id="lg-q"
                className="in"
                type="search"
                placeholder={t('log.search')}
                aria-label={t('log.search')}
                value={q}
                onChange={(e) => setQ(e.target.value)}
              />
            </label>
            <span className="sp" />
            <div className="tbar hide-phone">
              {on.includes('tech') && (
                <span className={cx('tag', !paused && 'ok')}>{t(paused ? 'dia.paused' : 'dia.live')}</span>
              )}
              {on.includes('tech') && (
                <IconBtn
                  id="lg-follow"
                  icon={paused ? Play : Pause}
                  label={paused ? t('dia.follow') : t('dia.pause')}
                  onClick={() => setPaused(!paused)}
                />
              )}
              <IconBtn icon={Download} label={t('dia.download')} onClick={save} />
            </div>
          </div>
          <div className="chips hide-phone" role="group" aria-label={t('log.kinds_t')}>
            {GROUPS.map((g) => {
              const I = on.includes(g) ? CircleCheck : ICON[g]
              return (
                <button key={g} type="button" className="chip" aria-pressed={on.includes(g)} onClick={() => toggle(g)}>
                  <I size={14} className="ic" aria-hidden />
                  {kindLabel(g)}
                  <em>{counts[g] ?? 0}</em>
                </button>
              )
            })}
          </div>
          {(day !== null || onlyInput) && (
            <div className="lg-filters">
              {day !== null && (
                <span className="dayf">
                  <Calendar size={14} aria-hidden />
                  {t('log.only_day', {
                    day: new Date(day).toLocaleDateString(lang, { weekday: 'short', ...DM }),
                  })}
                  <IconBtn icon={X} label={t('log.day_off')} onClick={() => setDay(null)} />
                </span>
              )}
              {onlyInput && (
                <Seg
                  id="in-who"
                  label={t('in.origin')}
                  value={who}
                  onChange={setWho}
                  options={[
                    ['all', t('in.all')],
                    ['deck', t('tab.deck'), Hand],
                    ['web', t('in.web_api'), MousePointer2],
                  ]}
                />
              )}
              {onlyInput && decks.length > 1 && (
                <Seg
                  id="in-deck"
                  label={t('deck.pick')}
                  value={dk}
                  onChange={setDk}
                  options={[
                    ['all', t('in.all_decks')],
                    ...decks.map((d, i) => [d.serial, deckName(d, i, decks.length, t)] as const),
                  ]}
                />
              )}
            </div>
          )}
          {/* eslint-disable-next-line jsx-a11y/no-noninteractive-tabindex -- the list scrolls in its own box and must be reachable by keyboard */}
          <div className="lg-box" ref={box} onScroll={onScroll} tabIndex={0} aria-label={t('tab.log')}>
            {list.length ? list : <div className="empty">{ev.error || t(ev.page ? 'log.none' : 'log.loading')}</div>}
          </div>
          <div className="lg-foot">
            <span>
              {ev.page?.oldest
                ? t('log.foot', {
                    n: fmt.num(ev.list.length),
                    total: fmt.num(ev.page.total),
                    date: lastAt ? new Date(lastAt * 1000).toLocaleDateString(lang, { weekday: 'short', ...DM }) : '–',
                  })
                : ''}
            </span>
            {ev.page && ev.page.store !== 'flash' && <span>{t(`log.store_${ev.page.store}`)}</span>}
          </div>
        </div>
      </section>
      {sheet && (
        <Dialog
          className="modal sheet"
          title={t('log.kinds_t')}
          titleClass="sheett"
          close={() => setSheet(false)}
          head={(h) => (
            <div className="sb sbhead">
              <div className="row">
                {h}
                <span className="sp" />
                <Btn sm onClick={() => setOn(GROUPS)}>
                  {t('in.all')}
                </Btn>
                <IconBtn icon={X} ghost label={t('c.close')} onClick={() => setSheet(false)} />
              </div>
            </div>
          )}
        >
          <div className="sb lg-sheet">
            {GROUPS.map((g) => {
              const I = ICON[g]
              return (
                <label key={g} className="opt">
                  <input type="checkbox" checked={on.includes(g)} onChange={() => toggle(g)} />
                  <I size={18} aria-hidden />
                  {kindLabel(g)}
                  <em>{counts[g] ?? 0}</em>
                </label>
              )
            })}
          </div>
        </Dialog>
      )}
    </div>
  )
}
