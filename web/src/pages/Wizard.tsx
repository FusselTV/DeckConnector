import {
  Cable,
  Check,
  CircleHelp,
  ChevronLeft,
  ChevronRight,
  Circle,
  CircleCheck,
  Code,
  Info,
  Languages,
  LayoutGrid,
  Link2,
  LoaderCircle,
  Monitor,
  Network as NetIcon,
  Pencil,
  Power,
  Radio,
  TriangleAlert,
  Wifi,
  X,
} from 'lucide-react'
import { useCallback, useEffect, useRef, useState, type ReactNode } from 'react'
import { api } from '../api/client'
import type { IpConf } from '../api/types'
import { putConfig, useActions } from '../app/actions'
import { refreshConfig, refreshStatus } from '../app/connection'
import { decksOf, hostLabel, reachList } from '../app/derive'
import { configErrAtom, langAtom, loginReasonAtom, wifiScanAtom } from '../app/state'
import { RestartOverlay } from '../app/Shell'
import { getState, useStore } from '../app/store'
import { codeText, errText, useUi } from '../app/ui'
import { Btn, Choice, describedBy, Field, SIcon, Switch } from '../components/controls'
import { CopyField, PwInput } from '../components/InputAction'
import { Notice } from '../components/Notice'
import { Row } from '../components/Row'
import { WifiPicker, type WifiChoice } from '../components/WifiPicker'
import { Logo } from '../deck/DeckFront'
import { MODELS } from '../deck/models'
import type { TKey } from '../i18n'
import { roam, ss, useLatest } from '../lib/util'
import {
  companionError,
  ipErrors,
  nameError,
  normPin,
  passwordError,
  pinError,
  securedWifi,
  wifiError,
  wsUrlError,
} from '../lib/validate'
import { chordSentence } from './DeckTab'
import { EMPTY, joinWifi, NetBlock } from './Network'
import { useFound } from './Sources'

const STEPS = ['lang', 'pw', 'net', 'src', 'done'] as const
type Step = (typeof STEPS)[number] | 'find' | 'moving'
const BACK: Partial<Record<Step, Step>> = { pw: 'lang', net: 'pw', find: 'net', src: 'net', done: 'src' }

// The device keeps the step (see FROM_NUM), so a refresh or a second browser resumes. `moving` is stored as the
// source step, so the page opened from the new QR code goes on there.
const FROM_NUM: Step[] = ['lang', 'lang', 'pw', 'pw', 'net', 'src', 'done']
const num = (s: Step) => (s === 'find' ? 4 : s === 'moving' ? 5 : FROM_NUM.lastIndexOf(s))
const viaAp = () => getState().status.via_ap ?? !!getState().status.network?.ap?.on
// Name and password live only in this page until the network step saves them. A reload asks for the password
// again; another browser or a reopened captive portal starts at the name, so setup never ends with values the
// person typed and then lost without seeing it.
const PW_PENDING = 'dc.wizpw'
const NAME_PENDING = 'dc.wizname'
const SEEN = 'dc.wizseen'
const resumed = (): Step => {
  const s = FROM_NUM[getState().status.setup?.step ?? 0] ?? 'lang'
  // Back on the setup Wi-Fi after the move: joining the network failed, so the network step again.
  if ((s === 'src' || s === 'moving') && viaAp()) return 'net'
  if ((s === 'pw' || s === 'net' || s === 'find') && !ss.get<boolean>(SEEN, false)) return 'lang'
  return (s === 'net' || s === 'find') && ss.get<boolean>(PW_PENDING, false) ? 'pw' : s
}

