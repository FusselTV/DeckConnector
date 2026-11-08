import { useCurrentFrame } from 'remotion'
import { AMBER, Deck, GREEN, keyBox, keyCenter, SD, Zone, type Key, showKeys, showStrip } from './hw'
import { camAt, Caption, Scene, type Shot } from './scene'
import { lin, pulse, ramp } from './time'
import { TEXT, type LangProps, type Text } from './text'

const S = 1080
const DECK = { x: 0, y: 0, s: 6 }
const CX = (SD.W * DECK.s) / 2
const BEAT = 100
const MENU = 16
const TAP = 44
const SWEEP = [50, 70] as const
const HOSTS = [
  { color: AMBER },
  { color: '#9b7bff' },
  { color: '#5aa9ff' },
] as const

function pages(t: Text, f: number) {
  return [
    { keys: showKeys(t, f, 0), strip: showStrip(t, 40) },
    {
      keys: [
        { bg: '#3b2a6b', top: t.scene, big: 'A', glow: '#9b7bff' },
        { bg: '#3b2a6b', top: t.scene, big: 'B', glow: '#9b7bff' },
        { bg: '#1c7a45', big: t.play, glow: GREEN },
        { bg: '#262b32', big: t.next, glow: '#5a6470' },
        { bg: '#8a2219', big: t.mute, glow: '#ff4b3e' },
        { bg: '#262b32', top: t.timer, big: '05:00', glow: '#5a6470' },
        { bg: '#7b5cff', big: 'Web' },
        { bg: '#262b32', big: 'Mail', glow: '#5a6470' },
      ] satisfies Key[],
      strip: (
        <>
          <Zone i={0} label={t.volume} value={60} accent="#9b7bff" />
          <Zone i={1} label={t.mic} value={70} accent={GREEN} />
          <Zone i={2} label={t.music} value={35} accent="#9b7bff" />
          <Zone i={3} label="Chat" value={50} accent="#9b7bff" />
        </>
      ),
    },
    {
      keys: [
        { bg: AMBER, fg: '#1a1200', top: t.light, big: '70%' },
        { bg: '#1f4f86', top: t.blinds, big: '30%', glow: '#3d8bff' },
        { bg: '#1c7a45', top: t.door, big: t.closed, glow: GREEN },
        { bg: '#262b32', top: t.temp, big: '21°', glow: '#5a6470' },
        { bg: '#1f4f86', top: t.fan, big: '2', glow: '#3d8bff' },
        { bg: '#b0461c', top: t.heat, big: t.on, glow: '#ff7a3d' },
        { bg: '#262b32', big: 'TV', glow: '#5a6470' },
        { bg: '#000', big: '14:32', glow: '#3a3f46' },
      ] satisfies Key[],
      strip: (
        <>
          <Zone i={0} label={t.light} value={70} />
          <Zone i={1} label={t.blinds} value={30} accent="#5aa9ff" />
          <Zone i={2} label={t.temp} value={52} accent="#ff7a50" />
          <Zone i={3} label={t.fan} value={40} accent="#5aa9ff" />
        </>
      ),
    },
  ] as const
}

/** The device's own menu: the hosts that can draw on the top row, the chosen one in amber. */
function menuKeys(t: Text, chosen: number): Key[] {
  const host = (i: number): Key => {
    const h = HOSTS[i] ?? HOSTS[0]
    const on = i === chosen
    const fg = on ? '#1a1200' : h.color
    return { bg: on ? AMBER : '#1b1f24', fg: on ? '#1a1200' : undefined, sub: [t.companion, t.app, t.server][i], glow: on ? AMBER : h.color, art: <Icon i={i} c={fg} /> }
  }
  return [
    host(0),
    host(1),
    host(2),
    { bg: '#262b32', big: t.menu, glow: '#5a6470' },
    { bg: '#123d27', top: t.lan, big: 'OK', glow: GREEN },
    { bg: '#1b1f24', top: t.wifi, big: 'OK', glow: '#5a6470' },
    { bg: '#1b1f24', big: '−', glow: '#5a6470' },
    { bg: '#1b1f24', big: '+', glow: '#5a6470' },
  ]
}

// Where the sweep is: 0 at the top left key, 1 past the bottom right.
const sweepAt = (i: number) => ((i % 4) + Math.floor(i / 4)) / 5

