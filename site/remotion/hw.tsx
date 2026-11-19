import { useId, type ComponentType, type ReactNode } from 'react'
import { Clock, Lightbulb, Mic, Music, Pause, Presentation, Speaker, Video, Volume2 } from 'lucide-react'
import { useVideoConfig } from 'remotion'
import type { Text } from './text'
import { AMBER, GREEN, Glow, glowId, type Pt } from './board'

export * from './board'

export const SANS = "'Geist Variable', ui-sans-serif, system-ui, sans-serif"
export const INK = '#f2f2ef'

/** Text drawn on the stage itself: dark in the page's light theme. */
export const useInk = () => (useVideoConfig().props.light === true ? '#15181c' : INK)

export function along(pts: readonly Pt[], t: number): Pt {
  const segs: [Pt, Pt, number][] = []
  pts.forEach((b, i) => {
    const a = pts[i - 1]
    if (a) segs.push([a, b, Math.hypot(b[0] - a[0], b[1] - a[1])])
  })
  let d = Math.max(0, Math.min(1, t)) * segs.reduce((s, g) => s + g[2], 0)
  for (const [a, b, l] of segs) {
    if (d <= l) {
      const k = l ? d / l : 0
      return [a[0] + (b[0] - a[0]) * k, a[1] + (b[1] - a[1]) * k]
    }
    d -= l
  }
  return pts.at(-1) ?? [0, 0]
}
/** A polyline with rounded corners, sampled so light can run along the curve. */
export function rounded(pts: readonly Pt[], r = 40): Pt[] {
  const out: Pt[] = []
  pts.forEach((b, i) => {
    const a = pts[i - 1]
    const c = pts[i + 1]
    if (!a || !c) return out.push(b)
    const l1 = Math.hypot(b[0] - a[0], b[1] - a[1])
    const l2 = Math.hypot(c[0] - b[0], c[1] - b[1])
    const k = Math.min(r, l1 / 2, l2 / 2)
    const p: Pt = [b[0] - ((b[0] - a[0]) / l1) * k, b[1] - ((b[1] - a[1]) / l1) * k]
    const q: Pt = [b[0] + ((c[0] - b[0]) / l2) * k, b[1] + ((c[1] - b[1]) / l2) * k]
    for (let j = 0; j <= 6; j++) {
      const u = j / 6
      out.push([
        (1 - u) ** 2 * p[0] + 2 * u * (1 - u) * b[0] + u * u * q[0],
        (1 - u) ** 2 * p[1] + 2 * u * (1 - u) * b[1] + u * u * q[1],
      ])
    }
  })
  return out
}
const path = (pts: readonly Pt[]) => pts.map((p, i) => `${i ? 'L' : 'M'}${p[0].toFixed(1)},${p[1].toFixed(1)}`).join('')

export function Cable({
  pts,
  w = 12,
  color = '#23272d',
  glow = 0,
  glowColor = AMBER,
  dash,
  opacity = 1,
}: {
  pts: readonly Pt[]
  w?: number
  color?: string
  glow?: number
  glowColor?: string
  dash?: string
  opacity?: number
}) {
  const d = path(pts)
  return (
    <g opacity={opacity} fill="none" strokeLinecap="round" strokeLinejoin="round">
      <path
        d={d}
        stroke="#000"
        strokeOpacity={0.5}
        strokeWidth={w * 1.6}
        transform={`translate(0 ${w * 0.5})`}
        strokeDasharray={dash}
      />
      <path d={d} stroke={color} strokeWidth={w} strokeDasharray={dash} />
      {glow > 0 && (
        <path d={d} stroke={glowColor} strokeOpacity={glow * 0.55} strokeWidth={w * 0.5} strokeDasharray={dash} />
      )}
      <path
        d={d}
        stroke="#fff"
        strokeOpacity={0.13}
        strokeWidth={w * 0.22}
        transform={`translate(0 ${-w * 0.22})`}
        strokeDasharray={dash}
      />
    </g>
  )
}

