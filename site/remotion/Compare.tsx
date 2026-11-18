import { useCurrentFrame, useVideoConfig } from 'remotion'
import {
  AMBER,
  at,
  Board,
  BOARD_ANT,
  BOARD_USB,
  BOARD_USBC,
  Cable,
  Deck,
  GREEN,
  Jack,
  Plug,
  Pool,
  RED,
  rounded,
  SANS,
  Shadow,
  showKeys,
  showStrip,
  Switch,
  switchPort,
  useInk,
  type Pt,
} from './hw'
import { menuKeys } from './Hosts'
import { Scene } from './scene'
import { pulse, ramp } from './time'
import { TEXT, type LangProps, type Text } from './text'

// Each side is drawn in a 600 x 480 box; landscape puts them next to each other, portrait stacks them.
const DECK = { x: 0, y: 150, s: 2.4 }
const RIGHT = 140 * DECK.s
const MENU = [150, 300] as const
const TAPS = [190, 230] as const

function Chip({ y, text, ok, k, size }: { y: number; text: string; ok: boolean; k: number; size: number }) {
  const ink = useInk()
  if (k <= 0.01) return null
  const c = ok ? GREEN : RED
  const h = size * 1.9
  return (
    <g transform={`translate(0 ${y + (1 - k) * 10})`} opacity={k}>
      <rect
        width={text.length * size * 0.56 + h * 1.5}
        height={h}
        rx={h / 2}
        fill={ok ? 'rgba(61,220,132,0.14)' : 'rgba(255,75,62,0.12)'}
        stroke={c}
        strokeOpacity={0.6}
        strokeWidth={2}
      />
      <path
        d={
          ok
            ? `M${h * 0.36} ${h * 0.52} l${h * 0.14} ${h * 0.14} l${h * 0.26} -${h * 0.3}`
            : `M${h * 0.38} ${h * 0.32} l${h * 0.36} ${h * 0.36} m0 -${h * 0.36} l-${h * 0.36} ${h * 0.36}`
        }
        stroke={c}
        strokeWidth={size * 0.17}
        strokeLinecap="round"
        fill="none"
      />
      <text x={h * 1.02} y={h * 0.66} fontFamily={SANS} fontSize={size} fontWeight={600} fill={ink}>
        {text}
      </text>
    </g>
  )
}

function Title({ name, dot }: { name: string; dot: string }) {
  const ink = useInk()
  return (
    <g>
      <circle cx={12} cy={-16} r={11} fill={dot} />
      <text x={36} y={0} fontFamily={SANS} fontSize={50} fontWeight={650} fill={ink} letterSpacing={-1.5}>
        {name}
      </text>
    </g>
  )
}

// The dock sits on the desk beside the deck: USB-C in on its left, RJ45 on top up into the switch.
const SW = { x: 368, y: 196, w: 220, h: 76 }
const SW_PORT = switchPort(SW, 0)
const DOCK = { x: SW_PORT[0] - 60, y: 386 }
const DOCK_RJ45: Pt = [SW_PORT[0], DOCK.y + 25]

function Dock({ x, y }: { x: number; y: number }) {
  return (
    <g transform={`translate(${x} ${y})`}>
      <Shadow x={60} y={88} rx={80} ry={14} />
      <rect x={-6} y={34} width={10} height={16} rx={4} fill="#b9bec5" />
      <rect width={120} height={84} rx={16} fill="#1b1e22" stroke="#3b4148" strokeWidth={2} />
      <rect width={120} height={14} rx={7} fill="#fff" opacity={0.06} />
      <rect x={44} y={12} width={32} height={26} rx={3} fill="#0b0d0f" stroke="#4a5058" />
      <circle cx={24} cy={62} r={5} fill="#5a6470" />
    </g>
  )
}

