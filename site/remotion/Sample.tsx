import { AbsoluteFill, interpolate, spring, useCurrentFrame, useVideoConfig } from 'remotion'

export function Sample() {
  const frame = useCurrentFrame()
  const { fps } = useVideoConfig()
  return (
    <AbsoluteFill className="items-center justify-center bg-lcd">
      <div className="grid grid-cols-4 gap-6">
        {Array.from({ length: 8 }, (_, i) => {
          const s = spring({ frame: frame - i * 4, fps, config: { damping: 12 } })
          const lit = interpolate(frame, [30 + i * 5, 40 + i * 5], [0, 1], { extrapolateLeft: 'clamp', extrapolateRight: 'clamp' })
          return (
            <div
              key={i}
              className="size-32 rounded-2xl border-2 border-hw-edge"
              style={{ transform: `scale(${s})`, background: `color-mix(in oklab, var(--amber) ${lit * 100}%, var(--hw))` }}
            />
          )
        })}
      </div>
    </AbsoluteFill>
  )
}
