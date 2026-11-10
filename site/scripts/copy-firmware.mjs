import { cpSync, existsSync, mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs'

const from = new URL('../../firmware/build/web-flash/', import.meta.url)
const to = new URL('../public/firmware/', import.meta.url)

// CI puts the release files into public/firmware/ itself; a local build only replaces them when it has its own.
if (existsSync(new URL('manifest.json', from))) {
  rmSync(to, { recursive: true, force: true })
  cpSync(from, to, { recursive: true })
} else if (!existsSync(new URL('manifest.json', to)))
  console.warn('copy-firmware: no firmware/build/web-flash, /flash offers only local files. Run uv run python -m dbtools.flashimage in tools/ first.')

// The release's own list describes its binaries; the repo's is the fallback without one.
const web = new URL('../../web/dist/licenses.md', import.meta.url)
mkdirSync(to, { recursive: true })
if (!existsSync(new URL('THIRD_PARTY_LICENSES.txt', to)))
  writeFileSync(
    new URL('THIRD_PARTY_LICENSES.txt', to),
    readFileSync(new URL('../../THIRD_PARTY_LICENSES.txt', import.meta.url), 'utf8') +
      (existsSync(web) ? `\n\nWeb page packages\n-----------------\n\n${readFileSync(web, 'utf8')}` : ''),
  )
