import { describe, expect, it } from 'vitest'
import { boostOf, rank, remember, type Cmd } from './rank'

const c = (id: string, label: string, group: Cmd['group'] = 'pal.set', kw = ''): Cmd => ({
  id,
  label,
  group,
  kw,
  run: () => undefined,
})
const cmds = [
  c('wlan', 'WLAN', 'pal.set', 'wifi wireless ssid'),
  c('bri', 'Helligkeit', 'pal.act', 'brightness'),
  c('net', 'Netzwerk', 'pal.go'),
  c('reboot', 'Neu starten', 'pal.act', 'restart reboot'),
  c('fw', 'Firmware', 'pal.go', 'update'),
  c('ntp', 'Feste IP-Adresse', 'pal.set', 'static'),
]
const ids = (r: Cmd[]) => r.map((x) => x.id)

describe('rank', () => {
  it('puts a label prefix first', () => {
    expect(ids(rank(cmds, 'net'))[0]).toBe('net')
  })
  it('finds words inside a label', () => {
    expect(ids(rank(cmds, 'ip'))[0]).toBe('ntp')
  })
  it('finds hidden keywords', () => {
    expect(ids(rank(cmds, 'wifi'))).toEqual(['wlan'])
  })
  it('does not find firmware for wifi', () => {
    expect(ids(rank(cmds, 'wifi'))).not.toContain('fw')
  })
  it('tolerates a typo', () => {
    expect(ids(rank(cmds, 'helligkiet'))).toContain('bri')
  })
  it('ignores accents and case', () => {
    expect(ids(rank([c('a', 'Übersicht')], 'uber'))).toEqual(['a'])
  })
  it('ranks a recent pick for the same query higher', () => {
    expect(ids(rank(cmds, 'ne'))[0]).toBe('reboot')
    expect(ids(rank(cmds, 'ne', remember([], 'ne', 'net')))[0]).toBe('net')
  })
  it('lets old picks fade', () => {
    const now = Date.now()
    expect(boostOf(remember([], 'ne', 'net', now - 14 * 86_400_000), 'ne', now).get('net')).toBeCloseTo(0.5)
    expect(boostOf(remember([], 'ne', 'net', now - 120 * 86_400_000), 'ne', now).get('net')).toBeLessThan(0.01)
  })
  it('shows only recent picks for an empty query', () => {
    expect(rank(cmds, '')).toEqual([])
    expect(ids(rank(cmds, '', remember([], 'x', 'fw')))).toEqual(['fw'])
  })
})
