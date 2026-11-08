import { pageMeta } from '~/lib/meta'
import { Landing } from '~/components/landing/Landing'

export default function Page() {
  return <Landing lang="de" />
}

export const metadata = pageMeta('de', '/')
