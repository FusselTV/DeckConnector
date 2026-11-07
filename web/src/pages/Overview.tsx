import {
  Check,
  ChevronDown,
  Clock,
  EthernetPort,
  Hand,
  LayoutGrid,
  MousePointer2,
  Plug,
  Plus,
  Power,
  Radio,
  RefreshCw,
  SlidersHorizontal,
  SquareMenu,
  SunDim,
  Turtle,
  Unplug,
  Usb,
  Wifi,
} from 'lucide-react'
import { memo, useCallback, useEffect, useRef, useState } from 'react'
import { api, pressReal, READONLY } from '../api/client'
import type { InputBody, InputEvent, Screen, Source } from '../api/types'
import { pendingAtom, useActions } from '../app/actions'
import { noteWebPress, retryNow } from '../app/connection'
import { decksOf, deckName, hk, hostLabel, hostName, KIND_ICON, sigWord, stateKey, viaKey } from '../app/derive'
import { useIssues } from '../app/issues'
import { armAtom, ARM_MS, deckSelAtom, nudgeAtom, type Tab } from '../app/state'
import { dispatch, getState, useStore } from '../app/store'
import { useUi } from '../app/ui'
import { Card } from '../components/Card'
import { Help, Led, Seg, SIcon, type Tone } from '../components/controls'
import { Notice, type NoticeData } from '../components/Notice'
import { DeckFront } from '../deck/DeckFront'
import { cx, ls, roam, useLatest } from '../lib/util'

export function Hero() {
  const { t, fmt } = useUi()
  const conn = useStore((s) => s.conn)
  const asOf = useStore((s) => s.asOf)
  const { list, notes } = useIssues()
  const items: NoticeData[] =
    conn === 'offline'
      ? [
          {
            id: 'off',
            icon: Unplug,
            lvl: 'bad',
            title: t('hero.off'),
            text: navigator.onLine ? t('hero.off_sub', { time: asOf ? fmt.time(asOf) : '–' }) : t('hero.off_me'),
            acts: [{ label: t('ban.retry'), icon: RefreshCw, pri: true, run: retryNow }],
          },
        ]
      : [...list, ...notes]
  if (!items.length) return null
  return (
    <section className={cx('notices', conn === 'reconnecting' && 'stale')} aria-labelledby="hero-h">
      <h2 id="hero-h" className="sr" tabIndex={-1}>
        {t('ntc.title')}
      </h2>
      {items.map((n) => (
        <Notice key={n.id} n={n} />
      ))}
    </section>
  )
}

