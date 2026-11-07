import {
  Bug,
  Check,
  CircleHelp,
  DatabaseBackup,
  FileBadge,
  FileUp,
  Gauge,
  Globe,
  Hand,
  Hash,
  Key,
  KeyRound,
  Languages,
  Layers,
  LayoutGrid,
  Link2,
  LogOut,
  MemoryStick,
  Moon,
  Power,
  Radio,
  ScrollText,
  Search,
  Server,
  ShieldCheck,
  Sparkles,
  SquareMenu,
  SunDim,
  Tag,
  Upload,
  Usb,
  Wifi,
  Wrench,
  X,
} from 'lucide-react'
import { useEffect, useMemo, useRef, useState, type KeyboardEvent } from 'react'
import { putConfig, useActions } from '../app/actions'
import { decksOf, deckName, hostLabel, KIND_ICON, stateKey } from '../app/derive'
import { deckSelAtom, langAtom, TABS, themeAtom, wifiScanAtom, type Tab } from '../app/state'
import { useStore } from '../app/store'
import { useTheme, useUi } from '../app/ui'
import { Dialog } from '../components/Dialog'
import type { Key as I18nKey } from '../i18n/types'
import { fold, rank, remember, type Cmd, type Pick } from '../lib/rank'
import { cx, ls } from '../lib/util'
import { groupScan } from '../lib/wifi'

const TAB_KW: Record<Tab, I18nKey> = {
  overview: 'kw.overview',
  sources: 'kw.sources',
  deck: 'kw.deck',
  network: 'kw.network',
  access: 'kw.access',
  firmware: 'kw.firmware',
  diagnose: 'kw.diagnose',
}

