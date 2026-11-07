import {
  Cable,
  Cpu,
  Languages,
  LayoutDashboard,
  LayoutGrid,
  LoaderCircle,
  LogOut,
  Menu,
  Moon,
  Network as NetIcon,
  Power,
  RefreshCw,
  Search,
  ShieldCheck,
  Stethoscope,
  Sun,
  Unplug,
  X,
  type LucideIcon,
} from 'lucide-react'
import { Component, useEffect, useRef, useState, type KeyboardEvent, type ReactNode } from 'react'
import { Notice } from '../components/Notice'
import { SaveBar, useDirtyTabs } from '../components/SaveBar'
import { Seg } from '../components/controls'
import { Logo } from '../deck/DeckFront'
import { mmss } from '../lib/format'
import { Access } from '../pages/Access'
import { DeckTab } from '../pages/DeckTab'
import { Diagnose } from '../pages/Diagnose'
import { Firmware } from '../pages/Firmware'
import { Network } from '../pages/Network'
import { Overview } from '../pages/Overview'
import { Sources } from '../pages/Sources'
import { LoginBar } from '../pages/Login'
import { Palette } from '../pages/Palette'
import { useActions } from './actions'
import { clearOta, retryNow } from './connection'
import { useIssues } from './issues'
import { langAtom, tabAtom, TABS, themeAtom, type Tab } from './state'
import { useStore } from './store'
import { useTheme, useUi } from './ui'
import { Dialog } from '../components/Dialog'

const TAB_META: Record<Tab, { icon: LucideIcon; group: 'run' | 'device' | 'care' }> = {
  overview: { icon: LayoutDashboard, group: 'run' },
  sources: { icon: Cable, group: 'run' },
  deck: { icon: LayoutGrid, group: 'device' },
  network: { icon: NetIcon, group: 'device' },
  access: { icon: ShieldCheck, group: 'device' },
  firmware: { icon: Cpu, group: 'care' },
  diagnose: { icon: Stethoscope, group: 'care' },
}
const GROUPS = ['run', 'device', 'care'] as const
const MOBILE: Tab[] = ['overview', 'sources', 'diagnose']
const PANELS: Record<Tab, () => React.JSX.Element> = {
  overview: Overview,
  sources: Sources,
  deck: DeckTab,
  network: Network,
  access: Access,
  firmware: Firmware,
  diagnose: Diagnose,
}

function useBadges(): Partial<Record<Tab, number>> {
  const { list } = useIssues()
  const offline = useStore((s) => s.conn === 'offline')
  const pair = useStore((s) => !!s.status.pairing?.request)
  if (offline) return {}
  return { overview: list.filter((i) => i.id !== 'pair').length, sources: pair ? 1 : 0 }
}

function Nav() {
  const { t, go } = useUi()
  const tab = tabAtom.use()
  const dirty = useDirtyTabs()
  const b = useBadges()
  const refs = useRef<Partial<Record<Tab, HTMLButtonElement | null>>>({})
  const onKey = (e: KeyboardEvent) => {
    const i = TABS.indexOf(tab)
    const j = {
      ArrowDown: (i + 1) % TABS.length,
      ArrowUp: (i + TABS.length - 1) % TABS.length,
      Home: 0,
      End: TABS.length - 1,
    }[e.key]
    if (j === undefined) return
    e.preventDefault()
    const k = TABS[j] as Tab
    go(k)
    refs.current[k]?.focus()
  }
  return (
    <div
      className="nav"
      role="tablist"
      aria-orientation="vertical"
      aria-label="Navigation"
      onKeyDown={onKey}
      tabIndex={-1}
    >
      {GROUPS.map((g) => (
        <div key={g} style={{ display: 'contents' }}>
          <span className="lbl navgrp" aria-hidden>
            {t(`grp.${g}`)}
          </span>
          {TABS.filter((k) => TAB_META[k].group === g).map((k) => {
            const I = TAB_META[k].icon
            const n = b[k] ?? 0
            const d = dirty.includes(k)
            return (
              <button
                key={k}
                ref={(el) => {
                  refs.current[k] = el
                }}
                type="button"
                role="tab"
                id={`tab-${k}`}
                aria-selected={tab === k}
                aria-controls="tabpanel"
                aria-label={[
                  t(`tab.${k}`),
                  n ? t('nav.badge', { n, tab: '' }).replace(/^,\s*/, '') : '',
                  d ? t('save.dirty') : '',
                ]
                  .filter(Boolean)
                  .join(', ')}
                tabIndex={tab === k ? 0 : -1}
                onClick={() => go(k)}
              >
                <I size={20} aria-hidden />
                <span>{t(`tab.${k}`)}</span>
                {d && <span className="ddot" aria-hidden />}
                {n > 0 && (
                  <span className="cnt" aria-hidden>
                    {n}
                  </span>
                )}
              </button>
            )
          })}
        </div>
      ))}
    </div>
  )
}

