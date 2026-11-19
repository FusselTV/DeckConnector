import { describe, expect, it } from 'vitest'
import { dateLocale, makeFmt } from './format'

const at = new Date(2026, 10, 19, 15, 4)

describe('dates follow the browser locale, words the page language', () => {
  it('a German browser gets 24 h, also with English words', () => {
    expect(makeFmt('en', dateLocale('en', 'de-DE')).time(at)).toBe('15:04')
    expect(makeFmt('de', dateLocale('de', 'de-DE')).time(at)).toBe('15:04')
    expect(makeFmt('en', dateLocale('en', 'de-DE')).dt(at, { weekday: 'long' })).toBe('Thursday')
  })
  it('a US browser gets 12 h, also with German words', () => {
    expect(makeFmt('en', dateLocale('en', 'en-US')).time(at)).toMatch(/^03:04\sPM$/)
    expect(makeFmt('de', dateLocale('de', 'en-US')).time(at)).toMatch(/^03:04\sPM$/)
    expect(makeFmt('de', dateLocale('de', 'en-US')).dt(at, { weekday: 'long' })).toBe('Donnerstag')
  })
  it('a browser locale Intl cannot read falls back to the page language', () => {
    expect(dateLocale('de', '')).toBe('de')
  })
})
