import { useCurrentFrame } from 'remotion'
import {
  AMBER,
  at,
  Board,
  BOARD_ANT,
  BOARD_RJ45,
  BOARD_USB,
  Cable,
  Deck,
  GREEN,
  INK,
  KEY,
  Pool,
  SANS,
  rounded,
  Trail,
  Zone,
  ZONE_ICONS,
  type Key,
  type Pt,
  showKeys,
} from './hw'
import { breathe, Caption, Scene } from './scene'
import { lin, pulse, ramp } from './time'
import { TEXT, type LangProps, type Text } from './text'

const S = 1080
const SW = { x: 780, y: 140, w: 220, h: 104 }
const AP = { x: 80, y: 324, w: 220, h: 104 }
const BOARD = { x: 420, y: 300, s: 1.3 }
const DECK = { x: 232, y: 560, s: 4.4 }
const RJ45 = at(BOARD, BOARD_RJ45)
const ANT = at(BOARD, BOARD_ANT)
const UP_LAN: Pt[] = [
  [SW.x + SW.w / 2, -300],
  [SW.x + SW.w / 2, SW.y],
]
const UP_AP = rounded([
  [-60, -300],
  [-60, AP.y + AP.h / 2],
  [AP.x, AP.y + AP.h / 2],
])
const LAN = rounded([[SW.x + SW.w / 2, SW.y + SW.h], [SW.x + SW.w / 2, RJ45[1]], RJ45])
const AIR: Pt[] = Array.from({ length: 13 }, (_, i) => {
  const u = i / 12
  const a: Pt = [AP.x + AP.w, AP.y + AP.h / 2]
  const c: Pt = [(a[0] + ANT[0]) / 2, ANT[1] - 60]
  return [
    (1 - u) ** 2 * a[0] + 2 * u * (1 - u) * c[0] + u * u * ANT[0],
    (1 - u) ** 2 * a[1] + 2 * u * (1 - u) * c[1] + u * u * ANT[1],
  ]
})
const USB = rounded([at(BOARD, BOARD_USB), [330, at(BOARD, BOARD_USB)[1]], [330, 620], [380, 620]])
const VIA_LAN = [...UP_LAN, ...LAN]
const VIA_AIR = [...UP_AP, ...AIR]

// out: the switch's uplink is pulled (PoE to the board stays); wifi: first frame over Wi-Fi; plug: uplink back; lan: traffic back on LAN.
const T = { out: 66, wifi: 126, plug: 214, lan: 236 } as const
const EVERY = 15
const RIDE = 30
const DOWN = 12

const WIDE = { x: 540, y: 592, z: 0.87, rx: 5, ry: 0 }

