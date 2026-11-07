import { describe, expect, it } from 'vitest'
import {
  cidrMask,
  companionError,
  corsError,
  ipErrors,
  isIP,
  isMask,
  passwordError,
  pinError,
  splitCidr,
  tokenNameError,
  wsUrlError,
} from './validate'

const st = { dhcp: false, ip: '192.168.1.20', mask: '255.255.255.0', gateway: '192.168.1.1', dns: '' }

describe('IP addresses', () => {
  it('accepts dotted quads only', () => {
    expect(isIP('10.0.0.5')).toBe(true)
    expect(isIP('256.1.1.1')).toBe(false)
    expect(isIP('1.2.3')).toBe(false)
    expect(isIP('01.2.3.4')).toBe(false)
  })
  it('knows real netmasks', () => {
    expect(isMask('255.255.254.0')).toBe(true)
    expect(isMask('255.0.255.0')).toBe(false)
  })
  it('turns a CIDR prefix into a mask', () => {
    expect(cidrMask(24)).toBe('255.255.255.0')
    expect(cidrMask(20)).toBe('255.255.240.0')
    expect(splitCidr('10.0.0.5/16')).toEqual({ ip: '10.0.0.5', mask: '255.255.0.0' })
    expect(splitCidr('10.0.0.5/40')).toEqual({ ip: '10.0.0.5/40' })
  })
  it('checks a static address form', () => {
    expect(ipErrors(st)).toEqual({})
    expect(ipErrors({ ...st, dhcp: true, ip: '' })).toEqual({})
    expect(ipErrors({ ...st, ip: '' }).ip).toBe('net.need')
    expect(ipErrors({ ...st, gateway: '10.0.0.1' }).gateway).toBe('net.gw_out')
    expect(ipErrors({ ...st, dns: 'x' }).dns).toBe('net.bad_ip')
  })
})

describe('pins and URLs', () => {
  it('takes a SHA-256 pin with or without colons', () => {
    expect(pinError('')).toBeNull()
    expect(pinError('ab'.repeat(32))).toBeNull()
    expect(pinError(Array(32).fill('AB').join(':'))).toBeNull()
    expect(pinError('abc')).toBe('src.pin_bad')
  })
  it('wants ws:// or wss:// for servers', () => {
    expect(wsUrlError('wss://example.org/x')).toBeNull()
    expect(wsUrlError('http://example.org')).toBe('src.bad_url')
  })
  it('takes the Companion address forms', () => {
    for (const ok of ['192.168.1.5', 'studio.local:16622', 'wss://c.example/satellite', ''])
      expect(companionError(ok), ok).toBeNull()
    for (const bad of ['999.1.1.1', 'host:99999', 'a b']) expect(companionError(bad), bad).toBe('wiz.addr_bad')
  })
  it('wants origins without a path for CORS', () => {
    expect(corsError('https://a.example, http://b:8123')).toBeNull()
    expect(corsError('https://a.example/x')).toBe('src.bad_url')
  })
})

describe('passwords and names', () => {
  it('needs 6 characters and a match', () => {
    expect(passwordError('', '')).toEqual({})
    expect(passwordError('abc', 'abc')).toEqual({ pw: 'access.pw_short' })
    expect(passwordError('abcdef', 'abcdeg')).toEqual({ rep: 'access.pw_diff' })
    expect(passwordError('abcdef', 'abcdef')).toEqual({})
  })
  it('limits token names', () => {
    expect(tokenNameError('home-assistant')).toBeNull()
    expect(tokenNameError('has space')).toBe('access.tbad')
    expect(tokenNameError('x'.repeat(24))).toBe('access.tbad')
  })
})
