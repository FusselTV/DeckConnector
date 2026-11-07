import {
  ArchiveRestore,
  Check,
  Cpu,
  DatabaseBackup,
  Download,
  ExternalLink,
  FileUp,
  KeyRound,
  Globe,
  Layers,
  LoaderCircle,
  Power,
  RefreshCw,
  Settings,
  Sparkles,
  Trash2,
  TriangleAlert,
} from 'lucide-react'
import { useCallback, useEffect, useRef, useState, type ReactNode } from 'react'
import { api } from '../api/client'
import type { Config, Frontend, Slot, UpdateCheck } from '../api/types'
import { putConfig } from '../app/actions'
import { expectRestart } from '../app/connection'
import { toast } from '../app/state'
import { useRoute, useStore } from '../app/store'
import { errText, useUi } from '../app/ui'
import { Card } from '../components/Card'
import { Btn, Choice, describedBy, Field, Help, Seg, SIcon, type Tone } from '../components/controls'
import { Modal } from '../components/Dialog'
import { FilePick } from '../components/InputAction'
import { KeyValue } from '../components/KeyValue'
import { Notice } from '../components/Notice'
import { Row } from '../components/Row'
import type { TKey } from '../i18n'
import { backupOf, planRestore } from '../lib/backup'
import { dbfwTarget, download, roam } from '../lib/util'
import { httpUrlError } from '../lib/validate'

type Target = Awaited<ReturnType<typeof dbfwTarget>>

function slotState(s: Slot): [Tone, TKey] {
  if (s.running) return ['ok', 'slot.running']
  if (s.state === 'valid') return ['', 'slot.valid']
  if (s.state === 'pending') return ['warn', 'slot.pending']
  if (s.state === 'invalid') return ['bad', 'slot.failed']
  return ['', 'slot.unknown']
}

function UpdateModal({ close, target }: { close: () => void; target: Slot | undefined }) {
  const { t, act, ask } = useUi()
  const [mode, setMode] = useState<'file' | 'url'>('file')
  const [file, setFile] = useState<File | null>(null)
  const [tg, setTg] = useState<Target>(null)
  const [url, setUrl] = useState('')
  const [err, setErr] = useState<string | null>(null)
  const [version, setVersion] = useState<string | null>(null)
  const canInspect = useRoute('POST', '/api/ota/inspect')
  // Only the reply for the file picked last counts; an earlier, slower one is dropped.
  const picked = useRef<File | null>(null)
  const pickFile = (f: File | null) => {
    picked.current = f
    setFile(f)
    setTg(null)
    setErr(null)
    setVersion(null)
    if (!f) return
    // The device reads the header itself where it can, and says whether it would take the file.
    if (canInspect)
      api.inspect(f).then(
        (r) => {
          if (picked.current !== f) return
          if (!r.ok) return setErr(r.error ?? t('fw.bad_header'))
          setTg(r.target === 'web' ? 'page' : (r.target ?? null))
          setVersion(r.version ?? null)
        },
        (e: unknown) => picked.current === f && setErr(errText(e, t)),
      )
    else void dbfwTarget(f).then((x) => picked.current === f && (x ? setTg(x) : setErr(t('fw.bad_header'))))
  }
  const what =
    mode === 'url'
      ? t('upd.w_url')
      : tg === 'page'
        ? t('upd.w_page')
        : tg === 'c6'
          ? t('upd.w_c6')
          : tg === 'p4'
            ? t('upd.w_p4', { slot: target?.slot ?? '–', old: target?.version ?? '–' })
            : null
  const go = async () => {
    if (mode === 'url') {
      const e = httpUrlError(url)
      if (e) return setErr(t(e))
      const ok = await ask({
        title: t('cf.fw', { tgt: t('tgt.p4') }),
        body: (
          <>
            <code className="wrap">{url}</code>
            <p>{t('fw.ver_unknown')}</p>
            {url.startsWith('http://') && <p className="warnline">{t('fw.http_warn')}</p>}
          </>
        ),
        ok: t('btn.install'),
      })
      if (!ok) return
      close()
      await act(() => api.otaUrl(url.trim()))
      return
    }
    if (!file || !tg) return
    if (tg === 'bootloader' || tg === 'ptable') return setErr(t('fw.boot_file'))
    close()
    await act(async () => {
      if (tg === 'page') {
        await api.putFrontend(file)
        toast(t('fw.page_updated', { v: file.name }))
        setTimeout(() => location.reload(), 800)
        return
      }
      await api.ota(file)
      expectRestart()
    })
  }
  return (
    <Modal
      icon={Download}
      title={t('fw.install')}
      close={close}
      foot={
        <>
          <Btn kind="ghost" onClick={close}>
            {t('c.cancel')}
          </Btn>
          <Btn
            kind="pri"
            icon={tg === 'page' ? Download : Power}
            disabled={mode === 'file' ? !tg || !!err : !url}
            onClick={() => void go()}
          >
            {tg === 'page' && mode === 'file' ? t('btn.install') : t('upd.go_restart')}
          </Btn>
        </>
      }
    >
      <div className="field">
        <span className="flabel">{t('upd.src')}</span>
        <Seg
          id="upd-mode"
          label={t('upd.src')}
          value={mode}
          onChange={(m) => (setMode(m), setErr(null))}
          options={[
            ['file', t('upd.file')],
            ['url', t('upd.url')],
          ]}
        />
      </div>
      {mode === 'file' ? (
        <Field id="upd-file" label={t('upd.file_l')} hint={t('fw.install_h')} err={err}>
          <FilePick id="upd-file" accept=".dbfw" file={file} onFile={pickFile} />
        </Field>
      ) : (
        <Field id="upd-url" label={t('upd.url')} err={err}>
          <input
            {...describedBy('upd-url', err)}
            className="in mono"
            type="url"
            placeholder="https://…/deckconnector.dbfw"
            value={url}
            onChange={(e) => (setUrl(e.target.value), setErr(null))}
          />
        </Field>
      )}
      {what && (
        <Notice
          n={{
            id: 'what',
            icon: Cpu,
            lvl: 'info',
            title: t('upd.what'),
            text: mode === 'file' ? `${what} ${version ? t('fw.file_v', { v: version }) : t('fw.ver_unknown')}` : what,
          }}
        />
      )}
    </Modal>
  )
}

