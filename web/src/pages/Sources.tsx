import {
  Cable,
  Check,
  RefreshCw,
  X,
  Crown,
  Link2,
  LoaderCircle,
  Monitor,
  Pencil,
  Plus,
  Radio,
  Server,
  Timer,
  Trash2,
  TriangleAlert,
} from 'lucide-react'
import { useEffect, useRef, useState } from 'react'
import { api } from '../api/client'
import type { Found, Source } from '../api/types'
import { pendingAtom, putConfig, useActions } from '../app/actions'
import { refreshStatus } from '../app/connection'
import { compDock, hk, hostName, KIND_ICON, stateKey } from '../app/derive'
import { useIssues } from '../app/issues'
import { toast } from '../app/state'
import { useRoute, useStore } from '../app/store'
import { useUi } from '../app/ui'
import { Card } from '../components/Card'
import { Btn, describedBy, Empty, Field, Help, IconBtn, Seg, SIcon, Switch, type Tone } from '../components/controls'
import { Modal } from '../components/Dialog'
import { CopyField } from '../components/InputAction'
import { Notice } from '../components/Notice'
import { mmss } from '../lib/format'
import { companionError, normPin, pinError, wsUrlError } from '../lib/validate'

type SrcType = 'comp' | 'app' | 'srv'
interface SrcRow extends Source {
  key: string
}
const typeOf = (s: Source): SrcType =>
  s.kind === 'server' ? 'srv' : s.kind === 'companion' && s.via !== 'dock' ? 'comp' : 'app'
const stTone = (s: Source): Tone =>
  s.connecting ? 'warn' : s.drawing ? 'acc' : s.chosen && !s.connected ? 'bad' : s.connected ? 'ok' : ''

/** Sources plus paired computers that are not connected right now, so they can be forgotten. */
function useRows(): SrcRow[] {
  const st = useStore((s) => s.status)
  const rows: SrcRow[] = (st.sources ?? []).map((s) => ({ ...s, key: hk(s) }))
  for (const h of st.pairing?.hosts ?? [])
    if (!rows.some((r) => r.host === h.ip))
      rows.push({
        kind: h.kind ?? 'unknown',
        host: h.ip,
        name: h.name,
        via: 'dock',
        connected: !!h.connected,
        chosen: false,
        drawing: false,
        key: `p|${h.ip}`,
      })
  return rows
}

function PinField({
  id,
  value,
  set,
  err,
}: {
  id: string
  value: string
  set: (v: string) => void
  err?: string | null
}) {
  const { t } = useUi()
  return (
    <Field id={id} label={t('src.pin')} hint={t('src.pin_hint')} err={err} help={t('src.pin_help')}>
      <input
        {...describedBy(id, err, true)}
        className="in mono"
        value={value}
        onChange={(e) => set(e.target.value)}
        spellCheck={false}
        autoComplete="off"
        placeholder="AB:CD:…"
      />
    </Field>
  )
}

/** Companions on the network. The first call starts a 2 s search; the second one has the answers. */
export function useFound(on = true) {
  const [found, setFound] = useState<Found[] | null>(null)
  useEffect(() => {
    if (!on) return
    let off = false
    void api
      .found()
      .then(() => new Promise((r) => setTimeout(r, 2200)))
      .then(() => api.found())
      .then(
        (f) => !off && setFound(Array.isArray(f) ? f : []),
        () => !off && setFound([]),
      )
    return () => {
      off = true
    }
  }, [on])
  return found
}

