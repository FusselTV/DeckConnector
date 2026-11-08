import { existsSync, readFileSync } from 'node:fs'
import type { Manifest } from './firmware'

// Read at build time: the static export has no server to ask later.
const file = 'public/firmware/manifest.json'
export const builtManifest: Manifest | null = existsSync(file) ? (JSON.parse(readFileSync(file, 'utf8')) as Manifest) : null
