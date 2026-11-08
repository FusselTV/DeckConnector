import {
  Activity,
  Bug,
  Check,
  Copy,
  Cpu,
  Download,
  Eraser,
  EthernetPort,
  Gauge,
  Hand,
  HardDrive,
  History,
  Key,
  Link2,
  MemoryStick,
  MousePointer2,
  Pause,
  Play,
  Power,
  Radio,
  RefreshCw,
  Route as RouteIcon,
  ScrollText,
  Timer,
  Trash2,
  TriangleAlert,
  Wifi,
} from 'lucide-react'
import { memo, useEffect, useMemo, useRef, useState } from 'react'
import { api, coredumpRawUrl } from '../api/client'
import type { DeviceEvent, InputEvent, Metrics, Profile, Route } from '../api/types'
import { followLog } from '../app/connection'
import { decksOf } from '../app/derive'
import { crashAtom, tabAtom, toast } from '../app/state'
import { useStore } from '../app/store'
import { useUi } from '../app/ui'
import { Card } from '../components/Card'
import { Btn, Help, IconBtn, Seg } from '../components/controls'
import { KeyValue } from '../components/KeyValue'
import { Row } from '../components/Row'
import { fold } from '../lib/util'
import { copyText, cx, download, useLatest } from '../lib/util'
import { inputText } from './Overview'

const LOG_LINES = 1000
const NONE: string[] = []

function Spark({ label, data, fmt }: { label: string; data: number[]; fmt: (n: number) => string }) {
  const n = data.length
  if (n < 2)
    return (
      <div className="spark">
        <div className="top2">
          <span className="lbl">{label}</span>
          <span className="val">–</span>
        </div>
      </div>
    )
  const pts = data.map((v, i) => [(i / (n - 1)) * 120, 32 - (v / 100) * 30] as const)
  const d = `M${pts.map((p) => `${p[0].toFixed(1)} ${p[1].toFixed(1)}`).join('L')}`
  const last = data[n - 1] ?? 0
  const end = pts[n - 1] ?? [0, 0]
  return (
    <div className="spark">
      <div className="top2">
        <span className="lbl">{label}</span>
        <span className="val">{fmt(last)}</span>
      </div>
      <svg viewBox="0 0 120 34" preserveAspectRatio="none" role="img" aria-label={`${label} ${fmt(last)}`}>
        <line
          x1="0"
          x2="120"
          y1="17"
          y2="17"
          stroke="var(--line)"
          strokeDasharray="2 3"
          vectorEffect="non-scaling-stroke"
        />
        <path d={`${d}L120 34L0 34Z`} fill="var(--amber-soft)" />
        <path d={d} fill="none" stroke="var(--amber)" strokeWidth="1.6" vectorEffect="non-scaling-stroke" />
        <circle cx={end[0] - 1.5} cy={end[1]} r="2" fill="var(--amber)" />
      </svg>
    </div>
  )
}