/** A burst of data: a hot head and a tail that fades behind it along the path. t runs 0..1 (plus len to clear). */
export function Trail({
  pts,
  t,
  len = 0.22,
  color = AMBER,
  w = 12,
}: {
  pts: readonly Pt[]
  t: number
  len?: number
  color?: string
  w?: number
}) {
  if (t <= 0 || t - len >= 1) return null
  const n = 14
  const segs: ReactNode[] = []
  for (let i = 0; i < n; i++) {
    const a = t - len * (1 - i / n)
    const b = t - len * (1 - (i + 1) / n)
    if (b <= 0 || a >= 1) continue
    const p = along(pts, a)
    const q = along(pts, b)
    const k = (i + 1) / n
    segs.push(
      <line
        key={i}
        x1={p[0]}
        y1={p[1]}
        x2={q[0]}
        y2={q[1]}
        stroke={color}
        strokeWidth={w * (0.25 + 0.75 * k)}
        strokeOpacity={k * k}
        strokeLinecap="round"
      />,
    )
  }
  const h = along(pts, t)
  return (
    <g>
      {segs}
      {t < 1 && (
        <>
          <circle cx={h[0]} cy={h[1]} r={w * 5} fill={`url(#${glowId(color)})`} />
          <circle cx={h[0]} cy={h[1]} r={w * 0.62} fill="#fff" />
        </>
      )}
    </g>
  )
}

/** A spot of light on the desk under a device. */
export function Pool({
  x,
  y,
  rx,
  ry,
  c = '#ffffff',
  o = 0.06,
}: {
  x: number
  y: number
  rx: number
  ry: number
  c?: string
  o?: number
}) {
  return <ellipse cx={x} cy={y} rx={rx} ry={ry} fill={`url(#${glowId(c)})`} opacity={o} />
}

export function Label({
  x,
  y,
  children,
  sub,
  anchor = 'middle',
  size = 34,
  o = 1,
}: {
  x: number
  y: number
  children: ReactNode
  sub?: ReactNode
  anchor?: 'start' | 'middle' | 'end'
  size?: number
  o?: number
}) {
  const ink = useInk()
  return (
    <text
      x={x}
      y={y}
      textAnchor={anchor}
      fontFamily={SANS}
      fontSize={size}
      fontWeight={600}
      fill={ink}
      letterSpacing={-size * 0.02}
      opacity={o}
    >
      {children}
      {sub && (
        <tspan x={x} dy={size * 1.2} fontSize={size * 0.72} fontWeight={500} fill="#8b929a">
          {sub}
        </tspan>
      )}
    </text>
  )
}

export type Glyph = ComponentType<{
  x?: number
  y?: number
  width?: number
  height?: number
  color?: string
  strokeWidth?: number
}>
/** One key picture on the 120 x 120 pixels a Stream Deck + key has: an icon over one bold word, coloured when on. */
export type Key = { icon?: Glyph; word?: string; bg?: string; fg?: string; glow?: string; art?: ReactNode }
export const TILE = '#26282c'
// The demo's key colours (web/src/demo/model.ts): grey when off, one colour per meaning when on.
export const KEY = {
  live: { on: '#e5322b' },
  audio: { on: '#1f9d55', fg: '#5fd08c' },
  light: { on: '#f5a524', fg: '#f5b748', ink: '#1a1200' },
  show: { on: '#3a78ee' },
} as const

export function Face({ k }: { k: Key }) {
  const fg = k.fg ?? (k.bg ? '#fff' : '#d4d7dc')
  const word = k.word ?? ''
  return (
    <>
      <rect width={120} height={120} fill={k.bg ?? TILE} />
      {k.art}
      {k.icon &&
        (word ? (
          <k.icon x={38} y={16} width={44} height={44} color={fg} strokeWidth={2.4} />
        ) : (
          <k.icon x={32} y={32} width={56} height={56} color={fg} strokeWidth={2.2} />
        ))}
      {word && (
        <text
          x={60}
          y={k.icon ? 98 : 72}
          textAnchor="middle"
          fontFamily={SANS}
          fontSize={28}
          fontWeight={800}
          fill={fg}
          letterSpacing={-0.5}
        >
          {word}
        </text>
      )}
    </>
  )
}

const dot =
  (c: string): Glyph =>
  ({ x, y, width, height }) => (
    <svg x={x} y={y} width={width} height={height} viewBox="0 0 24 24">
      <circle cx={12} cy={12} r={8} fill={c} />
    </svg>
  )
const REC = dot('#fff')

