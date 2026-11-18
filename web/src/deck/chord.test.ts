import { describe, expect, it } from 'vitest'
import type { Deck } from '../app/derive'
import { MODELS } from './models'
import type { T } from '../i18n'
import { chordSentence } from './chord'

const t: T = (key, vars) => `${key} ${JSON.stringify(vars ?? {})}`
const n4 = {
  model: 'Stream Dock N4',
  serial: 'x',
  kind: 'mk2',
  speed: 'high',
  chords: { auto: { on: 'keys', last: 9 }, keys: { on: 'keys', last: 9 }, encoders: { on: 'keys', last: 9 } },
} as Deck

describe('chordSentence', () => {
  it('names the controls the firmware picked when the deck has controls that cannot be held', () => {
    expect(chordSentence(t, 'encoders', MODELS.mk2, '2', n4)).toBe('chord.s_keys {"a":1,"b":10,"s":"2"}')
  })

  it('sends a deck where no two controls can be held to this page', () => {
    const akp153 = { ...n4, chords: { ...n4.chords, auto: { on: 'off', last: 0 } } } as Deck
    expect(chordSentence(t, 'auto', MODELS.mk2, '2', akp153)).toBe('chord.s_off {}')
  })

  it('keeps the Elgato rule for decks without that list', () => {
    expect(chordSentence(t, 'auto', MODELS.plus, '2')).toBe('chord.s_dials {"a":1,"b":4,"s":"2"}')
    expect(chordSentence(t, 'off', MODELS.plus, '2')).toBe('chord.s_off {}')
  })
})