/** The log in a bounded box; the newest LOG_LINES lines are kept. */
const LogCard = memo(function LogCard() {
  const { t } = useUi()
  const conn = useStore((s) => s.conn)
  const name = useStore((s) => s.status.name ?? 'deckconnector')
  // `gone` counts lines dropped from the front, so each line keeps its key while the list scrolls.
  const [{ lines, gone }, setLog] = useState({ lines: [] as string[], gone: 0 })
  const [follow, setFollow] = useState(true)
  const [q, setQ] = useState('')
  const [which, setWhich] = useState<'cur' | 'prev'>('cur')
  const [prev, setPrev] = useState<string[] | null | undefined>(undefined)
  const ref = useRef<HTMLPreElement>(null)
  const followRef = useLatest(follow)
  useEffect(() => {
    let off = false
    const add = (text: string, replace = false) => {
      if (!followRef.current) return
      const el = ref.current
      const atBottom = !el || el.scrollHeight - el.scrollTop - el.clientHeight < 24
      const more = text.split('\n').filter(Boolean)
      setLog((x) => {
        const all = [...(replace ? [] : x.lines), ...more]
        const cut = Math.max(0, all.length - LOG_LINES)
        return { lines: all.slice(cut), gone: x.gone + cut + (replace ? x.lines.length : 0) }
      })
      if (atBottom) requestAnimationFrame(() => el && (el.scrollTop = el.scrollHeight))
    }
    // The first ws log message carries everything still kept, so GET /api/log is only the fallback.
    const stop = followLog(add)
    const tm = setTimeout(() => {
      if (!off && !ref.current?.textContent)
        void api.log().then(
          (x) => !off && add(x),
          () => undefined,
        )
    }, 1500)
    return () => {
      off = true
      clearTimeout(tm)
      stop()
    }
  }, [followRef])
  const showPrev = (v: 'cur' | 'prev') => {
    setWhich(v)
    if (v === 'prev' && prev === undefined)
      api.log(true).then(
        (x) => setPrev(x.split('\n').filter(Boolean)),
        () => setPrev(null),
      )
  }
  const src = which === 'prev' ? (prev ?? NONE) : lines
  const base = which === 'prev' ? 0 : gone
  const text = src.join('\n')
  const shown = useMemo(() => {
    const all = src.map((l, i) => [base + i, l] as const)
    const fq = fold(q)
    return q ? all.filter(([, l]) => fold(l).includes(fq)) : all
  }, [src, base, q])
  const live = follow && conn === 'live'
  return (
    <Card
      id="log"
      live
      icon={ScrollText}
      title={t('dia.log')}
      right={
        <>
          <span className={cx('tag', live && 'ok')}>
            {live ? <Activity size={12} aria-hidden /> : <Pause size={12} aria-hidden />}
            {t(live ? 'dia.live' : 'dia.paused')}
          </span>
          <div className="tbar">
            <IconBtn
              id="log-follow"
              icon={follow ? Pause : Play}
              label={follow ? t('dia.pause') : t('dia.follow')}
              onClick={() => setFollow(!follow)}
            />
            <IconBtn
              icon={Copy}
              label={t('c.copy')}
              onClick={() => void copyText(text, ref.current).then((ok) => toast(t(ok ? 'c.copied' : 'c.copy_fail')))}
            />
            <IconBtn
              icon={Download}
              label={t('dia.download')}
              onClick={() => {
                download(`${name}.log`, text, 'text/plain')
                toast(t('c.downloaded', { file: `${name}.log` }))
              }}
            />
            <IconBtn
              icon={Eraser}
              label={t('dia.clear')}
              onClick={() => setLog((x) => ({ lines: [], gone: x.gone + x.lines.length }))}
            />
          </div>
        </>
      }
    >
      <div className="row">
        <Seg
          id="logsrc"
          label={t('dia.log')}
          value={which}
          onChange={showPrev}
          options={[
            ['cur', t('dia.cur')],
            ['prev', t('dia.prev')],
          ]}
        />
        <span className="sp" />
        <input
          id="logq"
          className="in sm"
          type="search"
          placeholder={t('dia.filter')}
          aria-label={t('dia.filter')}
          value={q}
          onChange={(e) => setQ(e.target.value)}
        />
      </div>
      {which === 'prev' && prev === null ? (
        <div className="empty">{t('dia.prev_none')}</div>
      ) : (
        // eslint-disable-next-line jsx-a11y/no-noninteractive-tabindex -- the log scrolls inside its box and must be reachable by keyboard
        <pre ref={ref} className="log" tabIndex={0} aria-label={t('dia.log')}>
          {shown.map(([k, l]) => (
            <div key={k} className={l[0]}>
              {l}
            </div>
          ))}
        </pre>
      )}
    </Card>
  )
})

