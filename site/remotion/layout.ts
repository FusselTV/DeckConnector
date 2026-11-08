import { rounded, type Pt } from './hw'
import type { Cam } from './scene'

export type Layout = {
  deck: { x: number; y: number; s: number }
  board: { x: number; y: number; s: number }
  sw: { x: number; y: number; w: number; h: number }
  mon: { x: number; y: number; s: number }
  usb: Pt[]
  poe: Pt[]
  net: Pt[]
  label: { x: number; y: number; size: number }
  wide: Cam
  cap: number
}

// Monitor is drawn at 480 x 340 and scaled; everything must stay inside the wide frame, captions sit top left.
export const LAND: Layout = {
  deck: { x: 70, y: 196, s: 3.7 },
  board: { x: 650, y: 470, s: 1.4 },
  sw: { x: 990, y: 440, w: 230, h: 90 },
  mon: { x: 800, y: 40, s: 0.88 },
  usb: rounded([[540, 420], [608, 420], [608, 554], [631, 554]], 20),
  poe: rounded([[964, 575], [1080, 575], [1080, 530]], 20),
  net: rounded([[1180, 440], [1180, 330]], 20),
  label: { x: 797, y: 452, size: 40 },
  wide: { x: 650, y: 365, z: 0.96, rx: 4, ry: 0 },
  cap: 46,
}
// Phones get a 4:5 stage: the deck fills the bottom, Companion sits above the frame and the camera pans up to it.
export const PORT: Layout = {
  deck: { x: 176, y: 600, s: 5.2 },
  board: { x: 372, y: 330, s: 1.6 },
  sw: { x: 770, y: 150, w: 250, h: 90 },
  mon: { x: 300, y: -720, s: 1 },
  usb: rounded([[300, 700], [300, 426], [350, 426]], 24),
  poe: rounded([[731, 450], [880, 450], [880, 240]], 24),
  net: rounded([[960, 150], [960, -500], [794, -500]], 30),
  label: { x: 540, y: 308, size: 44 },
  wide: { x: 540, y: 690, z: 1, rx: 3, ry: 0 },
  cap: 66,
}
