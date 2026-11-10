import { describe, expect, it } from 'vitest'
import { deviceLabel, REPO, sourceUrl } from './util'

describe('deviceLabel', () => {
  it('names the device and browser the deck shows', () => {
    const iphone =
      'Mozilla/5.0 (iPhone; CPU iPhone OS 17_5 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.5 Mobile/15E148 Safari/604.1'
    expect(deviceLabel(iphone)).toBe('iPhone · Safari')
    expect(
      deviceLabel('Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 Chrome/130.0 Safari/537.36'),
    ).toBe('Mac · Chrome')
    expect(deviceLabel('Mozilla/5.0 (Windows NT 10.0) AppleWebKit/537.36 Chrome/130.0 Safari/537.36 Edg/130.0')).toBe(
      'Windows · Edge',
    )
    expect(deviceLabel('curl/8')).toBe('Browser')
  })
})

describe('sourceUrl', () => {
  it('points at the tag or commit the device runs', () => {
    expect(sourceUrl('v1.2.0')).toBe(`${REPO}/tree/v1.2.0`)
    expect(sourceUrl('v1.2.0-5-gabc1234')).toBe(`${REPO}/tree/abc1234`)
    expect(sourceUrl('abc1234')).toBe(`${REPO}/tree/abc1234`)
    expect(sourceUrl('v1.2.0-5-gabc1234-dirty')).toBe(REPO)
    expect(sourceUrl(undefined)).toBe(REPO)
  })
})
