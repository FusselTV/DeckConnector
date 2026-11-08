// Static drawings built from the same hardware parts as the Remotion scenes, so the page has one art style.
import type { ReactNode } from 'react'
import { AMBER, at, Board, BOARD_RJ45, BOARD_USB, Cable, Deck, Face, Glow, GREEN, RED, rounded, SANS, showKeys, showStrip, type Pt } from '~/remotion/hw'
import { TEXT, type Lang } from '~/remotion/text'

const SOFT = '#a6acb3'

function Defs() {
  return (
    <defs>
      {[AMBER, GREEN, RED, '#ffffff', '#9b7bff', '#5aa9ff', '#2bb3a6'].map((c) => (
        <Glow key={c} c={c} />
      ))}
    </defs>
  )
}

function Tag({ x, y, children, anchor = 'middle', color = SOFT, size = 30 }: { x: number; y: number; children: ReactNode; anchor?: 'start' | 'middle' | 'end'; color?: string; size?: number }) {
  return (
    <text x={x} y={y} textAnchor={anchor} fontFamily={SANS} fontSize={size} fontWeight={600} fill={color} letterSpacing={-size * 0.01}>
      {children}
    </text>
  )
}

function Switch({ x, y, w = 260, h = 84 }: { x: number; y: number; w?: number; h?: number }) {
  const n = Math.floor((w - 24) / 56)
  return (
    <g transform={`translate(${x} ${y})`}>
      <ellipse cx={w / 2} cy={h + 8} rx={w * 0.55} ry={12} fill="#000" opacity={0.45} />
      <rect width={w} height={h} rx={10} fill="#1b1e22" stroke="#2d3238" strokeWidth={2} />
      <rect width={w} height={12} rx={6} fill="#fff" opacity={0.05} />
      {Array.from({ length: n }, (_, i) => (
        <g key={i}>
          <rect x={18 + i * 56} y={22} width={42} height={34} rx={3} fill="#0b0d0f" stroke="#3b4148" />
          <rect x={22 + i * 56} y={64} width={10} height={5} rx={1} fill={i === 0 ? GREEN : '#2a3a30'} />
          <rect x={36 + i * 56} y={64} width={10} height={5} rx={1} fill={i === 0 ? AMBER : '#3a2a0a'} />
        </g>
      ))}
    </g>
  )
}

function ShowDeck({ lang, x, y, s }: { lang: Lang; x: number; y: number; s: number }) {
  const t = TEXT[lang]
  return <Deck x={x} y={y} s={s} keys={showKeys(t, 0, 0)} strip={showStrip(t, 40)} />
}

const line = (pts: readonly Pt[]) => pts.map((p, i) => `${i ? 'L' : 'M'}${p[0]},${p[1]}`).join('')

export function Wiring({ lang, labels }: { lang: Lang; labels: { deck: string; usb: string; board: string; poe: string; switch: string } }) {
  const board = { x: 690, y: 420, s: 1.35 }
  const usb = at(board, BOARD_USB)
  const rj = at(board, BOARD_RJ45)
  const poe = rounded([rj, [1080, rj[1]], [1080, 234]], 24)
  return (
    <svg viewBox="0 0 1280 800" className="absolute inset-0 size-full" aria-hidden>
      <Defs />
      <Cable pts={[[540, usb[1]], usb]} w={14} />
      <Cable pts={poe} w={14} color="#2a2f36" />
      <path d={line(poe)} fill="none" stroke={AMBER} strokeWidth={5} strokeLinecap="round" opacity={0.9} />
      <Switch x={900} y={150} />
      <Board {...board} />
      <ShowDeck lang={lang} x={110} y={190} s={3.3} />
      <Tag x={341} y={700}>{labels.deck}</Tag>
      <Tag x={831} y={686}>{labels.board}</Tag>
      <Tag x={615} y={usb[1] - 22}>{labels.usb}</Tag>
      <Tag x={1104} y={390} anchor="start" color={AMBER}>
        {labels.poe}
      </Tag>
      <Tag x={1030} y={124}>{labels.switch}</Tag>
    </svg>
  )
}

