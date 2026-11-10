import { cpSync, existsSync, rmSync } from 'node:fs'

const from = new URL('../../firmware/build/web-flash/', import.meta.url)
const to = new URL('../public/firmware/', import.meta.url)

// CI puts the release files into public/firmware/ itself; a local build only replaces them when it has its own.
if (existsSync(new URL('manifest.json', from))) {
  rmSync(to, { recursive: true, force: true })
  cpSync(from, to, { recursive: true })
} else if (!existsSync(new URL('manifest.json', to)))
  console.warn('copy-firmware: no firmware/build/web-flash, /flash offers only local files. Run uv run python -m dbtools.flashimage in tools/ first.')

// The release's own list describes its binaries; the repo's is the fallback without one.
if (!existsSync(new URL('THIRD_PARTY_LICENSES.txt', to)))
  cpSync(new URL('../../THIRD_PARTY_LICENSES.txt', import.meta.url), new URL('THIRD_PARTY_LICENSES.txt', to))
