import { Compare } from '~/components/compare/Compare'
import { CompareLd, compareMeta } from '~/components/landing/seo'

export default function Page() {
  return (
    <>
      <CompareLd lang="en" />
      <Compare lang="en" />
    </>
  )
}

export const metadata = compareMeta('en')