/** Status strip: one line per tile, each a deep link (rules 16, 24). */
function Meter() {
  const { t, fmt, jump } = useUi()
  const conn = useStore((s) => s.conn)
  const st = useStore((s) => s.status)
  const decks = decksOf(st)
  const lan = st.network?.lan
  const wlan = st.network?.wlan
  const slowI = decks.findIndex((d) => d.speed !== 'high')
  const items: {
    k: string
    el: string
    to: Tab
    icon?: typeof Wifi
    tone: Tone
    v: string
    led?: boolean
    state?: string
    sel?: number
  }[] = [
    {
      k: 'meter.conn',
      el: 'card-health',
      to: 'diagnose',
      led: true,
      tone: conn === 'live' ? 'ok' : conn === 'offline' ? 'bad' : 'warn',
      v: st.name ?? '–',
      state: t(`conn.${conn}`),
    },
    {
      k: 'meter.lan',
      el: 'row-lan',
      to: 'network',
      icon: EthernetPort,
      tone: !lan?.cable ? '' : lan.dead ? 'bad' : 'ok',
      v: !lan?.cable ? t('meter.no_cable') : lan.dead ? t('meter.dead') : (lan.ip ?? ''),
    },
    {
      k: 'meter.wlan',
      el: 'row-wlan',
      to: 'network',
      icon: Wifi,
      tone: wlan?.ip ? ((wlan.rssi ?? 0) < -72 ? 'warn' : 'ok') : '',
      v: wlan?.ip ? `${wlan.ssid ?? ''}${wlan.rssi ? ' · ' + t(sigWord(wlan.rssi)) : ''}` : t('meter.off'),
    },
    {
      k: 'meter.deck',
      el: 'card-decks',
      to: 'deck',
      icon: LayoutGrid,
      tone: decks.length ? 'ok' : 'bad',
      v: !decks.length
        ? t('meter.missing')
        : decks.length > 1
          ? t('is.decks_n', { n: decks.length })
          : (decks[0]?.model ?? ''),
    },
    {
      k: 'meter.usb',
      el: 'usb-speed',
      to: 'deck',
      icon: Usb,
      sel: slowI >= 0 ? slowI : undefined,
      tone: !decks.length ? '' : slowI >= 0 ? 'warn' : 'ok',
      v: !decks.length
        ? '–'
        : slowI >= 0 && decks.length > 1
          ? t('meter.slow_n', { n: slowI + 1 })
          : t(slowI >= 0 ? 'meter.full' : 'meter.high'),
    },
    {
      k: 'meter.uptime',
      el: 'card-health',
      to: 'diagnose',
      icon: Clock,
      tone: '',
      v: st.uptime_s !== undefined ? fmt.up(st.uptime_s) : '–',
    },
  ]
  return (
    <ul className="meter">
      {items.map((it) => {
        const I = it.icon
        return (
          <li key={it.k}>
            <button
              type="button"
              aria-label={t('meter.go', {
                k: t(it.k as 'meter.lan'),
                v: it.v + (it.state ? ` · ${it.state}` : ''),
                tab: t(`tab.${it.to}`),
              })}
              title={it.v}
              onClick={() => {
                if (it.sel !== undefined) deckSelAtom.set(it.sel)
                jump(it.to, it.el)
              }}
            >
              <span className="k">
                {it.led ? (
                  <span role="status" aria-live="polite" className="ledwrap">
                    <Led tone={it.tone} pulse={conn === 'reconnecting'} />
                  </span>
                ) : (
                  I && <SIcon icon={I} tone={conn !== 'live' ? '' : it.tone} size={15} />
                )}
                <span className="lbl">{t(it.k as 'meter.lan')}</span>
              </span>
              <span className={cx('v', (it.tone === 'bad' || it.tone === 'warn') && it.tone)}>{it.v}</span>
            </button>
          </li>
        )
      })}
    </ul>
  )
}

function ArmSwitch() {
  const { t } = useUi()
  const armedAt = armAtom.use()
  const nudge = nudgeAtom.use()
  const locked = useStore((s) => s.conn !== 'live' || s.auth !== 'app' || !!s.restarting)
  const drawer = useStore((s) => s.status.sources?.find((x) => x.drawing))
  const [now, setNow] = useState(() => Date.now())
  useEffect(() => {
    if (!armedAt) return
    const id = setInterval(() => setNow(Date.now()), 1000)
    return () => clearInterval(id)
  }, [armedAt])
  const armed = armedAt > 0
  const left = Math.max(0, Math.ceil((armedAt + ARM_MS - now) / 1000))
  const real = drawer ? t('ov.real', { host: hostLabel(drawer, t) }) : t('ov.real_ui')
  return (
    <button
      type="button"
      id="arm"
      role="switch"
      aria-checked={armed}
      aria-describedby="arm-h"
      className={cx('hbtn armpill', armed && 'on', nudge > 0 && 'nudge')}
      title={armed ? real : t('arm.help')}
      onClick={() => {
        armAtom.set(armed ? 0 : Date.now())
        setNow(Date.now())
      }}
      disabled={locked || READONLY}
    >
      <span className="mtrack" aria-hidden />
      <span className="albl">{t('arm.label')}</span>
      <Hand size={15} className="ic aic" aria-hidden />
      {armed && (
        <span className="mono armleft" aria-hidden>
          {left} s
        </span>
      )}
      <span id="arm-h" className="sr">
        {armed ? `${t('arm.on', { s: left })}. ${real}` : `${t('arm.off')}. ${t('arm.help')}`}
      </span>
    </button>
  )
}

const evText = (e: InputEvent) => {
  const n = (e.key ?? 0) + 1
  if (e.kind === 'turn') return `encoder ${n} ${(e.delta ?? 0) > 0 ? '+' : ''}${e.delta ?? 0}`
  if (e.kind === 'encoder') return `encoder ${n} press`
  if (['tap', 'long', 'swipe', 'touch'].includes(e.kind)) return `touch ${e.kind}`
  return `key ${n} ${e.kind}`
}
export const inputText = evText