// Stream Deck + front in millimetres, measured on a real deck (web/src/deck/models.ts).
export const SD = {
  W: 140,
  H: 133.4,
  R: 11,
  k: 18.35,
  kr: 2.4,
  li: 1.075,
  x0: 16.68,
  y0: 11.75,
  px: 29.565,
  py: 21.6,
  win: { x: 10.33, y: 57.02, w: 119.32, h: 23.61 },
  strip: { x: 16, y: 62.5, w: 108, h: 13.5 },
  tray: { x: 10.08, y: 88.2, w: 119.39, h: 35.5, r: 6.5 },
  dials: { cy: 105.95, d: 14.4, x: 11.08, w: 118.26 },
}
export const keyBox = (i: number) => ({ x: SD.x0 + (i % 4) * SD.px, y: SD.y0 + Math.floor(i / 4) * SD.py, s: SD.k })
export const keyCenter = (d: { x: number; y: number; s: number }, i: number): Pt => {
  const b = keyBox(i)
  return [d.x + (b.x + b.s / 2) * d.s, d.y + (b.y + b.s / 2) * d.s]
}
export const dialX = (i: number) => SD.dials.x + (SD.dials.w / 4) * (i + 0.5)

export type DeckProps = {
  x: number
  y: number
  /** px per mm */
  s: number
  keys: (Key | null)[]
  /** The pictures a wipe runs over. */
  under?: (Key | null)[]
  strip?: ReactNode
  /** 0..1 per key: how far the picture has wiped in from the top, so keys can ignite one by one. */
  lit?: number[]
  /** 0..1 per key: a white flash as a new picture lands. */
  flash?: number[]
  /** 0..1 per key: how far it is pushed in. */
  down?: number[]
  /** 0..1 per key: amber ring. */
  ring?: number[]
  /** degrees per dial */
  dial?: number[]
  dialRing?: number[]
  stripLit?: number
  /** Colour of the bright line that leads a wipe. */
  edge?: string
  /** -1..1, where the reflection of the room sits on the body. */
  sheen?: number
  /** Drawn on top, in millimetres. */
  children?: ReactNode
}