function SaveModal({ close, cfg, full }: { close: () => void; cfg: Config; full: boolean }) {
  const { t, act } = useUi()
  const [what, setWhat] = useState<'cfg' | 'full'>('cfg')
  const go = () =>
    void act(async () => {
      const name = what === 'full' ? `${cfg.name}-backup.json` : `${cfg.name}-settings.json`
      download(name, what === 'full' ? JSON.stringify(await api.backup(), null, 1) : backupOf(cfg))
      toast(t('c.downloaded', { file: name }))
      close()
    })
  return (
    <Modal
      icon={DatabaseBackup}
      title={t('fw.save_t')}
      close={close}
      foot={
        <>
          <Btn kind="ghost" onClick={close}>
            {t('c.cancel')}
          </Btn>
          <Btn kind="pri" icon={Download} onClick={go}>
            {t('fw.save')}
          </Btn>
        </>
      }
    >
      <div className="choices" role="radiogroup" aria-label={t('fw.save_t')} onKeyDown={roam} tabIndex={-1}>
        <Choice
          on={what === 'cfg'}
          icon={Settings}
          label={t('fw.save_cfg')}
          hint={t('fw.save_cfg_h')}
          onPick={() => setWhat('cfg')}
        />
        {full && (
          <Choice
            on={what === 'full'}
            icon={KeyRound}
            label={t('fw.save_full')}
            hint={t('fw.save_full_h')}
            onPick={() => setWhat('full')}
          />
        )}
      </div>
      {what === 'full' && <p className="warnline">{t('fw.save_full_warn')}</p>}
    </Modal>
  )
}