function Health() {
  const { t, fmt } = useUi()
  const visible = tabAtom.use() === 'diagnose'
  const c = useStore((s) => s.status.counters)
  const [m, setM] = useState<Metrics | null>(null)
  const every = m?.every_s ?? 5
  useEffect(() => {
    if (!visible) return
    let off = false
    const load = () =>
      api.metrics().then(
        (x) => !off && setM(x),
        () => undefined,
      )
    void load()
    const id = setInterval(() => !document.hidden && void load(), every * 1000)
    return () => {
      off = true
      clearInterval(id)
    }
  }, [visible, every])
  return (
    <Card
      id="card-health"
      live
      icon={Activity}
      title={t('dia.health')}
      right={<Help right label={t('dia.health')} text={t('dia.every', { s: m?.every_s ?? 5 })} />}
    >
      <div className="sparks">
        <Spark label="CPU" data={m?.cpu ?? []} fmt={fmt.pct} />
        <Spark label="RAM" data={m?.heap ?? []} fmt={fmt.pct} />
        <Spark label="PSRAM" data={m?.psram ?? []} fmt={fmt.pct} />
      </div>
      {m?.flash &&
        Object.entries(m.flash).map(([k, v]) => (
          <div key={k} className="fill">
            <div className="hd">
              <span>{k}</span>
              <span className="mono">
                {fmt.bytes(v.used)} / {fmt.bytes(v.size)} · {fmt.pct(Math.round((v.used / v.size) * 100))}
              </span>
            </div>
            <div className="bar" style={{ flex: 'none' }}>
              <i style={{ width: `${(v.used / v.size) * 100}%` }} />
            </div>
          </div>
        ))}
      {c && (
        <>
          <div className="lbl">{t('dia.counters')}</div>
          <div className="cgrid">
            {(
              ['images', 'superseded', 'rejected', 'usb_errors', 'usb_recoveries', 'inputs', 'inputs_dropped'] as const
            ).map((k) => (
              <div key={k} className={k.startsWith('usb_') && (c[k] ?? 0) > 0 ? 'warn' : undefined}>
                <b>{fmt.num(c[k] ?? 0)}</b>
                <span>{t(`cnt.${k}`)}</span>
              </div>
            ))}
          </div>
        </>
      )}
    </Card>
  )
}

const IN_ICON = { deck: Hand, web: MousePointer2, api: Key }

function InputLog() {
  const { t, fmt } = useUi()
  const inputs = useStore((s) => s.inputs)
  const nDecks = useStore((s) => decksOf(s.status).length)
  const [who, setWho] = useState<'all' | 'deck' | 'web'>('all')
  const [dk, setDk] = useState('all')
  const [snap, setSnap] = useState<InputEvent[] | null>(null)
  const src = snap ?? inputs
  const rows = useMemo(
    () =>
      src.filter(
        (x) => (who === 'all' || (who === 'deck') === (x.origin === 'deck')) && (dk === 'all' || String(x.deck) === dk),
      ),
    [src, who, dk],
  )
  const origin = (x: InputEvent) =>
    x.origin === 'deck' ? t('tab.deck') : x.origin === 'api' ? t('in.o_api', { c: x.client ?? '' }) : t('in.o_web')
  return (
    <Row
      group="dia"
      id="dia-input"
      icon={Hand}
      title={t('in.title')}
      status={<span className={cx('tag', !snap && 'ok')}>{t(snap ? 'dia.paused' : 'dia.live')}</span>}
    >
      <div className="row">
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
        {nDecks > 1 && (
          <Seg
            id="in-deck"
            label={t('deck.pick')}
            value={dk}
            onChange={setDk}
            options={[
              ['all', t('in.all_decks')],
              ...Array.from({ length: nDecks }, (_, i) => [String(i + 1), t('deck.n', { n: i + 1 })] as const),
            ]}
          />
        )}
        <span className="sp" />
        <IconBtn
          id="in-live"
          icon={snap ? Play : Pause}
          label={snap ? t('dia.follow') : t('dia.pause')}
          onClick={() => setSnap(snap ? null : inputs)}
        />
      </div>
      <div className="tblw">
        <table className="intbl">
          <thead>
            <tr>
              <th>{t('in.time')}</th>
              <th>{t('in.origin')}</th>
              {nDecks > 1 && <th>{t('tab.deck')}</th>}
              <th>{t('in.kind')}</th>
              <th>{t('in.src')}</th>
            </tr>
          </thead>
          <tbody>
            {rows.length ? (
              rows.map((x, i) => {
                const I = IN_ICON[x.origin]
                return (
                  <tr key={`${x.at}-${i}`}>
                    <td className="mono">{fmt.clock(x.at)}</td>
                    <td>
                      <span className={`ino ${x.origin}`} title={origin(x)}>
                        <I size={14} aria-hidden />
                        <span>{origin(x)}</span>
                      </span>
                    </td>
                    {nDecks > 1 && <td className="mono">{x.deck}</td>}
                    <td className="mono">{inputText(x, t)}</td>
                    <td className="small">{x.source ?? ''}</td>
                  </tr>
                )
              })
            ) : (
              <tr>
                <td colSpan={5} className="muted">
                  {t('in.none')}
                </td>
              </tr>
            )}
          </tbody>
        </table>
      </div>
      <p className="small muted">{t('in.hint')}</p>
    </Row>
  )
}

