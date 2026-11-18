import type { Config } from '../api/types'
import type { Deck } from '../app/derive'
import type { T } from '../i18n'
import type { Model } from './models'

type Chord = Config['menu_chord']
export interface Hl {
  keys: number[]
  dials: number[]
}
export function chordHl(o: Chord, M: Model, d?: Deck): Hl {
  const c = o !== 'off' ? d?.chords?.[o] : undefined
  if (c)
    return c.on === 'off'
      ? { keys: [], dials: [] }
      : c.on === 'keys'
        ? { keys: [0, c.last], dials: [] }
        : { keys: [], dials: [0, c.last] }
  const nk = M.cols * M.rows
  const dials = M.dials?.n ?? 0
  const enc = { keys: [], dials: [0, Math.min(3, dials - 1)] }
  const key = { keys: [0, nk - 1], dials: [] }
  return o === 'encoders' ? enc : o === 'keys' ? key : o === 'auto' ? (dials ? enc : key) : { keys: [], dials: [] }
}
export function chordSentence(t: T, o: Chord, M: Model, holdS: string, d?: Deck) {
  const hl = chordHl(o, M, d)
  if (!hl.keys.length && !hl.dials.length) return t('chord.s_off')
  const [a = 0, b = 0] = hl.dials.length ? hl.dials : hl.keys
  return t(hl.dials.length ? 'chord.s_dials' : 'chord.s_keys', { a: a + 1, b: b + 1, s: holdS })
}
