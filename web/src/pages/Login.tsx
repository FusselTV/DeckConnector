import {
  Fingerprint,
  Info,
  KeyRound,
  Languages,
  LockOpen,
  MonitorSmartphone,
  RotateCcw,
  ShieldAlert,
} from 'lucide-react'
import { useCallback, useEffect, useState, type SyntheticEvent } from 'react'
import { api, ApiError } from '../api/client'
import { startSession } from '../app/connection'
import { langAtom, loginReasonAtom } from '../app/state'
import { dispatch, useStore } from '../app/store'
import { errText, useUi } from '../app/ui'
import { Btn, describedBy, Field } from '../components/controls'
import { PwInput } from '../components/InputAction'
import { Notice } from '../components/Notice'
import { Logo } from '../deck/DeckFront'
import { deviceLabel, ls } from '../lib/util'

function useLogin() {
  const { t } = useUi()
  const [pw, setPw] = useState('')
  const [err, setErr] = useState<string | null>(null)
  const [lock, setLock] = useState(0)
  const [busy, setBusy] = useState(false)
  useEffect(() => {
    if (!lock) return
    const id = setTimeout(() => setLock(lock - 1), 1000)
    return () => clearTimeout(id)
  }, [lock])
  const submit = async (e: SyntheticEvent) => {
    e.preventDefault()
    if (!pw || lock || busy) return
    setBusy(true)
    try {
      await api.login(pw)
      setPw('')
      setErr(null)
      loginReasonAtom.set(null)
      dispatch({ type: 'auth', auth: 'app' })
      startSession()
    } catch (x) {
      if (x instanceof ApiError && (x.status === 429 || x.code === 'locked' || x.message === 'locked')) {
        setLock(x.retry || 12)
        setErr(null)
      } else if (x instanceof ApiError && x.status === 0 && x.code !== 'timeout') setErr(t('login.offline'))
      else if (
        x instanceof ApiError &&
        (x.status === 401 || x.code === 'wrong_password' || x.message === 'wrong password')
      )
        setErr(t('login.wrong'))
      else setErr(errText(x, t))
    } finally {
      setBusy(false)
    }
  }
  return { pw, setPw, err, lock, busy, submit }
}

function useCountdown() {
  const [left, setLeft] = useState(0)
  useEffect(() => {
    if (!left) return
    const id = setTimeout(() => setLeft(left - 1), 1000)
    return () => clearTimeout(id)
  }, [left])
  return [left, setLeft] as const
}

/** Someone at the deck lets this browser in: the page asks, then polls until the deck answers. */
function useDeckLogin(why: (x: unknown) => void, done: () => void) {
  const { t } = useUi()
  const [id, setId] = useState<string | null>(null)
  const [left, setLeft] = useState(0)
  const [msg, setMsg] = useState<string | null>(null)
  useEffect(() => {
    if (!id) return
    let fails = 0
    const tick = setInterval(() => {
      api.loginState(id).then(
        (r) => {
          fails = 0
          setLeft(r.s)
          if (r.state === 'pending') return
          setId(null)
          if (r.state === 'accepted') done()
          else setMsg(t(r.state === 'denied' ? 'login.req_denied' : 'login.req_expired'))
        },
        (x: unknown) => {
          // A restart forgets the request (404); a few lost polls in a row mean the device is gone.
          if (!(x instanceof ApiError && x.status === 404) && ++fails < 5) return
          setId(null)
          setMsg(x instanceof ApiError && x.status === 404 ? t('login.req_gone') : errText(x, t))
        },
      )
    }, 1000)
    return () => clearInterval(tick)
  }, [id, done, t])
  const start = () => {
    setMsg(null)
    api.askLogin(deviceLabel(navigator.userAgent)).then((r) => {
      setLeft(r.s || 60)
      setId(r.id)
    }, why)
  }
  const cancel = () => {
    if (id) void api.cancelLogin(id).catch(() => undefined)
    setId(null)
  }
  return { waiting: !!id, left, msg, start, cancel }
}

