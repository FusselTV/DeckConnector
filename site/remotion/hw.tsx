import { useId, type ReactNode } from 'react'
import type { Text } from './text'

export const MONO = "'JetBrains Mono Variable', ui-monospace, Menlo, monospace"
export const SANS = "'Geist Variable', ui-sans-serif, system-ui, sans-serif"
export const AMBER = '#ffb020'
export const GREEN = '#3ddc84'
export const RED = '#ff4b3e'
export const INK = '#f2f2ef'

export type Pt = readonly [number, number]
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
      out.push([(1 - u) ** 2 * p[0] + 2 * u * (1 - u) * b[0] + u * u * q[0], (1 - u) ** 2 * p[1] + 2 * u * (1 - u) * b[1] + u * u * q[1]])
    }
  })
  return out
}
const path = (pts: readonly Pt[]) => pts.map((p, i) => `${i ? 'L' : 'M'}${p[0].toFixed(1)},${p[1].toFixed(1)}`).join('')

export function Cable({ pts, w = 12, color = '#23272d', glow = 0, glowColor = AMBER, dash, opacity = 1 }: { pts: readonly Pt[]; w?: number; color?: string; glow?: number; glowColor?: string; dash?: string; opacity?: number }) {
  const d = path(pts)
  return (
    <g opacity={opacity} fill="none" strokeLinecap="round" strokeLinejoin="round">
      <path d={d} stroke="#000" strokeOpacity={0.5} strokeWidth={w * 1.6} transform={`translate(0 ${w * 0.5})`} strokeDasharray={dash} />
      <path d={d} stroke={color} strokeWidth={w} strokeDasharray={dash} />
      {glow > 0 && <path d={d} stroke={glowColor} strokeOpacity={glow * 0.55} strokeWidth={w * 0.5} strokeDasharray={dash} />}
      <path d={d} stroke="#fff" strokeOpacity={0.13} strokeWidth={w * 0.22} transform={`translate(0 ${-w * 0.22})`} strokeDasharray={dash} />
    </g>
  )
}

/** Soft light, one gradient per colour. Ids are global and the content is the same everywhere, so two players can share them. */
export const glowId = (c: string) => `dc-glow-${c.replace('#', '')}`
export function Glow({ c }: { c: string }) {
  return (
    <radialGradient id={glowId(c)}>
      <stop offset="0" stopColor={c} stopOpacity={0.85} />
      <stop offset="0.25" stopColor={c} stopOpacity={0.35} />
      <stop offset="0.6" stopColor={c} stopOpacity={0.08} />
      <stop offset="1" stopColor={c} stopOpacity={0} />
    </radialGradient>
  )
}

