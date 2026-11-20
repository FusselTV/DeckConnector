import { describe, expect, it } from 'vitest'
import type { Config } from '../api/types'
import { starClose, starNever, starShow } from './star'

const NOW = 1_800_000_000
const DAY = 86_400
const cfg = (o: Partial<Config> = {}) =>
  ({ setup_done: true, star_off: false, star_fw: 'v1.0.0', star_until: 0, ...o }) as Config
const after = (c: Config, patch: object): Config => ({ ...c, ...patch })

describe('star card', () => {
  it('waits for a day of uptime after setup', () => {
    expect(starShow(cfg(), 'v1.0.0', DAY - 1, NOW)).toBe(false)
    expect(starShow(cfg(), 'v1.0.0', DAY, NOW)).toBe(true)
    expect(starShow(cfg({ setup_done: false }), 'v1.0.0', DAY, NOW)).toBe(false)
    expect(starShow(cfg(), 'v1.0.0', undefined, NOW)).toBe(false)
  })

  it('never shows on firmware without the fields', () => {
    expect(starShow(cfg({ star_off: undefined }), 'v1.0.0', DAY, NOW)).toBe(false)
  })

  it('shows after a firmware update until closed, not for an empty or older one', () => {
    expect(starShow(cfg(), 'v1.0.1', 60, NOW)).toBe(true)
    expect(starShow(cfg(), 'v1.0.10', 60, NOW)).toBe(true)
    expect(starShow(cfg({ star_fw: '' }), 'v1.0.1', 60, NOW)).toBe(false)
    expect(starShow(cfg({ star_fw: 'v1.0.9' }), 'v1.0.8', 60, NOW)).toBe(false)
    expect(starShow(cfg(), 'v1.0.0-5-gabc1234-dirty', 60, NOW)).toBe(false)
    expect(starShow(cfg({ star_fw: 'v1.0.0-rc3-480-gc6d552a' }), 'v1.0.0', 60, NOW)).toBe(false)
    expect(starShow(cfg({ star_fw: 'v1.0.0-rc3' }), 'v1.1.0-rc1', 60, NOW)).toBe(true)
    expect(starShow(after(cfg(), starClose('v1.0.1', NOW)), 'v1.0.1', 60, NOW)).toBe(false)
  })

  it('closed for now comes back after 30 days or with the next update', () => {
    const c = after(cfg(), starClose('v1.0.0', NOW))
    expect(starShow(c, 'v1.0.0', DAY, NOW + 29 * DAY)).toBe(false)
    expect(starShow(c, 'v1.0.0', DAY, NOW + 30 * DAY)).toBe(true)
    expect(starShow(c, 'v1.1.0', 60, NOW + 1)).toBe(true)
  })

  it('never again stays hidden, also after an update', () => {
    const c = after(cfg(), starNever('v1.0.0'))
    expect(starShow(c, 'v1.0.0', DAY * 9, NOW)).toBe(false)
    expect(starShow(c, 'v2.0.0', DAY, NOW)).toBe(false)
  })
})
