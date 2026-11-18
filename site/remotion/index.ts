import type { ComponentType } from 'react'
import type { LangProps } from './text'

export type Composition = {
  load: () => Promise<{ default: ComponentType<LangProps> }>
  durationInFrames: number
  fps: number
  width: number
  height: number
  /** First frame and the reduced-motion still; public/stills/ holds it as the poster (rendered by the site-shots harness). */
  start: number
  /** Size used when the stage is taller than wide. */
  portrait?: { width: number; height: number }
}

export const compositions = {
  hero: {
    load: () => import('./Hero'),
    durationInFrames: 360,
    fps: 30,
    width: 1280,
    height: 720,
    start: 0,
    portrait: { width: 1080, height: 1350 },
  },
  flash: {
    load: () => import('./Flash'),
    durationInFrames: 330,
    fps: 30,
    width: 1280,
    height: 720,
    start: 300,
    portrait: { width: 1080, height: 1350 },
  },
  hosts: { load: () => import('./Hosts'), durationInFrames: 300, fps: 30, width: 1080, height: 1080, start: 30 },
  failover: { load: () => import('./Failover'), durationInFrames: 330, fps: 30, width: 1080, height: 1080, start: 0 },
  compare: {
    load: () => import('./Compare'),
    durationInFrames: 360,
    fps: 30,
    width: 1280,
    height: 720,
    start: 0,
    portrait: { width: 1080, height: 1350 },
  },
} satisfies Record<string, Composition>

export type CompositionId = keyof typeof compositions
