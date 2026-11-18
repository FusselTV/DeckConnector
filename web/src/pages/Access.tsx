import {
  Download,
  ExternalLink,
  FileBadge,
  Gamepad2,
  Globe,
  Key,
  KeyRound,
  Lock,
  Pencil,
  Plus,
  ShieldCheck,
  Trash2,
  TriangleAlert,
  Undo2,
  RefreshCw,
} from 'lucide-react'
import { useCallback, useEffect, useState, type SyntheticEvent } from 'react'
import { api, ApiError, caUrl, certUrl } from '../api/client'
import type { Passkey, Right, TlsInfo, Token } from '../api/types'
import { putConfig } from '../app/actions'
import { expectRestart } from '../app/connection'
import { decksOf, hasScreen } from '../app/derive'
import { loginReasonAtom, toast } from '../app/state'
import { getState, useStore } from '../app/store'
import { errText, useUi } from '../app/ui'
import { Card } from '../components/Card'
import { Btn, describedBy, Empty, Field, Help, IconBtn, Seg, SIcon, Switch, Tag } from '../components/controls'
import { Modal } from '../components/Dialog'
import { CopyField, FilePick, PwInput } from '../components/InputAction'
import { KeyValue } from '../components/KeyValue'
import { Row } from '../components/Row'
import { useDraft } from '../components/SaveBar'
import { addPasskey, passkeysHere } from '../lib/passkey'
import { deviceLabel } from '../lib/util'
import { corsError, isIP, passwordError, tokenNameError } from '../lib/validate'

const RIGHT_ICON = { control: Gamepad2, full: ShieldCheck }

function TokenModal({ tokens, close, done }: { tokens: Token[]; close: () => void; done: () => void }) {
  const { t, act, ask, lang } = useUi()
  const [name, setName] = useState('')
  const [right, setRight] = useState<Right>('control')
  const [err, setErr] = useState<string | null>(null)
  const [made, setMade] = useState<string | null>(null)
  // While the token is made the dialog stays open, so its one-time secret is never lost.
  const [busy, setBusy] = useState(false)
  const create = async (e: SyntheticEvent) => {
    e.preventDefault()
    if (busy) return
    const e1 = tokenNameError(name)
    if (e1) return setErr(t(e1))
    const exists = tokens.some((x) => x.name === name)
    if (!exists && tokens.length >= 8) return setErr(t('access.tfull'))
    if (
      exists &&
      !(await ask({ title: t('cf.trepl', { name }), body: t('cf.trepl_b'), ok: t('btn.replace'), danger: true }))
    )
      return
    setBusy(true)
    await act(async () => {
      const r = await api.makeToken(name, right)
      setMade(r.token)
      done()
    })
    setBusy(false)
  }
  if (made)
    return (
      <Modal
        icon={Key}
        title={t('api.made', { name })}
        foot={
          <Btn kind="pri" onClick={close}>
            {t('wiz.s_done')}
          </Btn>
        }
      >
        <div className="warnline">
          <TriangleAlert size={16} aria-hidden />
          {t('access.tonce')}
        </div>
        <CopyField id="tokval" value={made} />
        <p className="small muted">
          {t('api.use')} <code>Authorization: Bearer …</code>
        </p>
      </Modal>
    )
  return (
    <Modal
      icon={Key}
      title={t('api.add')}
      close={busy ? () => undefined : close}
      foot={
        <>
          <Btn kind="ghost" onClick={close} disabled={busy}>
            {t('c.cancel')}
          </Btn>
          <button type="submit" form="tokform" className="btn pri" disabled={busy} aria-busy={busy || undefined}>
            {t('access.tcreate')}
          </button>
        </>
      }
    >
      <form id="tokform" className="stack" style={{ gap: 12 }} onSubmit={(e) => void create(e)} noValidate>
        <Field id="tname" label={t('access.tname')} err={err}>
          <input
            {...describedBy('tname', err)}
            className="in mono"
            placeholder={lang === 'de' ? 'z. B. stream-script' : 'e.g. stream-script'}
            value={name}
            maxLength={23}
            onChange={(e) => (setName(e.target.value), setErr(null))}
            data-autofocus
          />
        </Field>
        <div className="field">
          <span className="flabel">{t('api.right')}</span>
          <Seg
            id="tright"
            label={t('api.right')}
            value={right}
            onChange={setRight}
            options={[
              ['control', t('api.r_control'), Gamepad2],
              ['full', t('api.r_full'), ShieldCheck],
            ]}
          />
          <span className="hint">{t(right === 'full' ? 'api.r_full_h' : 'api.r_control_h')}</span>
        </div>
      </form>
    </Modal>
  )
}