export function Deck({
  x,
  y,
  s,
  keys,
  under = [],
  strip,
  lit = [],
  flash = [],
  down = [],
  ring = [],
  dial = [],
  dialRing = [],
  stripLit = 1,
  edge = '#fff',
  sheen = 0,
  children,
}: DeckProps) {
  const id = useId().replace(/:/g, '')
  const fi = SD.li
  const L = (i: number) => (keys[i] ? (lit[i] ?? 1) : 0)
  return (
    <g transform={`translate(${x} ${y}) scale(${s})`}>
      <defs>
        <linearGradient id={`${id}b`} x1="0" y1="0" x2="0" y2="1">
          <stop offset="0" stopColor="#2a2d33" />
          <stop offset="0.08" stopColor="#1d1f23" />
          <stop offset="0.6" stopColor="#141518" />
          <stop offset="1" stopColor="#0d0e10" />
        </linearGradient>
        <linearGradient id={`${id}r`} x1="0" y1="0" x2="1" y2="0.55">
          <stop offset={0.3 + sheen * 0.25} stopColor="#fff" stopOpacity={0} />
          <stop offset={0.42 + sheen * 0.25} stopColor="#fff" stopOpacity={0.07} />
          <stop offset={0.47 + sheen * 0.25} stopColor="#fff" stopOpacity={0.02} />
          <stop offset={0.62 + sheen * 0.25} stopColor="#fff" stopOpacity={0} />
        </linearGradient>
        <linearGradient id={`${id}g`} x1="0" y1="0" x2="0.7" y2="1">
          <stop offset="0" stopColor="#fff" stopOpacity={0.2} />
          <stop offset="0.35" stopColor="#fff" stopOpacity={0.03} />
          <stop offset="0.36" stopColor="#fff" stopOpacity={0} />
        </linearGradient>
        <linearGradient id={`${id}t`} x1="0" y1="0" x2="0" y2="1">
          <stop offset="0" stopColor="#08090a" />
          <stop offset="1" stopColor="#121316" />
        </linearGradient>
        <radialGradient id={`${id}c`} cx="0.35" cy="0.28" r="0.75">
          <stop offset="0" stopColor="#6a7078" />
          <stop offset="0.55" stopColor="#2c2f35" />
          <stop offset="1" stopColor="#1a1c20" />
        </radialGradient>
        <radialGradient id={`${id}sh`} cx="0.5" cy="0.5" r="0.5">
          <stop offset="0" stopColor="#000" stopOpacity={0.8} />
          <stop offset="1" stopColor="#000" stopOpacity={0} />
        </radialGradient>
        {keys.map((k, i) => (k ? <Glow key={i} c={k.glow ?? k.bg ?? TILE} /> : null))}
        {keys.map((_, i) => (
          <clipPath key={i} id={`${id}k${i}`}>
            <rect width={120} height={120} rx={10} />
          </clipPath>
        ))}
        {keys.map((_, i) => (
          <clipPath key={i} id={`${id}w${i}`}>
            <rect width={120} height={120 * L(i)} />
          </clipPath>
        ))}
      </defs>
      <ellipse cx={SD.W / 2} cy={SD.H + 2} rx={SD.W * 0.62} ry={14} fill={`url(#${id}sh)`} />
      <rect x={-1.2} y={1.4} width={SD.W + 2.4} height={SD.H} rx={SD.R + 1} fill="#050506" />
      <rect width={SD.W} height={SD.H} rx={SD.R} fill={`url(#${id}b)`} />
      <rect width={SD.W} height={SD.H} rx={SD.R} fill={`url(#${id}r)`} />
      <rect
        x={0.25}
        y={0.25}
        width={SD.W - 0.5}
        height={SD.H - 0.5}
        rx={SD.R}
        fill="none"
        stroke="#fff"
        strokeOpacity={0.12}
        strokeWidth={0.5}
      />
      {keys.map((k, i) => {
        const g = L(i) * (0.55 + 0.45 * (flash[i] ?? 0))
        if (!k || g < 0.02) return null
        const b = keyBox(i)
        const c = b.s / 2
        return (
          <circle
            key={i}
            cx={b.x + c}
            cy={b.y + c}
            r={b.s * (1.05 + 0.25 * (flash[i] ?? 0))}
            fill={`url(#${glowId(k.glow ?? k.bg ?? TILE)})`}
            opacity={g * 0.75}
          />
        )
      })}
      {keys.map((k, i) => {
        const b = keyBox(i)
        const d = down[i] ?? 0
        const r = ring[i] ?? 0
        const c = b.s / 2
        const on = L(i)
        const fl = k ? (flash[i] ?? 0) : 0
        return (
          <g
            key={i}
            transform={`translate(${b.x + c} ${b.y + c + d * 0.5}) scale(${1 - d * 0.05}) translate(${-c} ${-c})`}
          >
            <rect x={-0.6} y={-0.6} width={b.s + 1.2} height={b.s + 1.2} rx={SD.kr + 0.5} fill="#050607" />
            <rect width={b.s} height={b.s} rx={SD.kr} fill="#020303" />
            <svg x={fi} y={fi} width={b.s - 2 * fi} height={b.s - 2 * fi} viewBox="0 0 120 120">
              <g clipPath={`url(#${id}k${i})`}>
                <rect width={120} height={120} fill="#030405" />
                {on < 1 && under[i] && <Face k={under[i]} />}
                {k && on > 0.01 && (
                  <g clipPath={on < 1 ? `url(#${id}w${i})` : undefined}>
                    <Face k={k} />
                  </g>
                )}
                {k && on > 0.01 && on < 1 && (
                  <>
                    <rect y={120 * on - 16} width={120} height={16} fill={edge} opacity={0.4} />
                    <rect y={120 * on - 4} width={120} height={5} fill={edge} />
                  </>
                )}
                {fl > 0.01 && <rect width={120} height={120} fill="#fff" opacity={fl * 0.45} />}
              </g>
            </svg>
            <rect width={b.s} height={b.s} rx={SD.kr} fill={`url(#${id}g)`} />
            {r > 0 && (
              <rect
                x={-1.3}
                y={-1.3}
                width={b.s + 2.6}
                height={b.s + 2.6}
                rx={SD.kr + 1.3}
                fill="none"
                stroke={AMBER}
                strokeWidth={1}
                opacity={r}
              />
            )}
          </g>
        )
      })}
      <rect {...SD.win} rx={0.8} fill="#050607" />
      {stripLit > 0.02 && (
        <rect
          x={SD.strip.x - 6}
          y={SD.strip.y - 5}
          width={SD.strip.w + 12}
          height={SD.strip.h + 10}
          rx={6}
          fill={`url(#${glowId(AMBER)})`}
          opacity={stripLit * 0.22}
        />
      )}
      <svg
        x={SD.strip.x}
        y={SD.strip.y}
        width={SD.strip.w}
        height={SD.strip.h}
        viewBox="0 0 800 100"
        preserveAspectRatio="none"
      >
        <rect width={800} height={100} fill="#020303" />
        <g opacity={stripLit}>{strip}</g>
      </svg>
      <rect {...SD.strip} rx={0.8} fill={`url(#${id}g)`} opacity={0.7} />
      <rect x={SD.tray.x} y={SD.tray.y} width={SD.tray.w} height={SD.tray.h} rx={SD.tray.r} fill={`url(#${id}t)`} />
      <rect
        x={SD.tray.x}
        y={SD.tray.y + SD.tray.h - 0.6}
        width={SD.tray.w}
        height={0.6}
        rx={0.3}
        fill="#fff"
        opacity={0.06}
      />
      {[0, 1, 2, 3].map((i) => {
        const cx = dialX(i)
        const r = SD.dials.d / 2
        const rot = dial[i] ?? 0
        const g = dialRing[i] ?? 0
        return (
          <g key={i} transform={`translate(${cx} ${SD.dials.cy})`}>
            <circle r={r + 0.8} cy={1.6} fill="#000" opacity={0.6} />
            <circle r={r} fill="#24272c" />
            <g transform={`rotate(${rot})`} stroke="#41464d" strokeWidth={0.45}>
              {Array.from({ length: 36 }, (_, j) => (
                <line key={j} x1={0} y1={-r + 0.2} x2={0} y2={-r * 0.8} transform={`rotate(${j * 10})`} />
              ))}
            </g>
            <circle r={r * 0.7} fill={`url(#${id}c)`} />
            <circle r={r * 0.7} fill="none" stroke="#fff" strokeOpacity={0.1} strokeWidth={0.25} />
            <rect
              x={-0.35}
              y={-r * 0.64}
              width={0.7}
              height={r * 0.28}
              rx={0.35}
              fill="#b8bdc4"
              transform={`rotate(${rot})`}
            />
            {g > 0 && <circle r={r + 1.2} fill="none" stroke={AMBER} strokeWidth={0.9} opacity={g} />}
          </g>
        )
      })}
      {children}
    </g>
  )
}

