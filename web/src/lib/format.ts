import type { Lang } from '../i18n'
import { H12 } from './clock12'

const HM = { hour: '2-digit', minute: '2-digit' } as const

export type Clock = 'auto' | 'h23' | 'h12'

/** 12 or 24 h by the country of the browser's time zone, else by the first browser language with a region. */
export function hourCycle(
  lang: Lang,
  zone = Intl.DateTimeFormat().resolvedOptions().timeZone,
  langs: readonly string[] = navigator.languages,
): 'h23' | 'h12' {
  // An English browser is common outside the US, so its language says little about the clock.
  if (zone.includes('/') && !zone.startsWith('Etc/')) return H12.includes(` ${zone} `) ? 'h12' : 'h23'
  try {
    const tag = langs.find((l) => new Intl.Locale(l).region) ?? lang
    return new Intl.DateTimeFormat(tag, { hour: 'numeric' }).resolvedOptions().hourCycle?.startsWith('h1')
      ? 'h12'
      : 'h23'
  } catch {
    return 'h23'
  }
}

/** Words in the page's language, date order from the browser's locale. */
export function dateLocale(lang: Lang, clock: Clock = 'auto', browser = navigator.language): string {
  const hc = clock === 'auto' ? hourCycle(lang) : clock
  try {
    return new Intl.Locale(lang, { region: new Intl.Locale(browser).maximize().region, hourCycle: hc }).toString()
  } catch {
    return new Intl.Locale(lang, { hourCycle: hc }).toString()
  }
}

export function makeFmt(lang: Lang, loc = dateLocale(lang)) {
  // Built on first use and kept: an Intl formatter costs far more to make than to call.
  const made = new Map<string, Intl.NumberFormat>()
  const dates = new Map<string, Intl.DateTimeFormat>()
  const dt = (d: number | Date, o: Intl.DateTimeFormatOptions) => {
    const k = JSON.stringify(o)
    let f = dates.get(k)
    if (!f) dates.set(k, (f = new Intl.DateTimeFormat(loc, o)))
    return f.format(d)
  }
  const nf = (k: string, o?: Intl.NumberFormatOptions) => {
    let f = made.get(k)
    if (!f) made.set(k, (f = new Intl.NumberFormat(lang, o)))
    return f
  }
  const dec = (n: number) => nf('dec', { maximumFractionDigits: 1 }).format(n)
  // short: "3 Std. 17 Min." fits the overview tile on a phone, "3 Stunden 17 Minuten" did not
  const unit = (v: number, u: string) => nf(u, { style: 'unit', unit: u, unitDisplay: 'short' }).format(v)
  return {
    num: (n: number) => nf('num').format(n),
    dec,
    pct: (n: number) => nf('pct', { style: 'percent', maximumFractionDigits: 0 }).format(n / 100),
    bytes: (n: number) => (n >= 1048576 ? dec(n / 1048576) + ' MB' : dec(n / 1024) + ' KB'),
    dt,
    time: (d: number | Date) => dt(d, HM),
    up: (s: number) => {
      const d = Math.floor(s / 86400)
      const h = Math.floor((s % 86400) / 3600)
      const m = Math.floor((s % 3600) / 60)
      return d
        ? `${unit(d, 'day')} ${unit(h, 'hour')}`
        : h
          ? `${unit(h, 'hour')} ${unit(m, 'minute')}`
          : unit(m, 'minute')
    },
  }
}
export type Fmt = ReturnType<typeof makeFmt>

export const mmss = (s: number) => `${Math.floor(s / 60)}:${String(s % 60).padStart(2, '0')}`
