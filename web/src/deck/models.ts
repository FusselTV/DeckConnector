// Fronts in real millimetres; --mm (px per mm) comes from the container width.
// Stream Deck +: measured on a real deck and Elgato renders. + XL: SimDeck.
// MK.2, XL, Mini, Neo, Pedal, N4: retail outer sizes, grids and native key sizes estimated.
interface Box {
  x: number
  y: number
  w: number
  h: number
}
export interface Model {
  name: string
  W: number
  H: number
  R: number
  cols: number
  rows: number
  k: number
  kr: number
  li: number
  x0: number
  y0: number
  px: number
  py: number
  win?: Box
  /** `keys`: each zone is a key after the grid, and a swipe turns the encoder after the dials (Stream Dock N4). */
  strip?: Box & { z: number; keys?: boolean }
  tray?: Box & { r: number }
  dials?: { n: number; cy: number; d: number; x: number; w: number }
  info?: Box
  tp?: Box[]
  mark: { y: number }
}

export type ModelKey = 'plus' | 'plusxl' | 'mk2' | 'xl' | 'mini' | 'neo' | 'pedal' | 'n4'
export const MODELS: Record<ModelKey, Model> = {
  plus: {
    name: 'Stream Deck +',
    W: 140,
    H: 133.4,
    R: 11,
    cols: 4,
    rows: 2,
    k: 18.35,
    kr: 2.4,
    li: 1.075,
    x0: 16.68,
    y0: 11.75,
    px: 29.565,
    py: 21.6,
    win: { x: 10.33, y: 57.02, w: 119.32, h: 23.61 },
    strip: { x: 16, y: 62.5, w: 108, h: 13.5, z: 4 },
    tray: { x: 10.08, y: 88.2, w: 119.39, h: 35.5, r: 6.5 },
    dials: { n: 4, cy: 105.95, d: 14.4, x: 11.08, w: 118.26 },
    mark: { y: 5.5 },
  },
  plusxl: {
    name: 'Stream Deck + XL',
    W: 205,
    H: 147,
    R: 11,
    cols: 9,
    rows: 4,
    k: 14.8,
    kr: 2.1,
    li: 0,
    x0: 22,
    y0: 14,
    px: 18.28,
    py: 18.28,
    strip: { x: 22, y: 90.5, w: 161, h: 14, z: 6 },
    tray: { x: 14, y: 110, w: 177, h: 29, r: 6 },
    dials: { n: 6, cy: 124, d: 19, x: 22, w: 161 },
    mark: { y: 6.8 },
  },
  mk2: {
    name: 'Stream Deck MK.2',
    W: 118,
    H: 84,
    R: 6,
    cols: 5,
    rows: 3,
    k: 14.4,
    kr: 2.1,
    li: 0.4,
    x0: 14.2,
    y0: 11,
    px: 18.8,
    py: 18.8,
    mark: { y: 75.3 },
  },
  xl: {
    name: 'Stream Deck XL',
    W: 182,
    H: 112,
    R: 7,
    cols: 8,
    rows: 4,
    k: 15,
    kr: 2.1,
    li: 0.4,
    x0: 17,
    y0: 15,
    px: 19,
    py: 19,
    mark: { y: 101.3 },
  },
  mini: {
    name: 'Stream Deck Mini',
    W: 84,
    H: 60,
    R: 6,
    cols: 3,
    rows: 2,
    k: 15.5,
    kr: 2.1,
    li: 0.4,
    x0: 14.25,
    y0: 8,
    px: 20,
    py: 20,
    mark: { y: 51.3 },
  },
  neo: {
    name: 'Stream Deck Neo',
    W: 107,
    H: 78,
    R: 8,
    cols: 4,
    rows: 2,
    k: 15,
    kr: 2.1,
    li: 0.4,
    x0: 14.5,
    y0: 9,
    px: 21,
    py: 20,
    info: { x: 33, y: 50.5, w: 41, h: 11 },
    tp: [
      { x: 14.5, y: 50.5, w: 15, h: 11 },
      { x: 77.5, y: 50.5, w: 15, h: 11 },
    ],
    mark: { y: 71.3 },
  },
  pedal: {
    name: 'Stream Deck Pedal',
    W: 223,
    H: 141,
    R: 14,
    cols: 3,
    rows: 1,
    k: 62,
    kr: 6,
    li: 0,
    x0: 12.5,
    y0: 34,
    px: 70,
    py: 70,
    mark: { y: 126 },
  },
  n4: {
    name: 'Stream Dock N4',
    W: 140,
    H: 104,
    R: 8,
    cols: 5,
    rows: 2,
    k: 17,
    kr: 2.1,
    li: 0.4,
    x0: 16.5,
    y0: 11.5,
    px: 22.5,
    py: 21,
    strip: { x: 18, y: 54, w: 104, h: 18, z: 4, keys: true },
    dials: { n: 4, cy: 88, d: 14, x: 18, w: 104 },
    mark: { y: 5 },
  },
}

export function modelOf(name = '', keys?: number): ModelKey {
  const n = name.toLowerCase()
  if (n.includes('+') && n.includes('xl')) return 'plusxl'
  if (n.includes('+') || n.includes('plus')) return 'plus'
  if (n.includes('neo')) return 'neo'
  if (n.includes('pedal')) return 'pedal'
  if (n.includes('mini')) return 'mini'
  if (/\bn4\b|akp05/.test(n)) return 'n4'
  if (n.includes('xl')) return 'xl'
  if (/n3|akp03|cn002/.test(n)) return 'mini'
  if (keys === 6) return 'mini'
  if (keys === 32) return 'xl'
  return 'mk2'
}
