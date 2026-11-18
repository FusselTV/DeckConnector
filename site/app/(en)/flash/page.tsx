import { builtManifest } from '~/components/flash/built'
import { Flasher } from '~/components/flash/Flasher'
import { pageMeta } from '~/lib/meta'

export const metadata = pageMeta('en', '/flash/', {
  title: 'Install the firmware in your browser',
  description:
    'Install the DeckConnector firmware on an ESP32-P4 board from Chrome or Edge, then set Wi-Fi, password and name over USB.',
})

export default function Page() {
  return <Flasher lang="en" manifest={builtManifest} />
}
