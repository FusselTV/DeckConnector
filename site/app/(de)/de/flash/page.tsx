import { builtManifest } from '~/components/flash/built'
import { Flasher } from '~/components/flash/Flasher'
import { pageMeta } from '~/lib/meta'

export const metadata = pageMeta('de', '/flash/', {
  title: 'Flasher',
  description: 'DeckConnector-Firmware in Chrome oder Edge auf eine ESP32-P4-Platine spielen, dann WLAN, Passwort und Namen über USB einstellen.',
})

export default function Page() {
  return <Flasher lang="de" manifest={builtManifest} />
}