async function pem(f: File) {
  const s = await f.text()
  return /-----BEGIN [A-Z ]+-----/.test(s) ? s : null
}

function CertModal({ close }: { close: () => void }) {
  const { t, act } = useUi()
  const [crt, setCrt] = useState<File | null>(null)
  const [key, setKey] = useState<File | null>(null)
  const [info, setInfo] = useState<TlsInfo | null>(null)
  const [err, setErr] = useState<string | null>(null)
  useEffect(() => {
    if (!crt || !key) return
    void Promise.all([pem(crt), pem(key)]).then(async ([c, k]) => {
      if (!c || !k) return setErr(t('cert.bad'))
      try {
        setInfo(await api.tlsCheck(c, k))
      } catch (e) {
        setErr(errText(e, t))
      }
    })
  }, [crt, key, t])
  const apply = async () => {
    if (!crt || !key) return
    const [c, k] = await Promise.all([pem(crt), pem(key)])
    if (!c || !k) return
    const ok = await act(async () => {
      await api.putTls(c, k)
      expectRestart()
      return true
    })
    if (ok) close()
  }
  return (
    <Modal
      icon={FileBadge}
      title={t('cert.upload')}
      close={close}
      foot={
        <>
          <Btn kind="ghost" onClick={close}>
            {t('c.cancel')}
          </Btn>
          <Btn kind="pri" disabled={!info} onClick={() => void apply()}>
            {t('btn.apply')}
          </Btn>
        </>
      }
    >
      <p className="small muted">{t('cert.h')}</p>
      <Field id="crt" label={t('cert.crt')}>
        <FilePick
          id="crt"
          accept=".pem,.crt,.cer"
          file={crt}
          onFile={(f) => (setCrt(f), setInfo(null), setErr(null))}
        />
      </Field>
      <Field id="ckey" label={t('cert.key')} err={err}>
        <FilePick id="ckey" accept=".pem,.key" file={key} onFile={(f) => (setKey(f), setInfo(null), setErr(null))} />
      </Field>
      {info && (
        <KeyValue
          rows={[
            [t('cert.subject'), info.subject, true],
            [t('cert.issuer'), info.issuer, true],
            [t('cert.until'), info.not_after, true],
            ['SHA-256', info.sha256, true],
          ]}
        />
      )}
    </Modal>
  )
}

function PasskeyModal({ pk, close, done }: { pk: Passkey | null; close: () => void; done: () => void }) {
  const { t } = useUi()
  const [name, setName] = useState(pk?.name ?? deviceLabel(navigator.userAgent).slice(0, 23))
  const [err, setErr] = useState<string | null>(null)
  const [busy, setBusy] = useState(false)
  const save = async (e: SyntheticEvent) => {
    e.preventDefault()
    if (busy) return
    setBusy(true)
    try {
      await (pk ? api.renamePasskey(pk.id, name.trim()) : addPasskey(name.trim()))
      done()
      close()
    } catch (x) {
      setErr(x instanceof ApiError ? errText(x, t) : t('pk.fail'))
    } finally {
      setBusy(false)
    }
  }
  return (
    <Modal
      icon={KeyRound}
      title={t(pk ? 'pk.rename' : 'pk.add')}
      close={busy ? () => undefined : close}
      foot={
        <>
          <Btn kind="ghost" onClick={close} disabled={busy}>
            {t('c.cancel')}
          </Btn>
          <button type="submit" form="pkform" className="btn pri" disabled={busy} aria-busy={busy || undefined}>
            {t(pk ? 'c.save' : 'pk.add')}
          </button>
        </>
      }
    >
      <form id="pkform" className="stack" onSubmit={(e) => void save(e)} noValidate>
        <Field id="pkname" label={t('access.tname')} err={err}>
          <input
            {...describedBy('pkname', err)}
            className="in"
            value={name}
            maxLength={23}
            onChange={(e) => (setName(e.target.value), setErr(null))}
            data-autofocus
          />
        </Field>
      </form>
    </Modal>
  )
}