const EV_ICON: Record<string, typeof Power> = {
  boot: Power,
  source: Radio,
  wlan: Wifi,
  lan: EthernetPort,
  pairing: Link2,
  update: Cpu,
}

/** GET /api/events: what happened on the device since the start, newest first. */
function Events() {
  const { t, fmt } = useUi()
  const bootAt = useStore((s) => s.bootAt)
  const visible = tabAtom.use() === 'diagnose'
  const [list, setList] = useState<DeviceEvent[] | null>(null)
  useEffect(() => {
    if (!visible) return
    let off = false
    const load = () =>
      api.events().then(
        (x) => !off && setList(x),
        () => undefined,
      )
    void load()
    const id = setInterval(() => !document.hidden && void load(), 10_000)
    return () => {
      off = true
      clearInterval(id)
    }
  }, [visible])
  const rows = [...(list ?? [])].reverse()
  return (
    <Row group="dia" id="dia-events" icon={History} title={t('ev.title')}>
      <div className="tblw">
        <table className="intbl">
          <thead>
            <tr>
              <th>{t('in.time')}</th>
              <th>{t('ev.what')}</th>
            </tr>
          </thead>
          <tbody>
            {rows.length ? (
              rows.map((e) => {
                const I = EV_ICON[e.kind] ?? Activity
                return (
                  <tr key={`${e.since_s}-${e.kind}-${e.text}`}>
                    <td className="mono">{fmt.clock(e.at ? e.at * 1000 : bootAt + e.since_s * 1000)}</td>
                    <td>
                      <span className="ino">
                        <I size={14} aria-hidden />
                        <span>{e.text}</span>
                      </span>
                    </td>
                  </tr>
                )
              })
            ) : (
              <tr>
                <td colSpan={2} className="muted">
                  {t('ev.none')}
                </td>
              </tr>
            )}
          </tbody>
        </table>
      </div>
    </Row>
  )
}

