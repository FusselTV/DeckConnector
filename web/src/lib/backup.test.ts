import { describe, expect, it } from 'vitest'
import type { Config } from '../api/types'
import { planRestore } from './backup'

const ip = { dhcp: true, ip: '', mask: '', gateway: '', dns: '' }
const cfg: Config = {
  name: 'deck',
  controller: 'companion',
  host: '',
  brightness: 80,
  lan: ip,
  wlan: ip,
  wifi_ssid: '',
  wifi_pass_set: false,
  servers: ['', '', '', ''],
  server_pins: ['', '', '', ''],
  satellite: '',
  satellite_pin: '',
  https_only: false,
  cors_origins: '',
  menu_chord: 'auto',
  menu_hold_ms: 2000,
  usb_speed: 'auto',
}

describe('planRestore', () => {
  it('finds nothing to do in the same file', () => {
    expect(planRestore(cfg, cfg).rows).toEqual([])
  })
  it('lists changed leaves and skips read-only keys', () => {
    const p = planRestore(cfg, {
      ...cfg,
      brightness: 50,
      wifi_pass_set: true,
      lan: { ...ip, dhcp: false, ip: '10.0.0.2', mask: '255.0.0.0' },
    })
    expect(p.rows.map((r) => r.key).sort()).toEqual(['brightness', 'lan.dhcp', 'lan.ip', 'lan.mask'])
    expect(p.restart).toBe(true)
    expect(p.patch).not.toHaveProperty('wifi_pass_set')
    expect(p.errors).toEqual([])
  })
  it('instant keys need no restart', () => {
    expect(planRestore(cfg, { brightness: 10 }).restart).toBe(false)
  })
  it('reports invalid values', () => {
    expect(planRestore(cfg, { lan: { dhcp: false, ip: 'x', mask: '' } }).errors.length).toBeGreaterThan(0)
    expect(planRestore(cfg, { brightness: 200 }).errors).toEqual(['brightness: 0-100'])
    expect(planRestore(cfg, [1]).errors).toEqual(['not an object'])
  })
  it('takes the secrets of a full backup back and shows only whether they are set', () => {
    const p = planRestore(cfg, { ...cfg, admin_pass: 'hunter22', wifi_pass: 'wlan-secret', token: '' })
    expect(p.patch).toMatchObject({ admin_pass: 'hunter22', wifi_pass: 'wlan-secret', token: '' })
    expect(p.rows).toEqual([
      { key: 'admin_pass', now: true, file: true, secret: true },
      { key: 'wifi_pass', now: false, file: true, secret: true },
      { key: 'token', now: false, file: false, secret: true },
    ])
    expect(JSON.stringify(p.rows)).not.toMatch(/hunter22|wlan-secret/)
    expect(p.restart).toBe(true)
  })
})