/** A touch strip zone in its 200 x 100 slot: icon, level, bar. */
export function Zone({
  i,
  icon: I,
  value,
  accent = AMBER,
  active = 0,
}: {
  i: number
  icon: Glyph
  value: number
  accent?: string
  active?: number
}) {
  return (
    <g transform={`translate(${i * 200} 0)`}>
      <rect x={4} y={4} width={192} height={92} rx={8} fill="#0d0f12" />
      {active > 0 && <rect x={4} y={4} width={192} height={92} rx={8} fill={accent} opacity={active * 0.25} />}
      <I x={18} y={18} width={50} height={50} color={accent} strokeWidth={2.4} />
      <text x={182} y={62} textAnchor="end" fontFamily={SANS} fontSize={40} fontWeight={800} fill="#fff">
        {Math.round(value)}
        <tspan fontSize={22} fontWeight={700} dx={2}>
          %
        </tspan>
      </text>
      <rect x={18} y={82} width={164} height={6} rx={3} fill="#2a2e34" />
      <rect x={18} y={82} width={1.64 * value} height={6} rx={3} fill={accent} />
    </g>
  )
}

export const ZONE_ICONS = [Volume2, Mic, Music, Speaker] as const

/** The show page Companion draws: cameras, slides, pause, mic, light, rec, clock. */
export function showKeys(t: Text, live: 0 | 1 | 2): Key[] {
  const cam = (n: number): Key =>
    live === n ? { icon: Video, word: `CAM ${n + 1}`, bg: KEY.live.on } : { icon: Video, word: `CAM ${n + 1}` }
  return [
    cam(0),
    cam(1),
    { icon: Presentation, word: t.slides },
    { icon: Pause, word: t.pause },
    { icon: Mic, word: t.mic, bg: KEY.audio.on },
    { icon: Lightbulb, word: '70%', bg: KEY.light.on, fg: KEY.light.ink },
    { icon: REC, word: 'REC', bg: KEY.live.on },
    { icon: Clock, word: '14:32' },
  ]
}