function CompanionForm({ close }: { close: () => void }) {
  const { t, act } = useUi()
  const cfg = useStore((s) => s.config)
  const found = useFound()
  const [sat, setSat] = useState(cfg?.satellite ?? '')
  const [pin, setPin] = useState(cfg?.satellite_pin ?? '')
  const [err, setErr] = useState<{ sat?: string; pin?: string }>({})
  // Cancel or Escape while the address is tested abandons the save.
  const gone = useRef(false)
  useEffect(
    () => () => {
      gone.current = true
    },
    [],
  )
  const canProbe = useRoute('POST', '/api/sources/probe')
  const [busy, setBusy] = useState(false)
  const [failed, setFailed] = useState<string | null>(null)
  const save = async (addr: string, p: string) => {
    const e1 = companionError(addr)
    const e2 = addr.startsWith('wss://') ? pinError(p) : null
    if (e1 || e2 || !addr) {
      setErr({ sat: e1 ? t(e1) : !addr ? t('src.addr_hint') : undefined, pin: e2 ? t(e2) : undefined })
      document.getElementById(e2 && !e1 ? 'm-satpin' : 'm-sat')?.focus()
      return
    }
    // A failed test is shown once; saving the same address again keeps it anyway (Companion may start later).
    if (canProbe && failed !== addr) {
      setBusy(true)
      const r = await api
        .probe(addr, normPin(p))
        .catch((x: unknown) => ({ ok: false, error: x instanceof Error ? x.message : '' }))
      if (gone.current) return
      setBusy(false)
      if (!r.ok) {
        setFailed(addr)
        setErr({ sat: `${t('wiz.no_answer', { a: addr })}${r.error ? ` (${r.error})` : ''}` })
        return
      }
    }
    const prev = { satellite: cfg?.satellite ?? '', satellite_pin: cfg?.satellite_pin ?? '' }
    const ok = await act(async () => {
      await putConfig({ satellite: addr, satellite_pin: addr.startsWith('wss://') ? normPin(p) : '' })
      toast(t('c.saved'), { undo: () => void act(() => putConfig(prev)) })
      return true
    })
    if (ok) close()
  }
  return (
    <>
      <p className="small muted">{t('src.comp_what')}</p>
      <div className="lbl">{t('src.found')}</div>
      {found === null ? (
        <div className="empty">
          <LoaderCircle size={18} className="spin" aria-hidden />
          {t('src.searching')}
        </div>
      ) : found.length === 0 ? (
        <div className="empty">{t('src.none_found')}</div>
      ) : (
        <div className="list">
          {found.map((x) => {
            const v = x.port === 16622 ? x.host : `${x.host}:${x.port}`
            const cur = cfg?.satellite === v
            return (
              <div key={`${x.host}:${x.port}`} className="li">
                <SIcon icon={Radio} tone={cur ? 'ok' : ''} />
                <div className="grow">
                  <b>{x.name}</b> <span className="mono muted">{`${x.host}:${x.port}`}</span>
                </div>
                {cur ? (
                  <span className="tag ok">{t('src.in_use')}</span>
                ) : (
                  <button
                    type="button"
                    className="btn sm"
                    aria-label={t('src.connect_to', { name: x.name })}
                    onClick={() => void save(v, '')}
                  >
                    {t('src.connect')}
                  </button>
                )}
              </div>
            )
          })}
        </div>
      )}
      <Field
        id="m-sat"
        label={t('src.addr')}
        hint={t('src.addr_hint')}
        help={[t('src.t1'), t('src.t2'), t('src.t3'), t('src.t5')].join(' ')}
        err={err.sat}
      >
        <input
          {...describedBy('m-sat', err.sat, true)}
          className="in mono"
          value={sat}
          onChange={(e) => {
            setSat(e.target.value)
            setErr({})
          }}
          autoComplete="off"
          spellCheck={false}
        />
      </Field>
      {sat.startsWith('wss://') && (
        <PinField id="m-satpin" value={pin} set={(v) => (setPin(v), setErr({}))} err={err.pin} />
      )}
      <div className="mfoot inline">
        <Btn kind="ghost" onClick={close}>
          {t('c.cancel')}
        </Btn>
        <Btn kind="pri" disabled={busy} onClick={() => void save(sat.trim(), pin)}>
          {busy ? t('st.connecting') : t('c.save')}
        </Btn>
      </div>
    </>
  )
}