const SHOTS: Shot[] = [0, 1, 2].flatMap((b): Shot[] => {
  const B = b * BEAT
  const [kx, ky] = keyCenter(DECK, (b + 1) % 3)
  return [
    [B, { x: CX, y: 290, z: 1.2, rx: 8, ry: -5 }],
    [B + 22, { x: CX, y: 280, z: 1.24, rx: 6, ry: -3 }],
    [B + TAP, { x: CX + (kx - CX) * 0.12, y: ky + 150, z: 1.38, rx: 4, ry: (kx - CX) / 80 }],
    [B + SWEEP[1] + 6, { x: CX, y: 300, z: 1.16, rx: 9, ry: 5 }],
  ]
})
SHOTS.push([3 * BEAT, { x: CX, y: 290, z: 1.2, rx: 8, ry: -5 }])

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
  const lit = keys.map((_, i) => (l >= toNext(i) ? ramp(l, toNext(i), toNext(i) + 7) : l >= toMenu(i) ? ramp(l, toMenu(i), toMenu(i) + 6) : 1))
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
      cam={camAt(f, SHOTS)}
      over={
        <>
          {b === 0 && <Caption f={f} from={-30} to={MENU + 2} text={names[from] ?? ''} sub={t.inControl} size={48} />}
          <Caption f={f} from={B + MENU + 4} to={B + SWEEP[0] + 2} text={t.tapSwitch} size={48} />
          <Caption f={f} from={B + SWEEP[0] + 4} to={B + BEAT + MENU + 2} text={names[to] ?? ''} sub={t.inControl} size={48} />
        </>
      }
    >
      <Deck {...DECK} keys={keys} under={under} strip={strip} lit={lit} flash={flash} down={down} ring={ring} edge={sweeping ? hostColor : '#fff'} stripLit={l >= SWEEP[1] - 4 ? ramp(l, SWEEP[1] - 4, SWEEP[1] + 4) : 1} sheen={Math.sin((f / 300) * Math.PI * 6) * 0.6}>
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
  const cx = b.x + b.s * 0.62
  const cy = b.y + b.s * 0.36
  const k = ramp(l, TAP - 16, TAP - 1)
  const lift = (1 - k) * 10
  const press = pulse(l, TAP - 1, TAP + 8, 1, 6)
  const ripple = lin(l, TAP, TAP + 14)
  return (
    <g opacity={o}>
      <ellipse cx={cx + 3 + lift * 0.5} cy={cy + 5 + lift} rx={8} ry={9} fill="#000" opacity={0.35 * (1 - press * 0.5)} />
      {ripple > 0 && ripple < 1 && <circle cx={cx} cy={cy} r={5 + ripple * 11} fill="none" stroke="#fff" strokeWidth={1.2 * (1 - ripple)} opacity={1 - ripple} />}
      <circle cx={cx + lift * 0.4} cy={cy + lift * 0.6} r={6 - press * 0.8} fill="#fff" opacity={0.28 + press * 0.2} />
      <circle cx={cx + lift * 0.4} cy={cy + lift * 0.6} r={6 - press * 0.8} fill="none" stroke="#fff" strokeWidth={0.8} opacity={0.85} />
    </g>
  )
}

/** Small pictures for the host keys: a button grid, a deck, a server. */
function Icon({ i, c }: { i: number; c: string }) {
  if (i === 0)
    return (
      <g transform="translate(36 22)" fill={c}>
        {[0, 1, 2, 3].map((j) => (
          <rect key={j} x={(j % 2) * 26} y={Math.floor(j / 2) * 26} width={22} height={22} rx={5} />
        ))}
      </g>
    )
  if (i === 1)
    return (
      <g transform="translate(30 24)" fill="none" stroke={c} strokeWidth={6}>
        <rect width={60} height={46} rx={8} />
        <path d="M14 16 h8 M28 16 h8 M42 16 h4 M14 30 h8 M28 30 h8" strokeLinecap="round" />
      </g>
    )
  return (
    <g transform="translate(34 20)" fill="none" stroke={c} strokeWidth={6}>
      <rect width={52} height={22} rx={5} />
      <rect y={28} width={52} height={22} rx={5} />
      <circle cx={40} cy={11} r={2} fill={c} />
      <circle cx={40} cy={39} r={2} fill={c} />
    </g>
  )
}
