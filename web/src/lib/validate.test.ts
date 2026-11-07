import { describe, expect, it } from 'vitest'
import {
  cidrMask,
  companionError,
  corsError,
  ipErrors,
  isIP,
  isMask,
  NAME_RE,
  nameError,
  passwordError,
  pinError,
  splitCidr,
  tokenNameError,
  wifiError,
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
    expect(corsError('https://a.example/x')).toBe('access.cors_bad')
  })
})

describe('passwords and names', () => {
  it('needs 6 characters and a match', () => {
    expect(passwordError('')).toBeNull()
    expect(passwordError('abc')).toBe('access.pw_short')
    expect(passwordError('abcdef')).toBeNull()
  })
  it('limits token names', () => {
    expect(tokenNameError('home-assistant')).toBeNull()
    expect(tokenNameError('has space')).toBe('access.tbad')
    expect(tokenNameError('x'.repeat(24))).toBe('access.tbad')
  })
})

describe('values the firmware refuses or that lock the device out', () => {
  it('takes masks from /1 to /30 only', () => {
    expect(isMask('0.0.0.0')).toBe(false)
    expect(isMask('255.255.255.255')).toBe(false)
    expect(isMask('255.255.255.254')).toBe(false)
    expect(isMask('255.255.255.252')).toBe(true)
    expect(isMask('128.0.0.0')).toBe(true)
    expect(splitCidr('10.0.0.5/0')).toEqual({ ip: '10.0.0.5/0' })
  })
  it('refuses the network, broadcast and gateway address as the own address', () => {
    expect(ipErrors({ ...st, ip: '192.168.1.0' }).ip).toBe('net.ip_edge')
    expect(ipErrors({ ...st, ip: '192.168.1.255' }).ip).toBe('net.ip_edge')
    expect(ipErrors({ ...st, ip: '192.168.1.1' }).gateway).toBe('net.gw_self')
    expect(ipErrors({ ...st, ip: ' 192.168.1.20 ' }).ip).toBe('net.bad_ip')
    expect(ipErrors(st)).toEqual({})
  })
  it('checks the device name like the firmware and the restore', () => {
    expect(nameError('studio-deck')).toBeNull()
    for (const n of ['', 'my deck', '...', 'a'.repeat(32), '😀', '-deck', 'deck-'])
      expect(nameError(n)).toBe('net.bad_name')
    expect(nameError('a')).toBeNull()
  })
  it('keeps the name pattern valid for the input pattern attribute, which compiles it in v mode', () => {
    const re = new RegExp(`^(?:${NAME_RE.source})$`, 'v')
    expect(re.test('studio-deck')).toBe(true)
    expect(re.test('-deck')).toBe(false)
  })
  it('says when a password is too long instead of cutting it', () => {
    expect(passwordError('p'.repeat(24))).toBe('access.pw_long')
    expect(passwordError('p'.repeat(23))).toBeNull()
    expect(passwordError('ä'.repeat(12))).toBe('access.pw_long')
    expect(passwordError('äää')).toBeNull()
  })
  it('wants lower-case schemes, no spaces and short hosts in server URLs', () => {
    expect(wsUrlError('WS://h:1/x')).toBe('src.bad_url')
    expect(wsUrlError('wss://h /x')).toBe('src.bad_url')
    expect(wsUrlError('wss://h/"x')).toBe('src.bad_url')
    expect(wsUrlError(`wss://${'h'.repeat(64)}/x`)).toBe('src.bad_url')
    expect(companionError('a'.repeat(64))).toBe('wiz.addr_bad')
  })
  it('limits CORS and the Wi-Fi fields', () => {
    expect(corsError('http://' + 'a'.repeat(130))).toBe('access.cors_bad')
    expect(corsError('example.com')).toBe('access.cors_bad')
    expect(wifiError('  ', '')).toBe('wifi.pick')
    expect(wifiError('😀'.repeat(12), '')).toBe('wifi.pick')
    expect(wifiError('Home', 'short')).toBe('wifi.pass_len')
    expect(wifiError('Home', 'p'.repeat(64))).toBe('wifi.pass_len')
    expect(wifiError('Home', 'ab'.repeat(32))).toBeNull()
    expect(wifiError('Home', 'p'.repeat(63))).toBeNull()
    expect(wifiError('Home', '')).toBeNull()
  })
})