function AppForm({ close }: { close: () => void }) {
  const { t, fmt, act } = useUi()
  const bootAt = useStore((s) => s.bootAt)
  const P = useStore((s) => s.status.pairing)
  const ip = useStore((s) => s.status.network?.lan?.ip || s.status.network?.wlan?.ip || location.hostname)
  const a = usePairing()
  const canUndeny = useRoute('DELETE', '/api/pairing/denied/x')
  return (
    <>
      <p className="small muted">{t('src.app_what')}</p>
      <ol className="steps-ol">
        {t('src.app_steps')
          .split('|')
          .map((s, i) => (
            <li key={i}>
              {s}
              {i === 3 && <CopyField value={ip} />}
            </li>
          ))}
      </ol>
      {(P?.auto_open ?? !P?.hosts?.length) ? (
        <Notice n={{ id: 'auto', icon: Link2, lvl: 'info', title: t('pair.auto_t'), text: t('pair.auto') }} />
      ) : (
        <div className="row">
          {P.open ? (
            <>
              <span className="tag ok">
                <Timer size={14} aria-hidden />
                <span className="mono">{t('src.open_now', { t: mmss(P.open_s ?? 0) })}</span>
              </span>
              <Btn sm onClick={() => void act(() => api.dock({ pairing: 0 }).then(refreshStatus))}>
                {t('src.close')}
              </Btn>
            </>
          ) : (
            <Btn
              id="pair-open"
              icon={Link2}
              onClick={() => void act(() => api.dock({ pairing: 300 }).then(refreshStatus))}
            >
              {t('src.open')}
            </Btn>
          )}
        </div>
      )}
      {P?.denied?.map((d) => (
        <Notice
          key={d.ip}
          n={{
            id: `denied-${d.ip}`,
            icon: X,
            lvl: 'warn',
            title: `${t('wiz.denied')}: ${d.name || d.ip}`,
            text:
              d.since_s !== undefined && bootAt
                ? `${t('wiz.denied_at', { time: fmt.time(bootAt + d.since_s * 1000) })} ${t('wiz.denied_h')}`
                : t('wiz.denied_h'),
            acts: canUndeny
              ? [
                  {
                    label: t('wiz.ask_again'),
                    icon: RefreshCw,
                    run: () => act(() => api.undeny(d.ip).then(refreshStatus)),
                  },
                ]
              : [],
          }}
        />
      ))}
      <Switch
        id="only"
        on={!!P?.only_paired}
        onChange={(v) => void a.onlyPaired(v)}
        label={t('src.only')}
        hint={t('src.only_h')}
      />
      <div className="mfoot inline">
        <span className="sp" />
        <Btn kind="pri" onClick={close}>
          {t('c.done')}
        </Btn>
      </div>
    </>
  )
}

function ServerForm({ src, close }: { src?: Source; close: () => void }) {
  const { t, act } = useUi()
  const cfg = useStore((s) => s.config)
  const servers = cfg?.servers ?? ['', '', '', '']
  const pins = cfg?.server_pins ?? ['', '', '', '']
  const old = src?.kind === 'server' ? servers.indexOf(src.host) : -1
  const [url, setUrl] = useState(src?.kind === 'server' ? src.host : '')
  const [pin, setPin] = useState(old >= 0 ? (pins[old] ?? '') : '')
  const [err, setErr] = useState<string | null>(null)
  const full = servers.filter(Boolean).length >= 4 && old < 0
  const save = async () => {
    const e = wsUrlError(url) ?? (url.startsWith('wss://') ? pinError(pin) : null)
    if (e) {
      setErr(t(e))
      document.getElementById('m-url')?.focus()
      return
    }
    const s = [...servers]
    const p = [...pins]
    const i = old >= 0 ? old : s.findIndex((x) => !x)
    s[i] = url.trim()
    p[i] = url.startsWith('wss://') ? normPin(pin) : ''
    const prev = { servers, server_pins: pins }
    const ok = await act(async () => {
      await putConfig({ servers: s, server_pins: p })
      toast(t('c.saved'), { undo: () => void act(() => putConfig(prev)) })
      return true
    })
    if (ok) close()
  }
  return (
    <>
      <p className="small muted">{t('src.srv_what')}</p>
      {full ? (
        <div className="warnline">
          <TriangleAlert size={16} aria-hidden />
          {t('src.srv_full')}
        </div>
      ) : (
        <>
          <Field id="m-url" label={t('src.srv_url')} err={err}>
            <input
              {...describedBy('m-url', err)}
              className="in mono"
              placeholder="ws://host:port/deckconnector"
              value={url}
              onChange={(e) => (setUrl(e.target.value), setErr(null))}
              spellCheck={false}
            />
          </Field>
          {url.startsWith('wss://') && <PinField id="m-spin" value={pin} set={setPin} />}
        </>
      )}
      <div className="mfoot inline">
        <Btn kind="ghost" onClick={close}>
          {t('c.cancel')}
        </Btn>
        <Btn kind="pri" disabled={full} onClick={() => void save()}>
          {src ? t('c.save') : t('c.add')}
        </Btn>
      </div>
    </>
  )
}

