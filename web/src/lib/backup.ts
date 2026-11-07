import type { Config, ConfigPatch } from '../api/types'
import { ipErrors, NAME_RE } from './validate'

type Flat = Record<string, unknown>
export const flat = (o: object, p = ''): Flat =>
  Object.fromEntries(
    Object.entries(o).flatMap(([k, v]) =>
      v && typeof v === 'object' && !Array.isArray(v) ? Object.entries(flat(v as object, `${p}${k}.`)) : [[p + k, v]],
    ),
  )

// Keys a backup may carry. Read-only fields (wifi_pass_set, token_set) never go back to the device.
const WRITABLE = new Set<keyof Config>([
  'name',
  'controller',
  'host',
  'brightness',
  'lan',
  'wlan',
  'wifi_ssid',
  'wifi_bssid',
  'servers',
  'server_pins',
  'satellite',
  'satellite_pin',
  'https_only',
  'cors_origins',
  'menu_chord',
  'menu_hold_ms',
  'usb_speed',
  'dock_paired_only',
  'input_lock',
])
const RESTARTS = new Set(['name', 'lan', 'wlan', 'wifi_ssid', 'wifi_bssid', 'usb_speed'])

export interface RestorePlan {
  patch: ConfigPatch
  rows: { key: string; now: unknown; file: unknown }[]
  restart: boolean
  errors: string[]
}

/** What applying a settings file would change, and what in it is invalid. */
export function planRestore(cfg: Config, file: unknown): RestorePlan {
  const errors: string[] = []
  if (!file || typeof file !== 'object' || Array.isArray(file))
    return { patch: {}, rows: [], restart: false, errors: ['not an object'] }
  const obj = file as Record<string, unknown>
  const top = Object.keys(obj).filter(
    (k) => WRITABLE.has(k as keyof Config) && JSON.stringify(obj[k]) !== JSON.stringify(cfg[k as keyof Config]),
  )
  const patch = Object.fromEntries(top.map((k) => [k, obj[k]])) as ConfigPatch
  for (const k of ['lan', 'wlan'] as const) {
    const v = patch[k]
    if (v) for (const [f, e] of Object.entries(ipErrors({ ...cfg[k], ...v }))) errors.push(`${k}.${f}: ${e}`)
  }
  if (patch.name !== undefined && !NAME_RE.test(patch.name)) errors.push('name')
  if (patch.brightness !== undefined && !(patch.brightness >= 0 && patch.brightness <= 100))
    errors.push('brightness: 0-100')
  const fc = flat(cfg)
  const fo = flat(patch)
  const rows = Object.keys(fo)
    .filter((k) => JSON.stringify(fo[k]) !== JSON.stringify(fc[k]))
    .map((key) => ({ key, now: fc[key], file: fo[key] }))
  return { patch, rows, restart: top.some((k) => RESTARTS.has(k)), errors }
}

/** GET /api/config holds no secrets, so it is the backup as it is. */
export const backupOf = (cfg: Config) => JSON.stringify(cfg, null, 1)
