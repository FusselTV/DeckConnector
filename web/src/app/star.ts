const DAY = 86_400_000

/** What this browser keeps under dc.star. fw: the firmware it saw last; upd: an update not yet answered. */
export interface StarMemo {
  fw?: string
  upd?: boolean
  until?: number
  never?: boolean
}

/** The memo after a status read: a firmware other than the one seen last is an update. */
export function starSeen(m: StarMemo, fw: string | undefined): StarMemo {
  if (!fw || fw === m.fw) return m
  return { ...m, fw, upd: !!m.fw }
}

/** Shown once after an update, else from a day of uptime on, but never during setup or in a pause. */
export const starShow = (m: StarMemo, setupDone: boolean, uptimeS: number | undefined, now: number) =>
  !m.never && setupDone && (!!m.upd || ((uptimeS ?? 0) >= 86_400 && now >= (m.until ?? 0)))

export const starClose = (m: StarMemo, now: number): StarMemo => ({ ...m, upd: false, until: now + 30 * DAY })
export const starNever = (m: StarMemo): StarMemo => ({ ...m, upd: false, never: true })
