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
  satellite_auto: true,
  satellite_pin: '',
  https_only: false,
  cors_origins: '',
  trusted_proxy: '',
  menu_chord: 'auto',
  menu_hold_ms: 2000,
  lang: 'de',
  usb_speed: 'auto',
  max_decks: 2,
  screen_off_s: 0,
  wifi_bssid: '',
  wifi2_ssid: '',
  wifi2_pass_set: false,
  qr_login: true,
  ntp_server: 'pool.ntp.org',
  dock_paired_only: false,
  setup_done: true,
  token_set: false,
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
    expect(planRestore(cfg, { ...cfg, brightness: 10 }).restart).toBe(false)
  })
  it('reports invalid values', () => {
    expect(planRestore(cfg, { ...cfg, lan: { dhcp: false, ip: 'x', mask: '' } }).errors.length).toBeGreaterThan(0)
    expect(planRestore(cfg, { ...cfg, brightness: 200 }).errors).toEqual([{ key: 'brightness', err: 'fw.r_bri' }])
  })
  it('takes only a backup', () => {
    const not = [{ key: '', err: 'fw.not_backup' }]
    expect(planRestore(cfg, [1]).errors).toEqual(not)
    expect(planRestore(cfg, { name: 'my-app', version: '1.0.0', dependencies: {} }).errors).toEqual(not)
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
  it('the backup Wi-Fi goes back with its password and needs no restart', () => {
    const p = planRestore(cfg, { ...cfg, wifi2_ssid: 'Ersatz', wifi2_pass: 'backup-secret' })
    expect(p.patch).toEqual({ wifi2_ssid: 'Ersatz', wifi2_pass: 'backup-secret' })
    expect(p.restart).toBe(false)
  })
})
