import type { Metadata } from 'next'
import type { ReactNode } from 'react'
import { Root } from '~/components/root'
import { site } from '~/site.config'

export const metadata: Metadata = {
  metadataBase: new URL(site),
  title: { default: 'DeckConnector, your Stream Deck on the network', template: '%s | DeckConnector' },
  description: 'DeckConnector is an ESP32-P4 PoE board that puts an Elgato Stream Deck on the network for Bitfocus Companion and the Elgato app.',
  icons: '/favicon.svg',
}

export default function Layout({ children }: { children: ReactNode }) {
  return <Root lang="en">{children}</Root>
}
