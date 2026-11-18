import { useCurrentFrame } from 'remotion'
import {
  AppWindow,
  Blinds,
  Clapperboard,
  Clock,
  DoorClosed,
  EthernetPort,
  Fan,
  Flame,
  Globe,
  LayoutGrid,
  Lightbulb,
  Mail,
  MessageCircle,
  Mic,
  MicOff,
  Music,
  Play,
  Server,
  Settings,
  SkipForward,
  Sun,
  SunDim,
  Thermometer,
  Timer,
  Tv,
  Volume2,
  Wifi,
} from 'lucide-react'
import { AMBER, Deck, GREEN, keyBox, RED, SD, Zone, type Key, showKeys, showStrip } from './hw'
import { breathe, Caption, Scene } from './scene'
import { lin, pulse, ramp } from './time'
import { TEXT, type LangProps, type Text } from './text'

const S = 1080
const DECK = { x: 0, y: 0, s: 6 }
const CX = (SD.W * DECK.s) / 2
const BEAT = 100
const MENU = 16
const TAP = 44
const SWEEP = [50, 70] as const
const HOSTS = [{ color: AMBER }, { color: '#9b7bff' }, { color: '#5aa9ff' }] as const

function pages(t: Text, f: number) {
  return [
    { keys: showKeys(t, f, 0), strip: showStrip(40) },
    {
      keys: [
        { icon: Clapperboard, word: 'Intro', bg: '#5b3fc4', glow: '#9b7bff' },
        { icon: Clapperboard, word: t.game },
        { icon: Play, word: t.play, bg: '#30c060', glow: GREEN },
        { icon: SkipForward, word: t.next },
        { icon: MicOff, word: t.off, bg: RED },
        { icon: Timer, word: '05:00' },
        { icon: Globe, word: 'Web' },
        { icon: Mail, word: 'Mail' },
      ] satisfies Key[],
      strip: (
        <>
          <Zone i={0} icon={Volume2} value={60} accent="#9b7bff" />
          <Zone i={1} icon={Mic} value={70} accent={GREEN} />
          <Zone i={2} icon={Music} value={35} accent="#9b7bff" />
          <Zone i={3} icon={MessageCircle} value={50} accent="#9b7bff" />
        </>
      ),
    },
    {
      keys: [
        { icon: Lightbulb, word: '70%', bg: AMBER, fg: '#1a1200' },
        { icon: Blinds, word: '30%' },
        { icon: DoorClosed, word: t.closed, bg: '#30c060', glow: GREEN },
        { icon: Thermometer, word: '21°' },
        { icon: Fan, word: '2' },
        { icon: Flame, word: t.on.toUpperCase(), bg: '#d4561c', glow: '#ff7a3d' },
        { icon: Tv, word: 'TV' },
        { icon: Clock, word: '14:32' },
      ] satisfies Key[],
      strip: (
        <>
          <Zone i={0} icon={Lightbulb} value={70} />
          <Zone i={1} icon={Blinds} value={30} accent="#5aa9ff" />
          <Zone i={2} icon={Thermometer} value={52} accent="#ff7a50" />
          <Zone i={3} icon={Fan} value={40} accent="#5aa9ff" />
        </>
      ),
    },
  ] as const
}

const HOST_ICONS = [LayoutGrid, AppWindow, Server] as const

/** The device's own menu: the hosts that can draw on the top row, the chosen one in amber. */
export function menuKeys(t: Text, chosen: number): Key[] {
  const host = (i: number): Key => {
    const on = i === chosen
    return {
      icon: HOST_ICONS[i],
      bg: on ? AMBER : undefined,
      fg: on ? '#1a1200' : HOSTS[i]?.color,
      glow: on ? AMBER : HOSTS[i]?.color,
    }
  }
  return [
    host(0),
    host(1),
    host(2),
    { icon: Settings, word: t.menu },
    { icon: EthernetPort, word: t.lan, bg: '#30c060', glow: GREEN },
    { icon: Wifi, word: t.wifi },
    { icon: SunDim },
    { icon: Sun },
  ]
}

// Where the sweep is: 0 at the top left key, 1 past the bottom right.
const sweepAt = (i: number) => ((i % 4) + Math.floor(i / 4)) / 5

const WIDE = { x: CX, y: 330, z: 1.0, rx: 5, ry: 0 }