function UpdateCheckBox({ lead }: { lead: ReactNode }) {
  const { t, fmt, act, ask } = useUi()
  const fw = useStore((s) => s.status.firmware ?? '')
  const [st, setSt] = useState<
    | { state: 'idle' | 'checking' | 'none' }
    | { state: 'error'; msg: string }
    | { state: 'new'; u: UpdateCheck & { latest: string; url: string } }
  >({ state: 'idle' })
  const [at, setAt] = useState(0)
  const check = async () => {
    setSt({ state: 'checking' })
    try {
      const u = await api.updateCheck()
      setAt(Date.now())
      if (u.latest && u.url && u.latest !== u.current)
        setSt({ state: 'new', u: { ...u, latest: u.latest, url: u.url } })
      else setSt(u.error ? { state: 'error', msg: u.error } : { state: 'none' })
    } catch (e) {
      setSt({ state: 'error', msg: errText(e, t) })
    }
  }
  return (
    <>
      <div className="row acts">
        {lead}
        <Btn id="upd-check" icon={RefreshCw} disabled={st.state === 'checking'} onClick={() => void check()}>
          {t(st.state === 'checking' ? 'upd.checking' : 'upd.check')}
        </Btn>
      </div>
      {st.state === 'none' && (
        <p className="okline" role="status">
          <Check size={16} aria-hidden />
          {t('upd.none', { v: fw, time: fmt.time(at) })}
        </p>
      )}
      {st.state === 'error' && <p className="warnline">{st.msg}</p>}
      {st.state === 'new' && (
        <Notice
          n={{
            id: 'upd-new',
            icon: Download,
            lvl: 'pair',
            title: t('upd.new', { v: st.u.latest }),
            text: t('upd.new_h'),
            acts: [
              {
                label: t('btn.install'),
                icon: Download,
                pri: true,
                run: () =>
                  void ask({ title: t('upd.cf', { v: st.u.latest }), body: t('upd.cf_b'), ok: t('btn.install') }).then(
                    (ok) => (ok ? act(() => api.otaUrl(st.u.url)) : undefined),
                  ),
              },
              ...(st.u.notes?.startsWith('https://')
                ? [
                    {
                      label: t('upd.notes'),
                      icon: ExternalLink,
                      run: () => window.open(st.u.notes, '_blank', 'noopener'),
                    },
                  ]
                : []),
            ],
          }}
        />
      )}
    </>
  )
}

