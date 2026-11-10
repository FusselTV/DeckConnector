import { Compare } from '~/components/compare/Compare'
import { CompareLd, compareMeta } from '~/components/landing/seo'

export default function Page() {
  return (
    <>
      <CompareLd lang="de" />
      <Compare lang="de" />
    </>
  )
}

export const metadata = compareMeta('de')