/** A burst of data: a hot head and a tail that fades behind it along the path. t runs 0..1 (plus len to clear). */
export function Trail({ pts, t, len = 0.22, color = AMBER, w = 12 }: { pts: readonly Pt[]; t: number; len?: number; color?: string; w?: number }) {
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
    segs.push(<line key={i} x1={p[0]} y1={p[1]} x2={q[0]} y2={q[1]} stroke={color} strokeWidth={w * (0.25 + 0.75 * k)} strokeOpacity={k * k} strokeLinecap="round" />)
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
export function Pool({ x, y, rx, ry, c = '#ffffff', o = 0.06 }: { x: number; y: number; rx: number; ry: number; c?: string; o?: number }) {
  return <ellipse cx={x} cy={y} rx={rx} ry={ry} fill={`url(#${glowId(c)})`} opacity={o} />
}

export function Label({ x, y, children, sub, anchor = 'middle', size = 34, o = 1 }: { x: number; y: number; children: ReactNode; sub?: ReactNode; anchor?: 'start' | 'middle' | 'end'; size?: number; o?: number }) {
  return (
    <text x={x} y={y} textAnchor={anchor} fontFamily={SANS} fontSize={size} fontWeight={600} fill={INK} letterSpacing={-size * 0.02} opacity={o}>
      {children}
      {sub && (
        <tspan x={x} dy={size * 1.2} fontSize={size * 0.72} fontWeight={500} fill="#8b929a">
          {sub}
        </tspan>
      )}
    </text>
  )
}

/** One key picture, drawn on the 120 x 120 pixels a Stream Deck + key has. */
export type Key = { bg: string; fg?: string; top?: string; big?: string; sub?: string; dot?: string; glow?: string; art?: ReactNode }

const bigSize = (s: string) => (s.length <= 2 ? 56 : s.length <= 4 ? 42 : s.length <= 5 ? 36 : s.length <= 6 ? 31 : s.length <= 8 ? 26 : 21)
export function Face({ k }: { k: Key }) {
  const fg = k.fg ?? INK
  return (
    <>
      <rect width={120} height={120} fill={k.bg} />
      <rect width={120} height={60} fill="#fff" opacity={0.05} />
      {k.top && (
        <text x={60} y={34} textAnchor="middle" fontFamily={SANS} fontSize={20} fontWeight={700} fill={fg} opacity={0.78}>
          {k.top}
        </text>
      )}
      {k.big && (
        <text x={60} y={k.top ? 86 : k.sub ? 62 : 60 + bigSize(k.big) * 0.36} textAnchor="middle" fontFamily={SANS} fontSize={bigSize(k.big)} fontWeight={700} fill={fg} letterSpacing={-1}>
          {k.big}
        </text>
      )}
      {k.sub && (
        <text x={60} y={102} textAnchor="middle" fontFamily={SANS} fontSize={k.sub.length > 8 ? 17 : 20} fontWeight={700} fill={fg} opacity={0.85}>
          {k.sub}
        </text>
      )}
      {k.dot && <circle cx={100} cy={20} r={8} fill={k.dot} />}
      {k.art}
    </>
  )
}

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

export function Deck({ x, y, s, keys, under = [], strip, lit = [], flash = [], down = [], ring = [], dial = [], dialRing = [], stripLit = 1, edge = '#fff', sheen = 0, children }: DeckProps) {
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
        {keys.map((k, i) => (k ? <Glow key={i} c={k.glow ?? k.bg} /> : null))}
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
      <rect x={0.25} y={0.25} width={SD.W - 0.5} height={SD.H - 0.5} rx={SD.R} fill="none" stroke="#fff" strokeOpacity={0.12} strokeWidth={0.5} />
      {keys.map((k, i) => {
        const g = L(i) * (0.55 + 0.45 * (flash[i] ?? 0))
        if (!k || g < 0.02) return null
        const b = keyBox(i)
        const c = b.s / 2
        return <circle key={i} cx={b.x + c} cy={b.y + c} r={b.s * (1.05 + 0.25 * (flash[i] ?? 0))} fill={`url(#${glowId(k.glow ?? k.bg)})`} opacity={g * 0.75} />
      })}
      {keys.map((k, i) => {
        const b = keyBox(i)
        const d = down[i] ?? 0
        const r = ring[i] ?? 0
        const c = b.s / 2
        const on = L(i)
        const fl = k ? (flash[i] ?? 0) : 0
        return (
          <g key={i} transform={`translate(${b.x + c} ${b.y + c + d * 0.5}) scale(${1 - d * 0.05}) translate(${-c} ${-c})`}>
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
            {r > 0 && <rect x={-1.3} y={-1.3} width={b.s + 2.6} height={b.s + 2.6} rx={SD.kr + 1.3} fill="none" stroke={AMBER} strokeWidth={1} opacity={r} />}
          </g>
        )
      })}
      <rect {...SD.win} rx={0.8} fill="#050607" />
      {stripLit > 0.02 && <rect x={SD.strip.x - 6} y={SD.strip.y - 5} width={SD.strip.w + 12} height={SD.strip.h + 10} rx={6} fill={`url(#${glowId(AMBER)})`} opacity={stripLit * 0.22} />}
      <svg x={SD.strip.x} y={SD.strip.y} width={SD.strip.w} height={SD.strip.h} viewBox="0 0 800 100" preserveAspectRatio="none">
        <rect width={800} height={100} fill="#020303" />
        <g opacity={stripLit}>{strip}</g>
      </svg>
      <rect {...SD.strip} rx={0.8} fill={`url(#${id}g)`} opacity={0.7} />
      <rect x={SD.tray.x} y={SD.tray.y} width={SD.tray.w} height={SD.tray.h} rx={SD.tray.r} fill={`url(#${id}t)`} />
      <rect x={SD.tray.x} y={SD.tray.y + SD.tray.h - 0.6} width={SD.tray.w} height={0.6} rx={0.3} fill="#fff" opacity={0.06} />
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
            <rect x={-0.35} y={-r * 0.64} width={0.7} height={r * 0.28} rx={0.35} fill="#b8bdc4" transform={`rotate(${rot})`} />
            {g > 0 && <circle r={r + 1.2} fill="none" stroke={AMBER} strokeWidth={0.9} opacity={g} />}
          </g>
        )
      })}
      {children}
    </g>
  )
}

