import { Privacy, legalMeta } from '~/components/legal'

export const metadata = legalMeta('de', 'privacy')

export default function Page() {
  return <Privacy lang="de" />
}
