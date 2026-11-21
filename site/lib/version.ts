import { execSync } from 'node:child_process'
import { builtManifest } from '../components/flash/built'

// The release the flasher ships; a local build without it takes the newest release tag.
const tag = (): string => {
  try {
    return execSync("git describe --tags --abbrev=0 --match 'v*' --exclude '*-*'", { encoding: 'utf8' }).trim()
  } catch {
    return 'vX.Y.Z'
  }
}
export const version = builtManifest?.version ?? tag()

/** Docs examples write `vX.Y.Z`; the pages, the search index and llms-full.txt show the current release. */
export const withVersion = (text: string) => text.replaceAll('vX.Y.Z', version)

type Node = { type: string; value?: string; children?: Node[] }

const put = (node: Node): void => {
  if ((node.type === 'code' || node.type === 'inlineCode') && node.value) node.value = withVersion(node.value)
  node.children?.forEach(put)
}

export const remarkVersion = () => put
