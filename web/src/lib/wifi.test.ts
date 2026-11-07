import { describe, expect, it } from 'vitest'
import { groupScan } from './wifi'

describe('groupScan', () => {
  it('groups access points by name, strongest first, the current network on top', () => {
    const ap = (ssid: string, bssid: string, rssi: number) => ({ ssid, bssid, rssi, channel: 1, open: false })
    const g = groupScan([ap('A', '1', -70), ap('B', '2', -40), ap('A', '3', -50), ap('', '4', -30)], 'A')
    expect(g.map((x) => x.ssid)).toEqual(['A', 'B'])
    expect(g[0]?.rssi).toBe(-50)
    expect(g[0]?.aps.map((x) => x.bssid)).toEqual(['3', '1'])
  })
})