export function Firmware() {
  const { t, fmt, act, ask } = useUi()
  const st = useStore((s) => s.status)
  const cfg = useStore((s) => s.config)
  const ota = useStore((s) => s.ota)
  const canCheck = useRoute('GET', '/api/update/check')
  const canBackup = useRoute('GET', '/api/backup')
  const [slots, setSlots] = useState<Slot[] | null>(null)
  const [front, setFront] = useState<Frontend | null>(null)
  const [upd, setUpd] = useState(false)
  const [save, setSave] = useState(false)
  const rsRef = useRef<HTMLInputElement>(null)
  const slotRef = useRef<HTMLInputElement>(null)
  const [slotTarget, setSlotTarget] = useState<Slot | null>(null)
  const load = useCallback(() => {
    api.slots().then(setSlots, () => setSlots([]))
    api.frontend().then(setFront, () => setFront(null))
  }, [])
  useEffect(load, [load])
  const running = slots?.find((s) => s.running)
  const target = slots?.find((s) => !s.running && !s.next && s.state !== 'pending') ?? slots?.find((s) => !s.running)
  const bootSlot = async (s: Slot) => {
    if (
      !(await ask({
        title: t('cf.slot', { v: s.version ?? '', label: s.slot }),
        body: t('cf.slot_b', { v: s.version ?? '', cur: st.firmware ?? '' }),
        ok: t('btn.boot'),
      }))
    )
      return
    await act(async () => {
      await api.bootSlot(s.slot)
      expectRestart()
    })
  }
  const writeSlot = async (file: File) => {
    const s = slotTarget
    if (!s) return
    if ((await dbfwTarget(file)) !== 'p4') return toast(t('fw.bad_header'), { bad: true })
    if (
      !(await ask({
        title: t('cf.write', { slot: s.slot }),
        body: t('cf.write_b', { old: s.version ?? '–' }),
        ok: t('fw.write'),
      }))
    )
      return
    await act(async () => {
      await api.ota(file, s.slot, false)
      toast(t('fw.written', { slot: s.slot }))
      load()
    })
  }
  const resetPage = async () => {
    if (!(await ask({ title: t('cf.page_reset'), body: t('cf.page_reset_b'), ok: t('c.remove'), danger: true }))) return
    await act(async () => {
      await api.resetFrontend()
      toast(t('fw.page_reset_done'))
      setTimeout(() => location.reload(), 800)
    })
  }
  const restore = async (f: File) => {
    if (!cfg) return
    let obj: unknown
    try {
      obj = JSON.parse(await f.text())
    } catch {
      return toast(t('fw.restore_bad'), { bad: true })
    }
    const p = planRestore(cfg, obj)
    if (p.errors.length)
      return void ask({
        title: t('fw.restore_err'),
        body: (
          <>
            <p>{t('fw.restore_err_b')}</p>
            <ul className="plain">
              {p.errors.map((e) => (
                <li key={e}>
                  <code>{e}</code>
                </li>
              ))}
            </ul>
          </>
        ),
        ok: null,
        danger: true,
      })
    if (!p.rows.length) return toast(t('fw.restore_same'))
    const show = (v: unknown) =>
      v === undefined || v === ''
        ? '–'
        : Array.isArray(v)
          ? v.filter(Boolean).join(', ') || '–'
          : typeof v === 'object'
            ? JSON.stringify(v)
            : (v as string)
    const ok = await ask({
      title: t('cf.restore'),
      body: (
        <>
          <p>{t('cf.restore_b', { n: p.rows.length })}</p>
          <div className="tblw">
            <table className="diff">
              <thead>
                <tr>
                  <th>{t('cf.r_key')}</th>
                  <th>{t('cf.r_now')}</th>
                  <th>{t('cf.r_file')}</th>
                </tr>
              </thead>
              <tbody>
                {p.rows.map((r) => (
                  <tr key={r.key}>
                    <td className="mono">{r.key}</td>
                    <td className="mono">{r.secret ? (r.now ? t('fw.secret_set') : '–') : show(r.now)}</td>
                    <td className="mono" style={{ color: 'var(--fg)', fontWeight: 600 }}>
                      {r.secret ? (r.file ? t('fw.secret_set') : '–') : show(r.file)}
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        </>
      ),
      ok: p.restart ? t('c.save_restart') : t('btn.apply'),
    })
    if (!ok) return
    await act(async () => {
      const r = await putConfig(p.patch)
      if (!r.restart) toast(t('fw.restored'))
    })
  }
  const install = (
    <Btn id="fw-file" kind="pri" icon={Download} disabled={!!ota} onClick={() => setUpd(true)}>
      {t('fw.install')}
    </Btn>
  )
  const why = (s: Slot) =>
    s.why_not ??
    (s.running
      ? ''
      : s.state === 'pending'
        ? t('slot.why_pending')
        : s.level === undefined
          ? t('slot.why_nolevel')
          : s.state === 'invalid'
            ? t('slot.failed')
            : '')
  return (
    <div className="cards">
      <Card
        id="card-fw"
        icon={Cpu}
        title={t('fw.update')}
        right={<Help right label={t('fw.install')} text={t('fw.install_h')} />}
      >
        <KeyValue
          rows={[
            [t('fw.firmware'), t('fw.running_slot', { v: st.firmware ?? '–', n: running?.slot ?? '–' }), true],
            [t('tgt.c6'), st.wlan_firmware, true],
            [
              t('fw.page'),
              front && (
                <>
                  <span className="mono">{front.version ?? 'built-in'}</span>{' '}
                  <span className="muted small">
                    {t(front.source === 'partition' ? 'fw.src_partition' : 'fw.src_builtin')}
                    {front.size ? ` · ${fmt.bytes(front.size)}` : ''}
                  </span>
                </>
              ),
            ],
            [t('dia.id'), st.id, true],
            [t('dia.api'), st.api, true],
          ]}
        />
        {ota && (
          <div className="prog" role="status">
            <div className="row">
              <LoaderCircle size={18} className="spin" aria-hidden />
              <b>{t(`tgt.${ota.target}` as 'tgt.p4')}</b> · {ota.error ?? t(`ota.${ota.state}` as 'ota.done')}
              <span className="sp" />
              <span className="mono">{fmt.pct(ota.pct)}</span>
            </div>
            <div className="bar" style={{ flex: 'none' }}>
              <i style={{ width: `${ota.pct}%` }} />
            </div>
          </div>
        )}
        {canCheck ? <UpdateCheckBox lead={install} /> : <div className="row acts">{install}</div>}
        <Row
          id="slots"
          icon={Layers}
          title={t('fw.slots')}
          status={running && <span className="tag">{`${t('th.slot')} ${running.slot}`}</span>}
        >
          <p className="small muted">{t('fw.slots_h')}</p>
          <input
            ref={slotRef}
            type="file"
            accept=".dbfw"
            hidden
            onChange={(e) => {
              const f = e.target.files?.[0]
              e.target.value = ''
              if (f) void writeSlot(f)
            }}
          />
          <div className="list">
            {(slots ?? []).map((s) => {
              const [k, w] = slotState(s)
              const bootable =
                s.bootable ?? ((s.state === 'valid' || s.state === 'unknown') && s.level !== undefined && !s.running)
              const reason = why(s)
              return (
                <div key={s.slot} className="li slotrow">
                  <SIcon icon={Layers} tone={k} />
                  <div className="grow">
                    <b>
                      {t('th.slot')} {s.slot} · <span className="mono">{s.version ?? t('slot.unknown')}</span>
                    </b>
                    <br />
                    <span className="small muted">
                      {s.built}
                      {reason && ` · ${reason}`}
                    </span>
                  </div>
                  <span className={`tag ${k}`}>
                    {(k === 'warn' || k === 'bad') && <TriangleAlert size={12} aria-hidden />}
                    {s.state === 'empty' ? '–' : t(w)}
                  </span>
                  {!s.running && (
                    <div className="slotacts">
                      {bootable && s.state !== 'empty' && (
                        <Btn sm icon={Power} onClick={() => void bootSlot(s)}>
                          {t('slot.boot')}
                        </Btn>
                      )}
                      <Btn
                        sm
                        icon={FileUp}
                        onClick={() => {
                          setSlotTarget(s)
                          slotRef.current?.click()
                        }}
                      >
                        {t('fw.write')}
                      </Btn>
                    </div>
                  )}
                </div>
              )
            })}
            {front?.source === 'partition' && (
              <div className="li">
                <SIcon icon={Globe} />
                <div className="grow">
                  <b>{t('fw.web_rm')}</b>
                  <br />
                  <span className="small muted">{t('fw.web_rm_h')}</span>
                </div>
                <Btn id="web-rm" sm icon={Trash2} onClick={() => void resetPage()}>
                  {t('c.remove')}
                </Btn>
              </div>
            )}
          </div>
        </Row>
      </Card>
      <Card
        id="card-bak"
        icon={DatabaseBackup}
        title={t('fw.backup')}
        right={<Help right label={t('fw.backup')} text={`${t('fw.backup_h')} ${t('fw.not_in')}`} />}
      >
        <div className="row wrap acts">
          <Btn id="bak-dl" icon={Download} disabled={!cfg} onClick={() => setSave(true)}>
            {t('fw.save')}
          </Btn>
          <Btn id="bak-restore" icon={ArchiveRestore} disabled={!cfg} onClick={() => rsRef.current?.click()}>
            {t('fw.restore')}
          </Btn>
          <Btn
            icon={Sparkles}
            onClick={() =>
              void ask({ title: t('cf.setup'), body: t('cf.setup_b'), ok: t('pal.setup') }).then((ok) =>
                ok ? act(() => putConfig({ setup_done: false })) : undefined,
              )
            }
          >
            {t('pal.setup')}
          </Btn>
        </div>
        <input
          ref={rsRef}
          type="file"
          accept="application/json,.json"
          hidden
          onChange={(e) => {
            const f = e.target.files?.[0]
            e.target.value = ''
            if (f) void restore(f)
          }}
        />
      </Card>
      {upd && <UpdateModal close={() => setUpd(false)} target={target} />}
      {save && cfg && <SaveModal close={() => setSave(false)} cfg={cfg} full={canBackup} />}
    </div>
  )
}