function Passkeys() {
  const { t, act, ask } = useUi()
  const st = useStore((s) => s.status)
  const [list, setList] = useState<Passkey[] | null>(null)
  const [failed, setFailed] = useState(false)
  const [edit, setEdit] = useState<Passkey | true | null>(null)
  const load = useCallback(() => {
    api.passkeys().then(
      (x) => {
        setList(x)
        setFailed(false)
      },
      () => setFailed(true),
    )
  }, [])
  useEffect(load, [load])
  const here = passkeysHere()
  const drop = async (p: Passkey) => {
    if (
      !(await ask({ title: t('cf.pkdel', { name: p.name }), body: t('cf.pkdel_b'), ok: t('btn.delete'), danger: true }))
    )
      return
    await act(async () => {
      await api.dropPasskey(p.id)
      load()
    })
  }
  const add = (
    <Btn id="pk-add" icon={Plus} onClick={() => setEdit(true)} disabled={!list || list.length >= 8}>
      {t('pk.add')}
    </Btn>
  )
  const named = st.name ? `${st.name}.local` : ''
  return (
    <Card
      id="card-pk"
      icon={KeyRound}
      title={t('pk.title')}
      right={
        <>
          {here && !!list?.length && add}
          <Help right label={t('pk.title')} text={t('pk.h')} />
        </>
      }
    >
      {!here && (
        <div className="warnline">
          <TriangleAlert size={16} aria-hidden />
          <span>
            {t('err.passkey_host')} {named && <a href={`https://${named}/`}>{named}</a>}
          </span>
        </div>
      )}
      {failed ? (
        <div className="warnline">
          <TriangleAlert size={16} aria-hidden />
          {t('access.tload')}
          <Btn sm kind="ghost" icon={RefreshCw} onClick={load}>
            {t('ban.retry')}
          </Btn>
        </div>
      ) : list?.length ? (
        <div className="list">
          {list.map((p) => (
            <div key={p.id} className="li">
              <SIcon icon={KeyRound} />
              <span className="grow">{p.name}</span>
              <span className="mono small muted">{p.rp}</span>
              <IconBtn icon={Pencil} label={`${t('pk.rename')}: ${p.name}`} onClick={() => setEdit(p)} />
              <IconBtn icon={Trash2} label={`${t('btn.delete')}: ${p.name}`} onClick={() => void drop(p)} />
            </div>
          ))}
        </div>
      ) : (
        list &&
        here && (
          <Empty icon={KeyRound}>
            <p>{t('pk.empty')}</p>
            {add}
          </Empty>
        )
      )}
      {edit && <PasskeyModal pk={edit === true ? null : edit} close={() => setEdit(null)} done={load} />}
    </Card>
  )
}