function DockSide({ f, t, chip }: { f: number; t: Text; chip: number }) {
  const glow = 0.4 + 0.3 * Math.sin(f / 9)
  return (
    <g>
      <Pool x={RIGHT / 2} y={DECK.y + 300} rx={260} ry={60} c="#ffffff" o={0.05} />
      <Switch f={f} {...SW} />
      <Cable
        pts={[
          [RIGHT - 6, DOCK.y + 42],
          [DOCK.x, DOCK.y + 42],
        ]}
        w={10}
      />
      <Dock {...DOCK} />
      <Cable pts={[DOCK_RJ45, SW_PORT]} w={12} color="#2a2f36" glow={glow} glowColor={RED} />
      <Jack x={SW_PORT[0]} y={SW_PORT[1]} />
      <Jack x={DOCK_RJ45[0]} y={DOCK_RJ45[1]} />
      <Deck {...DECK} keys={showKeys(t, f, 0)} strip={showStrip(40)} />
      <Chip y={40} text={t.cableToSwitch} ok={false} k={1 - chip} size={26} />
      <Chip y={40} text={t.noHostMenu} ok={false} k={chip} size={26} />
    </g>
  )
}

function BoardSide({ f, t, menu, chosen, tap }: { f: number; t: Text; menu: number; chosen: number; tap: number[] }) {
  const B = { x: 400, y: 300, s: 0.8 }
  const usb = at(B, BOARD_USB)
  const c = at(B, BOARD_USBC)
  const plugX = c[0] - 20
  const keys = menu > 0.5 ? menuKeys(t, chosen) : showKeys(t, f, 0)
  const lit = keys.map(() => (menu > 0.5 ? ramp(menu, 0.5, 1) : 1 - ramp(menu, 0, 0.5)))
  const ring = keys.map((_, i) => (i < 3 ? (tap[i] ?? 0) : 0))
  const down = ring.map((r) => r * 0.8)
  const ant = at(B, BOARD_ANT)
  return (
    <g>
      <Pool x={RIGHT / 2} y={DECK.y + 300} rx={260} ry={60} c={AMBER} o={0.1} />
      <Pool x={ant[0]} y={ant[1]} rx={160} ry={120} c={GREEN} o={0.18} />
      <Cable pts={[[RIGHT - 6, usb[1]], usb]} w={10} glow={0.3} />
      <Cable pts={rounded([c, [plugX, c[1]], [plugX, 448]], 10)} w={8} color="#2a2f36" />
      <Plug x={plugX} y={440} s={0.6} />
      <Board
        {...B}
        wifi={0.6 + 0.4 * Math.sin(f / 8)}
        link={0}
        act={Math.floor(f / 4) % 2}
        sheen={Math.sin((f / 360) * Math.PI * 2)}
      />
      <Deck {...DECK} keys={keys} lit={lit} ring={ring} down={down} strip={showStrip(40)} />
      <Chip y={40} text={t.noCable} ok k={1 - menu} size={26} />
      <Chip y={40} text={t.hostsOnDeck} ok k={menu} size={26} />
    </g>
  )
}

export default function Compare({ lang }: LangProps) {
  const t = TEXT[lang]
  const f = useCurrentFrame()
  const { width: W, height: H } = useVideoConfig()
  const port = H > W
  const menu = ramp(f, MENU[0], MENU[0] + 14) * (1 - ramp(f, MENU[1], MENU[1] + 14))
  const chosen = f < TAPS[0] ? 0 : f < TAPS[1] ? 1 : 2
  const tap = [0, 1, 2].map((i) => (i ? pulse(f, TAPS[i - 1] ?? 0, (TAPS[i - 1] ?? 0) + 24, 2, 12) : 0))
  const [a, b] = port
    ? ['translate(195 82) scale(1.15)', 'translate(195 760) scale(1.15)']
    : ['translate(30 175)', 'translate(670 175)']
  return (
    <Scene w={W} h={H} cam={{ x: W / 2, y: H / 2, z: 1 }}>
      {port ? (
        <line x1={80} x2={1000} y1={672} y2={672} stroke="#2a2f35" strokeWidth={2} />
      ) : (
        <line x1={640} x2={640} y1={70} y2={650} stroke="#2a2f35" strokeWidth={2} />
      )}
      <g transform={a}>
        <Title name={t.dock} dot="#8b929a" />
        <DockSide f={f} t={t} chip={menu} />
      </g>
      <g transform={b}>
        <Title name={t.board} dot={GREEN} />
        <BoardSide f={f} t={t} menu={menu} chosen={chosen} tap={tap} />
      </g>
    </Scene>
  )
}