export default function Failover({ lang }: LangProps) {
  const f = useCurrentFrame()
  const t = TEXT[lang]
  const pull = ramp(f, T.out, T.out + 10) * (1 - ramp(f, T.plug - 10, T.plug))
  const plugged = f < T.out || f >= T.plug
  const wifiOn = ramp(f, T.wifi - 6, T.wifi) * (1 - ramp(f, T.lan, T.lan + 10))
  const lanOn = f < T.out ? 1 : ramp(f, T.lan - 4, T.lan + 4)

  // Bursts leave Companion every few frames over whichever link carries the deck at that moment.
  const trails: { pts: Pt[]; t: number; c: string }[] = []
  const usbTrails: typeof trails = []
  for (let s0 = Math.floor((f - RIDE - DOWN - 10) / EVERY) * EVERY; s0 <= f; s0 += EVERY) {
    const air = s0 >= T.wifi && s0 < T.lan - 4
    const lan = s0 < T.out - RIDE * 0.8 || s0 >= T.lan
    if (!air && !lan) continue
    const pts = air ? VIA_AIR : VIA_LAN
    trails.push({ pts, t: lin(f, s0, s0 + RIDE, 0, 1.25), c: AMBER })
    usbTrails.push({ pts: USB, t: lin(f, s0 + RIDE, s0 + RIDE + DOWN, 0, 1.3), c: AMBER })
  }

  // The deck keeps its picture through the gap; the meters stand still until frames flow again.
  const still = f >= T.out && f < T.wifi + 2 ? T.out : f
  const meter = (i: number) => 45 + 30 * Math.sin((still / 330) * Math.PI * 2 * (2 + i) + i * 1.7)
  const keys: Key[] = showKeys(t, 0).map((k, i) =>
    i === 6 ? { bg: '#0d0f12', glow: GREEN, art: <Meter v={[meter(0), meter(1), meter(2)]} /> } : k,
  )
  const port: Pt = [SW.x + SW.w / 2, SW.y]
  const upEnd: Pt = [port[0] + 70 * pull, port[1] - 80 * pull]
  const yank = pulse(f, T.out, T.out + 20, 2, 16)
  const seat = pulse(f, T.plug, T.plug + 18, 2, 14)
  const held =
    f >= T.out + 6 && f < T.wifi ? Math.min(ramp(f, T.out + 6, T.out + 14), 1 - ramp(f, T.wifi - 6, T.wifi)) : 0

  return (
    <Scene w={S} h={S} cam={breathe(f, 330, WIDE)} over={<Status t={t} f={f} />}>
      <Pool x={540} y={1000} rx={520} ry={260} c={AMBER} o={0.12} />
      <Cable pts={[UP_LAN[0] ?? port, upEnd]} w={10} glow={lanOn * 0.5} />
      <Cable pts={UP_AP} w={10} glow={wifiOn * 0.5} />
      <Cable pts={LAN} w={12} glow={0.4} />
      <rect x={SW.x + SW.w / 2 + 14} y={(SW.y + SW.h + RJ45[1]) / 2 - 20} width={86} height={40} rx={20} fill={AMBER} />
      <text
        x={SW.x + SW.w / 2 + 57}
        y={(SW.y + SW.h + RJ45[1]) / 2 + 9}
        textAnchor="middle"
        fontFamily={SANS}
        fontSize={26}
        fontWeight={700}
        fill="#1a1200"
      >
        PoE
      </text>
      {pull > 0 && (
        <g
          transform={`translate(${upEnd[0]} ${upEnd[1]}) rotate(${90 + pull * 25 + (f > T.out ? 22 * Math.sin((f - T.out) * 0.45) * Math.exp(-(f - T.out) / 14) : 0)})`}
        >
          <rect x={-6} y={-16} width={40} height={32} rx={4} fill="#c9ccd1" />
          <rect x={-6} y={-8} width={12} height={16} fill="#8d939b" />
        </g>
      )}
      {!plugged && (
        <circle
          cx={port[0]}
          cy={port[1]}
          r={60 + 50 * yank}
          fill={`url(#dc-glow-${AMBER.slice(1)})`}
          opacity={0.45 + 0.55 * yank}
        />
      )}
      {seat > 0 && (
        <circle cx={port[0]} cy={port[1]} r={60 + 40 * seat} fill={`url(#dc-glow-${GREEN.slice(1)})`} opacity={seat} />
      )}
      <path
        d={AIR.map((p, i) => `${i ? 'L' : 'M'}${p[0].toFixed(1)},${p[1].toFixed(1)}`).join('')}
        fill="none"
        stroke={wifiOn > 0.05 ? AMBER : '#4a5058'}
        strokeWidth={11}
        strokeLinecap="round"
        strokeDasharray="1 24"
        strokeDashoffset={-f * 2}
        opacity={0.6 + 0.4 * wifiOn}
      />
      <Cable pts={USB} w={12} />
      {trails.map((tr, i) => (
        <Trail key={i} pts={tr.pts} t={tr.t} color={tr.c} w={11} len={0.18} />
      ))}
      <Box {...AP} label={t.wifi} live={wifiOn} radio f={f} />
      <Box {...SW} label={t.lan} live={lanOn} f={f} />
      <Board
        {...BOARD}
        link={1}
        act={Math.floor(f / 4) % 2 && (f < T.out || f >= T.wifi) ? 1 : 0}
        wifi={0.25 + 0.75 * wifiOn}
        hot={wifiOn * 0.5 + yank * 0.3}
        sheen={Math.sin((f / 330) * Math.PI * 2)}
      />
      {yank > 0 && Math.floor(f / 2) % 2 === 0 && (
        <circle
          cx={at(BOARD, [184.5, 56])[0]}
          cy={at(BOARD, [184.5, 56])[1]}
          r={22}
          fill={`url(#dc-glow-${AMBER.slice(1)})`}
        />
      )}
      <Deck
        {...DECK}
        keys={keys}
        strip={
          <>
            {ZONE_ICONS.map((icon, i) => (
              <Zone key={i} i={i} icon={icon} value={meter(i)} accent={KEY.audio.fg} />
            ))}
          </>
        }
        sheen={Math.sin((f / 330) * Math.PI * 2) * 0.5}
      >
        {held > 0 && (
          <rect
            x={12}
            y={8}
            width={116}
            height={74}
            rx={4}
            fill="none"
            stroke={AMBER}
            strokeWidth={0.9}
            strokeDasharray="3 2"
            strokeDashoffset={-f * 0.2}
            opacity={held}
          />
        )}
      </Deck>
      {usbTrails.map((tr, i) => (
        <Trail key={i} pts={tr.pts} t={tr.t} color={tr.c} w={11} len={0.18} />
      ))}
    </Scene>
  )
}

