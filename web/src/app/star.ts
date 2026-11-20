const DAY = 86_400_000

/** What this browser keeps under dc.star. fw: the firmware it saw last; upd: an update not yet answered. */
export interface StarMemo {
  fw?: string
  upd?: boolean
  until?: number
  never?: boolean
}

/** A rollback or an older slot is no update. */
export function starSeen(m: StarMemo, fw: string | undefined): StarMemo {
  if (!fw || fw === m.fw) return m
  return { ...m, fw, upd: !!m.fw && fw.localeCompare(m.fw, 'en', { numeric: true }) > 0 }
}

export const starShow = (m: StarMemo, setupDone: boolean, uptimeS: number | undefined, now: number) =>
  !m.never && setupDone && (!!m.upd || ((uptimeS ?? 0) >= 86_400 && now >= (m.until ?? 0)))

export const starClose = (m: StarMemo, now: number): StarMemo => ({ ...m, upd: false, until: now + 30 * DAY })
export const starNever = (m: StarMemo): StarMemo => ({ ...m, upd: false, never: true })
