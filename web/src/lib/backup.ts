import type { Config, ConfigPatch } from '../api/types'
import type { Key } from '../i18n/types'
import { ipErrors, NAME_RE } from './validate'

// GET /api/config leaves these out, so a full backup's values cannot be compared and always go back.
const SECRETS = ['admin_pass', 'wifi_pass', 'wifi2_pass', 'token'] as const
const isSecret = (k: string) => (SECRETS as readonly string[]).includes(k)

type Flat = Record<string, unknown>
const flat = (o: object, p = ''): Flat =>
  Object.fromEntries(
    Object.entries(o).flatMap(([k, v]) =>
      v && typeof v === 'object' && !Array.isArray(v) ? Object.entries(flat(v as object, `${p}${k}.`)) : [[p + k, v]],
    ),
  )

// Keys a backup may carry. Read-only fields (wifi_pass_set, token_set) never go back to the device.
export const WRITABLE = new Set<keyof ConfigPatch>([
  'name',
  'controller',
  'host',
  'brightness',
  'volume',
  'speaker',
  'lan',
  'wlan',
  'wifi_ssid',
  'wifi_bssid',
  'wifi2_ssid',
  'servers',
  'server_pins',
  'satellite',
  'satellite_auto',
  'satellite_pin',
  'https_only',
  'cors_origins',
  'trusted_proxy',
  'menu_chord',
  'menu_hold_ms',
  'lang',
  'usb_speed',
  'max_decks',
  'screen_off_s',
  'screen_dim',
  'star_off',
  'star_fw',
  'star_until',
  'dock_paired_only',
  'qr_login',
  'ntp_server',
  'matter',
  ...SECRETS,
])
const RESTARTS = new Set(['name', 'lan', 'wlan', 'wifi_ssid', 'wifi_bssid', 'usb_speed', 'ntp_server'])
// The firmware restarts when a secret differs from the stored one, which the page cannot see.
export const restarts = (k: string) => RESTARTS.has(k) || (isSecret(k) && k !== 'wifi2_pass')

interface RestorePlan {
  patch: ConfigPatch
  /** For a secret, `now` and `file` say only whether it is set. */
  rows: { key: string; now: unknown; file: unknown; secret?: boolean }[]
  restart: boolean
  errors: { key: string; err: Key }[]
}

// Both kinds of backup (GET /api/config and /api/backup) carry these; another JSON file does not.
const MARKS = ['controller', 'lan', 'wlan', 'menu_chord', 'wifi_pass_set'] as const

/** What applying a settings file would change, and what in it is invalid. */
export function planRestore(cfg: Config, file: unknown): RestorePlan {
  const errors: RestorePlan['errors'] = []
  if (!file || typeof file !== 'object' || Array.isArray(file) || !MARKS.every((k) => k in file))
    return { patch: {}, rows: [], restart: false, errors: [{ key: '', err: 'fw.not_backup' }] }
  const obj = file as Record<string, unknown>
  const top = Object.keys(obj).filter(
    (k) =>
      WRITABLE.has(k as keyof ConfigPatch) &&
      (isSecret(k) || JSON.stringify(obj[k]) !== JSON.stringify(cfg[k as keyof Config])),
  )
  const patch = Object.fromEntries(top.map((k) => [k, obj[k]])) as ConfigPatch
  for (const k of ['lan', 'wlan'] as const) {
    const v = patch[k]
    if (v) for (const [f, e] of Object.entries(ipErrors({ ...cfg[k], ...v }))) errors.push({ key: `${k}.${f}`, err: e })
  }
  if (patch.name !== undefined && !NAME_RE.test(patch.name)) errors.push({ key: 'name', err: 'net.bad_name' })
  if (patch.brightness !== undefined && !(patch.brightness >= 0 && patch.brightness <= 100))
    errors.push({ key: 'brightness', err: 'fw.r_bri' })
  if (patch.volume !== undefined && !(patch.volume >= 0 && patch.volume <= 100))
    errors.push({ key: 'volume', err: 'fw.r_bri' })
  for (const k of SECRETS) if (k in patch && typeof patch[k] !== 'string') errors.push({ key: k, err: 'fw.r_text' })
  const isSet = { admin_pass: true, wifi_pass: cfg.wifi_pass_set, wifi2_pass: cfg.wifi2_pass_set, token: cfg.token_set }
  const fc = flat(cfg)
  const fo = flat(patch)
  const rows = Object.keys(fo)
    .filter((k) => isSecret(k) || JSON.stringify(fo[k]) !== JSON.stringify(fc[k]))
    .map((key) =>
      isSecret(key)
        ? { key, now: isSet[key as (typeof SECRETS)[number]], file: !!fo[key], secret: true }
        : { key, now: fc[key], file: fo[key] },
    )
  return {
    patch,
    rows,
    restart: top.some(restarts) || patch.matter === false,
    errors,
  }
}

/** GET /api/config holds no secrets, so it is the backup as it is. */
export const backupOf = (cfg: Config) => JSON.stringify(cfg, null, 1)
