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
  if (o === 'off') return { keys: [], dials: [] }
  // as the firmware's ui_menu_chord: the first and last dial, else the first and last key
  const dials = M.dials?.n ?? 0
  return o !== 'keys' && dials > 1 ? { keys: [], dials: [0, dials - 1] } : { keys: [0, M.cols * M.rows - 1], dials: [] }
}
export function chordSentence(t: T, o: Chord, M: Model, holdS: string, d?: Deck) {
  const hl = chordHl(o, M, d)
  if (!hl.keys.length && !hl.dials.length) return t('chord.s_off')
  const [a = 0, b = 0] = hl.dials.length ? hl.dials : hl.keys
  return t(hl.dials.length ? 'chord.s_dials' : 'chord.s_keys', { a: a + 1, b: b + 1, s: holdS })
}