/** A touch strip zone in its 200 x 100 slot: label, value, bar. */
export function Zone({ i, label, value, accent = AMBER, active = 0 }: { i: number; label: string; value: number; accent?: string; active?: number }) {
  return (
    <g transform={`translate(${i * 200} 0)`}>
      {i > 0 && <line x1={0} y1={10} x2={0} y2={90} stroke="#1d2126" strokeWidth={2} />}
      {active > 0 && <rect width={200} height={100} fill={accent} opacity={active * 0.22} />}
      <text x={16} y={38} fontFamily={MONO} fontSize={24} fontWeight={700} fill={INK} opacity={0.8}>
        {label}
      </text>
      <text x={186} y={38} textAnchor="end" fontFamily={MONO} fontSize={26} fontWeight={800} fill={INK}>
        {Math.round(value)}
      </text>
      <rect x={16} y={58} width={170} height={16} rx={4} fill="#22262c" />
      <rect x={16} y={58} width={1.7 * value} height={16} rx={4} fill={accent} />
    </g>
  )
}

export type BoardProps = { x: number; y: number; s?: number; link?: number; act?: number; power?: number; wifi?: number; hot?: number; ring?: number; sheen?: number }

/** Waveshare ESP32-P4-WIFI6-POE-ETH, top view, 210 x 150 at s = 1. USB-A on the left, RJ45 on the right. */
export const BOARD_USB: Pt = [-14, 60]
export const BOARD_RJ45: Pt = [224, 75]
export const BOARD_ANT: Pt = [162, 18]
export const at = (b: { x: number; y: number; s?: number }, p: Pt): Pt => [b.x + p[0] * (b.s ?? 1), b.y + p[1] * (b.s ?? 1)]

