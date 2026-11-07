import { describe, expect, it } from 'vitest'
import { de } from './de'
import { en } from './en'
import { makeT } from './index'

const vars = (s: string) => [...s.matchAll(/\{(\w+)\}/g)].map((m) => m[1]).sort()

describe('dictionaries', () => {
  it('de and en have the same keys', () => {
    expect(Object.keys(de).sort()).toEqual(Object.keys(en).sort())
  })

  it('every entry has text and the same variables in both languages', () => {
    for (const k of Object.keys(en) as (keyof typeof en)[]) {
      expect(de[k], k).not.toBe('')
      expect(vars(de[k]), k).toEqual(vars(en[k]))
    }
  })

  it('plural pairs are complete', () => {
    for (const k of Object.keys(en)) {
      if (k.endsWith('_one')) expect(Object.keys(en), k).toContain(k.replace(/_one$/, '_other'))
    }
  })

  it('German uses „…“ quotes, not straight ones', () => {
    for (const [k, s] of Object.entries(de)) expect(s.includes('"'), k).toBe(false)
  })
})

describe('t', () => {
  it('fills variables and picks the plural form', () => {
    const t = makeT('en')
    expect(t('hero.n', { n: 1 })).toBe('1 thing to check')
    expect(t('hero.n', { n: 3 })).toBe('3 things to check')
    expect(makeT('de')('deck.n', { n: 2 })).toBe('Deck 2')
  })

  it('falls back to the key for an unknown key', () => {
    expect(makeT('de')('nope.key' as 'c.save')).toBe('nope.key')
  })
})
