import { de } from './de'
import { en } from './en'
import type { Dict, Key, Lang, T, TKey, Vars } from './types'

export type { Lang, T, TKey } from './types'

// A new language is one more dictionary here. Missing keys fall back to English, then to the key itself.
export const DICTS: Record<Lang, Partial<Dict>> = { de, en }

export function makeT(lang: Lang): T {
  const plural = new Intl.PluralRules(lang)
  const dict = DICTS[lang]
  const look = (k: string) => dict[k as Key] ?? (en as Partial<Dict>)[k as Key]
  return (key: TKey, vars: Vars = {}) => {
    const n = vars.n
    const s =
      (typeof n === 'number' ? (look(`${key}_${plural.select(n)}`) ?? look(`${key}_other`)) : undefined) ??
      look(key) ??
      key
    return s.replace(/\{(\w+)\}/g, (_, x: string) => String(vars[x] ?? ''))
  }
}

export const guessLang = (): Lang => (/^de/i.test(navigator.language) ? 'de' : 'en')
