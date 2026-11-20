import { describe, expect, it } from 'vitest'
import { starClose, starNever, starSeen, starShow } from './star'

const NOW = 1_800_000_000_000
const DAY_S = 86_400

describe('star card', () => {
  it('waits for a day of uptime after setup', () => {
    expect(starShow({}, true, DAY_S - 1, NOW)).toBe(false)
    expect(starShow({}, true, DAY_S, NOW)).toBe(true)
    expect(starShow({}, false, DAY_S, NOW)).toBe(false)
    expect(starShow({}, true, undefined, NOW)).toBe(false)
  })

  it('shows once after a firmware update, not on the first visit', () => {
    const first = starSeen({}, 'v1.0.0')
    expect(first).toEqual({ fw: 'v1.0.0', upd: false })
    expect(starShow(first, true, 60, NOW)).toBe(false)
    expect(starSeen(first, 'v1.0.0')).toBe(first)
    const upd = starSeen(first, 'v1.0.1')
    expect(starShow(upd, true, 60, NOW)).toBe(true)
    const closed = starClose(upd, NOW)
    expect(starShow(starSeen(closed, 'v1.0.1'), true, 60, NOW)).toBe(false)
  })

  it('ignores a missing version and a rollback', () => {
    const m = starSeen({}, 'v1.0.9')
    expect(starSeen(m, undefined)).toBe(m)
    expect(starShow(starSeen(m, 'v1.0.10'), true, 60, NOW)).toBe(true)
    const back = starSeen(m, 'v1.0.8')
    expect(back).toMatchObject({ fw: 'v1.0.8', upd: false })
    expect(starShow(back, true, 60, NOW)).toBe(false)
  })

  it('closed for now comes back after 30 days or with the next update', () => {
    const m = starClose(starSeen({}, 'v1'), NOW)
    expect(starShow(m, true, DAY_S, NOW + 29 * DAY_S * 1000)).toBe(false)
    expect(starShow(m, true, DAY_S, NOW + 30 * DAY_S * 1000)).toBe(true)
    expect(starShow(starSeen(m, 'v2'), true, 60, NOW + 1000)).toBe(true)
  })

  it('never again stays hidden, also after an update', () => {
    const m = starNever(starSeen({}, 'v1'))
    expect(starShow(m, true, DAY_S * 9, NOW)).toBe(false)
    expect(starShow(starSeen(m, 'v2'), true, DAY_S, NOW)).toBe(false)
  })
})