function LastInput() {
  const { t, jump } = useUi()
  const last = useStore((s) => s.inputs[0])
  if (!last) return null
  const web = last.origin !== 'deck'
  const src = t(web ? 'ov.legend_web' : 'ov.legend_deck')
  return (
    <button
      type="button"
      className={cx('hbtn lastin', web && 'web')}
      title={`${t('ov.last')} · ${src} · ${t('in.open')}`}
      onClick={() => jump('diagnose', 'dia-input')}
    >
      {web ? <MousePointer2 size={14} className="ic" aria-hidden /> : <Hand size={14} className="ic" aria-hidden />}
      <span className="sr">
        {t('ov.last')}, {src}:{' '}
      </span>
      <span className="mono">{evText(last)}</span>
    </button>
  )
}

function DeckPicker() {
  const { t } = useUi()
  const st = useStore((s) => s.status)
  const sel = deckSelAtom.use()
  const decks = decksOf(st)
  if (decks.length < 2) return null
  return (
    <Seg
      id="deck-pick"
      label={t('deck.pick')}
      value={String(sel)}
      onChange={(v) => deckSelAtom.set(Number(v))}
      options={decks.map(
        (d, i) =>
          [
            String(i),
            <span key={i} className="row nowrap" style={{ gap: 4 }}>
              {t('deck.n', { n: i + 1 })}
              {d.speed !== 'high' && (
                <>
                  <Turtle size={15} className="ic slowic" aria-hidden />
                  <span className="sr">{t('deck.slow')}</span>
                </>
              )}
            </span>,
          ] as const,
      )}
    />
  )
}

/** Fetches the deck picture once, then again on every ws `screen`, never two at a time. */
function useScreen(serial: string | undefined) {
  const rev = useStore((s) => s.screenRev)
  const [screen, setScreen] = useState<Screen | null>(null)
  const busy = useRef(false)
  const again = useRef(false)
  useEffect(() => {
    let off = false
    const load = async () => {
      if (busy.current) {
        again.current = true
        return
      }
      busy.current = true
      try {
        const s = await api.screen(serial)
        if (!off) setScreen(s)
      } catch {
        /* the next screen event tries again */
      } finally {
        busy.current = false
        if (again.current && !off) {
          again.current = false
          void load()
        }
      }
    }
    void load()
    return () => {
      off = true
    }
  }, [serial, rev])
  return screen
}

const LiveDeck = memo(function LiveDeck() {
  const { t, act } = useUi()
  const st = useStore((s) => s.status)
  const sel = deckSelAtom.use()
  const armedAt = armAtom.use()
  const bri = useStore((s) => s.config?.brightness ?? 80)
  const last = useStore((s) => s.inputs[0])
  const decks = decksOf(st)
  const deck = decks[Math.min(sel, decks.length - 1)]
  const serial = decks.length > 1 ? deck?.serial : undefined
  const screen = useScreen(deck ? serial : undefined)
  const onLocked = useCallback(() => {
    nudgeAtom.set(Date.now())
    setTimeout(() => nudgeAtom.set(0), 1400)
  }, [])
  const ctx = useLatest({ act, serial, sel })
  const send = useCallback(
    (b: InputBody) => {
      const { act, serial, sel } = ctx.current
      // Every press keeps remote control on for another minute.
      armAtom.set(Date.now())
      const kind =
        'key' in b ? (b.action ?? 'press') : 'encoder' in b ? (b.action === 'turn' ? 'turn' : 'encoder') : b.touch
      const key = 'key' in b ? b.key : 'encoder' in b ? b.encoder : undefined
      noteWebPress(kind === 'press' ? 'down' : kind, key ?? 0)
      dispatch({
        type: 'input',
        ev: { at: Date.now(), origin: 'web', kind, key, delta: 'ticks' in b ? b.ticks : undefined, deck: sel + 1 },
      })
      void act(() => pressReal(b, serial))
    },
    [ctx],
  )
  if (!deck)
    return (
      <div className="nodeck">
        <div>
          <Unplug size={32} aria-hidden />
          <b style={{ color: 'var(--fg)' }}>{t('ov.nodeck')}</b>
          <span className="small">{t('ov.nodeck_h')}</span>
        </div>
      </div>
    )
  return (
    <DeckFront
      key={sel}
      model={deck.kind}
      label={deckName(deck, sel, decks.length, t)}
      screen={screen}
      brightness={bri}
      armed={armedAt > 0 && getState().conn === 'live'}
      last={last}
      onLocked={onLocked}
      send={send}
    />
  )
})