function ThemeLang() {
  const { t, lang } = useUi()
  const eff = useTheme()
  return (
    <>
      <button
        type="button"
        className="ibtn"
        aria-label={`${t('acct.lang')}: ${lang === 'de' ? 'English' : 'Deutsch'}`}
        title={t('acct.lang')}
        onClick={() => langAtom.set(lang === 'de' ? 'en' : 'de')}
      >
        <Languages size={17} aria-hidden />
        <span className="mono small">{lang.toUpperCase()}</span>
      </button>
      <button
        type="button"
        className="ibtn"
        aria-label={eff === 'dark' ? t('hdr.to_light') : t('hdr.to_dark')}
        title={eff === 'dark' ? t('hdr.to_light') : t('hdr.to_dark')}
        onClick={() => themeAtom.set(eff === 'dark' ? 'light' : 'dark')}
      >
        {eff === 'dark' ? <Sun size={17} aria-hidden /> : <Moon size={17} aria-hidden />}
      </button>
    </>
  )
}

function SideFoot() {
  const { t } = useUi()
  const a = useActions()
  return (
    <div className="sidefoot">
      <ThemeLang />
      <button
        type="button"
        className="ibtn"
        aria-label={t('hdr.logout')}
        title={t('hdr.logout')}
        onClick={() => void a.logout()}
      >
        <LogOut size={17} aria-hidden />
      </button>
    </div>
  )
}

function TabBar({ onMore }: { onMore: () => void }) {
  const { t, go } = useUi()
  const tab = tabAtom.use()
  const dirty = useDirtyTabs()
  const b = useBadges()
  return (
    <nav className="tabbar" aria-label="Navigation">
      {MOBILE.map((k) => {
        const I = TAB_META[k].icon
        const n = b[k] ?? 0
        return (
          <button
            key={k}
            type="button"
            aria-current={tab === k ? 'page' : undefined}
            aria-label={n ? t('nav.badge', { n, tab: t(`tab.${k}`) }) : undefined}
            onClick={() => go(k)}
          >
            <I size={22} aria-hidden />
            <span>{t(`tab.${k}`)}</span>
            {dirty.includes(k) && <span className="ddot" aria-hidden />}
            {n > 0 && (
              <span className="cnt" aria-hidden>
                {n}
              </span>
            )}
          </button>
        )
      })}
      <button
        type="button"
        aria-current={!MOBILE.includes(tab) ? 'page' : undefined}
        aria-haspopup="dialog"
        onClick={onMore}
      >
        <Menu size={22} aria-hidden />
        <span>{t('tab.more')}</span>
        {dirty.some((k) => !MOBILE.includes(k)) && <span className="ddot" aria-hidden />}
      </button>
    </nav>
  )
}