/** The three reasons: the deck needs no PC, one PoE cable, any host can draw. */
export function WhyArt({ i, lang, labels }: { i: number; lang: Lang; labels: string[] }) {
  if (i === 0) {
    const board = { x: 330, y: 92, s: 0.72 }
    const usb = at(board, BOARD_USB)
    return (
      <svg viewBox="0 0 640 264" className="size-full" aria-hidden>
        <Defs />
        <Cable pts={[[220, usb[1]], usb]} w={8} />
        <Cable pts={[at(board, BOARD_RJ45), [640, at(board, BOARD_RJ45)[1]]]} w={8} color="#2a2f36" />
        <Board {...board} />
        <ShowDeck lang={lang} x={50} y={40} s={1.4} />
        <g transform="translate(520 26)" opacity={0.6}>
          <rect width={86} height={58} rx={6} fill="none" stroke="#858c94" strokeWidth={3} strokeDasharray="7 6" />
          <path d="M30 72 H56 M43 58 V72" stroke="#858c94" strokeWidth={3} />
        </g>
        <path d="M510 18 L618 98" stroke={RED} strokeWidth={5} strokeLinecap="round" />
      </svg>
    )
  }
  if (i === 1) {
    const board = { x: 50, y: 70, s: 0.84 }
    const rj = at(board, BOARD_RJ45)
    return (
      <svg viewBox="0 0 640 264" className="size-full" aria-hidden>
        <Defs />
        <Cable pts={[rj, [420, rj[1]]]} w={12} color="#2a2f36" />
        <path d={`M${rj[0]} ${rj[1]} H420`} stroke={AMBER} strokeWidth={5} strokeLinecap="round" />
        <Board {...board} />
        <Switch x={420} y={rj[1] - 42} w={180} />
        <Tag x={330} y={rj[1] - 24} color={AMBER} size={24}>
          {labels[0]}
        </Tag>
        <Tag x={330} y={rj[1] + 44} size={24}>
          {labels[1]}
        </Tag>
      </svg>
    )
  }
  const keys = labels.slice(2, 5)
  return (
    <svg viewBox="0 0 640 264" className="size-full" aria-hidden>
      {keys.map((name, k) => {
        const x = 60 + k * 190
        const on = k === 0
        return (
          <g key={name} transform={`translate(${x} 46)`}>
            {on && <rect x={-10} y={-10} width={160} height={160} rx={26} fill="none" stroke={AMBER} strokeWidth={5} />}
            <rect width={140} height={140} rx={20} fill="#08090a" />
            <svg x={8} y={8} width={124} height={124} viewBox="0 0 120 120">
              <clipPath id={`why-key${k}`}>
                <rect width={120} height={120} rx={12} />
              </clipPath>
              <g clipPath={`url(#why-key${k})`}>
                <Face k={{ bg: ['#1f4f86', '#4b3a7a', '#1c7a45'][k] ?? '#262b32', big: name }} />
              </g>
            </svg>
            {on && (
              <Tag x={70} y={194} color={AMBER} size={24}>
                {labels[5]}
              </Tag>
            )}
          </g>
        )
      })}
    </svg>
  )
}

export function BoardCallouts({ labels }: { labels: [string, string, string] }) {
  const board = { x: 330, y: 110, s: 1.9 }
  const usbA = at(board, BOARD_USB)
  const usbC = at(board, [-6, 115])
  const rj = at(board, BOARD_RJ45)
  const lead = { stroke: '#5d656e', strokeWidth: 2, fill: 'none' }
  return (
    <svg viewBox="0 0 1000 520" className="absolute inset-0 size-full" aria-hidden>
      <Defs />
      <Board {...board} />
      <path d={`M${usbA[0] - 6} ${usbA[1]} H240`} {...lead} />
      <path d={`M${usbC[0] - 6} ${usbC[1]} H240`} {...lead} />
      <path d={`M${rj[0] + 6} ${rj[1]} H800`} {...lead} />
      {[usbA, usbC, rj].map(([x, y], i) => (
        <circle key={i} cx={i === 2 ? x + 6 : x - 6} cy={y} r={5} fill={AMBER} />
      ))}
      <Tag x={228} y={usbA[1] + 10} anchor="end" size={28}>
        {labels[1]}
      </Tag>
      <Tag x={228} y={usbC[1] + 10} anchor="end" size={28}>
        {labels[2]}
      </Tag>
      <Tag x={812} y={rj[1] + 10} anchor="start" size={28} color={AMBER}>
        {labels[0]}
      </Tag>
    </svg>
  )
}

/** Latest picture wins: older pictures for a key are dropped, only the newest lands. */
export function SpeedKeys({ label }: { label: string }) {
  const vals = [40, 45, 50, 55]
  return (
    <svg viewBox="0 0 560 190" className="w-full" aria-hidden>
      {vals.map((v, i) => {
        const last = i === vals.length - 1
        const size = last ? 170 : 104
        const x = last ? 382 : i * 124
        const y = last ? 10 : 50
        return (
          <g key={v} transform={`translate(${x} ${y})`} opacity={last ? 1 : 0.38}>
            <rect width={size} height={size} rx={size * 0.15} fill="#08090a" />
            <svg x={size * 0.05} y={size * 0.05} width={size * 0.9} height={size * 0.9} viewBox="0 0 120 120">
              <clipPath id={`speed-key${i}`}>
                <rect width={120} height={120} rx={12} />
              </clipPath>
              <g clipPath={`url(#speed-key${i})`}>
                <Face k={{ bg: last ? AMBER : '#262b32', fg: last ? '#1a1200' : undefined, top: label, big: String(v) }} />
              </g>
            </svg>
            {!last && <path d={`M14 ${size - 14} L${size - 14} 14`} stroke="#ff5b4f" strokeWidth={4} strokeLinecap="round" />}
          </g>
        )
      })}
    </svg>
  )
}

export type ArtProps =
  | { kind: 'why'; i: number; lang: Lang; labels: string[] }
  | { kind: 'wiring'; lang: Lang; labels: { deck: string; usb: string; board: string; poe: string; switch: string } }
  | { kind: 'board'; labels: [string, string, string] }
  | { kind: 'speed'; label: string }

export default function Art(p: ArtProps) {
  if (p.kind === 'why') return <WhyArt {...p} />
  if (p.kind === 'wiring') return <Wiring {...p} />
  if (p.kind === 'board') return <BoardCallouts {...p} />
  return <SpeedKeys {...p} />
}
