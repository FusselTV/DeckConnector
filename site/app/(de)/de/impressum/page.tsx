import { Imprint, legalMeta } from '~/components/legal'

export const metadata = legalMeta('de', 'imprint')

export default function Page() {
  return <Imprint lang="de" />
}
