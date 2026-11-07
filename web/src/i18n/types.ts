import type { en } from './en'

export type Key = keyof typeof en
export type Dict = Record<Key, string>
type PluralBase<K> = K extends `${infer B}_other` ? B : never
/** A key, or the base of a `_one`/`_other` pair, which picks its form from `n`. */
export type TKey = Key | PluralBase<Key>
export type Vars = Record<string, string | number | undefined>
export type T = (key: TKey, vars?: Vars) => string
export type Lang = 'de' | 'en'