export function Board({ x, y, s = 1, link = 1, act = 0, power = 1, wifi = 0, hot = 0, ring = 0, sheen = 0 }: BoardProps) {
  const id = useId().replace(/:/g, '')
  return (
    <g transform={`translate(${x} ${y}) scale(${s})`}>
      <defs>
        <linearGradient id={`${id}p`} x1="0" y1="0" x2="0.3" y2="1">
          <stop offset="0" stopColor="#1b3a2d" />
          <stop offset="1" stopColor="#0d2119" />
        </linearGradient>
        <linearGradient id={`${id}s`} x1="0" y1="0" x2="1" y2="0.7">
          <stop offset={0.25 + sheen * 0.3} stopColor="#fff" stopOpacity={0} />
          <stop offset={0.4 + sheen * 0.3} stopColor="#bfffe0" stopOpacity={0.11} />
          <stop offset={0.55 + sheen * 0.3} stopColor="#fff" stopOpacity={0} />
        </linearGradient>
        <linearGradient id={`${id}m`} x1="0" y1="0" x2="1" y2="1">
          <stop offset="0" stopColor="#e4e7ea" />
          <stop offset="0.5" stopColor="#a9afb6" />
          <stop offset="1" stopColor="#7d838b" />
        </linearGradient>
        <radialGradient id={`${id}sh`}>
          <stop offset="0" stopColor="#000" stopOpacity={0.75} />
          <stop offset="1" stopColor="#000" stopOpacity={0} />
        </radialGradient>
      </defs>
      <ellipse cx={105} cy={162} rx={150} ry={22} fill={`url(#${id}sh)`} />
      {hot > 0 && <circle cx={89} cy={75} r={150} fill={`url(#${glowId(AMBER)})`} opacity={hot * 0.55} />}
      <rect width={210} height={150} rx={8} fill={`url(#${id}p)`} stroke="#2f5a46" strokeWidth={1.4} />
      {[
        [10, 10],
        [200, 10],
        [10, 140],
        [200, 140],
      ].map(([cx, cy], i) => (
        <g key={i}>
          <circle cx={cx} cy={cy} r={5.5} fill="#d4aa55" />
          <circle cx={cx} cy={cy} r={3.2} fill="#0b0d0f" />
        </g>
      ))}
      <g stroke="#2f6b51" strokeWidth={1.6} fill="none">
        <path d="M30 46 H70 L78 54 V68" />
        <path d="M30 104 H70 L78 96 V86" />
        <path d="M112 75 H172" />
        <path d="M110 40 V26 H120" />
        <path d="M96 110 V128 H118" />
        <path d="M60 60 H66 M60 90 H66" />
      </g>
      {hot > 0 && (
        <g stroke={AMBER} strokeWidth={1.8} fill="none" opacity={hot}>
          <path d="M30 46 H70 L78 54 V68" />
          <path d="M112 75 H172" />
        </g>
      )}
      <rect x={-14} y={42} width={44} height={36} rx={2} fill={`url(#${id}m)`} />
      <rect x={-14} y={48} width={10} height={24} fill="#1a1c20" />
      <rect x={-12} y={52} width={6} height={16} fill="#2b5fd8" />
      <rect x={-6} y={102} width={16} height={26} rx={7} fill={`url(#${id}m)`} />
      <rect x={-4} y={108} width={6} height={14} rx={3} fill="#1a1c20" />
      <rect x={64} y={50} width={50} height={50} rx={3} fill="#121417" stroke="#33373d" />
      <rect x={66} y={52} width={46} height={18} rx={2} fill="#fff" opacity={0.05} />
      <text x={89} y={73} textAnchor="middle" fontFamily={MONO} fontSize={8} fontWeight={800} fill="#a6acb3">
        ESP32
      </text>
      <text x={89} y={84} textAnchor="middle" fontFamily={MONO} fontSize={8} fontWeight={800} fill="#a6acb3">
        -P4
      </text>
      {hot > 0 && <rect x={64} y={50} width={50} height={50} rx={3} fill="none" stroke={AMBER} strokeWidth={1.6} opacity={hot} />}
      {ring > 0 && (
        <circle cx={89} cy={75} r={36} fill="none" stroke={AMBER} strokeWidth={4} strokeLinecap="round" strokeDasharray={`${ring * 226} 400`} transform="rotate(-90 89 75)" />
      )}
      <rect x={118} y={14} width={50} height={36} rx={2} fill="#0f2a1e" />
      <rect x={118} y={22} width={38} height={28} rx={1.5} fill={`url(#${id}m)`} />
      <path d="M158 16 h8 v10 h-6 v8 h6" stroke="#d4aa55" strokeWidth={1.4} fill="none" />
      {wifi > 0 && (
        <g transform="translate(163 6)" fill="none" stroke={AMBER} strokeWidth={2.6} strokeLinecap="round" opacity={wifi}>
          <path d="M-8 -1 a11 11 0 0 1 16 0" />
          <path d="M-14 -7 a19 19 0 0 1 28 0" />
        </g>
      )}
      <rect x={118} y={96} width={48} height={40} rx={2} fill="#121316" stroke="#26292e" />
      <text x={142} y={120} textAnchor="middle" fontFamily={MONO} fontSize={9} fontWeight={800} fill="#767d85">
        PoE
      </text>
      {Array.from({ length: 6 }, (_, i) => (
        <rect key={i} x={30 + i * 7} y={118} width={4} height={8} rx={0.8} fill="#2b2d31" />
      ))}
      {Array.from({ length: 4 }, (_, i) => (
        <rect key={i} x={40 + i * 8} y={20} width={4} height={7} rx={0.8} fill="#a07a43" />
      ))}
      <circle cx={30} cy={98} r={3} fill={power ? '#ff3b30' : '#3a1a18'} />
      {power > 0 && <circle cx={30} cy={98} r={12} fill={`url(#${glowId(RED)})`} opacity={power} />}
      <rect x={172} y={50} width={52} height={50} rx={2} fill={`url(#${id}m)`} />
      <rect x={210} y={60} width={14} height={30} fill="#15171a" />
      <rect x={180} y={53} width={9} height={6} rx={1} fill={link ? GREEN : '#123322'} />
      {link > 0 && <circle cx={184.5} cy={56} r={12} fill={`url(#${glowId(GREEN)})`} opacity={link} />}
      <rect x={180} y={91} width={9} height={6} rx={1} fill={act > 0.5 ? AMBER : '#3a2a0a'} />
      {act > 0.5 && <circle cx={184.5} cy={94} r={12} fill={`url(#${glowId(AMBER)})`} />}
      <rect width={210} height={150} rx={8} fill={`url(#${id}s)`} />
    </g>
  )
}

