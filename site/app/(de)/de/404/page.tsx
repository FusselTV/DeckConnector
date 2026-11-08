import type { Metadata } from 'next'
import { NotFound, notFoundTitle } from '~/components/not-found'

// Moved to out/de/404.html after the build: Cloudflare Pages answers a missing /de/... with the closest 404.html.
export const metadata: Metadata = { title: { absolute: notFoundTitle('de') }, robots: { index: false } }

export default function Page() {
  return <NotFound lang="de" />
}
