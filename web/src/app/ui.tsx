import { createContext, useCallback, useContext, useEffect, useMemo, useState, type ReactNode } from 'react'
import { ApiError } from '../api/client'
import { makeT, type Lang, type T, type TKey } from '../i18n'
import { makeFmt, type Fmt } from '../lib/format'
import { ls } from '../lib/util'
import { getState } from './store'
import { armAtom, ARM_MS, fromHash, langAtom, tabAtom, themeAtom, toast, type Tab } from './state'

export interface AskOpts {
  title: string
  body?: ReactNode
  ok?: string | null
  danger?: boolean
  typed?: string
}

interface Ui {
  lang: Lang
  t: T
  fmt: Fmt
  go: (tab: Tab) => void
  jump: (tab: Tab, id: string) => void
  ask: (o: AskOpts) => Promise<boolean>
  /** The one guard for every change: refuses offline, after the session ended or while a restart runs. */
  act: <R>(fn: () => Promise<R> | R) => Promise<R | undefined>
}

const Ctx = createContext<Ui | null>(null)
export function useUi() {
  const v = useContext(Ctx)
  if (!v) throw new Error('useUi outside UiProvider')
  return v
}

const flash = (el: Element | null) => {
  const card = el?.closest('.card')
  if (!card) return
  card.classList.remove('flash')
  requestAnimationFrame(() => card.classList.add('flash'))
}

function openTarget(id: string) {
  const el = document.getElementById(id)
  if (!el) return
  for (let p: HTMLElement | null = el; p; p = p.parentElement) if (p instanceof HTMLDetailsElement) p.open = true
  el.scrollIntoView({ block: 'center' })
  const f = el.matches('input,select,button,textarea,summary')
    ? el
    : el.querySelector<HTMLElement>('summary,input,button')
  f?.focus({ preventScroll: true })
  flash(el)
}

export function UiProvider({ children, onAsk }: { children: ReactNode; onAsk: (o: AskOpts) => Promise<boolean> }) {
  const lang = langAtom.use()
  const theme = themeAtom.use()
  const t = useMemo(() => makeT(lang), [lang])
  const fmt = useMemo(() => makeFmt(lang), [lang])

  useEffect(() => {
    document.documentElement.lang = lang
    ls.set('dc.lang', lang)
  }, [lang])
  useEffect(() => {
    if (theme) document.documentElement.dataset.theme = theme
    else delete document.documentElement.dataset.theme
    ls.set('dc.theme', theme)
  }, [theme])
  useEffect(() => {
    const f = () => tabAtom.set(fromHash())
    addEventListener('hashchange', f)
    return () => removeEventListener('hashchange', f)
  }, [])
  const arm = armAtom.use()
  useEffect(() => {
    if (!arm) return
    const id = setTimeout(() => armAtom.set(0), arm + ARM_MS - Date.now())
    const hide = () => document.hidden && armAtom.set(0)
    document.addEventListener('visibilitychange', hide)
    return () => {
      clearTimeout(id)
      document.removeEventListener('visibilitychange', hide)
    }
  }, [arm])

  const go = useCallback((k: Tab) => {
    if (k !== fromHash()) location.hash = k
    tabAtom.set(k)
    window.scrollTo(0, 0)
  }, [])
  const jump = useCallback(
    (k: Tab, id: string) => {
      go(k)
      setTimeout(() => openTarget(id), 60)
    },
    [go],
  )
  const act = useCallback(
    async <R,>(fn: () => Promise<R> | R): Promise<R | undefined> => {
      const s = getState()
      const refuse = s.auth === 'expired' ? t('login.need') : s.conn !== 'live' || s.restarting ? t('c.not_sent') : null
      if (refuse) {
        toast(refuse, { bad: true })
        return undefined
      }
      try {
        return await fn()
      } catch (e) {
        toast(errText(e, t), { bad: true })
        return undefined
      }
    },
    [t],
  )
  const v = useMemo<Ui>(() => ({ lang, t, fmt, go, jump, ask: onAsk, act }), [lang, t, fmt, go, jump, onAsk, act])
  return <Ctx value={v}>{children}</Ctx>
}

/** The firmware's `code` in the page's language; its English text for a code this page does not know. */
export function codeText(t: T, code: string | undefined, english: string) {
  const k = `err.${code}`
  const s = code ? t(k as TKey) : k
  return s === k ? english : s
}

/** Words an error for a person: the firmware's reason where it gave one, never a proxy page or a script error. */
export function errText(e: unknown, t: T) {
  if (!(e instanceof ApiError)) return t('c.err_page')
  if (e.code === 'timeout') return t('c.timeout')
  if (e.status === 0) return t('c.not_sent')
  const m = codeText(t, e.code, e.message)
  return m ? t('c.refused', { m }) : t('c.err_http', { n: e.status })
}

export function useTheme() {
  const theme = themeAtom.use()
  const [sysLight, setSysLight] = useState(() => matchMedia('(prefers-color-scheme: light)').matches)
  useEffect(() => {
    const mq = matchMedia('(prefers-color-scheme: light)')
    const f = (e: MediaQueryListEvent) => setSysLight(e.matches)
    mq.addEventListener('change', f)
    return () => mq.removeEventListener('change', f)
  }, [])
  return theme ?? (sysLight ? 'light' : 'dark')
}