/** The show page Companion draws: cameras, slides, mic, title, rec, clock. */
export function showKeys(t: Text, f: number, live: 0 | 1 | 2): Key[] {
  const blink = Math.floor(f / 15) % 2 === 0
  const cam = (n: number): Key => (live === n ? { bg: RED, top: t.live, big: `CAM${n + 1}` } : { bg: '#262b32', big: `CAM${n + 1}`, glow: '#5a6470' })
  return [
    cam(0),
    cam(1),
    { bg: '#1f4f86', big: t.slides, glow: '#3d8bff' },
    { bg: '#4b3a7a', big: t.pause, glow: '#9b7bff' },
    { bg: '#1c7a45', top: t.mic, big: t.on.toUpperCase(), glow: GREEN },
    { bg: AMBER, fg: '#1a1200', big: t.title },
    { bg: '#15181c', big: 'REC', dot: blink ? RED : '#4a1a16', glow: blink ? RED : '#4a1a16' },
    { bg: '#000', big: '14:32', glow: '#3a3f46' },
  ]
}

export function showStrip(t: Text, music: number, active = 0) {
  return (
    <>
      <Zone i={0} label={t.master} value={80} />
      <Zone i={1} label={t.mic} value={65} accent={GREEN} />
      <Zone i={2} label={t.music} value={music} active={active} />
      <Zone i={3} label={t.room} value={55} />
    </>
  )
}

/** A PoE switch; the first two ports blink with traffic. */
export function Switch({ f = 0, x, y, w, h }: { f?: number; x: number; y: number; w: number; h: number }) {
  return (
    <g>
      <ellipse cx={x + w / 2} cy={y + h + 10} rx={w * 0.6} ry={16} fill="#000" opacity={0.5} />
      <rect x={x} y={y} width={w} height={h} rx={10} fill="#1b1e22" stroke="#2d3238" strokeWidth={2} />
      <rect x={x} y={y} width={w} height={14} rx={7} fill="#fff" opacity={0.05} />
      <text x={x + 18} y={y + 42} fontFamily={SANS} fontSize={30} fontWeight={700} fill={AMBER}>
        PoE
      </text>
      {Array.from({ length: Math.floor((w - 18) / 46) }, (_, i) => (
        <g key={i}>
          <rect x={x + 18 + i * 46} y={y + 56} width={36} height={30} rx={3} fill="#0b0d0f" stroke="#3b4148" />
          <rect x={x + 22 + i * 46} y={y + 52} width={8} height={4} rx={1} fill={i < 2 && Math.floor(f / 4 + i) % 3 ? GREEN : '#2a3a30'} />
        </g>
      ))}
    </g>
  )
}
