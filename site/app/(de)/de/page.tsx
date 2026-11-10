import { Landing } from '~/components/landing/Landing'
import { LandingLd, landingMeta } from '~/components/landing/seo'

export default function Page() {
  return (
    <>
      <LandingLd lang="de" />
      <Landing lang="de" />
    </>
  )
}

export const metadata = landingMeta('de')