function Details() {
  const { t, fmt, act, ask, lang } = useUi()
  const st = useStore((s) => s.status)
  const [routes, setRoutes] = useState<Route[]>([])
  useEffect(() => void api.routes().then(setRoutes, () => undefined), [])
  const crash = crashAtom.use()
  const [prof, setProf] = useState<Profile | null>(null)
  const loadProf = () => void api.profile().then(setProf, () => setProf(null))
  const clearCd = async () => {
    if (!(await ask({ title: t('cf.cd'), body: t('cf.cd_b'), ok: t('btn.delete'), danger: true }))) return
    await act(async () => {
      await api.clearCoredump()
      crashAtom.set(null)
    })
  }
  const boot = Object.entries(st.boot_ms ?? {}).sort((a, b) => a[1] - b[1])
  const bootMax = Math.max(1, ...boot.map((b) => b[1]))
  const mem = st.memory ?? {}
  const cdName = crash
    ? `${st.name ?? 'deckconnector'}-${st.firmware ?? ''}-${(crash.elf_sha256 ?? '').slice(0, 8)}-coredump.elf`
    : ''
  return (
    <Card id="card-dev" live icon={HardDrive} title={t('dia.details')}>
      {st.boot?.reason && (
        <KeyValue
          rows={[
            [
              t('dia.boot'),
              `${t(`boot.${st.boot.reason}` as 'boot.other')}${st.boot.at ? ` · ${new Date(st.boot.at * 1000).toLocaleString(lang)}` : ''}`,
            ],
          ]}
        />
      )}
      <InputLog />
      <Events />
      <Row
        group="dia"
        id="dia-crash"
        icon={Bug}
        title={t('dia.crash')}
        status={
          crash ? (
            <span className="tag warn">
              <TriangleAlert size={12} aria-hidden />
              {crash.task ?? fmt.bytes(crash.size)}
            </span>
          ) : (
            <span className="tag ok">{t('meter.none')}</span>
          )
        }
      >
        {crash ? (
          <>
            <KeyValue
              rows={[
                [
                  t('dia.cause'),
                  crash.mcause === undefined
                    ? t('dia.no_sum')
                    : crash.mcause === 7
                      ? t('dia.cause_7')
                      : `mcause ${crash.mcause}`,
                ],
                [t('dia.when'), crash.at],
                [
                  t('dia.regs'),
                  crash.pc
                    ? `pc ${crash.pc} · ra ${crash.ra ?? ''} · sp ${crash.sp ?? ''} · mtval ${crash.mtval ?? ''} · ${fmt.bytes(crash.size)}`
                    : undefined,
                  true,
                ],
              ]}
            />
            <div className="field">
              <span className="flabel">{t('dia.cd_cmd')}</span>
              <code className="wrap small codebox">idf.py coredump-info -c {cdName}</code>
            </div>
            <div className="row">
              <a className="btn sm" href={coredumpRawUrl} download={cdName}>
                <Download size={15} aria-hidden />
                {t('dia.cd_dl')}
              </a>
              <Btn sm kind="danger" icon={Trash2} onClick={() => void clearCd()}>
                {t('dia.cd_clear')}
              </Btn>
            </div>
          </>
        ) : (
          <div className="empty">
            <Check size={18} aria-hidden />
            {t('dia.no_crash')}
          </div>
        )}
      </Row>
      <Row group="dia" id="dia-profile" icon={Gauge} title={t('dia.profile')}>
        <div className="row">
          <span className="small muted">{t('dia.since')}</span>
          <span className="sp" />
          <Btn sm icon={RefreshCw} onClick={loadProf}>
            {t('dia.measure')}
          </Btn>
        </div>
        {prof?.routes && (
          <div className="tblw">
            <table>
              <thead>
                <tr>
                  <th>{t('th.route')}</th>
                  <th className="num">n</th>
                  <th className="num">avg µs</th>
                  <th className="num">max µs</th>
                </tr>
              </thead>
              <tbody>
                {prof.routes.map((r) => (
                  <tr key={r.method + r.uri}>
                    <td className="mono">
                      {r.method} {r.uri}
                    </td>
                    <td className="num">{r.count}</td>
                    <td className="num">{fmt.num(r.avg_us)}</td>
                    <td className="num">{fmt.num(r.max_us)}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        )}
        {prof && (
          <div className="tblw">
            <table>
              <thead>
                <tr>
                  <th>{t('th.task')}</th>
                  <th className="num">{t('th.cpu')}</th>
                  <th className="num">{t('th.stack')}</th>
                </tr>
              </thead>
              <tbody>
                {Object.keys({ ...prof.cpu_permille, ...prof.stack_free }).map((k) => (
                  <tr key={k}>
                    <td className="mono">{k}</td>
                    <td className="num">{prof.cpu_permille?.[k] ?? '–'}</td>
                    <td className="num">{prof.stack_free?.[k] ?? '–'}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        )}
      </Row>
      {boot.length > 0 && (
        <Row group="dia" id="dia-boot" icon={Timer} title={t('dia.boot_ms')}>
          <div className="bootl">
            {boot.map(([k, v]) => (
              <div key={k} className="r">
                <span className="mono">{k}</span>
                <span className="t">
                  <i style={{ width: `${(v / bootMax) * 100}%` }} />
                </span>
                <span>{fmt.num(v)} ms</span>
              </div>
            ))}
          </div>
        </Row>
      )}
      {Object.keys(mem).length > 0 && (
        <Row group="dia" id="dia-mem" icon={MemoryStick} title={t('dia.memory')}>
          <KeyValue
            rows={[
              ...(['heap_free', 'heap_min_free', 'heap_largest', 'psram_free', 'psram_largest'] as const).map(
                (k) =>
                  [t(`mem.${k}`), mem[k] !== undefined ? fmt.bytes(mem[k]) : undefined, true] as [
                    string,
                    string | undefined,
                    boolean,
                  ],
              ),
              [t('mem.tasks'), mem.tasks, true],
              [
                t('mem.sockets'),
                mem.sockets !== undefined ? `${mem.sockets} / ${mem.sockets_max ?? '–'}` : undefined,
                true,
              ],
            ]}
          />
        </Row>
      )}
      {routes.length > 0 && (
        <Row group="dia" id="dia-routes" icon={RouteIcon} title={t('dia.routes')}>
          <div className="tblw">
            <table>
              <tbody>
                {routes.map((r) => (
                  <tr key={r.method + r.path}>
                    <td className="mono">{r.method}</td>
                    <td className="mono">{r.path}</td>
                    <td>
                      <span className="tag">{r.access}</span>
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        </Row>
      )}
    </Card>
  )
}

export function Diagnose() {
  return (
    <div className="cards">
      <LogCard />
      <Health />
      <Details />
    </div>
  )
}
