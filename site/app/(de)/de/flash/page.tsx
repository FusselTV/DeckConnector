import { builtManifest } from '~/components/flash/built'
import { Flasher } from '~/components/flash/Flasher'
import { pageMeta } from '~/lib/meta'

export const metadata = pageMeta('de', '/flash/', {
  title: 'Firmware im Browser installieren',
  description:
    'Deck-Connector-Firmware in Chrome oder Edge auf eine ESP32-P4-Platine spielen, dann per USB ins WLAN bringen.',
})

export default function Page() {
  return <Flasher lang="de" manifest={builtManifest} />
}
