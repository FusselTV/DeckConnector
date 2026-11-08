'use client'
import { Player, type PlayerRef } from '@remotion/player'
import { useEffect, useLayoutEffect, useRef, useState } from 'react'
import { compositions, type Composition } from '.'
import type { StageProps } from './RemotionStage'
import type { Lang } from './text'

export default function Stage({ id }: StageProps) {
  const c: Composition = compositions[id]
  const ref = useRef<PlayerRef>(null)
  const [lang] = useState<Lang>(() => (document.documentElement.lang === 'de' ? 'de' : 'en'))
  const [still] = useState(() => matchMedia('(prefers-reduced-motion: reduce)').matches)
  const [narrow, setNarrow] = useState(false)
  const [portrait, setPortrait] = useState(false)
  const size = (portrait && c.portrait) || c

  // Only a stage on screen plays, so a long page never runs four loops at once.
  useEffect(() => {
    const p = ref.current
    const el = p?.getContainerNode()
    if (still || !p || !el) return
    const io = new IntersectionObserver(([e]) => (e?.isIntersecting ? p.play() : p.pause()), { threshold: 0.2 })
    io.observe(el)
    return () => io.disconnect()
  }, [still])

  // Measured before the first paint, so the player never draws one frame in the wrong framing.
  useLayoutEffect(() => {
    const el = ref.current?.getContainerNode()
    if (!el) return
    const box = el.parentElement ?? el
    const fit = ({ width, height }: { width: number; height: number }) => {
      setNarrow(width < 560)
      setPortrait(height > width)
    }
    fit(box.getBoundingClientRect())
    const ro = new ResizeObserver(([e]) => e && fit(e.contentRect))
    ro.observe(box)
    return () => ro.disconnect()
  }, [])

  return (
    <Player
      ref={ref}
      lazyComponent={c.load}
      inputProps={{ lang, narrow }}
      durationInFrames={c.durationInFrames}
      fps={c.fps}
      compositionWidth={size.width}
      compositionHeight={size.height}
      initialFrame={still ? c.still : (c.start ?? 0)}
      style={{ width: '100%', height: '100%' }}
      initiallyMuted
      clickToPlay={false}
      loop
      // An individual owns this open-source project, which Remotion's free license covers.
      acknowledgeRemotionLicense
    />
  )
}