export function showStrip(music: number, active = 0) {
  return (
    <>
      <Zone i={0} icon={Volume2} value={80} accent={KEY.audio.fg} />
      <Zone i={1} icon={Mic} value={65} accent={KEY.audio.fg} />
      <Zone i={2} icon={Music} value={music} accent={KEY.audio.fg} active={active} />
      <Zone i={3} icon={Speaker} value={55} accent={KEY.audio.fg} />
    </>
  )
}

/** A soft shadow on the desk; a flat black ellipse reads as a grey slab on the light stage. */
export function Shadow({ x, y, rx, ry, o = 0.7 }: { x: number; y: number; rx: number; ry: number; o?: number }) {
  return <ellipse cx={x} cy={y} rx={rx} ry={ry} fill={`url(#${glowId('#000000')})`} opacity={o} />
}

type Box = { x: number; y: number; w: number; h: number }
const PORT = { x0: 92, pitch: 40, w: 32, h: 26 }
/** Centre of switch port i, where a Jack goes. */
export const switchPort = (b: Box, i: number): Pt => [b.x + PORT.x0 + i * PORT.pitch + PORT.w / 2, b.y + b.h / 2 + 6]

/** A PoE switch from the front, ports inside the body; the first two blink with traffic. */
export function Switch({ f = 0, x, y, w, h }: { f?: number } & Box) {
  const n = Math.floor((w - PORT.x0 - 8) / PORT.pitch)
  return (
    <g>
      <Shadow x={x + w / 2} y={y + h + 6} rx={w * 0.62} ry={18} />
      <rect x={x} y={y} width={w} height={h} rx={10} fill="#1b1e22" stroke="#2d3238" strokeWidth={2} />
      <rect x={x} y={y} width={w} height={14} rx={7} fill="#fff" opacity={0.05} />
      <text x={x + 18} y={y + h / 2 + 16} fontFamily={SANS} fontSize={30} fontWeight={700} fill={AMBER}>
        PoE
      </text>
      {Array.from({ length: n }, (_, i) => {
        const [cx, cy] = switchPort({ x, y, w, h }, i)
        return (
          <g key={i}>
            <rect
              x={cx - PORT.w / 2}
              y={cy - PORT.h / 2}
              width={PORT.w}
              height={PORT.h}
              rx={3}
              fill="#0b0d0f"
              stroke="#3b4148"
            />
            <rect
              x={cx - PORT.w / 2 + 2}
              y={cy - PORT.h / 2 - 9}
              width={8}
              height={4}
              rx={1}
              fill={i < 2 && Math.floor(f / 4 + i) % 3 ? GREEN : '#2a3a30'}
            />
          </g>
        )
      })}
    </g>
  )
}

/** An RJ45 plug seated in a port at (x, y); draw it after the cable that ends there. */
export function Jack({ x, y }: { x: number; y: number }) {
  return (
    <g>
      <rect x={x - 12} y={y - 10} width={24} height={20} rx={2} fill="#c9ccd1" />
      <rect x={x - 5} y={y - 10} width={10} height={6} fill="#8d939b" />
    </g>
  )
}

/** A quadratic arc from a to b that bows up by lift, sampled for Trail. */
export function arc(a: Pt, b: Pt, lift: number): Pt[] {
  const c: Pt = [(a[0] + b[0]) / 2, Math.min(a[1], b[1]) - lift]
  return Array.from({ length: 17 }, (_, i) => {
    const u = i / 16
    return [
      (1 - u) ** 2 * a[0] + 2 * u * (1 - u) * c[0] + u * u * b[0],
      (1 - u) ** 2 * a[1] + 2 * u * (1 - u) * c[1] + u * u * b[1],
    ]
  })
}

