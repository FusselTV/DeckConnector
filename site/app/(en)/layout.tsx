import type { Metadata } from 'next'
import type { ReactNode } from 'react'
import { Root } from '~/components/root'
import { site } from '~/site.config'

export const metadata: Metadata = {
  metadataBase: new URL(site),
  title: { default: 'Deck Connector, your Stream Deck on the network', template: '%s | Deck Connector' },
  description:
    'Deck Connector is free firmware for an ESP32-P4 board that puts an Elgato Stream Deck on your network over Wi-Fi or Ethernet, for Bitfocus Companion and the Elgato app.',
  icons: '/favicon.svg',
}

export default function Layout({ children }: { children: ReactNode }) {
  return <Root lang="en">{children}</Root>
}
