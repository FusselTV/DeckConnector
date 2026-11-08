import type { Metadata } from 'next'
import { Root } from '~/components/root'

export const metadata: Metadata = { title: 'Page not found | DeckConnector', icons: '/favicon.svg' }

export default function NotFound() {
  return (
    <Root lang="en">
      <main className="mx-auto w-full max-w-3xl px-4 py-24 text-center">
        <h1 className="text-3xl font-semibold tracking-tight">Page not found</h1>
        <p className="mt-4 text-fd-muted-foreground" lang="de">
          Seite nicht gefunden
        </p>
        <p className="mt-8 flex justify-center gap-6 font-medium text-fd-primary underline underline-offset-4">
          <a href="/">Home</a>
          <a href="/de/" lang="de">
            Startseite
          </a>
        </p>
      </main>
    </Root>
  )
}
