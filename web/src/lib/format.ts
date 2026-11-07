import type { Lang } from '../i18n'

export function makeFmt(lang: Lang) {
  const num = new Intl.NumberFormat(lang)
  const dec = new Intl.NumberFormat(lang, { maximumFractionDigits: 1 })
  const pct = new Intl.NumberFormat(lang, { style: 'percent', maximumFractionDigits: 0 })
  const unit = (v: number, u: string) =>
    new Intl.NumberFormat(lang, { style: 'unit', unit: u, unitDisplay: 'long' }).format(v)
  return {
    num: (n: number) => num.format(n),
    dec: (n: number) => dec.format(n),
    pct: (n: number) => pct.format(n / 100),
    bytes: (n: number) => (n >= 1048576 ? dec.format(n / 1048576) + ' MB' : dec.format(n / 1024) + ' KB'),
    time: (d: number | Date) => new Date(d).toLocaleTimeString(lang, { hour: '2-digit', minute: '2-digit' }),
    clock: (d: number | Date) =>
      new Date(d).toLocaleTimeString(lang, { hour: '2-digit', minute: '2-digit', second: '2-digit' }),
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
