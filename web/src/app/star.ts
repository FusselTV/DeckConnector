import type { Config, ConfigPatch } from '../api/types'

const DAY_S = 86_400

/** A rollback or an older slot is no update. */
const newer = (a: string, b: string) => a.localeCompare(b, 'en', { numeric: true }) > 0

export const starShow = (c: Config, fw: string | undefined, uptimeS: number | undefined, nowS: number) =>
  c.setup_done &&
  c.star_off === false &&
  !!fw &&
  ((!!c.star_fw && newer(fw, c.star_fw)) || ((uptimeS ?? 0) >= DAY_S && nowS >= (c.star_until ?? 0)))

export const starClose = (fw: string, nowS: number): ConfigPatch => ({ star_fw: fw, star_until: nowS + 30 * DAY_S })
export const starNever = (fw: string): ConfigPatch => ({ star_fw: fw, star_off: true })
