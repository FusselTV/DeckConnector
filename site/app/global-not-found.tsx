import type { Metadata } from 'next'
import { NotFound, notFoundTitle } from '~/components/not-found'
import { Root } from '~/components/root'

export const metadata: Metadata = { title: notFoundTitle('en'), icons: '/favicon.svg', robots: { index: false } }

export default function Page() {
  return (
    <Root lang="en">
      <NotFound lang="en" />
    </Root>
  )
}
