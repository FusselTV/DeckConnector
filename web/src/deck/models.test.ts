import { describe, expect, it } from 'vitest'
import { MODELS, modelOf } from './models'

describe('modelOf', () => {
  it('maps the names the firmware reports', () => {
    expect(modelOf('Stream Deck +')).toBe('plus')
    expect(modelOf('Stream Deck + XL')).toBe('plusxl')
    expect(modelOf('Stream Deck XL')).toBe('xl')
    expect(modelOf('Stream Deck Mini')).toBe('mini')
    expect(modelOf('Stream Deck Neo')).toBe('neo')
    expect(modelOf('Stream Deck MK.2')).toBe('mk2')
    expect(modelOf('Stream Deck', 32)).toBe('xl')
  })
  it('keeps every key on the front', () => {
    for (const [k, m] of Object.entries(MODELS)) {
      expect(m.x0 + (m.cols - 1) * m.px + m.k, k).toBeLessThanOrEqual(m.W)
      expect(m.y0 + (m.rows - 1) * m.py + m.k, k).toBeLessThanOrEqual(m.H)
    }
  })
})
