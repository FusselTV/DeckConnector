import { useCurrentFrame, useVideoConfig } from 'remotion'
import {
  Air,
  AMBER,
  at,
  Board,
  BOARD_ANT,
  Cable,
  Deck,
  Face,
  GREEN,
  INK,
  keyCenter,
  Label,
  MONO,
  Plug,
  Pool,
  Router,
  SANS,
  showKeys,
  showStrip,
  Trail,
  Waves,
  type Pt,
} from './hw'
import { LAND, PORT, type Layout } from './layout'
import { breathe, Caption, Scene } from './scene'
import { lin, pulse } from './time'
import { fill, TEXT, type LangProps, type Text } from './text'

const rev = (p: Pt[]) => [...p].reverse()

// Two presses: CAM2 goes live, later CAM1 again, so the loop ends where it starts.
const PRESSES = [
  { p: 36, key: 1 },
  { p: 206, key: 0 },
] as const
const D = 360
const ev = (p: number) =>
  ({
    usbUp: [p + 4, p + 20],
    airUp: [p + 22, p + 52],
    hit: p + 54,
    airDown: [p + 66, p + 90],
    usbDown: [p + 90, p + 102],
    land: p + 102,
  }) as const
/** The key of the last press that reached Companion (hit) or came back to the deck (land); before the first, the loop's last. */
const lastKey = (f: number, when: 'hit' | 'land') => {
  let key: 0 | 1 = PRESSES[1].key
  for (const pr of PRESSES) if (f >= ev(pr.p)[when]) key = pr.key
  return key
}

export default function Hero({ lang }: LangProps) {
  const t = TEXT[lang]
  const f = useCurrentFrame()
  const { width: W, height: H } = useVideoConfig()
  const port = H > W
  const L = port ? PORT : LAND
  const AIR: Pt[] = [...L.air, ...L.net]
  const USB = L.usb
  const cam = breathe(f, D, L.wide, port ? 0.05 : 0.07, 4)
  const cap = L.cap
  const keys = showKeys(t, f, lastKey(f, 'land'))
  const lit = keys.map(() => 1)
  const pulses = (fn: (p: number) => number) =>
    keys.map((_, i) => Math.max(0, ...PRESSES.map(({ p, key }) => (i === key ? fn(p) : 0))))
  const flash = keys.map((_, i) =>
    i < 2 ? Math.max(...PRESSES.map(({ p }) => 0.5 * pulse(f, ev(p).land, ev(p).land + 16, 1, 14))) : 0,
  )
  const down = pulses((p) => pulse(f, p - 2, p + 9, 2, 6))
  const ring = pulses((p) => pulse(f, p, p + 40, 2, 14))
  const any = (fn: (e: ReturnType<typeof ev>) => number) => Math.max(...PRESSES.map(({ p }) => fn(ev(p))))
  const usbHot = any((e) =>
    Math.max(pulse(f, e.usbUp[0], e.usbUp[1] + 6, 2, 6), pulse(f, e.usbDown[0], e.usbDown[1] + 6, 2, 6)),
  )
  const airHot = any((e) =>
    Math.max(pulse(f, e.airUp[0], e.airUp[1] + 6, 2, 6), pulse(f, e.airDown[0], e.airDown[1] + 6, 2, 6)),
  )
  const hot = any((e) =>
    Math.max(pulse(f, e.usbUp[1] - 2, e.airUp[0] + 8, 2, 8), pulse(f, e.airDown[1] - 2, e.usbDown[0] + 8, 2, 8)),
  )
  const ant = at(L.board, BOARD_ANT)
  const tip: Pt = [L.router.x + 40, L.router.y - 46]

  return (
    <Scene
      w={W}
      h={H}
      cam={cam}
      over={
        <>
          <Caption f={f} from={-30} to={390} text={t.heroWifi} sub={t.heroWifiSub} dot={GREEN} size={cap} />
        </>
      }
    >
      <Pool
        x={L.deck.x + 70 * L.deck.s}
        y={L.deck.y + 125 * L.deck.s}
        rx={130 * L.deck.s}
        ry={28 * L.deck.s}
        o={0.22}
        c={AMBER}
      />
      <Pool
        x={L.mon.x + 240 * L.mon.s}
        y={L.mon.y + 170 * L.mon.s}
        rx={400 * L.mon.s}
        ry={300 * L.mon.s}
        c="#5aa9ff"
        o={0.12}
      />
      <Cable pts={USB} w={14} glow={usbHot} />
      <Cable pts={L.net} w={12} color="#2a2f36" glow={airHot} />
      {L.power && <Cable pts={L.power.pts} w={10} color="#2a2f36" />}
      <Monitor t={t} f={f} m={L.mon} />
      <Pool x={ant[0]} y={ant[1]} rx={220} ry={160} c={GREEN} o={0.12 + 0.12 * airHot} />
      <Router f={f} {...L.router} />
      {L.power && <Plug {...L.power.plug} />}
      <Air pts={L.air.slice(0, 17)} f={f} on={0.6 + 0.4 * airHot} />
      <Waves x={tip[0]} y={tip[1]} f={f + 18} size={port ? 34 : 30} deg={-50} />
      <Board
        {...L.board}
        link={0}
        wifi={0.55 + 0.45 * airHot}
        act={hot > 0.3 && Math.floor(f / 3) % 2 ? 1 : 0}
        hot={hot}
        sheen={Math.sin((f / 360) * Math.PI * 2)}
      />
      <Deck {...L.deck} keys={keys} strip={showStrip(40)} lit={lit} flash={flash} down={down} ring={ring} sheen={0.1} />
      {PRESSES.map(({ p }) => {
        const e = ev(p)
        return (
          <g key={p}>
            <Trail pts={USB} t={lin(f, e.usbUp[0], e.usbUp[1], 0, 1.3)} w={16} len={0.3} />
            <Trail pts={AIR} t={lin(f, e.airUp[0], e.airUp[1], 0, 1.3)} w={16} len={0.28} />
            {[0, 6].map((k) => (
              <g key={k}>
                <Trail
                  pts={rev(AIR)}
                  t={lin(f, e.airDown[0] + k, e.airDown[1] + k, 0, 1.3)}
                  w={16}
                  len={0.28}
                  color={k ? '#ffffff' : GREEN}
                />
                <Trail
                  pts={rev(USB)}
                  t={lin(f, e.usbDown[0] + k, e.usbDown[1] + k, 0, 1.3)}
                  w={16}
                  len={0.3}
                  color={k ? '#ffffff' : GREEN}
                />
              </g>
            ))}
          </g>
        )
      })}
      {PRESSES.map(({ p, key }) => (
        <PressBurst key={p} f={f} p={p} k={key} deck={L.deck} />
      ))}
      <Label x={L.label.x} y={L.label.y} size={L.label.size}>
        {t.board}
      </Label>
    </Scene>
  )
}