function Box({
  x,
  y,
  w,
  h,
  label,
  live,
  radio,
  f,
}: {
  x: number
  y: number
  w: number
  h: number
  label: string
  live: number
  radio?: boolean
  f: number
}) {
  const c = GREEN
  return (
    <g>
      {live > 0 && (
        <circle cx={x + w / 2} cy={y + h / 2} r={w * 0.75} fill={`url(#dc-glow-${c.slice(1)})`} opacity={live * 0.35} />
      )}
      {radio &&
        [x + 40, x + w - 40].map((ax) => (
          <rect key={ax} x={ax - 5} y={y - 44} width={10} height={50} rx={5} fill="#2d3238" />
        ))}
      <rect
        x={x}
        y={y}
        width={w}
        height={h}
        rx={14}
        fill="#1b1e22"
        stroke={live > 0.5 ? c : '#2d3238'}
        strokeWidth={3}
      />
      <text x={x + 22} y={y + 46} fontFamily={SANS} fontSize={40} fontWeight={650} fill={live > 0.5 ? INK : '#858c94'}>
        {label}
      </text>
      {Array.from({ length: 6 }, (_, i) => (
        <circle
          key={i}
          cx={x + 30 + i * 32}
          cy={y + 78}
          r={6}
          fill={live > 0.5 && (radio || Math.floor(f / 3 + i) % 3) ? c : '#3b4148'}
        />
      ))}
      {radio && live > 0 && (
        <g
          transform={`translate(${x + w + 12} ${y + h / 2}) rotate(-90)`}
          fill="none"
          stroke={GREEN}
          strokeWidth={5}
          strokeLinecap="round"
        >
          {[0, 1, 2].map((i) => {
            const k = (f / 24 + i / 3) % 1
            return (
              <path
                key={i}
                d={`M${-30 - k * 60} ${k * 20} a${40 + k * 80} ${40 + k * 80} 0 0 0 ${60 + k * 120} 0`}
                opacity={live * (1 - k)}
              />
            )
          })}
        </g>
      )}
    </g>
  )
}

function Status({ t, f }: { t: Text; f: number }) {
  return (
    <>
      <Caption f={f} from={-30} to={T.out + 2} text={t.onLan} dot={GREEN} size={48} />
      <Caption f={f} from={T.out} to={T.wifi + 2} text={t.unplugged} sub={t.pictureHeld} size={48} />
      <Caption
        f={f}
        from={T.wifi}
        to={T.lan + 2}
        text={t.onWifi}
        big="≈ 6 s"
        sub={t.stillDrawing}
        dot={GREEN}
        size={48}
      />
      <Caption f={f} from={T.lan} to={304} text={t.backOnLan} dot={GREEN} size={48} />
      <Caption f={f} from={300} to={340} text={t.onLan} dot={GREEN} size={48} />
    </>
  )
}

function Meter({ v }: { v: number[] }) {
  return (
    <>
      {v.map((h, i) => (
        <g key={i}>
          <rect x={20 + i * 30} y={16} width={22} height={88} rx={4} fill="#1d2126" />
          <rect x={20 + i * 30} y={104 - h * 0.88} width={22} height={h * 0.88} rx={4} fill={h > 66 ? AMBER : GREEN} />
        </g>
      ))}
    </>
  )
}
