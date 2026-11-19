import { describe, expect, it } from 'vitest'
import { dateLocale, hourCycle, makeFmt } from './format'

const at = new Date(2026, 10, 19, 15, 4)

describe('the clock follows the time zone, words the page language', () => {
  it.each([
    ['Europe/Berlin', ['en-US'], 'h23'],
    ['America/New_York', ['en-US'], 'h12'],
    ['America/Chicago', ['de-DE'], 'h12'],
    ['Asia/Kolkata', ['en'], 'h12'],
    ['Asia/Calcutta', ['en'], 'h12'],
    ['Asia/Tokyo', ['en'], 'h23'],
    ['America/Sao_Paulo', ['en-US'], 'h23'],
    ['Australia/Sydney', ['de'], 'h12'],
  ] as const)('%s with %j: %s', (zone, langs, want) => {
    expect(hourCycle('en', zone, langs)).toBe(want)
  })
  it('without a place in the time zone, the first browser language with a region decides, then the page language', () => {
    expect(hourCycle('en', 'UTC', ['en', 'de-DE', 'en-US'])).toBe('h23')
    expect(hourCycle('de', 'Etc/UTC', ['en-US'])).toBe('h12')
    expect(hourCycle('en', 'UTC', ['en'])).toBe('h12')
    expect(hourCycle('de', 'UTC', ['de'])).toBe('h23')
  })
  it('a set clock wins, also with words in the other language', () => {
    expect(makeFmt('en', dateLocale('en', 'h23', 'en-US')).time(at)).toBe('15:04')
    expect(makeFmt('de', dateLocale('de', 'h12', 'de-DE')).time(at)).toMatch(/^03:04\sPM$/)
    expect(makeFmt('de', dateLocale('de', 'h12', 'de-DE')).dt(at, { weekday: 'long' })).toBe('Donnerstag')
  })
  it('a browser locale Intl cannot read keeps the clock', () => {
    expect(makeFmt('de', dateLocale('de', 'h12', '')).time(at)).toMatch(/^03:04\sPM$/)
  })
})
