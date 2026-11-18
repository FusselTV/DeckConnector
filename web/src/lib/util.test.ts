import { describe, expect, it } from 'vitest'
import release from '../../../.github/workflows/release.yml?raw'
import { otherImage } from './util'

const notes = 'https://github.com/FusselTV/DeckConnector/releases/tag/v1.2.0'

describe('otherImage', () => {
  it('names the assets the release workflow uploads', () => {
    expect(release).toContain('out/deckconnector-matter-{tag}.dbfw')
    expect(release).toContain('out/deckconnector-{tag}.dbfw')
    const base = 'https://github.com/FusselTV/DeckConnector/releases/download/v1.2.0/'
    expect(otherImage({ current: 'v1.2.0', latest: 'v1.2.0', notes }, false)?.url).toBe(
      `${base}deckconnector-matter-v1.2.0.dbfw`,
    )
    expect(otherImage({ current: 'v1.2.0', latest: 'v1.2.0', notes }, true)?.url).toBe(
      `${base}deckconnector-v1.2.0.dbfw`,
    )
  })

  it('offers nothing older than the running firmware', () => {
    expect(otherImage({ current: 'v1.3.0', latest: 'v1.2.0', notes }, false)).toBeNull()
    expect(otherImage({ current: 'v1.1.0', latest: 'v1.2.0', notes, url: 'x' }, false)?.v).toBe('v1.2.0')
    expect(otherImage({ current: 'v1.2.0', latest: null }, false)).toBeNull()
  })
})
