import { Landing } from '~/components/landing/Landing'
import { LandingLd, landingMeta } from '~/components/landing/seo'

export default function Page() {
  return (
    <>
      <LandingLd lang="en" />
      <Landing lang="en" />
    </>
  )
}

export const metadata = landingMeta('en')
