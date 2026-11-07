import { Info, KeyRound, Languages, LockOpen, ShieldAlert } from 'lucide-react'
import { useEffect, useState, type SyntheticEvent } from 'react'
import { api, ApiError } from '../api/client'
import { startSession } from '../app/connection'
import { langAtom, loginReasonAtom } from '../app/state'
import { dispatch, useStore } from '../app/store'
import { useUi } from '../app/ui'
import { describedBy, Field } from '../components/controls'
import { PwInput } from '../components/InputAction'
import { Notice } from '../components/Notice'
import { Logo } from '../deck/DeckFront'

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
      if (x instanceof ApiError && (x.status === 429 || x.message === 'locked')) {
        setLock(12)
        setErr(null)
      } else if (x instanceof ApiError && x.status === 0) setErr(t('login.offline'))
      else setErr(t('login.wrong'))
    } finally {
      setBusy(false)
    }
  }
  return { pw, setPw, err, lock, busy, submit }
}

export function Login() {
  const { t, lang } = useUi()
  const reason = loginReasonAtom.use()
  const L = useLogin()
  const plain = location.protocol === 'http:' && !/^(localhost|127\.)/.test(location.hostname)
  return (
    <div className="center">
      <form className="panelbox" onSubmit={(e) => void L.submit(e)}>
        <div className="row">
          <Logo />
          <h1 style={{ fontSize: 22 }}>{t('app.name')}</h1>
          <span className="sp" />
          <button
            type="button"
            className="ibtn"
            aria-label={`${t('acct.lang')}: ${lang === 'de' ? 'English' : 'Deutsch'}`}
            onClick={() => langAtom.set(lang === 'de' ? 'en' : 'de')}
          >
            <Languages size={17} aria-hidden />
            <span className="mono small">{lang.toUpperCase()}</span>
          </button>
        </div>
        <h2 id="login-h" style={{ fontSize: 20 }}>
          {t('login.title')}
        </h2>
        {reason && !L.lock && <Notice n={{ id: 'why', icon: Info, lvl: 'info', title: t(`login.${reason}`) }} />}
        {L.lock > 0 && (
          <Notice n={{ id: 'lock', icon: ShieldAlert, lvl: 'bad', title: t('login.locked', { n: L.lock }) }} />
        )}
        <Field id="lpw" label={t('login.pass')} err={L.err} hint={t('login.hint')}>
          <PwInput
            {...describedBy('lpw', L.err, true)}
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
        {plain && (
          <Notice n={{ id: 'http', icon: KeyRound, lvl: 'info', title: t('login.http'), text: t('login.http_h') }} />
        )}
      </form>
    </div>
  )
}

/** Session ended while the page was open: the status stays visible, changes wait for the password (api-gaps #20). */
export function LoginBar() {
  const { t } = useUi()
  const L = useLogin()
  const expired = useStore((s) => s.auth === 'expired')
  if (!expired) return null
  return (
    <form className="notice warn loginbar" onSubmit={(e) => void L.submit(e)}>
      <b>{t('login.expired_bar')}</b>
      <Field id="lpw-bar" label={t('login.pass')} err={L.err}>
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
