import type { Lang } from '~/lib/i18n'
import type { Rich, VsRow } from '~/components/landing/copy'

/** yes / no / partly / neutral, then the cell text. */
export type Cell = [mark: 'y' | 'n' | 'p' | 'i', text: string]
/** win: the column that is better on this row, if one is. `top` rows also show on the landing page. */
export type Row = { k: string; a: Cell; b: Cell; win?: 'a' | 'b'; top?: true }

const en = {
  meta: {
    title: 'DeckConnector vs. Elgato Network Dock',
    description:
      'DeckConnector vs the Elgato Network Dock: Wi-Fi, price, Companion, Elgato app, several computers. Honest table, tested on the bench.',
  },
  crumbs: ['Home', 'Compare'],
  h1: 'DeckConnector vs Elgato Network Dock',
  lead: [
    'The ',
    ['dock', 'Elgato Network Dock'],
    ' needs a network cable. DeckConnector also runs on Wi-Fi.',
  ] as Rich,
  summary: [
    { k: 'Wi-Fi', icon: 'wifi', v: ['n', 'y'] },
    { k: 'Network cable', icon: 'lan', v: ['y', 'y'] },
    { k: 'Companion and the Elgato app', icon: 'apps', v: ['y', 'y'] },
    { k: 'Switch computers on the deck', icon: 'hosts', v: ['n', 'y'] },
    { k: 'Case and mount', icon: 'case', v: ['y', 'n'] },
    { k: 'Official support', icon: 'support', v: ['y', 'n'] },
  ] as VsRow[],
  all: 'Show all 20 points',
  cols: ['DeckConnector', 'Elgato Network Dock'],
  better: 'better',
  groups: [
    ['Network and power', 4],
    ['Software', 5],
    ['Day to day', 5],
    ['What you get', 6],
  ] as [string, number][],
  rows: [
    { k: 'Wi-Fi', a: ['y', 'Yes, 2.4 GHz'], b: ['n', 'No'], win: 'a', top: true },
    { k: 'Ethernet', a: ['y', 'Yes, 100 Mbit/s'], b: ['y', 'Yes'] , top: true },
    { k: 'Power', a: ['i', 'USB-C or PoE'], b: ['i', 'PoE (802.3af) or USB-C PD, supply not included'] },
    { k: 'Wi-Fi backup when the cable fails', a: ['y', 'Yes, automatic'], b: ['n', 'No'], win: 'a', top: true },
    { k: 'Elgato app', a: ['y', 'Yes, as a Network Dock'], b: ['y', 'Yes'], top: true },
    { k: 'Bitfocus Companion', a: ['y', 'Yes, Satellite or Network Dock'], b: ['y', 'Yes, Network Dock'], top: true },
    { k: 'Your own software', a: ['y', 'Yes, open protocol and API'], b: ['n', 'No'], win: 'a' },
    { k: 'Several computers at once', a: ['y', '4 dock hosts and 4 servers connected at once'], b: ['p', 'Not documented'], win: 'a', top: true },
    { k: 'Switch computer on the deck', a: ['y', 'Yes, two taps'], b: ['n', 'No'], win: 'a' },
    { k: 'Web UI with a live deck', a: ['y', 'Yes'], b: ['n', 'No'], win: 'a' },
    { k: 'Settings on the deck', a: ['y', 'Yes, full menu'], b: ['p', 'IP settings only'], win: 'a' },
    { k: 'Updates', a: ['y', 'Signed, roll back on failure'], b: ['y', 'Firmware updates exist'] },
    { k: 'Back after a power cut', a: ['y', 'Companion in about 7 s, Elgato app in 9 to 13 s (3 of 4 tries)'], b: ['p', 'Companion in about 18 s. The Elgato app did not reconnect until it restarted (2 of 2 tries)'], win: 'a' },
    { k: 'Speed', a: ['y', 'Same or faster on our bench'], b: ['y', 'Reference'] },
    { k: 'Price', a: ['i', 'Board about $25 to $35, firmware free'], b: ['i', '$79.99'], win: 'a', top: true },
    { k: 'Tested decks', a: ['p', 'Stream Deck + (more in the driver)'], b: ['y', 'Most current decks'], win: 'b', top: true },
    { k: 'Case and mount', a: ['n', 'No, a bare board'], b: ['y', 'Yes, bracket and XL mount'], win: 'b', top: true },
    { k: 'Setup', a: ['p', 'Flash once in the browser'], b: ['y', 'Plug in'], win: 'b' },
    { k: 'Official support', a: ['n', 'No, a community project'], b: ['y', 'Yes, 1 year warranty'], win: 'b', top: true },
    { k: 'Source code', a: ['y', 'Open, AGPL-3.0'], b: ['n', 'Closed'], win: 'a' },
  ] as Row[],
  sources: [
    'Dock facts from the ',
    ['dock', 'Elgato product page'],
    ' (November 2026). Speed and power-cut results from our bench with a Stream Deck +.',
  ] as Rich,
  dockWins: {
    title: 'Where the dock is better',
    items: [
      ['Plug and play', 'No flashing. Unbox, plug in, done.'],
      ['Case and mounts', 'A finished box with a bracket. DeckConnector is a bare board for now.'],
      ['More decks tested', 'Elgato supports most current decks. We have tested the Stream Deck + only.'],
      ['Official support', 'Elgato and Bitfocus support their dock. We are a community project, and an app update could break us.'],
    ] as [string, string][],
  },
  faq: {
    title: 'Questions',
    items: [
      [
        'Does the Elgato Network Dock have Wi-Fi?',
        'No. The Network Dock connects over an Ethernet cable only, powered by PoE or USB-C. DeckConnector works over Wi-Fi and Ethernet.',
      ],
      [
        'Does DeckConnector work with the Elgato app?',
        'Yes. The app finds the board as a Network Dock by itself. We test with the Stream Deck app 7.6 on Windows.',
      ],
      ['Do I need PoE?', 'No. Power the board over USB-C and use Wi-Fi. PoE works too if your switch has it.'],
      [
        'Which Stream Decks work?',
        'We test with the Stream Deck +. The MK.2, XL, Neo and Stream Deck V2 are in the driver but not tested yet.',
      ],
      [
        'Is DeckConnector official?',
        'No. It is a community project, not affiliated with Elgato, Corsair or Bitfocus. An update of the Elgato app or Companion could break it until we fix it.',
      ],
      [
        'Is it as fast as the Network Dock?',
        'On our bench it was the same speed or faster with Companion and the Elgato app. The numbers are on the docs speed page.',
      ],
    ] as [string, string][],
  },
  next: {
    title: 'Ready to try it?',
    flash: 'Flash your board in the browser',
    start: 'Read the getting started guide',
    hardware: 'Check the supported hardware',
    speed: 'See the speed measurements',
  },
}

