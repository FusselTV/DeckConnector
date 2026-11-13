import { describe, expect, it } from 'vitest'
import { candidates, companionError, worst } from './companion'

describe('Companion address candidates', () => {
  it("tries the input first, then Companion's own ports and the proxy path", () => {
    expect(candidates('192.168.1.20')).toEqual([
      '192.168.1.20',
      'ws://192.168.1.20:16623',
      'wss://192.168.1.20/satellite',
    ])
    expect(candidates('studio.local:16623')).toEqual([
      'studio.local:16623',
      'ws://studio.local:16623',
      'studio.local',
      'wss://studio.local/satellite',
    ])
  })
  it('turns http into ws and also tries the secure one, and the other way round', () => {
    expect(candidates('http://c.example:8000')).toEqual([
      'ws://c.example:8000',
      'wss://c.example:8000',
      'ws://c.example:8000/satellite',
      'c.example',
      'ws://c.example:16623',
      'wss://c.example/satellite',
    ])
    expect(candidates('https://companion.int.example/satellite')).toEqual([
      'wss://companion.int.example/satellite',
      'ws://companion.int.example/satellite',
      'companion.int.example',
      'ws://companion.int.example:16623',
    ])
    expect(candidates('tcp://10.0.0.5:16622')[0]).toBe('10.0.0.5')
  })
  it('refuses what is not an address', () => {
    for (const bad of ['999.1.1.1', 'host:99999', 'a b', 'ftp://x', '']) expect(candidates(bad), bad).toEqual([])
    expect(companionError('a b')).toBe('wiz.addr_bad')
    expect(companionError('')).toBeNull()
  })
  it('keeps every candidate short enough for the setting, six at most', () => {
    const long = `${'a'.repeat(50)}.example`
    for (const a of candidates(`https://${long}`)) expect(a.length).toBeLessThan(64)
    expect(candidates('https://x.example').length).toBeLessThanOrEqual(6)
  })
  it('names the failure that says the most', () => {
    expect(
      worst([
        { addr: 'a', code: 'probe_timeout', error: '' },
        { addr: 'b', code: 'probe_not_ws', error: '' },
        { addr: 'c', code: 'probe_refused', error: '' },
      ])?.addr,
    ).toBe('b')
  })
})
