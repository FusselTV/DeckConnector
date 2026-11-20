import { describe, expect, it } from 'vitest'
import type { Config, InputEvent } from '../api/types'
import { INPUT_CAP, initial, reduce, share } from './store'

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

  it('takes settings another client changed from ws state into the config', () => {
    const cfg = { brightness: 80, volume: 50, speaker: true } as unknown as Config
    const s1 = reduce(initial, { type: 'config', config: cfg })
    const s2 = reduce(s1, { type: 'ws', now: 1, msg: { t: 'state', brightness: 40, volume: 50, speaker: false } })
    expect(s2.config).toMatchObject({ brightness: 40, volume: 50, speaker: true })
    expect(reduce(s2, { type: 'ws', now: 1, msg: { t: 'state', brightness: 40 } }).config).toBe(s2.config)
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

  it('adds log entries on top and keeps at most the ring size', () => {
    const many = Array.from({ length: INPUT_CAP }, (_, i) => ev({ at: i }))
    const a = reduce(initial, { type: 'inputs', list: many, replace: true })
    const b = reduce(a, { type: 'inputs', list: [ev({ at: 999 })] })
    expect(b.inputs).toHaveLength(INPUT_CAP)
    expect(b.inputs[0]?.at).toBe(999)
    expect(reduce(b, { type: 'inputs', list: [ev({ at: 5 })], replace: true }).inputs).toHaveLength(1)
  })
})

describe('share', () => {
  it('keeps unchanged parts by reference and the whole when nothing changed', () => {
    const a = { network: { lan: { ip: '1' } }, decks: [{ serial: 'x' }], uptime_s: 1 }
    const b = share(a, { network: { lan: { ip: '1' } }, decks: [{ serial: 'x' }], uptime_s: 2 })
    expect(b).not.toBe(a)
    expect(b.network).toBe(a.network)
    expect(b.decks).toBe(a.decks)
    expect(b.uptime_s).toBe(2)
    expect(share(a, structuredClone(a))).toBe(a)
    expect(share(a, { ...a, decks: [] }).decks).toEqual([])
  })
})