const de: typeof en = {
  meta: {
    title: 'DeckConnector vs. Elgato Network Dock',
    description:
      'DeckConnector und Elgato Network Dock im Vergleich: WLAN, Preis, Companion, Elgato-App, mehrere Rechner. Ehrlich, auf dem Prüfstand getestet.',
  },
  crumbs: ['Startseite', 'Vergleich'],
  h1: 'DeckConnector und Elgato Network Dock im Vergleich',
  lead: [
    'Das ',
    ['dock', 'Elgato Network Dock'],
    ' braucht ein Netzwerkkabel. DeckConnector läuft auch im WLAN.',
  ],
  summary: [
    { k: 'WLAN', icon: 'wifi', v: ['n', 'y'] },
    { k: 'Netzwerkkabel', icon: 'lan', v: ['y', 'y'] },
    { k: 'Companion und Elgato-App', icon: 'apps', v: ['y', 'y'] },
    { k: 'Rechner am Deck wechseln', icon: 'hosts', v: ['n', 'y'] },
    { k: 'Gehäuse und Halterung', icon: 'case', v: ['y', 'n'] },
    { k: 'Offizieller Support', icon: 'support', v: ['y', 'n'] },
  ],
  all: 'Alle 20 Punkte zeigen',
  cols: ['DeckConnector', 'Elgato Network Dock'],
  better: 'besser',
  groups: [
    ['Netzwerk und Strom', 4],
    ['Software', 5],
    ['Im Alltag', 5],
    ['Was du bekommst', 6],
  ],
  rows: [
    { k: 'WLAN', a: ['y', 'Ja, 2,4 GHz'], b: ['n', 'Nein'], win: 'a', top: true },
    { k: 'LAN', a: ['y', 'Ja, 100 Mbit/s'], b: ['y', 'Ja'], top: true },
    { k: 'Strom', a: ['i', 'USB-C oder PoE'], b: ['i', 'PoE (802.3af) oder USB-C PD, Netzteil nicht dabei'] },
    { k: 'WLAN springt ein, wenn das Kabel ausfällt', a: ['y', 'Ja, automatisch'], b: ['n', 'Nein'], win: 'a', top: true },
    { k: 'Elgato Stream-Deck-App', a: ['y', 'Ja, als Network Dock'], b: ['y', 'Ja'], top: true },
    { k: 'Bitfocus Companion', a: ['y', 'Ja, Satellite oder Network Dock'], b: ['y', 'Ja, Network Dock'], top: true },
    { k: 'Eigene Software', a: ['y', 'Ja, offenes Protokoll und API'], b: ['n', 'Nein'], win: 'a' },
    { k: 'Mehrere Rechner gleichzeitig', a: ['y', '4 Dock-Hosts und 4 Server gleichzeitig verbunden'], b: ['p', 'Nicht dokumentiert'], win: 'a', top: true },
    { k: 'Rechner am Deck wechseln', a: ['y', 'Ja, zweimal tippen'], b: ['n', 'Nein'], win: 'a' },
    { k: 'Web-UI mit Live-Deck', a: ['y', 'Ja'], b: ['n', 'Nein'], win: 'a' },
    { k: 'Einstellungen am Deck', a: ['y', 'Ja, volles Menü'], b: ['p', 'Nur IP-Einstellungen'], win: 'a' },
    { k: 'Updates', a: ['y', 'Signiert, mit Rückfall bei Fehlern'], b: ['y', 'Firmware-Updates gibt es'] },
    { k: 'Wieder da nach Stromausfall', a: ['y', 'Companion nach rund 7 s, Elgato-App nach 9 bis 13 s (3 von 4 Versuchen)'], b: ['p', 'Companion nach rund 18 s. Die Elgato-App verband sich erst nach ihrem Neustart wieder (2 von 2 Versuchen)'], win: 'a' },
    { k: 'Tempo', a: ['y', 'Gleich schnell oder schneller auf unserem Prüfstand'], b: ['y', 'Referenz'] },
    { k: 'Preis', a: ['i', 'Platine etwa 25 bis 35 Euro, Firmware gratis'], b: ['i', '89,99 Euro'], win: 'a', top: true },
    { k: 'Getestete Decks', a: ['p', 'Stream Deck + (weitere im Treiber)'], b: ['y', 'Die meisten aktuellen Decks'], win: 'b', top: true },
    { k: 'Gehäuse und Halterung', a: ['n', 'Nein, nackte Platine'], b: ['y', 'Ja, Halterung und XL-Halter'], win: 'b', top: true },
    { k: 'Einrichtung', a: ['p', 'Einmal im Browser flashen'], b: ['y', 'Einstecken'], win: 'b' },
    { k: 'Offizieller Support', a: ['n', 'Nein, Community-Projekt'], b: ['y', 'Ja, 1 Jahr Garantie'], win: 'b', top: true },
    { k: 'Quellcode', a: ['y', 'Offen, AGPL-3.0'], b: ['n', 'Geschlossen'], win: 'a' },
  ],
  sources: [
    'Angaben zum Dock von der ',
    ['dock', 'Elgato-Produktseite'],
    ' (November 2026). Tempo und Stromausfall auf unserem Prüfstand mit einem Stream Deck + gemessen.',
  ],
  dockWins: {
    title: 'Wo das Dock besser ist',
    items: [
      ['Einstecken und fertig', 'Kein Flashen. Auspacken, anstecken, läuft.'],
      ['Gehäuse und Halterung', 'Eine fertige Box mit Halterung. DeckConnector ist vorerst eine nackte Platine.'],
      ['Mehr getestete Decks', 'Elgato unterstützt die meisten aktuellen Decks. Wir haben nur das Stream Deck + getestet.'],
      ['Offizieller Support', 'Elgato und Bitfocus unterstützen ihr Dock. Wir sind ein Community-Projekt, ein App-Update kann uns ausbremsen.'],
    ],
  },
  faq: {
    title: 'Häufige Fragen',
    items: [
      [
        'Hat das Elgato Network Dock WLAN?',
        'Nein. Das Network Dock hängt nur am Netzwerkkabel, mit Strom per PoE oder USB-C. DeckConnector läuft per WLAN und LAN.',
      ],
      [
        'Läuft DeckConnector mit der Elgato Stream-Deck-App?',
        'Ja. Die App findet die Platine von selbst als Network Dock. Wir testen mit der Stream-Deck-App 7.6 unter Windows.',
      ],
      ['Brauche ich PoE?', 'Nein. Die Platine bekommt Strom per USB-C und nutzt WLAN. PoE geht auch, wenn dein Switch es kann.'],
      [
        'Welche Stream Decks laufen?',
        'Wir testen mit dem Stream Deck +. MK.2, XL, Neo und Stream Deck V2 sind im Treiber, aber noch nicht getestet.',
      ],
      [
        'Ist DeckConnector offiziell?',
        'Nein. Es ist ein Community-Projekt, kein Produkt von Elgato, Corsair oder Bitfocus. Ein Update der Elgato-App oder von Companion kann es stören, bis wir es anpassen.',
      ],
      [
        'Ist es so schnell wie das Network Dock?',
        'Auf unserem Prüfstand war es mit Companion und der Elgato-App gleich schnell oder schneller. Die Zahlen stehen auf der Tempo-Seite der Anleitung.',
      ],
    ],
  },
  next: {
    title: 'Lust, es auszuprobieren?',
    flash: 'Platine im Browser flashen',
    start: 'Zur Anleitung für den Einstieg',
    hardware: 'Unterstützte Hardware ansehen',
    speed: 'Zu den Tempo-Messungen',
  },
}

export const compare: Record<Lang, typeof en> = { en, de }
