import type { ComponentType } from 'react'
import type { LangProps } from './text'

export type Composition = {
  load: () => Promise<{ default: ComponentType<LangProps> }>
  durationInFrames: number
  fps: number
  width: number
  height: number
  /** Frame shown when the reader prefers reduced motion. */
  still: number
  /** First frame, so a page opens on a frame that shows the whole setup. */
  start?: number
  /** Size used when the stage is taller than wide. */
  portrait?: { width: number; height: number }
}

export const compositions = {
  hero: { load: () => import('./Hero'), durationInFrames: 360, fps: 30, width: 1280, height: 720, still: 270, start: 236, portrait: { width: 1080, height: 1350 } },
  flash: { load: () => import('./Flash'), durationInFrames: 330, fps: 30, width: 1280, height: 720, still: 312 },
  hosts: { load: () => import('./Hosts'), durationInFrames: 300, fps: 30, width: 1080, height: 1080, still: 84, start: 30 },
  failover: { load: () => import('./Failover'), durationInFrames: 330, fps: 30, width: 1080, height: 1080, still: 170 },
} satisfies Record<string, Composition>

export type CompositionId = keyof typeof compositions
