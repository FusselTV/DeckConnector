import type { ComponentType } from 'react'
import { Sample } from './Sample'

export type Composition = {
  component: ComponentType
  durationInFrames: number
  fps: number
  width: number
  height: number
}

export const compositions = {
  sample: { component: Sample, durationInFrames: 90, fps: 30, width: 1280, height: 720 },
} satisfies Record<string, Composition>

export type CompositionId = keyof typeof compositions