/** No password at hand: the deck lets this browser in, shows the password, or confirms a reset. */
function Forgot() {
  const { t, ask } = useUi()
  const [shown, setShown] = useCountdown()
  const [reset, setReset] = useCountdown()
  const [err, setErr] = useState<string | null>(null)
  const why = (x: unknown) => {
    setErr(x instanceof ApiError && x.status === 409 ? t('login.no_deck') : t('login.offline'))
  }
  const done = useCallback(() => {
    loginReasonAtom.set(null)
    dispatch({ type: 'auth', auth: 'app' })
    startSession()
  }, [])
  const L = useDeckLogin(why, done)
  const show = () => {
    setErr(null)
    api.showPassword().then(() => setShown(30), why)
  }
  const askReset = async () => {
    setErr(null)
    if (!(await ask({ title: t('login.reset_t'), body: t('login.reset_b'), ok: t('login.reset_go'), danger: true })))
      return
    api.askReset().then((r) => setReset(r.s || 60), why)
  }
  return (
    <div className="forgot">
      {L.waiting ? (
        <Notice
          n={{
            id: 'req',
            icon: Fingerprint,
            lvl: 'info',
            title: t('login.req_wait', { s: L.left }),
            text: t('login.req_wait_h'),
            acts: [{ label: t('c.cancel'), run: L.cancel }],
          }}
        />
      ) : reset > 0 ? (
        <Notice
          n={{
            id: 'reset',
            icon: RotateCcw,
            lvl: 'warn',
            title: t('login.reset_wait', { s: reset }),
            text: t('login.reset_wait_h'),
            acts: [
              {
                label: t('c.cancel'),
                run: () =>
                  void api.cancelReset().then(
                    () => setReset(0),
                    (x: unknown) => setErr(errText(x, t)),
                  ),
              },
            ],
          }}
        />
      ) : (
        <>
          <Btn icon={Fingerprint} onClick={() => (setErr(null), L.start())}>
            {t('login.req')}
          </Btn>
          <Btn icon={MonitorSmartphone} onClick={show} disabled={shown > 0}>
            {shown > 0 ? t('login.shown', { s: shown }) : t('login.show')}
          </Btn>
          <button type="button" className="linkbtn" onClick={() => void askReset()}>
            {t('login.forgot')}
          </button>
        </>
      )}
      {L.msg && (
        <p className="warnline" role="status">
          {L.msg}
        </p>
      )}
      {err && (
        <p className="warnline" role="alert">
          {err}
        </p>
      )}
    </div>
  )
}

export function Login() {
  const { t, lang } = useUi()
  const reason = loginReasonAtom.use()
  const L = useLogin()
  const [httpOk, setHttpOk] = useState(() => ls.get('dc.httpOk', false))
  const plain = location.protocol === 'http:' && !/^(localhost|127\.)/.test(location.hostname) && !httpOk
  return (
    <div className="center">
      <div className="panelbox login">
        <button
          type="button"
          className="ibtn langbtn"
          aria-label={`${t('acct.lang')}: ${lang === 'de' ? 'English' : 'Deutsch'}`}
          onClick={() => langAtom.set(lang === 'de' ? 'en' : 'de')}
        >
          <Languages size={17} aria-hidden />
          <span className="mono small">{lang.toUpperCase()}</span>
        </button>
        <div className="brand">
          <Logo s={52} />
          <h1>{t('app.name')}</h1>
        </div>
        <form aria-label={t('login.title')} onSubmit={(e) => void L.submit(e)}>
          {reason && !L.lock && <Notice n={{ id: 'why', icon: Info, lvl: 'info', title: t(`login.${reason}`) }} />}
          {L.lock > 0 && (
            <Notice n={{ id: 'lock', icon: ShieldAlert, lvl: 'bad', title: t('login.locked', { n: L.lock }) }} />
          )}
          <Field id="lpw" label={t('wiz.s_pw')} err={L.err}>
            <PwInput
              {...describedBy('lpw', L.err)}
              value={L.pw}
              onChange={(e) => L.setPw(e.target.value)}
              autoComplete="current-password"
              // eslint-disable-next-line jsx-a11y/no-autofocus -- the login page has one field
              autoFocus
            />
          </Field>
          <button type="submit" className="btn pri" disabled={!L.pw || L.lock > 0 || L.busy}>
            <LockOpen size={16} aria-hidden />
            {t('login.btn')}
          </button>
        </form>
        <Forgot />
        {plain && (
          <Notice
            n={{
              id: 'http',
              icon: KeyRound,
              lvl: 'info',
              title: t('login.http'),
              text: t('login.http_h'),
              acts: [
                {
                  label: t('is.hide'),
                  ghost: true,
                  run: () => {
                    ls.set('dc.httpOk', true)
                    setHttpOk(true)
                  },
                },
              ],
            }}
          />
        )}
      </div>
    </div>
  )
}

/** Session ended while the page was open: the status stays visible, changes wait for the password. */
export function LoginBar() {
  const { t } = useUi()
  const L = useLogin()
  const expired = useStore((s) => s.auth === 'expired')
  if (!expired) return null
  return (
    <form className="notice warn loginbar" onSubmit={(e) => void L.submit(e)}>
      <b>{t('login.expired_bar')}</b>
      <Field id="lpw-bar" label={t('wiz.s_pw')} err={L.err}>
        <div className="inrow">
          <input
            {...describedBy('lpw-bar', L.err)}
            className="in"
            type="password"
            value={L.pw}
            onChange={(e) => L.setPw(e.target.value)}
            autoComplete="current-password"
          />
          <button type="submit" className="btn pri" disabled={!L.pw || L.lock > 0 || L.busy}>
            {t('login.btn')}
          </button>
        </div>
      </Field>
    </form>
  )
}
