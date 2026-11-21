import type { Config, ConfigPatch } from '../api/types'

const DAY_S = 86_400

// Only the release number counts: a dev build or rc of the same release is no update, a rollback neither.
const rel = (v: string) => (v.match(/\d+/g) ?? []).slice(0, 3).map(Number)
const newer = (a: string, b: string) => {
  const [x, y] = [rel(a), rel(b)]
  const i = x.findIndex((n, k) => n !== y[k])
  return i >= 0 && (x[i] ?? -1) > (y[i] ?? -1)
}

export const starShow = (c: Config, fw: string, uptimeS: number | undefined, nowS: number) =>
  c.setup_done &&
  c.star_off === false &&
  ((!!c.star_fw && newer(fw, c.star_fw)) || ((uptimeS ?? 0) >= DAY_S && nowS >= (c.star_until ?? 0)))

export const starClose = (fw: string, nowS: number): ConfigPatch => ({ star_fw: fw, star_until: nowS + 30 * DAY_S })
export const starNever = (fw: string): ConfigPatch => ({ star_fw: fw, star_off: true })
