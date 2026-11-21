import type { Lang } from '~/lib/i18n'
import type { Out } from './links'

/** Text with outbound links: a plain string, or [link, text]. */
export type Rich = (string | [Out, string])[]
export type FaqItem = [q: string, a: string, link?: [text: string, href: string]]
export const plain = (r: Rich) => r.map((x) => (typeof x === 'string' ? x : x[1])).join('')

/** A comparison row: label, then [Network Dock, DeckConnector], each yes or no. */
type Mark = 'y' | 'n'
export type VsRow = { k: string; icon: 'wifi' | 'hosts' | 'case' | 'lan' | 'apps' | 'support'; v: [Mark, Mark] }

const en = {
  meta: {
    title: 'Stream Deck on Wi-Fi and Ethernet, DeckConnector',
    description:
      'Free firmware puts your Elgato Stream Deck on Wi-Fi or Ethernet. For Bitfocus Companion and the Elgato app, Home Assistant optional. An Elgato Network Dock alternative.',
  },
  title: ['Your Stream Deck,', 'on Wi-Fi.'],
  lead: 'Free firmware for a board from about $25. Works with Companion and the Elgato app.',
  flash: 'Set it up',
  demo: 'Live demo',
  heroAlt:
    'A Stream Deck + on a DeckConnector board that is on Wi-Fi with only a power plug. Companion sends the key pictures, a key press goes back, Companion answers.',
  net: {
    title: 'Wi-Fi or Ethernet. Or both.',
    text: 'With a cable plugged in too, Wi-Fi takes over when it fails.',
    alt: 'The network cable behind the board fails. The deck keeps drawing over Wi-Fi and moves back when the cable returns.',
  },
  hosts: {
    title: 'Switch computers with one tap',
    text: 'Companion, the Elgato app and your own server connect at once.',
    alt: 'The deck switches between Companion, the Elgato app and an own server with one tap.',
  },
  vs: {
    title: 'Instead of a Network Dock',
    alt: 'The Elgato Network Dock needs a network cable. DeckConnector works over Wi-Fi with a power plug and picks the computer on the deck.',
    cols: ['Network Dock', 'DeckConnector'],
    priceLabel: 'Price',
    price: ['$79.99', '$25 to $35'],
    priceNote: ['', 'firmware free'],
    rows: [
      { k: 'Wi-Fi', icon: 'wifi', v: ['n', 'y'] },
      { k: 'Switch computers on the deck', icon: 'hosts', v: ['n', 'y'] },
      { k: 'Case and mount', icon: 'case', v: ['y', 'n'] },
    ] as VsRow[],
    yes: 'yes',
    no: 'no',
    link: 'Full comparison',
  },
  flashing: {
    title: 'Flash it in your browser',
    text: 'Chrome or Edge, about a minute, nothing to install.',
    alt: 'The flasher connects to the board over USB, writes the firmware, then joins your Wi-Fi.',
    board: 'Get the board',
  },
  works: {
    items: [
      ['companion', 'Bitfocus Companion'],
      ['app', 'Elgato app'],
      ['ha', 'Home Assistant (optional)'],
    ] as [Out, string][],
    own: 'Your own server',
    ownLink: 'own-server/',
  },
  faq: {
    title: 'Questions',
    items: [
      [
        'What is DeckConnector?',
        'Free, open source firmware for the Waveshare ESP32-P4-WIFI6-POE-ETH board. The board drives an Elgato Stream Deck over USB and puts it on Wi-Fi or Ethernet, like an Elgato Network Dock with Wi-Fi.',
      ],
      ['Do I need PoE?', 'No. A USB-C power supply and Wi-Fi are enough. PoE is an option.'],
      [
        'Which decks work?',
        'Most Elgato Stream Decks, and with Companion also Stream Docks from Mirabox and Ajazz. Only the Stream Deck + is tested on real hardware. The others pass tests on emulated decks and should work.',
        ['All models', '/docs/hardware/#decks'],
      ],
      ['Which browser does the flasher need?', 'Chrome or Edge on a desktop. Firefox and Safari have no Web Serial.'],
      ['Does it work on 5 GHz Wi-Fi?', 'No. The board has 2.4 GHz Wi-Fi only.'],
    ] as FaqItem[],
    more: 'More in the docs',
  },
  cta: {
    title: 'Put your deck on Wi-Fi',
    docs: 'Docs',
  },
}