function useCommands(): Cmd[] {
  const { t, go, jump, lang, ask } = useUi()
  const a = useActions()
  const eff = useTheme()
  const st = useStore((s) => s.status)
  const scan = wifiScanAtom.use()
  return useMemo(() => {
    const to = (
      id: string,
      label: string,
      kw: string,
      icon: Cmd['icon'],
      tab: Tab,
      el: string,
      detail?: I18nKey,
    ): Cmd => ({
      id,
      group: 'pal.set',
      label,
      kw,
      icon,
      hint: t(`tab.${tab}`),
      detail: detail && t(detail),
      run: () => jump(tab, el),
    })
    const decks = decksOf(st)
    const P = st.pairing
    const cmds: Cmd[] = [
      ...TABS.map((k): Cmd => ({
        id: `go-${k}`,
        group: 'pal.go',
        label: t(`tab.${k}`),
        kw: t(TAB_KW[k]),
        run: () => go(k),
      })),
      to('s-bri', t('ov.bright'), `${t('kw.bri')} helligkeit dimmen`, SunDim, 'overview', 'bri-ov'),
      ...[25, 50, 75, 100].map((n): Cmd => ({
        id: `bri${n}`,
        group: 'pal.act',
        label: t('pal.bri', { n }),
        kw: t('kw.bri'),
        icon: SunDim,
        mut: true,
        run: () => a.brightness(n),
      })),
      to('s-who', t('ov.who'), 'aktive quelle active source', Radio, 'overview', 'card-who', 'ov.who_help'),
      to('s-wlan', t('wifi.list'), t('kw.wlan'), Wifi, 'network', 'wifi-q'),
      to('s-static', t('net.static'), t('kw.static'), Hash, 'network', 'lan-dhcp', 'net.cidr'),
      to('s-name', t('net.name'), 'name hostname mdns local', Tag, 'network', 'name', 'net.find_first'),
      to('s-pw', t('access.pw_new'), t('kw.pw'), KeyRound, 'access', 'npw'),
      to('s-https', t('access.https_only'), 'https tls ssl', ShieldCheck, 'access', 'https', 'access.https_only_h'),
      to('s-fp', t('access.fp'), t('kw.fp'), KeyRound, 'access', 'fp', 'access.fp_h'),
      to('s-token', `${t('access.tokens')} · ${t('access.tcreate')}`, t('kw.token'), Key, 'access', 'tok-add', 'api.h'),
      to('s-cors', t('access.cors'), 'cors origin', Globe, 'access', 'cors', 'access.cors_h'),
      to('s-cert', t('cert.upload'), 'zertifikat certificate pem tls', FileBadge, 'access', 'card-https', 'cert.h'),
      to('s-sat', t('src.addr'), t('kw.sat'), Radio, 'sources', 'card-src'),
      to(
        's-pin',
        t('src.pin'),
        'pin fingerprint zertifikat certificate sha256 wss',
        KeyRound,
        'sources',
        'card-src',
        'src.pin_help',
      ),
      to(
        's-trouble',
        t('src.trouble'),
        'companion fehler error port docker caddy proxy',
        Wrench,
        'sources',
        'card-src',
        'src.t2',
      ),
      to('s-pair', t('src.open'), t('kw.pair'), Link2, 'sources', 'card-src', 'pair.auto'),
      to('s-srv', t('src.srv_url'), 'server websocket ws mdns', Server, 'sources', 'card-src'),
      to('s-chord', t('deck.chord'), `${t('kw.menu')} chord`, SquareMenu, 'deck', 'chord'),
      to('s-usb', t('deck.speed'), 'usb hub speed full high langsam slow', Usb, 'deck', 'usb-speed', 'deck.speed_h'),
      to('s-fw', t('fw.install'), t('kw.fwfile'), FileUp, 'firmware', 'fw-file', 'fw.install_h'),
      to('s-slots', t('fw.slots'), 'slot rollback ota', Layers, 'firmware', 'slots', 'fw.slots_h'),
      to('s-bak', t('fw.dl'), t('kw.restore'), DatabaseBackup, 'firmware', 'bak-dl', 'fw.backup_h'),
      to('s-rest', t('fw.restore'), t('kw.restore'), Upload, 'firmware', 'bak-restore'),
      to('s-log', t('dia.log'), `${t('kw.log')} protokoll journal`, ScrollText, 'diagnose', 'log'),
      to(
        's-input',
        t('in.title'),
        'eingaben input log tasten keys presses dial',
        Hand,
        'diagnose',
        'dia-input',
        'in.hint',
      ),
      to('s-crash', t('dia.crash'), 'coredump crash absturz', Bug, 'diagnose', 'dia-crash'),
      to('s-prof', t('dia.profile'), 'profile cpu task stack', Gauge, 'diagnose', 'dia-profile'),
      to('s-mem', t('dia.memory'), 'heap psram ram memory', MemoryStick, 'diagnose', 'dia-mem'),
      {
        id: 'a-arm',
        group: 'pal.act',
        label: t('pal.arm'),
        detail: t('arm.help'),
        kw: 'fernsteuerung remote arm',
        icon: Hand,
        run: () => jump('overview', 'arm'),
      },
      {
        id: 'a-menu',
        group: 'pal.act',
        label: t('ov.menu'),
        kw: t('kw.menu'),
        icon: SquareMenu,
        mut: true,
        run: () => a.menu(),
      },
      {
        id: 'a-rec',
        group: 'pal.act',
        label: t('act.reconnect'),
        kw: t('kw.reconnect'),
        icon: Usb,
        mut: true,
        run: () => a.reconnect(decks.length),
      },
      {
        id: 'a-reboot',
        group: 'pal.act',
        label: t('ov.reboot'),
        kw: t('kw.reboot'),
        icon: Power,
        mut: true,
        run: () => a.reboot(),
      },
      {
        id: 'a-setup',
        group: 'pal.act',
        label: t('pal.setup'),
        kw: 'einrichtung setup wizard',
        icon: Sparkles,
        mut: true,
        run: () =>
          ask({ title: t('cf.setup'), body: t('cf.setup_b'), ok: t('pal.setup') }).then((ok) =>
            ok ? putConfig({ setup_done: false }) : undefined,
          ),
      },
      {
        id: 'a-lang',
        group: 'pal.act',
        label: t('pal.lang'),
        kw: 'sprache language deutsch english',
        icon: Languages,
        run: () => langAtom.set(lang === 'de' ? 'en' : 'de'),
      },
      {
        id: 'a-theme',
        group: 'pal.act',
        label: t('pal.theme'),
        kw: 'design theme dunkel hell dark light',
        icon: Moon,
        run: () => themeAtom.set(eff === 'dark' ? 'light' : 'dark'),
      },
      {
        id: 'a-out',
        group: 'pal.act',
        label: t('hdr.logout'),
        kw: 'logout abmelden',
        icon: LogOut,
        run: () => a.logout(),
      },
      ...(P?.request
        ? [
            {
              id: 'a-acc',
              group: 'pal.act',
              label: `${t('act.accept')}: ${P.request_name || P.request}`,
              kw: t('kw.accept'),
              icon: Check,
              mut: true,
              run: () => a.accept(P.request ?? '', P.request_name || P.request || ''),
            } satisfies Cmd,
            {
              id: 'a-deny',
              group: 'pal.act',
              label: `${t('act.deny')}: ${P.request_name || P.request}`,
              kw: 'ablehnen deny',
              icon: X,
              mut: true,
              run: () => a.deny(P.request ?? ''),
            } satisfies Cmd,
          ]
        : []),
      ...groupScan([
        ...scan,
        ...(st.network?.wlan?.ssid
          ? [
              {
                ssid: st.network.wlan.ssid,
                bssid: st.network.wlan.bssid ?? '',
                rssi: st.network.wlan.rssi ?? -99,
                channel: 0,
                open: false,
              },
            ]
          : []),
      ]).map((g): Cmd => ({
        id: `wifi-${g.ssid}`,
        group: 'pal.set',
        label: g.ssid,
        kw: `wlan wifi ssid ${g.aps.map((x) => `${x.bssid} ${x.bssid.replace(/:/g, '')}`).join(' ')}`,
        icon: Wifi,
        hint: t('tab.network'),
        run: () => jump('network', 'row-wlan'),
      })),
      ...decks.map((d, i): Cmd => ({
        id: `deck-${d.serial}`,
        group: 'pal.set',
        label: deckName(d, i, decks.length, t),
        kw: `${d.model} ${d.serial} deck usb`,
        icon: LayoutGrid,
        hint: t('tab.deck'),
        run: () => (deckSelAtom.set(i), jump('deck', 'card-decks')),
        extras: [
          { id: 'live', label: t('pal.live'), run: () => (deckSelAtom.set(i), jump('overview', 'card-deck')) },
          { id: 'menu', label: t('deck.menu'), run: () => a.menu(decks.length > 1 ? d.serial : undefined) },
        ],
      })),
      ...(st.sources ?? [])
        .filter((s) => s.kind !== 'unknown')
        .map((s): Cmd => ({
          id: `src-${s.kind}-${s.host}`,
          group: 'pal.src',
          label: hostLabel(s, t),
          kw: `quelle source ${s.host} ${s.via ?? ''}`,
          icon: KIND_ICON[s.kind],
          hint: t(stateKey(s, null)),
          run: () => jump('sources', 'card-src'),
          extras:
            !s.chosen && s.connected ? [{ id: 'ctl', label: t('src.make_ctl_s'), run: () => a.pick(s) }] : undefined,
        })),
    ]
    return cmds
  }, [t, go, jump, lang, ask, a, eff, st, scan])
}