export function Access() {
  const { t, act, ask, go } = useUi()
  const st = useStore((s) => s.status)
  const cfg = useStore((s) => s.config)
  const decks = decksOf(st)
  const [pw, setPw] = useState('')
  const [cors, setCors] = useState<string | null>(null)
  const [proxy, setProxy] = useState<string | null>(null)
  const [tokens, setTokens] = useState<Token[] | null>(null)
  const [tokErr, setTokErr] = useState(false)
  const [tm, setTm] = useState(false)
  const [cm, setCm] = useState(false)
  const loadTokens = useCallback(() => {
    api.tokens().then(
      (x) => {
        setTokens(x)
        setTokErr(false)
      },
      () => setTokErr(true),
    )
  }, [])
  useEffect(loadTokens, [loadTokens])
  const corsV = cors ?? cfg?.cors_origins ?? ''
  const pwE = passwordError(pw)
  const corsE = corsError(corsV)
  const restart = !!pw
  const corsDirty = cors !== null && cors !== cfg?.cors_origins
  const proxyV = proxy ?? cfg?.trusted_proxy ?? ''
  const proxyE = proxyV && !isIP(proxyV) ? 'net.bad_ip' : null
  const proxyDirty = proxy !== null && proxy !== cfg?.trusted_proxy
  useDraft('access', {
    dirty: restart || corsDirty || proxyDirty,
    restart,
    reset: () => {
      setPw('')
      setCors(null)
      setProxy(null)
    },
    check: () => {
      if (!pwE && !corsE && !proxyE) return true
      go('access')
      setTimeout(() => document.getElementById(pwE ? 'npw' : corsE ? 'cors' : 'proxy')?.focus(), 80)
      return false
    },
    confirm: { title: t('cf.pw'), body: t('cf.pw_b') },
    patch: {
      ...(corsDirty ? { cors_origins: corsV } : {}),
      ...(proxyDirty ? { trusted_proxy: proxyV } : {}),
      ...(restart ? { admin_pass: pw } : {}),
    },
    saved: () => {
      setCors(null)
      setProxy(null)
      if (!restart) return
      setPw('')
      loginReasonAtom.set('pw')
      if (!getState().restarting) expectRestart()
    },
  })
  const httpsOnly = async (v: boolean) => {
    if (v && !(await ask({ title: t('cf.https'), body: t('cf.https_b'), ok: t('btn.on') }))) return
    await act(() => putConfig({ https_only: v }))
  }
  const delToken = async (n: string) => {
    if (!(await ask({ title: t('cf.tdel', { name: n }), body: t('cf.tdel_b'), ok: t('btn.delete'), danger: true })))
      return
    await act(async () => {
      await api.dropToken(n)
      toast(t('access.tdeleted', { name: n }))
      loadTokens()
    })
  }
  const host = st.network?.lan?.ip || st.network?.wlan?.ip || location.hostname
  const addBtn = (
    <Btn id="tok-add" icon={Plus} onClick={() => setTm(true)} disabled={!tokens}>
      {t('api.add')}
    </Btn>
  )
  return (
    <div className="cols2">
      <div className="stack">
        <Card
          id="card-pw"
          icon={KeyRound}
          title={t('access.pw')}
          right={<Help right label={t('access.pw')} text={t('cf.pw_b')} />}
        >
          <div className="grid2">
            <Field id="npw" label={t('access.pw_new')} hint={t('access.pw_h')} err={pwE ? t(pwE) : null}>
              <PwInput
                {...describedBy('npw', pwE, true)}
                minLength={6}
                value={pw}
                onChange={(e) => setPw(e.target.value)}
              />
            </Field>
          </div>
          {cfg && !(decks.length && !decks.some(hasScreen)) && (
            <Switch
              id="qr-login"
              on={cfg.qr_login}
              onChange={(v) => void act(() => putConfig({ qr_login: v }))}
              label={t('access.qr')}
              hint={t(cfg.setup_done ? 'access.qr_done' : 'access.qr_h')}
            />
          )}
        </Card>
        <Card
          id="card-https"
          icon={Lock}
          title={t('access.https')}
          right={<Help right label={t('access.fp')} text={t('access.fp_h')} />}
        >
          <Switch
            id="https"
            on={!!cfg?.https_only}
            onChange={(v) => void httpsOnly(v)}
            label={t('access.https_only')}
            hint={t('access.https_only_h')}
            disabled={!cfg}
          />
          {st.https_fingerprint && (
            <div className="field">
              <span className="flabel">{t('access.fp')}</span>
              <div className="inrow">
                <div style={{ flex: 1, minWidth: 0 }}>
                  <CopyField id="fp" value={st.https_fingerprint} />
                </div>
                <a
                  className="ibtn"
                  href={`https://${host}/`}
                  aria-label={t('access.https_open')}
                  title={t('access.https_open')}
                >
                  <ExternalLink size={16} aria-hidden />
                </a>
              </div>
            </div>
          )}
          <div className="field">
            <span className="flabel">{t('cert.title')}</span>
            <span className="small">{st.https_custom ? t('cert.applied') : t('cert.own')}</span>
          </div>
          <div className="row">
            {st.https_custom ? (
              <a className="btn" id="cert-dl" href={certUrl} download={`${st.name ?? 'deckconnector'}.pem`}>
                <Download size={16} aria-hidden />
                {t('cert.dl')}
              </a>
            ) : (
              <a className="btn" id="ca-dl" href={caUrl} download={`${st.name ?? 'deckconnector'}-ca.crt`}>
                <Download size={16} aria-hidden />
                {t('cert.ca')}
              </a>
            )}
            {st.https_custom ? (
              <Btn icon={Undo2} onClick={() => void act(async () => (await api.resetTls(), expectRestart()))}>
                {t('cert.reset')}
              </Btn>
            ) : (
              <Btn id="cert-up" icon={FileBadge} onClick={() => setCm(true)}>
                {t('cert.upload')}
              </Btn>
            )}
          </div>
          {st.https_ca && (
            <p className="small" id="ca-fp">
              {t('cert.ca_fp')} <span className="mono">{st.https_ca}</span>
            </p>
          )}
          <Field id="proxy" label={t('access.proxy')} hint={t('access.proxy_h')} err={proxyE ? t(proxyE) : null}>
            <input
              {...describedBy('proxy', proxyE, true)}
              className="in mono"
              value={proxyV}
              disabled={!cfg}
              placeholder="192.168.1.15"
              onChange={(e) => setProxy(e.target.value.trim())}
              spellCheck={false}
            />
          </Field>
          {!st.https_custom && (
            <Row id="trust-fold" icon={ShieldCheck} title={t('cert.trust')}>
              <p className="small muted">{t('cert.t_ip', { name: `${st.name ?? 'deckconnector'}.local` })}</p>
              <KeyValue
                rows={[
                  ['iPhone, iPad', t('cert.t_ios')],
                  ['macOS', t('cert.t_mac')],
                  ['Windows', t('cert.t_win')],
                  ['Android', t('cert.t_android')],
                  ['Firefox, Linux', t('cert.t_other')],
                ]}
              />
            </Row>
          )}
        </Card>
        <Passkeys />
      </div>
      <Card
        id="card-tok"
        icon={Key}
        title={t('api.title')}
        right={
          <>
            {!!tokens?.length && addBtn}
            <Help right label={t('api.title')} text={t('api.h')} />
          </>
        }
      >
        {tokErr ? (
          <div className="warnline">
            <TriangleAlert size={16} aria-hidden />
            {t('access.tload')}
            <Btn sm kind="ghost" icon={RefreshCw} onClick={loadTokens}>
              {t('ban.retry')}
            </Btn>
          </div>
        ) : tokens?.length ? (
          <div className="list">
            {tokens.map((x) => {
              const r = x.right ?? 'control'
              return (
                <div key={x.name} className="li">
                  <SIcon icon={Key} />
                  <span className="grow mono">{x.name}</span>
                  <Tag icon={RIGHT_ICON[r]}>{t(r === 'full' ? 'api.r_full' : 'api.r_control')}</Tag>
                  <IconBtn
                    icon={Trash2}
                    label={`${t('btn.delete')}: ${x.name}`}
                    onClick={() => void delToken(x.name)}
                  />
                </div>
              )
            })}
          </div>
        ) : (
          tokens && (
            <Empty icon={Key}>
              <p>{t('api.empty')}</p>
              {addBtn}
            </Empty>
          )
        )}
        <Row
          id="cors-fold"
          icon={Globe}
          title={t('access.cors')}
          status={
            <span className="mono small ell" title={cfg?.cors_origins}>
              {cfg?.cors_origins || '–'}
            </span>
          }
        >
          <Field id="cors" label={t('access.cors_l')} hint={t('access.cors_h')} err={corsE ? t(corsE) : null}>
            <input
              {...describedBy('cors', corsE, true)}
              className="in mono"
              value={corsV}
              disabled={!cfg}
              onChange={(e) => setCors(e.target.value)}
              spellCheck={false}
            />
          </Field>
        </Row>
      </Card>
      {tm && tokens && <TokenModal tokens={tokens} close={() => setTm(false)} done={loadTokens} />}
      {cm && <CertModal close={() => setCm(false)} />}
    </div>
  )
}
