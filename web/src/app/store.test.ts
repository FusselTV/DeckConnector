import { describe, expect, it } from 'vitest'
import type { InputEvent } from '../api/types'
import { addInput, hasRoute, INPUT_CAP, initial, reduce } from './store'

const ev = (o: Partial<InputEvent>): InputEvent => ({ at: 0, origin: 'deck', kind: 'down', key: 0, ...o })

describe('reduce', () => {
  it('merges ws state into the last full status and keeps what state leaves out', () => {
    const s1 = reduce(initial, {
      type: 'status',
      status: { uptime_s: 50, name: 'a', counters: { images: 3 } },
      now: 60_000,
    })
    const s2 = reduce(s1, { type: 'ws', now: 1, msg: { t: 'state', name: 'b' } })
    expect(s2.status).toMatchObject({ uptime_s: 50, name: 'b', counters: { images: 3 } })
    expect(s2.bootAt).toBe(10_000)
  })

  it('remembers when the connection was lost, once', () => {
    const a = reduce(initial, { type: 'conn', conn: 'reconnecting', now: 10 })
    const b = reduce(a, { type: 'conn', conn: 'offline', now: 20 })
    expect(b.asOf).toBe(10)
    expect(reduce(b, { type: 'conn', conn: 'live', now: 30 }).asOf).toBeNull()
  })

  it('returns the same state when nothing changes, so subscribers stay quiet', () => {
    expect(reduce(initial, { type: 'conn', conn: 'live', now: 1 })).toBe(initial)
    expect(reduce(initial, { type: 'ws', now: 1, msg: { t: 'ping' } })).toBe(initial)
  })

  it('counts screen events and tracks ota progress', () => {
    const s = reduce(initial, { type: 'ws', now: 1, msg: { t: 'screen' } })
    expect(s.screenRev).toBe(1)
    const o = reduce(s, { type: 'ws', now: 1, msg: { t: 'ota', target: 'p4', state: 'download', pct: 40 } })
    expect(o.ota).toEqual({ target: 'p4', state: 'download', pct: 40, error: undefined })
  })

  it('logs deck input but not simulated presses', () => {
    const a = reduce(initial, { type: 'ws', now: 5, msg: { t: 'input', kind: 'down', index: 2 } })
    expect(a.inputs[0]).toMatchObject({ origin: 'deck', key: 2, at: 5 })
    expect(reduce(a, { type: 'ws', now: 6, msg: { t: 'input', kind: 'down', index: 1, simulated: true } })).toBe(a)
  })
})

describe('addInput', () => {
  it('collapses quick turns of one dial', () => {
    let l = addInput([], ev({ kind: 'turn', key: 1, delta: 1, at: 0 }))
    l = addInput(l, ev({ kind: 'turn', key: 1, delta: 2, at: 500 }))
    expect(l).toHaveLength(1)
    expect(l[0]?.delta).toBe(3)
    l = addInput(l, ev({ kind: 'turn', key: 1, delta: 1, at: 3000 }))
    expect(l).toHaveLength(2)
  })
  it('keeps at most the ring size', () => {
    let l: InputEvent[] = []
    for (let i = 0; i < INPUT_CAP + 20; i++) l = addInput(l, ev({ at: i, key: i % 8 }))
    expect(l).toHaveLength(INPUT_CAP)
    expect(l[0]?.at).toBe(INPUT_CAP + 19)
  })
})

describe('hasRoute', () => {
  it('matches exact and wildcard routes, and counts unknown as no', () => {
    const r = [{ method: 'GET', path: '/api/screen/*', access: 'control' as const }]
    expect(hasRoute(r, 'GET', '/api/screen/3.jpg')).toBe(true)
    expect(hasRoute(r, 'POST', '/api/screen/3.jpg')).toBe(false)
    expect(hasRoute([], 'GET', '/api/input/log')).toBe(false)
  })
})
