'use client'
import { Player } from '@remotion/player'
import { compositions } from '.'
import type { StageProps } from './RemotionStage'

export default function Stage({ id }: StageProps) {
  const c = compositions[id]
  return (
    <Player
      component={c.component}
      durationInFrames={c.durationInFrames}
      fps={c.fps}
      compositionWidth={c.width}
      compositionHeight={c.height}
      style={{ width: '100%', aspectRatio: `${c.width} / ${c.height}` }}
      autoPlay
      initiallyMuted
      loop
    />
  )
}
