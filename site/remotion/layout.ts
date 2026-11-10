import { arc, at, BOARD_ANT, BOARD_USB, BOARD_USBC, rounded, type Pt } from './hw'
import type { Cam } from './scene'

type Box = { x: number; y: number; w: number; h: number }
export type Layout = {
  deck: { x: number; y: number; s: number }
  board: { x: number; y: number; s: number }
  router: Box
  mon: { x: number; y: number; s: number }
  usb: Pt[]
  net: Pt[]
  /** Board antenna to the router's left antenna, then along the router top to where net starts. */
  air: Pt[]
  power?: { pts: Pt[]; plug: { x: number; y: number; s: number } }
  label: { x: number; y: number; size: number }
  wide: Cam
  cap: number
}

const radio = (board: Layout['board'], r: Box, net0: Pt, lift: number): Pt[] => {
  const tip: Pt = [r.x + 40, r.y - 46]
  return [...arc(at(board, BOARD_ANT), tip, lift), [tip[0], r.y], [net0[0], r.y]]
}

// Monitor is drawn at 480 x 340 and scaled; everything must stay inside the wide frame, captions sit top left.
// The deck cable runs straight into the board's USB-A, the power plug hangs from its USB-C, the name sits under the board.
const landBoard = { x: 690, y: 440, s: 1.3 }
const landRouter = { x: 1040, y: 450, w: 220, h: 90 }
const landNet = rounded([[1160, 450], [1160, 310]], 20)
const landC = at(landBoard, BOARD_USBC)
export const LAND: Layout = {
  deck: { x: 60, y: 196, s: 3.7 },
  board: landBoard,
  router: landRouter,
  mon: { x: 820, y: 44, s: 0.8 },
  usb: [[560, at(landBoard, BOARD_USB)[1]], at(landBoard, BOARD_USB)],
  net: landNet,
  air: radio(landBoard, landRouter, landNet[0] ?? [0, 0], 40),
  power: { pts: rounded([landC, [640, landC[1]], [640, 660]], 14), plug: { x: 640, y: 652, s: 0.8 } },
  label: { x: 826, y: 704, size: 34 },
  wide: { x: 660, y: 380, z: 0.93, rx: 5, ry: 0 },
  cap: 46,
}
// Phones get a 4:5 stage: caption top left, Companion top right, board and router in the middle, deck at the bottom.
const portBoard = { x: 230, y: 470, s: 1.5 }
const portRouter = { x: 720, y: 560, w: 240, h: 90 }
const portNet = rounded([[880, 560], [880, 360]], 20)
const portC = at(portBoard, BOARD_USBC)
export const PORT: Layout = {
  deck: { x: 274, y: 790, s: 3.8 },
  board: portBoard,
  router: portRouter,
  mon: { x: 560, y: 70, s: 0.88 },
  usb: rounded([[330, 805], [330, 772], [96, 772], [96, at(portBoard, BOARD_USB)[1]], at(portBoard, BOARD_USB)], 24),
  net: portNet,
  air: radio(portBoard, portRouter, portNet[0] ?? [0, 0], 70),
  power: { pts: rounded([portC, [160, portC[1]], [160, 690]], 14), plug: { x: 160, y: 680, s: 0.85 } },
  label: { x: 387, y: 748, size: 44 },
  wide: { x: 540, y: 675, z: 0.97, rx: 2, ry: 0 },
  cap: 66,
}
