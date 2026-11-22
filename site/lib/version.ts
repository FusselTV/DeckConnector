import { execSync } from 'node:child_process'
import { builtManifest } from '../components/flash/built'

// The release the flasher ships; a local build (no manifest, or one of a dev firmware) takes the newest release tag.
const tag = (): string => {
  try {
    const cmd = "git describe --tags --abbrev=0 --match 'v*' --exclude '*-*'"
    return execSync(cmd, { encoding: 'utf8', stdio: ['ignore', 'pipe', 'ignore'] }).trim()
  } catch {
    return 'vX.Y.Z'
  }
}
const built = builtManifest?.version
export const version = built && /^v\d+\.\d+\.\d+$/.test(built) ? built : tag()

/** Docs examples write `{{version}}`; the pages, the search index and llms-full.txt show the current release. */
export const withVersion = (text: string) => text.replaceAll('{{version}}', version)

type Node = { type: string; value?: string; children?: Node[] }

const put = (node: Node): void => {
  if ((node.type === 'code' || node.type === 'inlineCode') && node.value) node.value = withVersion(node.value)
  node.children?.forEach(put)
}

export const remarkVersion = () => put
