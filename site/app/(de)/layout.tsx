import type { Metadata } from 'next'
import type { ReactNode } from 'react'
import { Root } from '~/components/root'

export const metadata: Metadata = {
  title: { default: 'DeckConnector, dein Stream Deck im Netzwerk', template: '%s | DeckConnector' },
  description: 'DeckConnector ist eine ESP32-P4-Platine mit PoE, die ein Elgato Stream Deck ins Netzwerk bringt, für Bitfocus Companion und die Elgato-App.',
  icons: '/favicon.svg',
}

export default function Layout({ children }: { children: ReactNode }) {
  return <Root lang="de">{children}</Root>
}