/** Add or edit a source; commits with its own button, never the save bar (rule 15). */
export function SourceModal({ type: type0, src, close }: { type?: SrcType; src?: Source; close: () => void }) {
  const { t } = useUi()
  const [type, setType] = useState<SrcType>(type0 ?? 'comp')
  return (
    <Modal icon={src ? Pencil : Plus} title={src ? t('src.edit', { name: hostName(src) }) : t('src.add')} close={close}>
      {!src && (
        <Seg
          id="srctype"
          label={t('src.type')}
          value={type}
          onChange={setType}
          options={[
            ['comp', t('prog.companion'), Radio],
            ['app', t('prog.app'), Monitor],
            ['srv', t('prog.server'), Server],
          ]}
        />
      )}
      {type === 'comp' ? (
        <CompanionForm close={close} />
      ) : type === 'app' ? (
        <AppForm close={close} />
      ) : (
        <ServerForm src={src} close={close} />
      )}
    </Modal>
  )
}

function usePairing() {
  const { t, act, ask } = useUi()
  return {
    onlyPaired: async (v: boolean) => {
      const st = (await import('../app/store')).getState().status
      const stray = (st.sources ?? []).filter(
        (s) => s.kind === 'app' && s.connected && !st.pairing?.hosts?.some((h) => h.ip === s.host),
      )
      if (
        v &&
        stray.length &&
        !(await ask({
          title: t('cf.only'),
          body: t('cf.only_b', { list: stray.map(hostName).join(', ') }),
          ok: t('btn.on'),
        }))
      )
        return
      await act(() => putConfig({ dock_paired_only: v }))
    },
  }
}