function PressBurst({ f, p, k: key, deck }: { f: number; p: number; k: number; deck: Layout['deck'] }) {
  const k = lin(f, p, p + 18)
  if (k <= 0 || k >= 1) return null
  const [x, y] = keyCenter(deck, key)
  const u = deck.s / 4.4
  return (
    <circle
      cx={x}
      cy={y}
      r={(50 + 90 * k) * u}
      fill="none"
      stroke={AMBER}
      strokeWidth={6 * u * (1 - k)}
      opacity={1 - k}
    />
  )
}

function Monitor({ t, f, m }: { t: Text; f: number; m: Layout['mon'] }) {
  const [x, y, w, h] = [0, 0, 480, 340]
  const keys = showKeys(t, f, lastKey(f, 'hit'))
  const btn = 92
  const gap = 12
  const gx = x + (w - (4 * btn + 3 * gap)) / 2
  const hits = PRESSES.map(({ p, key }) => ({ key, v: pulse(f, ev(p).hit, ev(p).hit + 34, 2, 14) }))
  const log = fill(t.keyDown, { n: lastKey(f, 'hit') + 1 })
  const logHot = Math.max(...hits.map((h) => h.v))
  return (
    <g transform={`translate(${m.x} ${m.y}) scale(${m.s})`}>
      <rect x={x + w / 2 - 36} y={y + h + 8} width={72} height={42} fill="#1b1e22" />
      <rect x={x + w / 2 - 100} y={y + h + 46} width={200} height={12} rx={6} fill="#22262b" />
      <rect
        x={x - 14}
        y={y - 14}
        width={w + 28}
        height={h + 28}
        rx={20}
        fill="#0d0f11"
        stroke="#2d3238"
        strokeWidth={2}
      />
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
            {hits.map(
              (h) =>
                i === h.key &&
                h.v > 0 && (
                  <rect
                    key={h.key}
                    x={bx - 5}
                    y={by - 5}
                    width={btn + 10}
                    height={btn + 10}
                    rx={14}
                    fill="none"
                    stroke={AMBER}
                    strokeWidth={5}
                    opacity={h.v}
                  />
                ),
            )}
          </g>
        )
      })}
      <rect
        x={x + 20}
        y={y + h - 62}
        width={w - 40}
        height={48}
        rx={10}
        fill="#0a0b0d"
        stroke={AMBER}
        strokeOpacity={logHot}
        strokeWidth={3}
      />
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
