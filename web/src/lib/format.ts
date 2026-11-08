import type { Lang } from '../i18n'

export function makeFmt(lang: Lang) {
  // Built on first use and kept: an Intl formatter costs far more to make than to call.
  const made = new Map<string, Intl.NumberFormat>()
  const nf = (k: string, o?: Intl.NumberFormatOptions) => {
    let f = made.get(k)
    if (!f) made.set(k, (f = new Intl.NumberFormat(lang, o)))
    return f
  }
  const dec = (n: number) => nf('dec', { maximumFractionDigits: 1 }).format(n)
  const unit = (v: number, u: string) => nf(u, { style: 'unit', unit: u, unitDisplay: 'long' }).format(v)
  return {
    num: (n: number) => nf('num').format(n),
    dec,
    pct: (n: number) => nf('pct', { style: 'percent', maximumFractionDigits: 0 }).format(n / 100),
    bytes: (n: number) => (n >= 1048576 ? dec(n / 1048576) + ' MB' : dec(n / 1024) + ' KB'),
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