function DeckCard() {
  const { t } = useUi()
  const hasDeck = useStore((s) => decksOf(s.status).length > 0)
  const [open, setOpen] = useState(() => ls.get('dc.deckOpen', !matchMedia('(pointer: coarse)').matches))
  const narrow = matchMedia('(max-width:599px)').matches
  const shut = !open && narrow
  return (
    <Card
      id="card-deck"
      icon={LayoutGrid}
      title={t('ov.deck')}
      className={cx('deckcard', shut && 'shut')}
      right={
        <>
          {!shut && hasDeck && (
            <>
              <LastInput />
              <ArmSwitch />
            </>
          )}
          <button
            type="button"
            className="ibtn ghost deckToggle"
            aria-expanded={open}
            aria-controls="deckbody"
            aria-label={t('ov.toggle_deck')}
            title={t('ov.toggle_deck')}
            onClick={() => {
              setOpen(!open)
              ls.set('dc.deckOpen', !open)
            }}
          >
            <ChevronDown size={18} className={open ? 'chev up' : 'chev'} aria-hidden />
          </button>
        </>
      }
    >
      <div id="deckbody" className="stack" style={{ gap: 12 }} hidden={shut}>
        <DeckPicker />
        <LiveDeck />
        {hasDeck && (
          <div className="lastin-b">
            <LastInput />
          </div>
        )}
      </div>
    </Card>
  )
}

function HostRow({
  s,
  first,
  pending,
  onPick,
}: {
  s: Source
  first: boolean
  pending: string | null
  onPick: (s: Source) => void
}) {
  const { t } = useUi()
  const locked = useStore((x) => x.conn !== 'live')
  const unk = s.kind === 'unknown'
  const bad = s.chosen && !s.connected && !s.connecting
  const dockComp = s.kind === 'companion' && s.via === 'dock'
  const tone: Tone = locked ? '' : bad ? 'bad' : dockComp ? 'warn' : s.drawing || s.connected ? 'ok' : ''
  return (
    <button
      type="button"
      data-roam
      aria-pressed={s.chosen}
      className={cx('host', bad && 'warnrow', s.chosen && 'chosen')}
      tabIndex={first ? 0 : -1}
      aria-disabled={unk || pending ? true : undefined}
      title={unk ? t('st.unknown_hint') : undefined}
      onClick={() => onPick(s)}
    >
      <SIcon icon={KIND_ICON[s.kind]} tone={tone} />
      <span className={cx('nm', (s.kind === 'server' || !s.name) && 'mono')}>{hostName(s)}</span>
      <span className="st">
        <span
          className={cx(
            'tag',
            s.drawing ? 'amber' : bad ? 'bad' : s.connecting || pending === hk(s) ? 'warn' : s.connected ? 'ok' : '',
          )}
        >
          {s.chosen && <Check size={12} className="ic" aria-hidden />}
          {t(stateKey(s, pending))}
        </span>
      </span>
      <span className="sub">
        {s.name && <span className="mono">{s.host} · </span>}
        {t(`prog.${s.kind}`)}
        <span className="hide-s">{!unk && ` · ${t(viaKey(s))}`}</span>
      </span>
    </button>
  )
}

/** "Aktive Quelle": pick who draws (rules 25, 29). */
export function Hosts({ limit }: { limit?: boolean }) {
  const { t, go } = useUi()
  const a = useActions()
  const sources = useStore((s) => s.status.sources) ?? []
  const pending = pendingAtom.use()
  const [all, setAll] = useState(false)
  const list = limit && !all ? sources.filter((s) => s.connected || s.chosen || s.connecting) : sources
  if (!sources.some((s) => s.kind !== 'unknown'))
    return (
      <div className="empty" style={{ margin: 16 }}>
        <Plug size={18} aria-hidden />
        <div>
          {t('ov.who_empty')}{' '}
          <button type="button" className="btn sm" onClick={() => go('sources')}>
            {t('act.sources')}
          </button>
        </div>
      </div>
    )
  const first = Math.max(
    0,
    list.findIndex((s) => s.chosen),
  )
  return (
    // eslint-disable-next-line jsx-a11y/no-noninteractive-element-interactions -- arrow keys move between the row buttons inside
    <div className="hosts" role="group" aria-label={t('ov.who')} onKeyDown={roam}>
      {list.map((s, i) => (
        <HostRow key={hk(s)} s={s} first={i === first} pending={pending} onPick={(x) => void a.pick(x)} />
      ))}
      {limit && (sources.length > list.length || all) && (
        <button type="button" className="btn ghost sm morebtn" onClick={() => setAll(!all)}>
          {all ? t('ov.less') : t('ov.all', { n: sources.length })}
        </button>
      )}
    </div>
  )
}