function MoreSheet({ close }: { close: () => void }) {
  const { t, go, lang } = useUi()
  const a = useActions()
  const tab = tabAtom.use()
  const dirty = useDirtyTabs()
  const eff = useTheme()
  const pick = (k: Tab) => {
    close()
    go(k)
  }
  return (
    <Dialog
      className="modal sheet"
      title={t('tab.more')}
      titleClass="sheett"
      close={close}
      head={(h) => (
        <div className="sb sbhead">
          <div className="row">
            <span className="grab" aria-hidden />
          </div>
          <div className="row">
            {h}
            <span className="sp" />
            <button type="button" className="ibtn ghost" aria-label={t('c.close')} onClick={close}>
              <X size={18} aria-hidden />
            </button>
          </div>
        </div>
      )}
    >
      <div className="sb">
        <div className="sheetgrid">
          {GROUPS.map((g) => {
            const xs = TABS.filter((k) => TAB_META[k].group === g && !MOBILE.includes(k))
            return xs.length ? (
              <div key={g}>
                <span className="lbl" style={{ margin: '6px 10px 2px', display: 'block' }}>
                  {t(`grp.${g}`)}
                </span>
                {xs.map((k) => {
                  const I = TAB_META[k].icon
                  return (
                    <button
                      key={k}
                      type="button"
                      className="tabbtn"
                      aria-current={tab === k ? 'page' : undefined}
                      onClick={() => pick(k)}
                    >
                      <I size={20} aria-hidden />
                      {t(`tab.${k}`)}
                      {dirty.includes(k) && <span className="ddot" title={t('save.dirty')} />}
                    </button>
                  )
                })}
              </div>
            ) : null
          })}
        </div>
        <div className="acctsheet">
          <div className="field">
            <span className="flabel">{t('acct.lang')}</span>
            <Seg
              id="m-lang"
              label={t('acct.lang')}
              value={lang}
              onChange={(v) => langAtom.set(v)}
              options={[
                ['de', 'Deutsch'],
                ['en', 'English'],
              ]}
            />
          </div>
          <div className="field">
            <span className="flabel">{t('acct.theme')}</span>
            <Seg
              id="m-theme"
              label={t('acct.theme')}
              value={eff}
              onChange={(v) => themeAtom.set(v)}
              options={[
                ['light', t('acct.light'), Sun],
                ['dark', t('acct.dark'), Moon],
              ]}
            />
          </div>
          <div>
            <button
              type="button"
              className="btn"
              onClick={() => {
                close()
                void a.logout()
              }}
            >
              <LogOut size={16} aria-hidden />
              {t('hdr.logout')}
            </button>
          </div>
        </div>
      </div>
    </Dialog>
  )
}

/** A reply in a shape this page does not expect breaks one tab, never the whole page. */
class Guard extends Component<{ text: string; again: string; children: ReactNode }, { failed: boolean }> {
  state = { failed: false }
  static getDerivedStateFromError() {
    return { failed: true }
  }
  render() {
    if (!this.state.failed) return this.props.children
    return (
      <div className="empty" role="alert">
        {this.props.text}
        <button type="button" className="btn sm" onClick={() => this.setState({ failed: false })}>
          {this.props.again}
        </button>
      </div>
    )
  }
}

function Banners() {
  const { t, fmt, go } = useUi()
  const tab = tabAtom.use()
  const conn = useStore((s) => s.conn)
  const asOf = useStore((s) => s.asOf)
  const ota = useStore((s) => s.ota)
  const expired = useStore((s) => s.auth === 'expired')
  const { list } = useIssues()
  const show = list.filter((i) => !(tab === 'sources' && i.id === 'pair'))
  const first = show[0]
  return (
    <div className="banners">
      {expired && <LoginBar />}
      {conn === 'offline' && tab !== 'overview' && (
        <Notice
          n={{
            id: 'off',
            icon: Unplug,
            lvl: 'bad',
            title: t('hero.off'),
            text: !navigator.onLine ? t('hero.off_me') : t('ban.offline', { time: asOf ? fmt.time(asOf) : '–' }),
            acts: [{ label: t('ban.retry'), icon: RefreshCw, run: retryNow }],
          }}
        />
      )}
      {conn === 'reconnecting' && (
        <Notice
          n={{
            id: 'rec',
            icon: RefreshCw,
            lvl: 'warn',
            title: t('conn.reconnecting'),
            text: t('hero.stale', { time: asOf ? fmt.time(asOf) : '–' }),
          }}
        />
      )}
      {ota && (
        <Notice
          n={{
            id: 'ota',
            icon: Cpu,
            lvl: ota.state === 'error' ? 'bad' : 'info',
            title: t('ban.update', {
              target: t(`tgt.${ota.target}` as 'tgt.p4'),
              state: ota.error ?? t(`ota.${ota.state}` as 'ota.done'),
            }),
            extra: ota.state !== 'error' && (
              <span className="row nprog">
                <span className="bar">
                  <i style={{ width: `${Math.max(0, Math.min(100, ota.pct))}%` }} />
                </span>
                <span className="mono">{fmt.pct(Math.max(0, Math.min(100, ota.pct)))}</span>
              </span>
            ),
            acts: ota.state === 'error' ? [{ label: t('c.close'), icon: X, run: clearOta }] : undefined,
          }}
        />
      )}
      {first && conn !== 'offline' && tab !== 'overview' && (
        <Notice
          n={{
            ...first,
            acts: [
              ...(first.acts ?? []).slice(0, 2),
              ...(show.length > 1
                ? [{ label: t('hero.more', { n: show.length - 1 }), ghost: true, run: () => go('overview') }]
                : []),
            ],
          }}
        />
      )}
    </div>
  )
}

