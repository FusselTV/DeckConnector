import type { Lang } from '~/lib/i18n'

export const waveshare = 'https://www.waveshare.com/esp32-p4-wifi6-poe-eth.htm'

export function out(lang: Lang) {
  const elgato = lang === 'de' ? 'https://www.elgato.com/de/de' : 'https://www.elgato.com/us/en'
  return {
    companion: 'https://bitfocus.io/companion',
    deck: `${elgato}/p/stream-deck-plus`,
    app: `${elgato}/s/stream-deck-app`,
    dock: `${elgato}/p/network-dock-stream-deck`,
    board: waveshare,
    p4: 'https://www.espressif.com/en/products/socs/esp32-p4',
    satellite: 'https://github.com/bitfocus/companion-satellite',
  }
}
export type Out = keyof ReturnType<typeof out>

export const docs = {
  network: 'network/',
  hosts: 'hosts/',
  companion: 'companion/',
  app: 'elgato-app/',
  hardware: 'hardware/',
  start: 'getting-started/',
  license: 'license/',
  // Requested from the docs owner: the bench numbers move to this page.
  speed: 'speed/',
} as const
