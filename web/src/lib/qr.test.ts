import { describe, expect, it } from 'vitest'
import { qrPath } from './qr'

describe('qrPath', () => {
  it('reads the modules row by row, lowest bit first', () => {
    expect(qrPath('2:0d')).toEqual({ n: 2, d: 'M0 0h1v1h-1zM0 1h1v1h-1zM1 1h1v1h-1z' })
  })

  it('crosses byte borders', () => {
    expect(qrPath('3:0001').d).toBe('M2 2h1v1h-1z')
  })

  it('draws nothing without a size', () => {
    expect(qrPath('')).toEqual({ n: 0, d: '' })
  })
})