function RestartOverlay() {
  const { t } = useUi()
  const r = useStore((s) => s.restarting)
  const [el, setEl] = useState(0)
  useEffect(() => {
    if (!r) return
    const id = setInterval(() => setEl(Math.floor((Date.now() - r.t0) / 1000)), 250)
    return () => clearInterval(id)
  }, [r])
  if (!r) return null
  return (
    <div className="overlay" role="alertdialog" aria-modal="true" aria-labelledby="rs-t">
      <div className="ovbox">
        <div className="row">
          <Power size={22} aria-hidden />
          <h2 id="rs-t">{t('rs.title')}</h2>
        </div>
        <div className="big" aria-hidden>
          {mmss(el)}
        </div>
        <div className="bar indet" style={{ flex: 'none' }}>
          <i />
        </div>
        <p>{t('rs.body')}</p>
        {r.addr && (
          <a className="in linkbox" href={r.addr}>
            {r.addr}
          </a>
        )}
        <p className="row small muted">
          <LoaderCircle size={15} className="spin" aria-hidden />
          {t('rs.wait')}
        </p>
        {el > 150 && <p className="small muted">{t('rs.timeout')}</p>}
      </div>
    </div>
  )
}

export function Shell() {
  const { t, go } = useUi()
  const tab = tabAtom.use()
  const name = useStore((s) => s.status.name)
  const fw = useStore((s) => s.status.firmware)
  const stale = useStore((s) => s.conn !== 'live')
  const restarting = useStore((s) => !!s.restarting)
  const [pal, setPal] = useState(false)
  const [more, setMore] = useState(false)
  useEffect(() => {
    const f = (e: globalThis.KeyboardEvent) => {
      if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'k') {
        e.preventDefault()
        if (!restarting) setPal((p) => !p)
      }
    }
    addEventListener('keydown', f)
    return () => removeEventListener('keydown', f)
  }, [restarting])
  // A visited tab stays mounted (hidden), so its unsaved draft survives a tab switch (rule 4).
  const [seen, setSeen] = useState<Tab[]>([tab])
  if (!seen.includes(tab)) setSeen([...seen, tab])
  return (
    <>
      <a className="skip" href="#panel">
        {t('c.skip')}
      </a>
      <div className="shell" inert={restarting}>
        <aside className="side">
          <button
            type="button"
            className="brand home"
            aria-label={`${t('app.name')} · ${t('tab.overview')}`}
            onClick={() => go('overview')}
          >
            <Logo />
            <div>
              <b>{t('app.name')}</b>
              <small className="mono">{fw}</small>
            </div>
          </button>
          <button
            type="button"
            className="ibtn search sidesearch"
            aria-label={t('hdr.search')}
            aria-keyshortcuts="Control+K"
            onClick={() => setPal(true)}
          >
            <Search size={16} aria-hidden />
            <span className="stxt">{t('hdr.search')}</span>
            <kbd>Ctrl K</kbd>
          </button>
          <Nav />
          <SideFoot />
        </aside>
        <div className="main">
          <header className="top mtop">
            <div className="mtt">
              <h1>{t(`tab.${tab}`)}</h1>
              <small>{name}</small>
            </div>
            <div className="tools">
              <button type="button" className="ibtn" aria-label={t('hdr.search')} onClick={() => setPal(true)}>
                <Search size={17} aria-hidden />
              </button>
            </div>
          </header>
          <main className="content" id="panel" tabIndex={-1}>
            <Banners />
            <div
              id="tabpanel"
              role="tabpanel"
              aria-labelledby={`tab-${tab}`}
              className={stale ? 'stack stale' : 'stack'}
            >
              {seen.map((k) => {
                const P = PANELS[k]
                return (
                  <div key={k} className="stack" hidden={k !== tab}>
                    <h2 className="sr">{t(`tab.${k}`)}</h2>
                    <Guard text={t('c.err_page')} again={t('ban.retry')}>
                      <P />
                    </Guard>
                  </div>
                )
              })}
            </div>
            <SaveBar />
          </main>
        </div>
        <TabBar onMore={() => setMore(true)} />
      </div>
      <RestartOverlay />
      {pal && <Palette close={() => setPal(false)} />}
      {more && <MoreSheet close={() => setMore(false)} />}
    </>
  )
}
