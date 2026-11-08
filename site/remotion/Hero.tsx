import { useCurrentFrame, useVideoConfig } from 'remotion'
import { AMBER, Board, Cable, Deck, Face, GREEN, INK, keyCenter, Label, MONO, Pool, SANS, showKeys, showStrip, Switch, Trail, type Pt } from './hw'
import { LAND, PORT, type Layout } from './layout'
import { camAt, Caption, Scene, type Shot } from './scene'
import { lin, pulse, ramp } from './time'
import { fill, TEXT, type LangProps, type Text } from './text'

const rev = (p: Pt[]) => [...p].reverse()

const P = 88
const KEY = 1
const T = {
  inEth: [0, 12],
  inUsb: [12, 20],
  usbUp: [P + 4, P + 20],
  ethUp: [P + 22, P + 52],
  hit: P + 54,
  live: P + 60,
  ethDown: [P + 66, P + 90],
  usbDown: [P + 90, P + 102],
  land: P + 102,
  dial: 262,
  out: [340, 356],
} as const
const ORDER = [0, 1, 4, 2, 5, 3, 6, 7]
const ignite = (i: number) => 18 + ORDER.indexOf(i) * 2

function shots(L: Layout, W: number, H: number): Shot[] {
  const w = L.wide
  const [kx, ky] = keyCenter(L.deck, KEY)
  const kz = 13.2 / L.deck.s
  const mw = (480 + 28) * L.mon.s
  const mh = (340 + 90) * L.mon.s
  const mz = Math.min((W * (W > H ? 0.62 : 0.86)) / mw, (H * 0.8) / mh)
  const mx = L.mon.x + 240 * L.mon.s
  const my = L.mon.y + 185 * L.mon.s
  const tilt = W > H ? 10 : 5
  return [
    [0, w],
    [58, { ...w, z: w.z * 1.04, ry: -2 }],
    [80, { x: kx, y: ky, z: kz, rx: 4, ry: -2 }],
    [98, { x: kx + 8, y: ky + 4, z: kz * 1.03, rx: 3, ry: 0 }],
    [120, { ...w, z: w.z * 1.02 }],
    [134, { ...w, z: w.z * 1.04, ry: 3 }],
    [152, { x: mx, y: my, z: mz, rx: 2, ry: tilt }],
    [170, { x: mx - 4, y: my, z: mz * 1.025, rx: 2, ry: tilt }],
    [194, w],
    [320, { ...w, z: w.z * 1.05, ry: -3 }],
    [360, w],
  ]
}
const SHOTS = { land: shots(LAND, 1280, 720), port: shots(PORT, 1080, 1350) }