const de: typeof en = {
  meta: {
    title: 'Stream Deck im WLAN und LAN, DeckConnector',
    description:
      'Freie Firmware bringt dein Elgato Stream Deck ins WLAN oder LAN. Für Bitfocus Companion und die Elgato-App, Home Assistant optional. Alternative zum Elgato Network Dock.',
  },
  title: ['Dein Stream Deck', 'im WLAN.'],
  lead: 'Freie Firmware für eine Platine ab 25 Euro. Läuft mit Companion und der Elgato-App.',
  flash: 'Jetzt einrichten',
  demo: 'Live-Demo',
  heroAlt:
    'Ein Stream Deck + an einer DeckConnector-Platine, die nur mit einem Netzteil im WLAN ist. Companion schickt die Tastenbilder, ein Tastendruck geht zurück, Companion antwortet.',
  net: {
    title: 'WLAN oder LAN. Oder beides.',
    text: 'Steckt zusätzlich ein Kabel, springt WLAN ein, wenn es ausfällt.',
    alt: 'Das Netzwerkkabel hinter der Platine fällt aus. Das Deck zeichnet über WLAN weiter und wechselt zurück, sobald das Kabel wieder da ist.',
  },
  hosts: {
    title: 'Rechner wechseln mit einem Tipp',
    text: 'Companion, Elgato-App und eigener Server sind gleichzeitig verbunden.',
    alt: 'Das Deck wechselt mit einem Tipp zwischen Companion, der Elgato-App und einem eigenen Server.',
  },
  vs: {
    title: 'Statt eines Network Docks',
    alt: 'Das Elgato Network Dock braucht ein Netzwerkkabel. DeckConnector läuft mit einem Netzteil im WLAN und wählt den Rechner direkt am Deck.',
    cols: ['Network Dock', 'DeckConnector'],
    priceLabel: 'Preis',
    price: ['89,99 Euro', '25 bis 35 Euro'],
    priceNote: ['', 'Firmware frei'],
    rows: [
      { k: 'WLAN', icon: 'wifi', v: ['n', 'y'] },
      { k: 'Rechner am Deck wechseln', icon: 'hosts', v: ['n', 'y'] },
      { k: 'Gehäuse und Halterung', icon: 'case', v: ['y', 'n'] },
    ],
    yes: 'ja',
    no: 'nein',
    link: 'Zum ganzen Vergleich',
  },
  flashing: {
    title: 'Flashen direkt im Browser',
    text: 'Chrome oder Edge, rund eine Minute, nichts zu installieren.',
    alt: 'Der Flasher verbindet sich per USB mit der Platine, spielt die Firmware auf und verbindet sie dann mit deinem WLAN.',
    board: 'Platine kaufen',
  },
  works: {
    items: [
      ['companion', 'Bitfocus Companion'],
      ['app', 'Elgato-App'],
      ['ha', 'Home Assistant (optional)'],
    ],
    own: 'Eigener Server',
    ownLink: 'own-server/',
  },
  faq: {
    title: 'Häufige Fragen',
    items: [
      [
        'Was ist DeckConnector?',
        'Eine freie Open-Source-Firmware für die Platine Waveshare ESP32-P4-WIFI6-POE-ETH. Die Platine steuert ein Elgato Stream Deck per USB und bringt es ins WLAN oder LAN, wie ein Elgato Network Dock mit WLAN.',
      ],
      ['Brauche ich PoE?', 'Nein. Ein USB-C-Netzteil und WLAN reichen. PoE geht auch.'],
      [
        'Welche Decks laufen?',
        'Die meisten Elgato Stream Decks und mit Companion auch Stream Docks von Mirabox und Ajazz. Nur das Stream Deck + ist an echter Hardware getestet. Die anderen bestehen Tests mit emulierten Decks und sollten laufen.',
        ['Alle Modelle', '/de/docs/hardware/#decks'],
      ],
      [
        'Welchen Browser braucht der Flasher?',
        'Chrome oder Edge am Desktop. Firefox und Safari können kein Web Serial.',
      ],
      ['Geht 5-GHz-WLAN?', 'Nein. Die Platine kann nur 2,4-GHz-WLAN.'],
    ],
    more: 'Mehr in der Anleitung',
  },
  cta: {
    title: 'Bring dein Deck ins WLAN',
    docs: 'Anleitung',
  },
}

export const copy: Record<Lang, typeof en> = { en, de }
