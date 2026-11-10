import type { ReactNode } from 'react'
import { AbsoluteFill, Easing, useCurrentFrame, useVideoConfig } from 'remotion'
import { AMBER, Glow, GREEN, INK, RED, SANS } from './hw'
import { ramp } from './time'

/** Where the camera looks (world point x, y), how close (z), and its tilt in degrees. */
export type Cam = { x: number; y: number; z: number; rx?: number; ry?: number }
export type Shot = readonly [frame: number, cam: Cam]

/** The page's light theme: a pale studio, the hardware stays as it is. */
export const useLight = () => useVideoConfig().props.light === true

const cine = Easing.bezier(0.6, 0, 0.2, 1)
export function camAt(f: number, shots: readonly Shot[]): Cam {
  const i = shots.findIndex(([a]) => a > f)
  const last = shots[shots.length - 1]
  const first = shots[0]
  if (!first || !last) return { x: 0, y: 0, z: 1 }
  if (i === -1) return last[1]
  const prev = shots[i - 1]
  const next = shots[i]
  if (!prev || !next) return first[1]
  const [a, A] = prev
  const [b, B] = next
  const k = cine((f - a) / (b - a))
  const mix = (p = 0, q = 0) => p + (q - p) * k
  // Zoom moves in log space, so a push in feels as fast at the end as at the start.
  return { x: mix(A.x, B.x), y: mix(A.y, B.y), z: A.z * Math.pow(B.z / A.z, k), rx: mix(A.rx, B.rx), ry: mix(A.ry, B.ry) }
}

/** One slow push in and back out over the loop, with an optional side-to-side turn in degrees; the whole setup stays in frame. */
export const breathe = (f: number, d: number, cam: Cam, k = 0.03, turn = 0): Cam => {
  const ph = (f / d) * Math.PI * 2
  return { ...cam, z: cam.z * (1 + (k * (1 - Math.cos(ph))) / 2), ry: (cam.ry ?? 0) + turn * Math.sin(ph) }
}

const GRAIN = `url("data:image/svg+xml,${encodeURIComponent(
  '<svg xmlns="http://www.w3.org/2000/svg" width="180" height="180"><filter id="n"><feTurbulence type="fractalNoise" baseFrequency="0.9" numOctaves="2" stitchTiles="stitch"/><feColorMatrix values="0 0 0 0 1  0 0 0 0 1  0 0 0 0 1  0 0 0 0.55 0"/></filter><rect width="180" height="180" filter="url(#n)"/></svg>',
)}")`

/**
 * A dark studio: the world is drawn in its own units and the camera frames it.
 * Behind it sits soft out-of-focus light, in front a vignette and grain, then captions.
 */