/** Radio between two antennas: a dotted line that drifts toward b. */
export function Air({ pts, f, on = 1, w = 11 }: { pts: readonly Pt[]; f: number; on?: number; w?: number }) {
  if (on <= 0.01) return null
  return (
    <path
      d={path(pts)}
      fill="none"
      stroke={AMBER}
      strokeWidth={w}
      strokeLinecap="round"
      strokeDasharray={`1 ${w * 2.2}`}
      strokeDashoffset={-f * 2}
      opacity={0.35 + 0.5 * on}
    />
  )
}

/** Rings that leave an antenna tip and fade, rotated by deg (0 = up). */
export function Waves({
  x,
  y,
  f,
  on = 1,
  size = 40,
  deg = 0,
  c = GREEN,
  period = 36,
}: {
  x: number
  y: number
  f: number
  on?: number
  size?: number
  deg?: number
  c?: string
  period?: number
}) {
  if (on <= 0.01) return null
  return (
    <g
      transform={`translate(${x} ${y}) rotate(${deg})`}
      fill="none"
      stroke={c}
      strokeWidth={size * 0.12}
      strokeLinecap="round"
    >
      {[0, 1, 2].map((i) => {
        const k = (f / period + i / 3) % 1
        const r = size * (0.5 + 1.5 * k)
        return (
          <path key={i} d={`M${-r * 0.7} ${-r * 0.7} A${r} ${r} 0 0 1 ${r * 0.7} ${-r * 0.7}`} opacity={on * (1 - k)} />
        )
      })}
    </g>
  )
}

/** A Wi-Fi router: two antennas, lights that blink with traffic. Antenna tips sit at y - 46. */
export function Router({ f = 0, x, y, w, h, live = 1 }: { f?: number; live?: number } & Box) {
  return (
    <g>
      <Shadow x={x + w / 2} y={y + h + 6} rx={w * 0.62} ry={18} />
      {live > 0 && (
        <circle cx={x + w / 2} cy={y + h / 2} r={w * 0.8} fill={`url(#${glowId(GREEN)})`} opacity={live * 0.25} />
      )}
      {[x + 40, x + w - 40].map((ax) => (
        <rect key={ax} x={ax - 6} y={y - 46} width={12} height={52} rx={6} fill="#2d3238" />
      ))}
      <rect
        x={x}
        y={y}
        width={w}
        height={h}
        rx={14}
        fill="#1b1e22"
        stroke={live > 0.5 ? GREEN : '#2d3238'}
        strokeOpacity={0.7}
        strokeWidth={2.5}
      />
      <rect x={x} y={y} width={w} height={14} rx={7} fill="#fff" opacity={0.05} />
      <g
        transform={`translate(${x + 34} ${y + h / 2 + 16})`}
        fill="none"
        stroke={live > 0.5 ? GREEN : '#5a6470'}
        strokeWidth={5}
        strokeLinecap="round"
      >
        <path d="M-14 -12 a20 20 0 0 1 28 0" />
        <path d="M-24 -22 a34 34 0 0 1 48 0" />
        <circle cx={0} cy={-2} r={3} fill={live > 0.5 ? GREEN : '#5a6470'} />
      </g>
      {Array.from({ length: 4 }, (_, i) => (
        <circle
          key={i}
          cx={x + w - 34 - i * 26}
          cy={y + h / 2 + 4}
          r={6}
          fill={live > 0.5 && Math.floor(f / 3 + i) % 3 ? GREEN : '#3b4148'}
        />
      ))}
    </g>
  )
}

/** A USB-C power supply; the cable leaves its top at (x, y). */
export function Plug({ x, y, s = 1, o = 1 }: { x: number; y: number; s?: number; o?: number }) {
  if (o <= 0.01) return null
  return (
    <g transform={`translate(${x} ${y}) scale(${s})`} opacity={o}>
      <rect x={-8} y={0} width={16} height={18} rx={3} fill="#2a2f36" />
      <rect x={-34} y={16} width={68} height={58} rx={12} fill="#e9ebee" stroke="#8b929a" strokeWidth={1.5} />
      <rect x={-34} y={16} width={68} height={12} rx={6} fill="#fff" opacity={0.6} />
      <path d="M4 32 L-8 50 H2 L-4 66 L10 44 H0 Z" fill={AMBER} />
    </g>
  )
}
