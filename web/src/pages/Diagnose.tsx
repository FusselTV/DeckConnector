import {
  Activity,
  Bug,
  Check,
  Download,
  Gauge,
  HardDrive,
  MemoryStick,
  RefreshCw,
  Route as RouteIcon,
  Timer,
  Trash2,
  TriangleAlert,
} from 'lucide-react'
import { useEffect, useState } from 'react'
import { api, coredumpRawUrl } from '../api/client'
import type { Metrics, Profile, Route } from '../api/types'
import { crashAtom, tabAtom } from '../app/state'
import { useStore } from '../app/store'
import { useUi } from '../app/ui'
import { Card } from '../components/Card'
import { Btn, Help } from '../components/controls'
import { KeyValue } from '../components/KeyValue'
import { Row } from '../components/Row'
import { ScrollArea } from '../components/ScrollArea'

const FLASH = { firmware: 'dia.fl_firmware', settings: 'dia.fl_settings' } as const
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
        {!!m?.temp?.some(Boolean) && <Spark label="Chip" data={m.temp} fmt={(v) => `${fmt.num(v)}\u00a0°C`} />}
      </div>
      {m?.flash &&
        Object.entries(m.flash).map(([k, v]) => (
          <div key={k} className="fill">
            <div className="hd">
              <span>{k in FLASH ? t(FLASH[k as keyof typeof FLASH]) : k}</span>
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

function Details() {
  const { t, fmt, act, ask } = useUi()
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
              `${t(`boot.${st.boot.reason}` as 'boot.other')}${st.boot.at ? ` · ${fmt.dt(st.boot.at * 1000, { dateStyle: 'short', timeStyle: 'medium' })}` : ''}`,
            ],
          ]}
        />
      )}
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
              <code className="wrap small codebox">{`idf.py coredump-info -c ${cdName}`}</code>
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
          <p className="small muted">{t('dia.routes_h')}</p>
          <ScrollArea className="tblw">
            <table>
              <thead>
                <tr>
                  <th>{t('th.method')}</th>
                  <th>{t('th.route')}</th>
                  <th>{t('api.right')}</th>
                </tr>
              </thead>
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
          </ScrollArea>
        </Row>
      )}
    </Card>
  )
}

export function Diagnose() {
  return (
    <div className="cards">
      <Health />
      <Details />
    </div>
  )
}
