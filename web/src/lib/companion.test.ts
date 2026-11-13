import { describe, expect, it } from 'vitest'
import { candidates, companionError, fallback, worst } from './companion'

describe('Companion address candidates', () => {
  it("tries the input first, then Companion's own ports and the proxy path", () => {
    expect(candidates('192.168.1.20')).toEqual([
      '192.168.1.20',
      'ws://192.168.1.20:16623',
      'wss://192.168.1.20/satellite',
    ])
    expect(candidates('studio.local:16623')).toEqual([
      'ws://studio.local:16623',
      'studio.local:16623',
      'studio.local',
      'wss://studio.local/satellite',
    ])
    expect(candidates('studio.local:16622')).toEqual([
      'studio.local',
      'ws://studio.local:16623',
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
    expect(candidates('HTTPS://companion.int.example/satellite')).toEqual([
      'wss://companion.int.example/satellite',
      'ws://companion.int.example/satellite',
      'companion.int.example',
      'ws://companion.int.example:16623',
    ])
    expect(candidates('tcp://10.0.0.5:16622')[0]).toBe('10.0.0.5')
  })
  it('refuses what is not an address, also what the setting would refuse', () => {
    for (const bad of ['999.1.1.1', 'host:99999', 'a b', 'ftp://x', '', 'wss://x.example/a"b'])
      expect(candidates(bad), bad).toEqual([])
    expect(companionError('a b')).toBe('wiz.addr_bad')
    expect(companionError('')).toBeNull()
  })
  it('keeps every candidate short enough for the setting', () => {
    for (const a of candidates(`https://${'a'.repeat(50)}.example`)) expect(a.length).toBeLessThan(64)
  })
  it('saves the host, not the web page port, when nothing answered', () => {
    expect(fallback('http://10.0.0.5:8000')).toBe('10.0.0.5')
    expect(fallback('studio.local:17000')).toBe('studio.local:17000')
    expect(fallback('wss://c.example/satellite')).toBe('wss://c.example/satellite')
  })
  it("names the failure that says the most, Satellite's own port behind a web page address", () => {
    const tried = [
      { addr: 'a.example', code: 'probe_timeout', error: '' },
      { addr: 'ws://a.example:8000', code: 'probe_not_ws', error: '' },
      { addr: 'ws://a.example:16623', code: 'probe_refused', error: '' },
    ]
    expect(worst(tried, 'a.example')?.addr).toBe('ws://a.example:8000')
    expect(worst(tried, 'http://a.example:8000')?.addr).toBe('a.example')
  })
})
