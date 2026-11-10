import { Imprint, legalMeta } from '~/components/legal'

export const metadata = legalMeta('en', 'imprint')

export default function Page() {
  return <Imprint lang="en" />
}