export default function Hero({ lang }: LangProps) {
  const t = TEXT[lang]
  const f = useCurrentFrame()
  const { width: W, height: H } = useVideoConfig()
  const port = H > W
  const L = port ? PORT : LAND
  const ETH: Pt[] = [...L.poe, ...L.net]
  const USB = L.usb
  const cam = camAt(f, port ? SHOTS.port : SHOTS.land)
  const cap = L.cap
  const off = 1 - ramp(f, T.out[0], T.out[1])
  const deckLive = f >= T.land && f < T.out[1] ? 1 : 0
  const keys = showKeys(t, f, deckLive)
  const lit = keys.map((_, i) => ramp(f, ignite(i), ignite(i) + 7) * off)
  const flash = keys.map((_, i) => Math.max(0.4 * pulse(f, ignite(i) + 4, ignite(i) + 16, 2, 10), i < 2 ? 0.6 * pulse(f, T.land, T.land + 16, 1, 14) : 0))
  const down = keys.map((_, i) => (i === KEY ? pulse(f, P - 2, P + 9, 2, 6) : 0))
  const ring = keys.map((_, i) => (i === KEY ? pulse(f, P, P + 40, 2, 14) : 0))
  const music = 40 + 32 * ramp(f, T.dial + 40, T.dial + 52)
  const dial = [0, 0, 36 * ramp(f, T.dial, T.dial + 14), 0]
  const dialRing = [0, 0, pulse(f, T.dial, T.dial + 30, 2, 12), 0]
  const usbHot = Math.max(pulse(f, T.inUsb[0] - 4, T.inUsb[1] + 4, 2, 6), pulse(f, T.usbUp[0], T.usbUp[1] + 6, 2, 6), pulse(f, T.usbDown[0], T.usbDown[1] + 6, 2, 6))
  const ethHot = Math.max(pulse(f, T.inEth[0], T.inEth[1] + 4, 2, 6), pulse(f, T.ethUp[0], T.ethUp[1] + 6, 2, 6), pulse(f, T.ethDown[0], T.ethDown[1] + 6, 2, 6))
  const hot = Math.max(pulse(f, T.inEth[1] - 2, T.inUsb[0] + 8, 2, 8), pulse(f, T.usbUp[1] - 2, T.ethUp[0] + 8, 2, 8), pulse(f, T.ethDown[1] - 2, T.usbDown[0] + 8, 2, 8), pulse(f, T.dial + 14, T.dial + 30, 2, 8))
  const d0 = T.dial + 2
  const cable = Math.min(ramp(f, 236, 250), 1 - ramp(f, T.out[0], T.out[0] + 10))

  return (
    <Scene
      w={W}
      h={H}
      cam={cam}
      over={
        <>
          <Caption f={f} from={4} to={P - 6} text={t.heroIn} size={cap} />
          <Caption f={f} from={P + 4} to={144} text={t.heroOut} size={cap} />
          <Caption f={f} from={176} to={232} text={t.heroBack} dot={GREEN} size={cap} />
          <Caption f={f} from={232} to={T.out[1]} text={t.heroCable} sub={t.heroNoPc} dot={GREEN} size={cap} />
        </>
      }
    >
      <Pool x={L.deck.x + 70 * L.deck.s} y={L.deck.y + 125 * L.deck.s} rx={130 * L.deck.s} ry={28 * L.deck.s} o={0.07 + 0.08 * Math.max(...lit)} c={AMBER} />
      <Pool x={L.mon.x + 240 * L.mon.s} y={L.mon.y + 170 * L.mon.s} rx={400 * L.mon.s} ry={300 * L.mon.s} c="#5aa9ff" o={0.12} />
      <Cable pts={USB} w={14} glow={usbHot} />
      <Cable pts={L.net} w={12} color="#2a2f36" glow={ethHot} />
      <Cable pts={L.poe} w={14} color="#2a2f36" glow={0.45 + 0.5 * ethHot} />
      <Monitor t={t} f={f} m={L.mon} />
      <Switch f={f} {...L.sw} />
      <Board {...L.board} act={hot > 0.3 && Math.floor(f / 3) % 2 ? 1 : 0} hot={hot} sheen={Math.sin((f / 360) * Math.PI * 2)} />
      <Deck {...L.deck} keys={keys} strip={showStrip(t, music, pulse(f, T.dial + 40, T.dial + 70, 2, 12))} lit={lit} flash={flash} down={down} ring={ring} dial={dial} dialRing={dialRing} stripLit={ramp(f, 22, 34) * off} sheen={(cam.x - 600) / 800} />
      {[0, 5, 10].map((k) => (
        <g key={k}>
          <Trail pts={rev(ETH)} t={lin(f, T.inEth[0] + k, T.inEth[1] + k, 0, 1.3)} w={16} len={0.28} color={k === 5 ? '#ffffff' : AMBER} />
          <Trail pts={rev(USB)} t={lin(f, T.inUsb[0] + k, T.inUsb[1] + k, 0, 1.3)} w={16} len={0.3} color={k === 5 ? '#ffffff' : AMBER} />
        </g>
      ))}
      <Trail pts={USB} t={lin(f, T.usbUp[0], T.usbUp[1], 0, 1.3)} w={16} len={0.3} />
      <Trail pts={ETH} t={lin(f, T.ethUp[0], T.ethUp[1], 0, 1.3)} w={16} len={0.28} />
      {[0, 6].map((k) => (
        <g key={k}>
          <Trail pts={rev(ETH)} t={lin(f, T.ethDown[0] + k, T.ethDown[1] + k, 0, 1.3)} w={16} len={0.28} color={k ? '#ffffff' : GREEN} />
          <Trail pts={rev(USB)} t={lin(f, T.usbDown[0] + k, T.usbDown[1] + k, 0, 1.3)} w={16} len={0.3} color={k ? '#ffffff' : GREEN} />
        </g>
      ))}
      <Trail pts={USB} t={lin(f, d0, d0 + 12, 0, 1.3)} w={12} />
      <Trail pts={ETH} t={lin(f, d0 + 12, d0 + 32, 0, 1.3)} w={12} />
      <Trail pts={rev(ETH)} t={lin(f, d0 + 34, d0 + 52, 0, 1.3)} w={12} color={GREEN} />
      <Trail pts={rev(USB)} t={lin(f, d0 + 52, d0 + 62, 0, 1.3)} w={12} color={GREEN} />
      <PressBurst f={f} deck={L.deck} />
      {cable > 0 && <path d={L.poe.map((p, i) => `${i ? 'L' : 'M'}${p[0]},${p[1]}`).join('')} fill="none" stroke={AMBER} strokeWidth={10} strokeLinecap="round" strokeDasharray="18 30" strokeDashoffset={-f * 3} opacity={cable} />}
      <Label x={L.label.x} y={L.label.y} size={L.label.size} o={1 - ramp(cam.z / L.wide.z, 1.15, 1.45)}>
        {t.board}
      </Label>
    </Scene>
  )
}