export function Scene({ w, h, cam, dim = 0, over, children }: { w: number; h: number; cam: Cam; dim?: number; over?: ReactNode; children: ReactNode }) {
  const f = useCurrentFrame()
  const { durationInFrames: d } = useVideoConfig()
  const light = useLight()
  // A slow handheld drift that closes on itself over the loop.
  const ph = (f / d) * Math.PI * 2
  const x = cam.x + (Math.sin(ph * 2) * 5) / cam.z
  const y = cam.y + (Math.cos(ph * 3) * 4) / cam.z
  const bx = (-cam.x * 0.04) % 400
  const by = (-cam.y * 0.04) % 400
  return (
    <AbsoluteFill style={{ background: light ? 'radial-gradient(120% 90% at 50% 40%, #fbfbfa 0%, #eceeef 55%, #dcdfe2 100%)' : 'radial-gradient(120% 90% at 50% 40%, #16191d 0%, #0c0e10 55%, #060708 100%)', overflow: 'hidden' }}>
      <AbsoluteFill
        style={{
          transform: `translate(${bx}px, ${by}px) scale(1.1)`,
          background: [
            `radial-gradient(circle at 12% 18%, rgba(255,176,32,0.10) 0, rgba(255,176,32,0) ${w * 0.1}px)`,
            `radial-gradient(circle at 86% 14%, rgba(90,169,255,0.07) 0, rgba(90,169,255,0) ${w * 0.13}px)`,
            `radial-gradient(circle at 78% 84%, rgba(255,176,32,0.06) 0, rgba(255,176,32,0) ${w * 0.16}px)`,
            `radial-gradient(circle at 24% 88%, rgba(61,220,132,0.04) 0, rgba(61,220,132,0) ${w * 0.12}px)`,
          ].join(','),
        }}
      />
      <AbsoluteFill style={{ perspective: w * 1.4 }}>
        <AbsoluteFill style={{ transform: `rotateX(${cam.rx ?? 0}deg) rotateY(${cam.ry ?? 0}deg)` }}>
          <svg viewBox={`${-w * 0.1} ${-h * 0.1} ${w * 1.2} ${h * 1.2}`} width={w * 1.2} height={h * 1.2} style={{ position: 'absolute', left: -w * 0.1, top: -h * 0.1 }}>
            <defs>
              {[AMBER, GREEN, RED, '#ffffff', '#9b7bff', '#5aa9ff', '#2bb3a6', '#000000'].map((c) => (
                <Glow key={c} c={c} />
              ))}
            </defs>
            <g transform={`translate(${w / 2} ${h / 2}) scale(${cam.z}) translate(${-x} ${-y})`}>{children}</g>
          </svg>
        </AbsoluteFill>
      </AbsoluteFill>
      <AbsoluteFill style={{ background: `radial-gradient(75% 75% at 50% 46%, rgba(0,0,0,0) 55%, rgba(0,0,0,${light ? 0.1 : 0.62}) 100%)` }} />
      {!light && <AbsoluteFill style={{ backgroundImage: GRAIN, opacity: 0.07 }} />}
      {dim > 0 && <AbsoluteFill style={{ background: '#050607', opacity: dim }} />}
      {over}
    </AbsoluteFill>
  )
}

/** The one caption style: top left, a status dot, a mono headline, an optional big number and a line under it. */
export function Caption({ f, from, to, text, big, sub, dot = AMBER, size = 52 }: { f: number; from: number; to: number; text: string; big?: string; sub?: string; dot?: string; size?: number }) {
  const light = useLight()
  const o = Math.min(ramp(f, from, from + 8), 1 - ramp(f, to - 8, to))
  if (o <= 0) return null
  const pad = size * 1.15
  return (
    <>
      <AbsoluteFill style={{ background: light ? `radial-gradient(${size * 16}px ${size * 9}px at 0 0, rgba(251,251,250,0.85), rgba(251,251,250,0) 100%)` : `radial-gradient(${size * 16}px ${size * 9}px at 0 0, rgba(5,6,7,0.78), rgba(5,6,7,0) 100%)`, opacity: o }} />
      <div style={{ position: 'absolute', left: pad, top: pad * 0.85, opacity: o, transform: `translateY(${(1 - ramp(f, from, from + 12)) * -16}px)`, fontFamily: SANS, color: light ? '#15181c' : INK, textShadow: light ? 'none' : '0 2px 18px rgba(0,0,0,0.85)' }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: size * 0.4, fontSize: size, fontWeight: 650, letterSpacing: -size * 0.025, lineHeight: 1.1 }}>
          <span style={{ flex: 'none', width: size * 0.36, height: size * 0.36, borderRadius: '50%', background: dot, boxShadow: `0 0 ${size * 0.5}px ${dot}` }} />
          {text}
        </div>
        {big && <div style={{ marginLeft: size * 0.76, fontSize: size * 1.9, fontWeight: 650, letterSpacing: -size * 0.06, fontFamily: SANS, color: dot, lineHeight: 1.05, fontVariantNumeric: 'tabular-nums' }}>{big}</div>}
        {sub && <div style={{ marginLeft: size * 0.76, marginTop: size * 0.15, fontSize: size * 0.62, fontWeight: 500, color: light ? '#4a5058' : '#c4c9cf' }}>{sub}</div>}
      </div>
    </>
  )
}