function useStep() {
  const [step, setStep] = useState<Step>(resumed)
  const wentBack = useRef(false)
  useEffect(() => {
    // Next never moves the device's step back, so a second, older tab cannot undo the first one.
    if (wentBack.current || num(step) >= (getState().status.setup?.step ?? 0))
      void api.setup(num(step)).catch(() => undefined)
    wentBack.current = false
    document.getElementById('wz-step')?.focus()
  }, [step])
  const set = useCallback((s: Step, back = false) => {
    wentBack.current = back
    setStep(s)
  }, [])
  // Browser back = wizard back: one guard entry in the history, stepped back on popstate.
  const back = useLatest(BACK[step])
  useEffect(() => {
    history.pushState({ wiz: 1 }, '')
    const f = () => {
      if (back.current) set(back.current, true)
      history.pushState({ wiz: 1 }, '')
    }
    addEventListener('popstate', f)
    return () => removeEventListener('popstate', f)
  }, [back, set])
  return [step, set] as const
}

export function Wizard() {
  const { t, fmt, act, ask, lang } = useUi()
  const a = useActions()
  const [step, setStep] = useStep()
  const st = useStore((s) => s.status)
  const cfg = useStore((s) => s.config)
  const restarting = useStore((s) => !!s.restarting)
  const reach = useStore((s) => s.restarting?.reach)
  const cfgErr = configErrAtom.use()
  const cable = !!st.network?.lan?.cable
  const onAp = st.via_ap ?? !!st.network?.ap?.on
  const rerun = !onAp && !!st.setup?.rerun
  const drawer = st.sources?.find((s) => s.drawing)
  const decks = decksOf(st)
  const [pw, setPw] = useState('')
  const [pwTried, setPwTried] = useState(false)
  // Edits over the stored values: the wizard can open before GET /api/config answers.
  const [d, setD] = useState<{
    name?: string
    lan?: IpConf
    wlan?: IpConf
    useWlan?: boolean
    wifi?: WifiChoice
    editNet?: boolean
  }>(() => ({ name: ss.get<string | undefined>(NAME_PENDING, undefined) }))
  const name = d.name ?? cfg?.name ?? ''
  const lan = d.lan ?? cfg?.lan ?? EMPTY
  const wlan = d.wlan ?? cfg?.wlan ?? EMPTY
  const useWlan = d.useWlan ?? (!!cfg?.wifi_ssid || !cable)
  const wifi = d.wifi ?? { ssid: cfg?.wifi_ssid ?? '', pass: '', bssid: cfg?.wifi_bssid ?? '' }
  const setName = (name: string) => (ss.set(NAME_PENDING, name), setD((o) => ({ ...o, name })))
  const setLan = (lan: IpConf) => setD((o) => ({ ...o, lan }))
  const setWlan = (wlan: IpConf) => setD((o) => ({ ...o, wlan }))
  const setUseWlan = (useWlan: boolean) => setD((o) => ({ ...o, useWlan }))
  const setWifi = (wifi: WifiChoice) => setD((o) => ({ ...o, wifi }))
  // A page that reached the device over its network keeps that network unless asked.
  const editNet = d.editNet ?? !(!onAp && (cable || !!cfg?.wifi_ssid))
  const restarts = editNet || !!pw || name !== cfg?.name
  const nameE = nameError(name)
  const [nameTried, setNameTried] = useState(false)
  const [test, setTest] = useState<null | 'run' | 'ok' | 'bad'>(null)
  const [wErr, setWErr] = useState<string | null>(null)
  const [src, setSrc] = useState<'app' | 'comp' | 'srv' | null>(null)
  const [addr, setAddr] = useState('')
  const [pin, setPin] = useState('')
  const [busy, setBusy] = useState<string | null>(null)
  const [mErr, setMErr] = useState<{ to: string; m: string } | null>(null)
  const [linked, setLinked] = useState<string | null>(null)
  const [url, setUrl] = useState('')
  const [uPin, setUPin] = useState('')
  const [uErr, setUErr] = useState<string | null>(null)
  const [srcErr, setSrcErr] = useState<string | null>(null)
  const [waitS, setWaitS] = useState(0)
  const pwE = passwordError(pw)
  const where = reachList(st, !!cfg?.https_only, name || (cfg?.name ?? ''))
  const hold = (cfg?.menu_hold_ms ?? 2000) / 1000

  const found = useFound(step === 'src' && src === 'comp')
  // While the app path waits, the status shows a pairing request as soon as the app asks.
  useEffect(() => {
    if (step !== 'src' || src !== 'app') return
    const t0 = Date.now()
    const id = setInterval(() => {
      void refreshStatus()
      setWaitS(Math.floor((Date.now() - t0) / 1000))
    }, 2000)
    return () => {
      clearInterval(id)
      setWaitS(0)
    }
  }, [step, src])

  const runTest = async () => {
    const e = wifiError(wifi.ssid, wifi.pass, securedWifi(wifiScanAtom.get(), wifi.ssid))
    if (e) {
      setWErr(t(e))
      return false
    }
    setTest('run')
    setWErr(null)
    const r = await joinWifi(wifi).catch(() => 'failed')
    setTest(r === null ? 'ok' : 'bad')
    if (r !== null) setWErr(r === 'lost' ? t('wifi.lost') : t('wiz.test_bad'))
    return r === null
  }
  const nextLang = () => {
    if (!nameE) return (ss.set(SEEN, true), setStep('pw'))
    setNameTried(true)
    document.getElementById('wname')?.focus()
  }
  const nextNet = async () => {
    if (editNet) {
      if (Object.keys(ipErrors(lan)).length || (useWlan && Object.keys(ipErrors(wlan)).length)) {
        const fx = document.getElementById('w-expert')
        if (fx instanceof HTMLDetailsElement) fx.open = true
        setTimeout(() => document.querySelector<HTMLElement>('[aria-invalid="true"]')?.focus(), 50)
        return
      }
      if (useWlan && test !== 'ok' && !(await runTest())) return
    }
    if (onAp) setStep('find')
    else if (restarts) await saveNet()
    else setStep('src')
  }
  const saveNet = async () => {
    const ok = await act(async () => {
      // A joined Wi-Fi is already saved by the test; everything else goes in one PUT, the password last of all.
      await putConfig({
        name,
        ...(editNet && {
          lan,
          wlan: useWlan ? wlan : EMPTY,
          ...(!useWlan ? { wifi_ssid: '' } : {}),
        }),
        ...(pw ? { admin_pass: pw } : {}),
      })
      return true
    })
    if (!ok) return
    ss.set(PW_PENDING, undefined)
    ss.set(NAME_PENDING, undefined)
    if (pw) loginReasonAtom.set('pw')
    setStep(onAp ? 'moving' : 'src')
  }
  const compConnect = async (target: string, p: string) => {
    const e = companionError(target) ?? (target.startsWith('wss://') ? pinError(p) : null)
    if (e || !target) return setMErr({ to: target, m: t(e ?? 'wiz.addr_bad') })
    setMErr(null)
    setLinked(null)
    setBusy(target)
    try {
      const r = await api.probe(target, normPin(p)).catch((x: unknown) => setMErr({ to: target, m: errText(x, t) }))
      if (!r) return
      if (!r.ok)
        return setMErr({ to: target, m: r.error ? codeText(t, r.code, r.error) : t('wiz.no_answer', { a: target }) })
      const ok = await act(() =>
        putConfig({
          satellite: target,
          satellite_pin: target.startsWith('wss://') ? normPin(p) : '',
          controller: 'companion',
          host: 'satellite',
        }),
      )
      if (ok !== undefined) setLinked(target)
    } finally {
      setBusy(null)
    }
  }
  const srvConnect = async () => {
    const e = wsUrlError(url) ?? (url.startsWith('wss://') ? pinError(uPin) : null)
    if (e) return setUErr(t(e))
    if (!cfg) return setUErr(t('is.cfg'))
    setUErr(null)
    const servers = [...cfg.servers]
    const pins = [...cfg.server_pins]
    const i = servers.findIndex((x) => !x || x === url)
    if (i < 0) return setUErr(t('src.srv_full'))
    servers[i] = url
    pins[i] = url.startsWith('wss://') ? normPin(uPin) : ''
    await act(() => putConfig({ servers, server_pins: pins, controller: 'server', host: url }))
  }
  const finish = async () => {
    const ok = await act(() => putConfig({ setup_done: true }))
    if (ok === undefined) return
    await api.setup(0).catch(() => undefined)
    await Promise.all([refreshConfig(), refreshStatus()])
  }
  const skip = async () => {
    if (await ask({ title: t('cf.skip'), body: t('cf.skip_b'), ok: t('wiz.skip') })) await finish()
  }

  const satErr = mErr && mErr.to === addr.trim() ? mErr.m : null
  const idx = STEPS.indexOf(step === 'find' || step === 'moving' ? 'net' : step)
  const deckSays: TKey = rerun
    ? 'wiz.d_live'
    : step === 'lang'
      ? 'wiz.d_qr'
      : step === 'moving'
        ? 'wiz.d_newqr'
        : step === 'done' || (step === 'src' && drawer)
          ? 'wiz.d_done'
          : st.pairing?.request
            ? 'wiz.d_pair'
            : 'wiz.d_step'
  const backBtn = BACK[step] ? (
    <Btn id="w-back" kind="ghost" icon={ChevronLeft} onClick={() => setStep(BACK[step] as Step, true)}>
      {t('c.back')}
    </Btn>
  ) : (
    <span />
  )
  const next = (label: string, run: () => void, id?: string, disabled?: boolean) => (
    <button type="button" id={id} className="btn pri" onClick={run} disabled={disabled}>
      {label}
      <ChevronRight size={16} aria-hidden />
    </button>
  )
  const pane = (title: string, body: ReactNode, foot: ReactNode) => (
    <>
      <h2 id="wz-step" tabIndex={-1} style={{ fontSize: 22 }}>
        {title}
      </h2>
      {body}
      <p className="deckline">
        <LayoutGrid size={15} className="ic" aria-hidden />
        <span>
          <b>{t(decks.length > 1 ? 'wiz.on_deck1' : 'wiz.on_deck')}</b> {t(deckSays, { n: idx + 1, all: STEPS.length })}
        </span>
      </p>
      {foot && <div className="row wizfoot">{foot}</div>}
    </>
  )

  let body: ReactNode
  if (step === 'lang')
    body = pane(
      t('wiz.lang_q'),
      <>
        <div className="langpick" role="radiogroup" aria-label={t('wiz.lang_q')} onKeyDown={roam} tabIndex={-1}>
          <Choice on={lang === 'de'} icon={Circle} label="Deutsch" onPick={() => langAtom.set('de')} />
          <Choice on={lang === 'en'} icon={Circle} label="English" onPick={() => langAtom.set('en')} />
        </div>
        <Field
          id="wname"
          label={t('wiz.name')}
          hint={t('wiz.name_h', { url: `${nameE ? '…' : name}.local` })}
          err={nameTried && nameE ? t(nameE) : null}
        >
          <input
            {...describedBy('wname', nameTried && nameE, true)}
            className="in mono"
            maxLength={31}
            autoCapitalize="none"
            autoCorrect="off"
            spellCheck={false}
            value={name}
            onChange={(e) => setName(e.target.value.trim())}
          />
        </Field>
        {!rerun && (
          <Notice
            n={{
              id: 'webfree',
              icon: LayoutGrid,
              lvl: 'info',
              title: t('wiz.webfree_t'),
              text: t('wiz.webfree', { a: 1, b: decks[0]?.encoders ?? 4 }),
            }}
          />
        )}
      </>,
      <>
        <span />
        {next(t('c.next'), nextLang)}
      </>,
    )
  else if (step === 'pw')
    body = pane(
      t('wiz.pw_q'),
      <>
        <p className="muted">{t(rerun ? 'wiz.pw_rerun' : 'wiz.pw_why')}</p>
        {!pw && ss.get<boolean>(PW_PENDING, false) && <p className="warnline">{t('wiz.pw_again')}</p>}
        <div className="grid2">
          <Field id="wpw" label={t('access.pw_new')} hint={t('access.pw_h')} err={pwTried && pwE ? t(pwE) : null}>
            <PwInput {...describedBy('wpw', pwTried && pwE, true)} value={pw} onChange={(e) => setPw(e.target.value)} />
          </Field>
        </div>
      </>,
      <>
        {backBtn}
        <div className="row">
          <Btn kind="ghost" onClick={() => (setPw(''), ss.set(PW_PENDING, undefined), setStep('net'))}>
            {t('wiz.pw_keep')}
          </Btn>
          {next(t('c.next'), () => {
            setPwTried(true)
            if (pwE) return
            ss.set(PW_PENDING, !!pw || undefined)
            setStep('net')
          })}
        </div>
      </>,
    )
  else if (step === 'net')
    body = pane(
      t('wiz.net_q'),
      <>
        {!editNet ? (
          <Notice
            n={{
              id: 'kept',
              icon: NetIcon,
              lvl: 'ok',
              title: t('wiz.net_kept'),
              text: where.join(' · '),
              acts: [{ label: t('wiz.net_change'), icon: Pencil, run: () => setD((o) => ({ ...o, editNet: true })) }],
            }}
          />
        ) : (
          <>
            <div className={cable ? 'okline' : 'warnline'}>
              {cable ? <Check size={16} aria-hidden /> : <TriangleAlert size={16} aria-hidden />}
              {cable ? t('wiz.lan_ok') : t('wiz.lan_none')}
            </div>
            {cable && (
              <Switch
                id="w-usewlan"
                on={useWlan}
                onChange={(v) => (setUseWlan(v), setTest(null), setWErr(null))}
                label={t('wiz.use_wlan')}
                hint={t('wiz.use_wlan_h')}
              />
            )}
            {useWlan && (
              <>
                <WifiPicker
                  id="w-wifi"
                  value={wifi}
                  current={rerun ? st.network?.wlan?.ssid : ''}
                  err={wErr}
                  onChange={(v) => (setWifi(v), setTest(null), setWErr(null))}
                />
                {test === 'ok' && (
                  <p className="okline" role="status">
                    <Check size={16} aria-hidden />
                    {t('wiz.test_ok', { ssid: wifi.ssid, ip: st.network?.wlan?.ip ?? '' })}
                  </p>
                )}
              </>
            )}
            <Row id="w-expert" group="wiz" icon={NetIcon} title={t('wiz.expert')}>
              {cable && (
                <>
                  <div className="lbl">{t('net.lan')}</div>
                  <NetBlock id="wlan0" v={lan} set={setLan} err={ipErrors(lan)} cur={st.network?.lan?.ip} />
                </>
              )}
              {useWlan && (
                <>
                  <div className="lbl">{t('net.wlan')}</div>
                  <NetBlock id="wlan1" v={wlan} set={setWlan} err={ipErrors(wlan)} cur={st.network?.wlan?.ip} />
                </>
              )}
            </Row>
          </>
        )}
        {!onAp && restarts && <p className="small muted">{t('wiz.restart_h')}</p>}
      </>,
      <>
        {backBtn}
        <button
          type="button"
          id="w-next"
          className="btn pri"
          disabled={test === 'run' || !cfg}
          onClick={() => void nextNet()}
        >
          {test === 'run' ? (
            <>
              <LoaderCircle size={16} className="spin" aria-hidden />
              {t('wiz.testing')}
            </>
          ) : !onAp && restarts ? (
            <>
              <Power size={16} aria-hidden />
              {t('c.save_restart')}
            </>
          ) : (
            <>
              {t('c.next')}
              <ChevronRight size={16} aria-hidden />
            </>
          )}
        </button>
      </>,
    )
  else if (step === 'find')
    body = pane(
      t('wiz.find_q'),
      <>
        <p className="muted">{t('wiz.find_h')}</p>
        {where.map((u) => (
          <CopyField key={u} value={u} link />
        ))}
        <p className="small muted">{t('net.find_first')}</p>
      </>,
      <>
        {backBtn}
        <Btn kind="pri" icon={Power} disabled={!cfg} onClick={() => void saveNet()}>
          {t('wiz.find_go')}
        </Btn>
      </>,
    )
  else if (step === 'moving')
    body = pane(
      t('wiz.moving'),
      <>
        <ol className="steps-ol big">
          <li>{t('wiz.mv_1')}</li>
          <li>{t('wiz.mv_2')}</li>
        </ol>
        <p className="muted">{t('wiz.mv_fallback')}</p>
        {(reach ?? where).map((u) => (
          <CopyField key={u} value={u} link />
        ))}
        <p className="warnline">
          <Info size={16} aria-hidden />
          {t('wiz.mv_fail')}
        </p>
      </>,
      null,
    )
  else if (step === 'src') {
    const req = st.pairing?.request
    body = pane(
      t('wiz.src_q'),
      <>
        <div className="choices" role="radiogroup" aria-label={t('wiz.src_q')} onKeyDown={roam} tabIndex={-1}>
          <Choice
            on={src === 'app'}
            first={!src}
            icon={Monitor}
            label={t('prog.app')}
            hint={t('wiz.p_app_h')}
            onPick={() => (setSrc('app'), setSrcErr(null))}
          />
          <Choice
            on={src === 'comp'}
            icon={Radio}
            label={t('wiz.p_comp')}
            hint={t('wiz.p_comp_h')}
            onPick={() => (setSrc('comp'), setSrcErr(null))}
          />
          <Choice
            on={src === 'srv'}
            icon={Code}
            label={t('wiz.p_other')}
            hint={t('wiz.p_other_h')}
            onPick={() => (setSrc('srv'), setSrcErr(null))}
          />
        </div>
        {src === 'app' && (
          <>
            <ol className="steps-ol">
              {t('src.app_steps')
                .split('|')
                .slice(0, 4)
                .map((s, i) => (
                  <li key={i}>
                    {s}
                    {i === 3 && <CopyField value={st.network?.lan?.ip || st.network?.wlan?.ip || location.hostname} />}
                  </li>
                ))}
            </ol>
            {req ? (
              <Notice
                n={{
                  id: 'pair',
                  icon: Link2,
                  lvl: 'pair',
                  title: t('pair.t', { name: st.pairing?.request_name || req, ip: req }),
                  text: t('pair.h'),
                  acts: [
                    {
                      label: t('act.accept'),
                      icon: Check,
                      pri: true,
                      run: () =>
                        a
                          .accept(req, st.pairing?.request_name || req)
                          .then(() => act(() => putConfig({ controller: 'app', host: req }))),
                    },
                    { label: t('act.deny'), icon: X, run: () => a.deny(req) },
                  ],
                }}
              />
            ) : (
              !drawer && (
                <div className="empty">
                  <LoaderCircle size={18} className="spin" aria-hidden />
                  {t((st.pairing?.auto_open ?? !st.pairing?.hosts?.length) ? 'wiz.app_wait_open' : 'wiz.app_wait')}
                </div>
              )
            )}
            {!req && !drawer && waitS >= 60 && (
              <Notice
                n={{
                  id: 'nothing',
                  icon: CircleHelp,
                  lvl: 'info',
                  title: t('src.nothing'),
                  text: [t('src.nothing_1'), t('src.nothing_2'), t('src.nothing_3')].join(' '),
                }}
              />
            )}
          </>
        )}
        {src === 'comp' && (
          <>
            {found === null ? (
              <div className="empty">
                <LoaderCircle size={18} className="spin" aria-hidden />
                {t('src.searching')}
              </div>
            ) : (
              found.length > 0 && (
                <div className="list">
                  {found.map((x) => {
                    const v = x.port === 16622 ? x.host : `${x.host}:${x.port}`
                    const inUse = cfg?.satellite === v
                    return (
                      <div key={v} className="li">
                        <SIcon icon={Radio} tone={inUse ? 'ok' : ''} />
                        <div className="grow">
                          <b>{x.name}</b> <span className="mono muted">{v}</span>
                          {(x.version || x.surfaces) && (
                            <>
                              <br />
                              <span className="small muted">
                                {x.version ? `Companion ${x.version}` : ''}
                                {x.surfaces ? ` · ${t('wiz.surfaces', { n: x.surfaces })}` : ''}
                              </span>
                            </>
                          )}
                        </div>
                        {inUse ? (
                          <span className="tag acc">{t('src.in_use')}</span>
                        ) : (
                          <Btn sm disabled={!!busy} onClick={() => void compConnect(v, '')}>
                            {busy === v ? t('st.connecting') : t('wiz.use', { name: x.name })}
                          </Btn>
                        )}
                      </div>
                    )
                  })}
                </div>
              )
            )}
            {mErr && mErr.to !== addr.trim() && (
              <p className="warnline" role="alert">
                {mErr.m}
              </p>
            )}
            {linked && !drawer && (
              <p className="okline" role="status">
                <Check size={16} aria-hidden />
                {t('wiz.linked', { a: linked })}
              </p>
            )}
            <p className="small muted">{t('wiz.docker')}</p>
            <Field id="w-sat" label={t('src.addr')} hint={t('src.addr_hint')} err={satErr}>
              <div className="inrow">
                <input
                  {...describedBy('w-sat', satErr, true)}
                  className="in mono"
                  value={addr}
                  onChange={(e) => setAddr(e.target.value)}
                  spellCheck={false}
                />
                <Btn disabled={!addr || !!busy} onClick={() => void compConnect(addr.trim(), pin)}>
                  {busy === addr.trim() ? t('st.connecting') : t('src.connect')}
                </Btn>
              </div>
            </Field>
            {addr.startsWith('wss://') && (
              <Field id="w-satpin" label={t('src.pin')} hint={t('src.pin_hint')}>
                <input
                  {...describedBy('w-satpin', null, true)}
                  className="in mono"
                  value={pin}
                  onChange={(e) => setPin(e.target.value)}
                  spellCheck={false}
                />
              </Field>
            )}
          </>
        )}
        {src === 'srv' && (
          <>
            <Field id="w-srv" label={t('src.srv_url')} err={uErr} hint={t('wiz.srv_h')}>
              <div className="inrow">
                <input
                  {...describedBy('w-srv', uErr, true)}
                  className="in mono"
                  placeholder="ws://host:port/deckconnector"
                  value={url}
                  onChange={(e) => (setUrl(e.target.value), setUErr(null))}
                  spellCheck={false}
                />
                <Btn disabled={!url} onClick={() => void srvConnect()}>
                  {t('src.connect')}
                </Btn>
              </div>
            </Field>
            {url.startsWith('wss://') && (
              <Field id="w-srvpin" label={t('src.pin')} hint={t('src.pin_hint')}>
                <input
                  {...describedBy('w-srvpin', null, true)}
                  className="in mono"
                  value={uPin}
                  onChange={(e) => setUPin(e.target.value)}
                  spellCheck={false}
                />
              </Field>
            )}
          </>
        )}
        {drawer && (
          <Notice
            n={{
              id: 'conn',
              icon: CircleCheck,
              lvl: 'ok',
              title: t('wiz.connected', { host: hostLabel(drawer, t) }),
              text: t('wiz.src_switch'),
            }}
          />
        )}
        {srcErr && (
          <p className="err" role="alert">
            {srcErr}
          </p>
        )}
      </>,
      <>
        {backBtn}
        <div className="row">
          <Btn kind="ghost" onClick={() => setStep('done')}>
            {t('wiz.src_later')}
          </Btn>
          {next(t('c.next'), () => (drawer || linked ? setStep('done') : setSrcErr(t('wiz.pick_first'))))}
        </div>
      </>,
    )
  } else
    body = pane(
      drawer ? t('wiz.done_q') : t('wiz.done_q_none'),
      <>
        {drawer ? (
          <Notice
            n={{
              id: 'done',
              icon: CircleCheck,
              lvl: 'ok',
              title: t('wiz.connected', { host: hostLabel(drawer, t) }),
              text:
                decks.length > 1
                  ? t('wiz.done_look_n', { prog: t(`prog.${drawer.kind}`), n: decks.length })
                  : t('wiz.done_look', { prog: t(`prog.${drawer.kind}`) }),
            }}
          />
        ) : linked ? (
          <Notice n={{ id: 'linked', icon: Radio, lvl: 'info', title: t('wiz.linked', { a: linked }) }} />
        ) : (
          <Notice
            n={{
              id: 'none',
              icon: Cable,
              lvl: 'info',
              title: t('wiz.no_src'),
              text: t('wiz.no_src_h', { tab: t('tab.sources') }),
            }}
          />
        )}
        {where.slice(0, 2).map((u) => (
          <CopyField key={u} value={u} link />
        ))}
        {decks[0] && (
          <p className="muted">{chordSentence(t, cfg?.menu_chord ?? 'auto', MODELS[decks[0].kind], fmt.dec(hold))}</p>
        )}
        <p className="small muted">{t('wiz.https_hint')}</p>
      </>,
      <>
        {backBtn}
        {next(t('wiz.go'), () => void finish(), 'w-done')}
      </>,
    )

  const overlay = restarting && step !== 'moving'
  return (
    <>
      <div className="center" inert={overlay}>
        <div className="panelbox wide" role="region" aria-labelledby="wz-t">
          <div className="row">
            <Logo s={40} />
            <div style={{ flex: '1 1 150px', minWidth: 0, overflowWrap: 'anywhere' }}>
              <h1 id="wz-t" style={{ fontSize: 22 }}>
                {t(rerun ? 'wiz.title_rerun' : 'wiz.title')}
              </h1>
              <span className="mono small muted">{cfg?.name}</span>
            </div>
            {step !== 'lang' && (
              <button
                type="button"
                id="wz-lang"
                className="ibtn"
                aria-label={t('pal.lang')}
                title={t('pal.lang')}
                onClick={() => langAtom.set(lang === 'de' ? 'en' : 'de')}
              >
                <Languages size={16} aria-hidden />
                <span>{lang.toUpperCase()}</span>
              </button>
            )}
          </div>
          {onAp && idx <= 2 && step !== 'moving' && (
            <div className="small muted row nowrap">
              <Wifi size={15} className="ic" aria-hidden />
              <span>{t('wiz.ap')}</span>
            </div>
          )}
          <div>
            <ol className="stepper" aria-hidden style={{ gridTemplateColumns: `repeat(${STEPS.length},1fr)` }}>
              {STEPS.map((k, i) => (
                <li key={k} className={i <= idx ? 'on' : ''} />
              ))}
            </ol>
            <p className="small steptxt" aria-live="polite">
              {t('wiz.step', { n: idx + 1, all: STEPS.length, name: t(`wiz.s_${STEPS[idx] ?? 'lang'}`) })}
            </p>
          </div>
          {cfgErr !== null && (
            <p className="warnline" role="alert">
              {t('is.cfg')} {errText(cfgErr, t)}
            </p>
          )}
          {body}
          {idx >= 1 && !['moving', 'src', 'done'].includes(step) && (
            <button type="button" className="linkbtn" onClick={() => void (rerun ? finish() : skip())}>
              {t(rerun ? 'wiz.rerun_cancel' : 'wiz.skip')}
            </button>
          )}
        </div>
      </div>
      {overlay && <RestartOverlay />}
    </>
  )
}
