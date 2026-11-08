export interface Part {
  path: string
  offset: number
  size: number
  sha256?: string
}

export interface Build {
  erase_all: boolean
  parts: Part[]
}

export interface Manifest {
  name: string
  chip_id: number
  chip_revision: { min: number; max: number }
  version: string
  built?: string
  chip: string
  esptool: { baud: number; baud_fallback: number }
  console: { baud: number; improv: boolean }
  builds: { factory: Build; update?: Build }
}

export type Mode = 'factory' | 'update'

/** A part with its bytes, ready for esptool. */
export interface Image extends Part {
  data: Uint8Array
}

/** Where the bytes come from: the site's /firmware/ folder, or files the developer picked. */
export interface Source {
  manifest: Manifest
  local: boolean
  read: (path: string) => Promise<Uint8Array>
}

export class FirmwareError extends Error {
  readonly path: string
  readonly kind: 'fetch' | 'sha' | 'size'
  constructor(kind: 'fetch' | 'sha' | 'size', path: string) {
    super(`${kind}: ${path}`)
    this.kind = kind
    this.path = path
  }
}

const hex = (b: ArrayBuffer) => Array.from(new Uint8Array(b), (x) => x.toString(16).padStart(2, '0')).join('')

export function siteSource(manifest: Manifest, base = '/firmware/'): Source {
  return {
    manifest,
    local: false,
    read: async (path) => {
      const r = await fetch(base + path, { cache: 'no-cache' }).catch(() => null)
      if (!r?.ok) throw new FirmwareError('fetch', path)
      return new Uint8Array(await r.arrayBuffer())
    },
  }
}

/** Every part of a build, each checked against its size and SHA-256 before anything is written. */
export async function loadBuild(src: Source, mode: Mode): Promise<Image[]> {
  const build = src.manifest.builds[mode]
  if (!build) return []
  return Promise.all(
    build.parts.map(async (p) => {
      const data = await src.read(p.path)
      if (p.size && data.length !== p.size) throw new FirmwareError('size', p.path)
      if (p.sha256 && hex(await crypto.subtle.digest('SHA-256', data as Uint8Array<ArrayBuffer>)) !== p.sha256.toLowerCase())
        throw new FirmwareError('sha', p.path)
      return { ...p, data }
    }),
  )
}

export const totalSize = (b: Build | undefined) => b?.parts.reduce((a, p) => a + p.size, 0) ?? 0

const P4: Omit<Manifest, 'builds' | 'version'> = {
  name: 'DeckConnector',
  chip_id: 18,
  chip_revision: { min: 301, max: 399 },
  chip: 'esp32p4',
  esptool: { baud: 921600, baud_fallback: 115200 },
  console: { baud: 115200, improv: true },
}

/** Files a developer picked: a manifest with its .bin files, or one .bin written at 0x0 as a full install. */
export async function localSource(files: File[]): Promise<Source> {
  const byName = new Map(files.map((f) => [f.name, f]))
  const read = async (path: string) => {
    const f = byName.get(path.split('/').pop() ?? path)
    if (!f) throw new FirmwareError('fetch', path)
    return new Uint8Array(await f.arrayBuffer())
  }
  const json = byName.get('manifest.json')
  if (json) return { manifest: JSON.parse(await json.text()) as Manifest, local: true, read }
  const bins = files.filter((f) => f.name.endsWith('.bin'))
  const bin = bins[0]
  if (bins.length !== 1 || !bin) throw new FirmwareError('fetch', 'manifest.json')
  const part = { path: bin.name, offset: 0, size: bin.size }
  return { manifest: { ...P4, version: bin.name, builds: { factory: { erase_all: true, parts: [part] } } }, local: true, read }
}