function BriSlider() {
  const { t, fmt } = useUi()
  const a = useActions()
  const saved = useStore((s) => s.config?.brightness)
  const hasDeck = useStore((s) => decksOf(s.status).length > 0)
  const [v, setV] = useState<number | null>(null)
  const shown = v ?? saved ?? 80
  return (
    <div className="slider">
      <SunDim size={18} aria-hidden />
      <input
        id="bri-ov"
        type="range"
        min={0}
        max={100}
        step={5}
        value={shown}
        aria-label={t('ov.bright')}
        aria-valuetext={fmt.pct(shown)}
        disabled={!hasDeck || saved === undefined}
        onChange={(e) => setV(Number(e.target.value))}
        onPointerUp={() => v !== null && void a.brightness(v).finally(() => setV(null))}
        onKeyUp={() => v !== null && void a.brightness(v).finally(() => setV(null))}
      />
      <output htmlFor="bri-ov">{fmt.pct(shown)}</output>
    </div>
  )
}

function Controls() {
  const { t, fmt } = useUi()
  const a = useActions()
  const st = useStore((s) => s.status)
  const sel = deckSelAtom.use()
  const hold = useStore((s) => s.config?.menu_hold_ms ?? 2000)
  const decks = decksOf(st)
  const deck = decks[Math.min(sel, decks.length - 1)]
  return (
    <Card id="card-ctl" icon={SlidersHorizontal} title={t('ov.controls')}>
      <BriSlider />
      {decks.length > 1 && <p className="small muted">{t('ctl.all', { deck: t('deck.n', { n: sel + 1 }) })}</p>}
      <div className="btnrow3">
        <button
          type="button"
          className="btn"
          disabled={!deck}
          title={t('ov.chord', { a: 1, b: deck?.encoders ?? 4, s: fmt.dec(hold / 1000) })}
          onClick={() => void a.menu(decks.length > 1 ? deck?.serial : undefined)}
        >
          <SquareMenu size={18} aria-hidden />
          <span>{t('ov.menu_s')}</span>
        </button>
        <button type="button" className="btn" title={t('ov.rec_h')} onClick={() => void a.reconnect(decks.length)}>
          <Usb size={18} aria-hidden />
          <span>{t('ov.rec_s')}</span>
        </button>
        <button type="button" className="btn" title={t('ov.reboot_h')} onClick={() => void a.reboot()}>
          <Power size={18} aria-hidden />
          <span>{t('ov.reboot_s')}</span>
        </button>
      </div>
    </Card>
  )
}

export function Overview() {
  const { t } = useUi()
  const multi = useStore((s) => decksOf(s.status).length > 1)
  const narrow = matchMedia('(max-width:599px)').matches
  return (
    <>
      <Hero />
      <div className="ov">
        <Meter />
        <DeckCard />
        <div className="stack sidecol">
          <Card
            id="card-who"
            icon={Radio}
            title={t('ov.who')}
            right={
              <>
                <AddSourceBtn />
                <Help right label={t('ov.who')} text={t('ov.who_help') + (multi ? ` ${t('deck.map')}` : '')} />
              </>
            }
          >
            <div style={{ margin: '-14px -16px' }}>
              <Hosts limit={narrow} />
            </div>
          </Card>
          <Controls />
        </div>
      </div>
    </>
  )
}

function AddSourceBtn() {
  const { t, jump } = useUi()
  return (
    <button
      type="button"
      className="ibtn"
      aria-label={t('src.add')}
      title={t('src.add')}
      onClick={() => jump('sources', 'src-add')}
    >
      <Plus size={16} aria-hidden />
    </button>
  )
}
