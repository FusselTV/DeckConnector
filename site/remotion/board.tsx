import { useId } from 'react'

// The board drawing, without Remotion: the flasher draws it too and must not load the player for it.

export const MONO = "'JetBrains Mono Variable', ui-monospace, Menlo, monospace"
export const AMBER = '#ffb020'
export const GREEN = '#3ddc84'
export const RED = '#ff5b4f'

export type Pt = readonly [number, number]

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

export type BoardProps = { x: number; y: number; s?: number; link?: number; act?: number; power?: number; wifi?: number; hot?: number; ring?: number; sheen?: number }

/** Waveshare ESP32-P4-WIFI6-POE-ETH, top view, 210 x 150 at s = 1. USB-A on the left, RJ45 on the right. */
export const BOARD_USB: Pt = [-14, 60]
export const BOARD_USBC: Pt = [-6, 115]
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
      <text x={89} y={73} textAnchor="middle" fontFamily={MONO} fontSize={8} fontWeight={800} fill="#afb4ba">
        ESP32
      </text>
      <text x={89} y={84} textAnchor="middle" fontFamily={MONO} fontSize={8} fontWeight={800} fill="#afb4ba">
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