export function Sources() {
  const { t, act, ask } = useUi()
  const a = useActions()
  const rows = useRows()
  const st = useStore((s) => s.status)
  const cfg = useStore((s) => s.config)
  const pending = pendingAtom.use()
  const pair = useIssues().list.find((i) => i.id === 'pair')
  const [modal, setModal] = useState<{ type?: SrcType; src?: Source } | null>(null)
  const cd = compDock(st, cfg?.satellite)
  const remove = async (s: Source) => {
    const ty = typeOf(s)
    if (ty === 'comp') {
      if (!(await ask({ title: t('cf.disc'), body: t('cf.disc_b'), ok: t('src.remove'), danger: true }))) return
      await act(async () => {
        await putConfig({ satellite: '', satellite_pin: '' })
        toast(t('src.removed'))
      })
    } else if (ty === 'srv' && cfg) {
      const prev = { servers: cfg.servers, server_pins: cfg.server_pins }
      const i = cfg.servers.indexOf(s.host)
      await act(async () => {
        await putConfig({
          servers: cfg.servers.map((x, j) => (j === i ? '' : x)),
          server_pins: cfg.server_pins.map((x, j) => (j === i ? '' : x)),
        })
        toast(t('src.srv_removed'), { undo: () => void act(() => putConfig(prev)) })
      })
    } else
      await act(async () => {
        await api.dock({ forget: s.host })
        toast(t('src.forgot', { name: hostName(s) }))
        await refreshStatus()
      })
  }
  const addBtn = (
    <Btn id="src-add" icon={Plus} onClick={() => setModal({ type: 'comp' })}>
      {t('src.add')}
    </Btn>
  )
  return (
    <>
      {pair && <Notice n={pair} />}
      <Card
        id="card-src"
        icon={Cable}
        title={t('src.table')}
        right={
          <>
            {rows.length > 0 && addBtn}
            <Help right label={t('src.table')} text={t('src.intro')} />
          </>
        }
      >
        {rows.length === 0 ? (
          <Empty icon={Cable}>
            <p>{t('src.empty')}</p>
            {addBtn}
          </Empty>
        ) : (
          <div className="srctbl" role="table" aria-label={t('src.table')}>
            <div className="srow head" role="row">
              <span role="columnheader">{t('src.col_type')}</span>
              <span role="columnheader">{t('src.col_host')}</span>
              <span role="columnheader">{t('src.col_conn')}</span>
              <span role="columnheader">{t('src.col_state')}</span>
              <span role="columnheader" className="sr-acts">
                <span className="sr">{t('th.action')}</span>
              </span>
            </div>
            {rows.map((s) => {
              const unk = s.kind === 'unknown'
              const bad = cd.some((x) => x.host === s.host)
              const canCtl = !unk && s.connected && !s.chosen && (s.kind !== 'companion' || s.via === 'satellite')
              return (
                <div key={s.key} className={s.chosen ? 'srow chosen' : 'srow'} role="row">
                  <span role="cell" className="sr-type">
                    <SIcon icon={KIND_ICON[s.kind]} tone={bad ? 'warn' : stTone(s)} />
                    <span>{t(`prog.${s.kind}`)}</span>
                  </span>
                  <span role="cell" className="sr-host">
                    <b className={s.name ? '' : 'mono'}>{hostName(s)}</b>
                    {s.name && <span className="mono small muted">{s.host}</span>}
                  </span>
                  <span role="cell" className="sr-conn">
                    <span className="small">
                      {s.via === 'satellite' ? 'Satellite' : s.kind === 'server' ? 'WebSocket' : 'Dock'}
                    </span>
                    {s.error && <span className="warnline small">{s.error}</span>}
                    {bad && (
                      <span className="warnline small">
                        <TriangleAlert size={13} aria-hidden />
                        {t('is.compdock_h')}
                      </span>
                    )}
                  </span>
                  <span role="cell" className="sr-state">
                    <span className={`tag ${bad ? 'warn' : stTone(s)}`}>
                      {s.chosen && <Check size={12} aria-hidden />}
                      {t(stateKey(s, pending))}
                    </span>
                  </span>
                  <span role="cell" className="sr-acts">
                    {bad && (
                      <Btn sm icon={Radio} onClick={() => void a.toSatellite(s)}>
                        {t('act.to_sat')}
                      </Btn>
                    )}
                    {canCtl && !bad && (
                      <IconBtn
                        icon={Crown}
                        label={t('src.make_ctl', { name: hostName(s) })}
                        onClick={() => void a.pick(s)}
                      />
                    )}
                    <IconBtn
                      icon={Pencil}
                      label={t('src.edit', { name: hostName(s) })}
                      onClick={() => setModal({ type: typeOf(s), src: s })}
                    />
                    <IconBtn icon={Trash2} label={`${t('c.remove')}: ${hostName(s)}`} onClick={() => void remove(s)} />
                  </span>
                </div>
              )
            })}
          </div>
        )}
      </Card>
      {(st.pairing?.hosts?.length ?? 0) > 1 && (
        <div>
          <Btn
            kind="danger"
            icon={Trash2}
            onClick={() =>
              void ask({
                title: t('cf.forget_all'),
                body: t('cf.forget_all_b'),
                ok: t('btn.forget_all'),
                danger: true,
              }).then((ok) => ok && act(() => api.dock({ forget: true }).then(refreshStatus)))
            }
          >
            {t('src.forget_all')}
          </Btn>
        </div>
      )}
      {modal && <SourceModal type={modal.type} src={modal.src} close={() => setModal(null)} />}
    </>
  )
}