function PressBurst({ f, deck }: { f: number; deck: Layout['deck'] }) {
  const k = lin(f, P, P + 18)
  if (k <= 0 || k >= 1) return null
  const [x, y] = keyCenter(deck, KEY)
  const u = deck.s / 4.4
  return <circle cx={x} cy={y} r={(50 + 90 * k) * u} fill="none" stroke={AMBER} strokeWidth={6 * u * (1 - k)} opacity={1 - k} />
}

function Monitor({ t, f, m }: { t: Text; f: number; m: Layout['mon'] }) {
  const [x, y, w, h] = [0, 0, 480, 340]
  const live: 0 | 1 = f >= T.live && f < T.out[1] ? 1 : 0
  const keys = showKeys(t, f, live)
  const btn = 92
  const gap = 12
  const gx = x + (w - (4 * btn + 3 * gap)) / 2
  const hit = pulse(f, T.hit, T.hit + 34, 2, 14)
  const log = f >= T.dial + 2 + 32 && f < T.out[1] ? fill(t.dialTurn, { n: 3, d: '+3' }) : f >= T.hit && f < T.out[1] ? fill(t.keyDown, { n: KEY + 1 }) : ''
  const logHot = Math.max(hit, pulse(f, T.dial + 34, T.dial + 64, 2, 14))
  return (
    <g transform={`translate(${m.x} ${m.y}) scale(${m.s})`}>
      <rect x={x + w / 2 - 36} y={y + h + 8} width={72} height={42} fill="#1b1e22" />
      <rect x={x + w / 2 - 100} y={y + h + 46} width={200} height={12} rx={6} fill="#22262b" />
      <rect x={x - 14} y={y - 14} width={w + 28} height={h + 28} rx={20} fill="#0d0f11" stroke="#2d3238" strokeWidth={2} />
      <rect x={x} y={y} width={w} height={h} rx={8} fill="#0f1113" />
      <rect x={x} y={y} width={w} height={56} rx={8} fill="#171a1d" />
      <circle cx={x + 30} cy={y + 28} r={9} fill={GREEN} />
      <text x={x + 52} y={y + 40} fontFamily={SANS} fontSize={34} fontWeight={650} fill={INK}>
        {t.companion}
      </text>
      {keys.map((k, i) => {
        const bx = gx + (i % 4) * (btn + gap)
        const by = y + 70 + Math.floor(i / 4) * (btn + gap)
        return (
          <g key={i}>
            <svg x={bx} y={by} width={btn} height={btn} viewBox="0 0 120 120">
              <clipPath id={`hero-mon${i}`}>
                <rect width={120} height={120} rx={12} />
              </clipPath>
              <g clipPath={`url(#hero-mon${i})`}>
                <Face k={k} />
              </g>
            </svg>
            {i === KEY && hit > 0 && <rect x={bx - 5} y={by - 5} width={btn + 10} height={btn + 10} rx={14} fill="none" stroke={AMBER} strokeWidth={5} opacity={hit} />}
          </g>
        )
      })}
      <rect x={x + 20} y={y + h - 62} width={w - 40} height={48} rx={10} fill="#0a0b0d" stroke={AMBER} strokeOpacity={logHot} strokeWidth={3} />
      <text x={x + 36} y={y + h - 29} fontFamily={MONO} fontSize={24} fontWeight={700} fill="#c4c9cf">
        <tspan fill={AMBER}>{'> '}</tspan>
        {log}
      </text>
      <linearGradient id="hero-glass" x1="0" y1="0" x2="1" y2="1">
        <stop offset="0" stopColor="#fff" stopOpacity={0.07} />
        <stop offset="0.4" stopColor="#fff" stopOpacity={0} />
      </linearGradient>
      <rect x={x} y={y} width={w} height={h} rx={8} fill="url(#hero-glass)" />
    </g>
  )
}
