export type Composition = {
  durationInFrames: number
  fps: number
  width: number
  height: number
  /** Where playback starts (#t= on the video) and the frame of the poster in public/stills/. */
  start: number
  /** Size of the -port video, which phones below 640px get. */
  portrait?: { width: number; height: number }
}

export const compositions = {
  hero: { durationInFrames: 360, fps: 30, width: 1280, height: 720, start: 0, portrait: { width: 1080, height: 1350 } },
  flash: { durationInFrames: 330, fps: 30, width: 1280, height: 720, start: 300, portrait: { width: 1080, height: 1350 } },
  hosts: { durationInFrames: 300, fps: 30, width: 1080, height: 1080, start: 30 },
  failover: { durationInFrames: 330, fps: 30, width: 1080, height: 1080, start: 0 },
  compare: { durationInFrames: 360, fps: 30, width: 1280, height: 720, start: 0, portrait: { width: 1080, height: 1350 } },
} satisfies Record<string, Composition>

export type CompositionId = keyof typeof compositions