export function Palette({ close }: { close: () => void }) {
  const { t, act } = useUi()
  const cmds = useCommands()
  const [q, setQ] = useState('')
  const [sel, setSel] = useState(0)
  const [ex, setEx] = useState(-1)
  const [picks, setPicks] = useState<Pick[]>(() => ls.get<Pick[]>('dc.picks', []))
  const listRef = useRef<HTMLUListElement>(null)
  const res = useMemo(() => {
    const num = /^(?:hell\w*|brightness|bri\w*|dimm\w*)\s+(\d{1,3})\s*%?$/.exec(fold(q))
    if (num) {
      const n = Math.min(100, Number(num[1]))
      return [
        {
          id: 'bri',
          group: 'pal.act',
          label: t('pal.bri', { n }),
          icon: SunDim,
          mut: true,
          run: () => putConfig({ brightness: n }),
        } satisfies Cmd,
      ]
    }
    if (q.trim()) return rank(cmds, q, picks).slice(0, 14)
    const recent = rank(cmds, '', picks).map((c) => ({ ...c, group: 'pal.recent' as const }))
    const ids = new Set(recent.map((c) => c.id))
    return [
      ...recent,
      ...cmds.filter((c) => c.group === 'pal.act' && !ids.has(c.id)).slice(0, 6),
      ...cmds.filter((c) => c.group === 'pal.go'),
    ]
  }, [q, cmds, picks, t])
  useEffect(() => {
    listRef.current?.querySelector(`#pal-o-${sel}`)?.scrollIntoView({ block: 'nearest' })
  }, [sel])
  const locked = useStore((s) => s.conn !== 'live' || s.auth !== 'app')
  const run = (c: Cmd | undefined, x?: { run: () => unknown }) => {
    if (!c || (c.mut && locked)) return
    const next = remember(picks, q, c.id)
    setPicks(next)
    ls.set('dc.picks', next)
    close()
    const f = x ? x.run : c.run
    setTimeout(() => (c.mut && !x ? void act(f) : f()), 0)
  }
  const cur = res[sel]
  const extras = cur?.extras ?? []
  const onKey = (e: KeyboardEvent<HTMLInputElement>) => {
    if (e.key === 'ArrowDown') {
      e.preventDefault()
      setSel((s) => Math.min(res.length - 1, s + 1))
      setEx(-1)
    } else if (e.key === 'ArrowUp') {
      e.preventDefault()
      setSel((s) => Math.max(0, s - 1))
      setEx(-1)
    } else if (
      (e.key === 'Tab' || (e.key === 'ArrowRight' && e.currentTarget.selectionStart === q.length)) &&
      extras.length
    ) {
      e.preventDefault()
      const n = extras.length + 1
      setEx((x) => ((x + 1 + (e.shiftKey ? -1 : 1) + n) % n) - 1)
    } else if (e.key === 'Enter') {
      e.preventDefault()
      run(cur, ex >= 0 ? extras[ex] : undefined)
    }
  }
  let lastG: string | null = null
  return (
    <Dialog className="modal pal" title={t('hdr.search')} titleClass="sr" close={close}>
      <div className="pin">
        <Search size={18} aria-hidden />
        <input
          role="combobox"
          aria-label={t('hdr.search')}
          aria-expanded="true"
          aria-controls="pal-list"
          aria-activedescendant={res.length ? `pal-o-${sel}` : undefined}
          aria-autocomplete="list"
          placeholder={t('pal.ph')}
          value={q}
          onChange={(e) => {
            setQ(e.target.value)
            setSel(0)
            setEx(-1)
          }}
          onKeyDown={onKey}
          data-autofocus
        />
      </div>
      <ul id="pal-list" ref={listRef} role="listbox" aria-label={t('hdr.search')}>
        {res.length === 0 ? (
          <li className="grp muted">{t('pal.none')}</li>
        ) : (
          res.map((c, i) => {
            const head = c.group !== lastG
            lastG = c.group
            const I = c.icon ?? CircleHelp
            const off = c.mut && locked
            return (
              <li key={c.group + c.id} role="presentation" style={{ display: 'contents' }}>
                {head && (
                  <span role="presentation" className="grp lbl" style={{ display: 'block' }}>
                    {t(c.group)}
                  </span>
                )}
                {/* eslint-disable-next-line jsx-a11y/click-events-have-key-events -- the combobox input carries the keyboard */}
                <span
                  role="option"
                  id={`pal-o-${i}`}
                  className="palopt"
                  aria-selected={i === sel}
                  aria-disabled={off || undefined}
                  tabIndex={-1}
                  onPointerMove={() => sel !== i && setSel(i)}
                  onClick={() => run(c)}
                >
                  <I size={17} aria-hidden />
                  <span className="pl">
                    <span>{c.label}</span>
                    {c.detail && i === sel && <small className="muted">{c.detail}</small>}
                  </span>
                  {i === sel && c.extras ? (
                    <span className="pex">
                      {c.extras.map((x, j) => (
                        <button
                          key={x.id}
                          type="button"
                          tabIndex={-1}
                          className={cx('btn sm', ex === j && 'pri')}
                          onClick={(e) => (e.stopPropagation(), run(c, x))}
                        >
                          {x.label}
                        </button>
                      ))}
                    </span>
                  ) : (
                    <span className="ph">{off ? t('pal.off') : (c.hint ?? '')}</span>
                  )}
                </span>
              </li>
            )
          })
        )}
      </ul>
      <div className="pf">
        <span>
          <kbd>↑</kbd> <kbd>↓</kbd> {t('pal.pick')}
        </span>
        <span>
          <kbd>Tab</kbd> {t('pal.more')}
        </span>
        <span>
          <kbd>↵</kbd> {t('pal.run')}
        </span>
      </div>
    </Dialog>
  )
}