export default function Hosts({ lang }: LangProps) {
  const f = useCurrentFrame()
  const t = TEXT[lang]
  const b = Math.floor(f / BEAT)
  const B = b * BEAT
  const from = b % 3
  const to = (b + 1) % 3
  const l = f - B
  const all = pages(t, f)
  const cur = all[from] ?? all[0]
  const next = all[to] ?? all[0]
  const names = [t.companion, t.app, t.server]

  // Each key wipes to the menu, then to the new host's picture, along a diagonal.
  const toMenu = (i: number) => MENU + i * 1.5
  const toNext = (i: number) => SWEEP[0] + sweepAt(i) * (SWEEP[1] - SWEEP[0])
  const menu = menuKeys(t, l >= TAP - 1 ? to : from)
  const keys = cur.keys.map((k, i) => (l >= toNext(i) ? next.keys[i] : l >= toMenu(i) ? menu[i] : k) ?? null)
  const under = cur.keys.map((k, i) => (l >= toNext(i) ? menu[i] : k) ?? null)
  const lit = keys.map((_, i) =>
    l >= toNext(i) ? ramp(l, toNext(i), toNext(i) + 7) : l >= toMenu(i) ? ramp(l, toMenu(i), toMenu(i) + 6) : 1,
  )
  const flash = keys.map((_, i) => 0.35 * pulse(l, toNext(i) + 4, toNext(i) + 16, 2, 10))
  const down = keys.map((_, i) => (i === to ? pulse(l, TAP - 2, TAP + 7, 2, 5) : 0))
  const ring = keys.map((_, i) => (i === to && l < SWEEP[0] ? pulse(l, TAP, SWEEP[0] - 1, 2, 5) : 0))
  const strip = l >= SWEEP[1] - 4 ? next.strip : cur.strip
  const hostColor = (HOSTS[to] ?? HOSTS[0]).color
  const sweeping = l >= SWEEP[0] - 2

  return (
    <Scene
      w={S}
      h={S}
      cam={breathe(f, 3 * BEAT, WIDE)}
      over={
        <>
          <Caption
            f={f}
            from={B - BEAT + SWEEP[0] + 2}
            to={B + SWEEP[0] + 2}
            text={names[from] ?? ''}
            sub={t.inControl}
            size={48}
          />
          <Caption
            f={f}
            from={B + SWEEP[0] + 2}
            to={B + BEAT + SWEEP[0] + 2}
            text={names[to] ?? ''}
            sub={t.inControl}
            size={48}
          />
        </>
      }
    >
      <Deck
        {...DECK}
        keys={keys}
        under={under}
        strip={strip}
        lit={lit}
        flash={flash}
        down={down}
        ring={ring}
        edge={sweeping ? hostColor : '#fff'}
        stripLit={l >= SWEEP[1] - 4 ? ramp(l, SWEEP[1] - 4, SWEEP[1] + 4) : 1}
        sheen={Math.sin((f / 300) * Math.PI * 6) * 0.6}
      >
        <Finger l={l} target={to} />
      </Deck>
    </Scene>
  )
}

/** A fingertip: a soft shadow, a pad of light, and a ring when it lands. */
function Finger({ l, target }: { l: number; target: number }) {
  const o = Math.min(ramp(l, TAP - 16, TAP - 8), 1 - ramp(l, TAP + 2, TAP + 9))
  if (o <= 0) return null
  const b = keyBox(target)
  const cx = b.x + b.s / 2
  const cy = b.y + b.s * 0.36
  const k = ramp(l, TAP - 16, TAP - 1)
  const lift = (1 - k) * 10
  const press = pulse(l, TAP - 1, TAP + 8, 1, 6)
  const ripple = lin(l, TAP, TAP + 14)
  return (
    <g opacity={o}>
      <ellipse
        cx={cx + 3 + lift * 0.5}
        cy={cy + 5 + lift}
        rx={8}
        ry={9}
        fill="#000"
        opacity={0.35 * (1 - press * 0.5)}
      />
      {ripple > 0 && ripple < 1 && (
        <circle
          cx={cx}
          cy={cy}
          r={5 + ripple * 11}
          fill="none"
          stroke="#fff"
          strokeWidth={1.2 * (1 - ripple)}
          opacity={1 - ripple}
        />
      )}
      <circle cx={cx + lift * 0.4} cy={cy + lift * 0.6} r={6 - press * 0.8} fill="#fff" opacity={0.28 + press * 0.2} />
      <circle
        cx={cx + lift * 0.4}
        cy={cy + lift * 0.6}
        r={6 - press * 0.8}
        fill="none"
        stroke="#fff"
        strokeWidth={0.8}
        opacity={0.85}
      />
    </g>
  )
}
