import type { Lang } from '~/lib/i18n'

export type Feature = { title: string; text: string }

export const waveshare = 'https://www.waveshare.com/esp32-p4-wifi6-poe-eth.htm'

const en = {
  eyebrow: 'Open firmware · ESP32-P4 · PoE',
  title: ['Your Stream Deck,', 'on the network.'],
  lead: 'A small PoE board drives the deck over USB. Companion, the Elgato App or your own server reach it over the network.',
  flash: 'Flash the board',
  demo: 'Live demo',
  docs: 'Read the docs',
  note: 'Free under the AGPL-3.0. Flashes from Chrome or Edge in a few minutes.',
  heroAlt:
    'A Stream Deck + wired to a DeckConnector board and a PoE switch. Companion sends key pictures over the network, a key press goes back, and Companion answers.',
  stats: [
    ['0.5', 'ms', 'per key picture'],
    ['1.7', 'ms', 'full screen, 9 KB'],
    ['≤ 6', 's', 'LAN to WLAN failover'],
    ['8', '', 'hosts at once'],
  ],
  measured: 'Measured on a Stream Deck + over LAN.',
  why: {
    label: 'Why',
    title: ['Keep the deck.', 'Lose the PC next to it.'],
    art: ['Power', 'Data', 'Companion', 'Elgato', 'Server', 'draws'],
    items: [
      {
        title: 'No PC at the deck',
        text: 'The board speaks USB to the deck and the network to everything else. Companion can run on any machine, also in Docker.',
      },
      {
        title: 'One PoE cable',
        text: 'Power and data on one network cable. The board powers the deck from its own USB port.',
      },
      {
        title: 'Works with what you have',
        text: 'No plugin and no extra software. Hosts find the board on the network like an Elgato Network Dock.',
      },
    ],
  },
  proof: {
    label: 'Open',
    title: 'Nothing to take on trust',
    items: [
      ['AGPL-3.0', 'Firmware, web UI and tools. Read the code, build it yourself.'],
      ['Tested hardware', 'Runs daily on a Stream Deck + with Companion and the Elgato App.'],
      ['Bench numbers', 'The timings on this page come from the bench tools in the repository.'],
    ],
    source: 'Source on GitHub',
  },
  actions: {
    flash: ['Flash', 'Install the firmware from the browser and set Wi-Fi and password.'],
    demo: ['Demo', 'Click through the real web UI with a virtual deck.'],
    docs: ['Docs', 'Setup for Companion, the Elgato App, Home Assistant and own servers.'],
  },
  how: {
    label: 'How it works',
    title: 'From the box to your first button in three steps',
    steps: [
      {
        n: '01',
        title: 'Flash',
        text: 'Connect the board by USB and open the flasher in Chrome or Edge. It writes the firmware and sets your Wi-Fi and admin password.',
        alt: 'The flasher connects to the board over USB, writes the firmware, then sets Wi-Fi, password and name.',
      },
      {
        n: '02',
        title: 'Plug in',
        text: 'Plug the Stream Deck into the board and the board into a PoE port. The deck starts and shows its address.',
        alt: 'Stream Deck + connected by USB to the board, the board connected to a PoE switch with one network cable.',
      },
      {
        n: '03',
        title: 'Pick a host',
        text: 'Every host that connects shows up on the deck. One tap picks the one that draws, and its picture is there at once.',
        alt: 'The deck switches between Companion, the Elgato App and an own server with one tap.',
      },
    ],
  },
  wiring: { deck: 'Stream Deck', usb: 'USB', board: 'DeckConnector', poe: 'PoE', switch: 'Switch' },
  features: {
    label: 'Features',
    title: 'Built like a network device',
    speed: {
      title: 'As fast as a cable to the computer',
      text: 'A key image takes 0.5 ms from the server to the end of the USB transfer. A full screen image of 9 KB takes 1.7 ms. The newest image for each area wins, so a slow link never builds a queue.',
      queue: ['Music', 'dropped', 'shown'],
    },
    failover: {
      title: 'LAN and WLAN at the same time',
      text: 'Connections run over LAN. If a host cannot be reached there, the next try goes over WLAN, and it moves back as soon as the LAN answers again. A dead LAN shows within 6 s.',
      points: ['LAN first, always', 'WLAN takes over within 6 s', 'Back to LAN on its own'],
      alt: 'The LAN uplink behind the PoE switch drops while PoE keeps the board powered. The deck keeps drawing over WLAN and moves back when the LAN returns.',
    },
    list: [
      {
        title: 'Looks like an Elgato Network Dock',
        text: 'Companion and the Elgato App find the board over mDNS, like an Elgato Network Dock.',
      },
      {
        title: 'Moves Companion to Satellite',
        text: 'When Companion’s Satellite API answers, the board switches it over by itself. Also from Docker.',
      },
      {
        title: 'Signed updates that roll back',
        text: 'Every file is signed. Firmware that does not come up goes back to the old one.',
      },
      {
        title: 'Gets a hung deck back',
        text: 'If the deck stops taking pictures for 3 s, the board reconnects it on its own.',
      },
      {
        title: 'A live deck in your browser',
        text: 'Click, hold, swipe and turn it like the real one. In English and German.',
      },
      {
        title: 'JSON API and live events',
        text: 'One web socket carries every press and picture. Home Assistant reads it over the REST API.',
      },
    ] satisfies Feature[],
  },
  works: {
    label: 'Works with',
    items: ['Bitfocus Companion', 'Elgato App', 'Home Assistant', 'Your own server'],
  },
  hw: {
    label: 'Hardware',
    title: 'What you need',
    parts: [
      [
        'Waveshare ESP32-P4-WIFI6-POE-ETH',
        'ESP32-P4 with chip revision v3.1 or newer, an ESP32-C6 for WLAN, PoE and a USB-A port. It powers the deck.',
      ],
      ['An Elgato Stream Deck', 'Plugged in with its own USB cable. Nothing on the deck changes.'],
      ['A PoE switch or injector', 'One network cable carries power and data. Without PoE, power the board over USB-C.'],
      ['A USB-C cable', 'For the first flash, and for power without PoE.'],
    ],
    buy: 'Buy the board at Waveshare',
    ports: ['PoE · LAN', 'USB-A · deck', 'USB-C · flash'] as [string, string, string],
    decks: 'Supported decks',
    list: ['Stream Deck +', 'Stream Deck MK.2', 'Stream Deck XL', 'Stream Deck Neo', 'Stream Deck (V2)'],
    tested: 'tested',
    untested: 'In the driver, expected to work, not tested yet',
  },
  faq: {
    label: 'FAQ',
    title: 'Questions',
    items: [
      [
        'Do I still need a computer next to the deck?',
        'No. Companion or the Elgato App can run on any computer in the network, Companion also in Docker. The deck only needs the board and a network cable.',
      ],
      [
        'Which browser does the flasher need?',
        'A desktop browser with Web Serial, such as Chrome or Edge. Firefox and Safari do not have it.',
      ],
      [
        'Can I break the board by flashing?',
        'No. The chip always starts its USB loader, which cannot be overwritten, so you can flash it again at any time.',
      ],
      [
        'Does it need Wi-Fi?',
        'No. LAN is enough. WLAN is the fallback when the LAN fails, and the way in for the first setup. The Wi-Fi chip has no 5 GHz radio.',
      ],
      [
        'Can several computers use the deck?',
        'Up to four servers and four dock hosts connect at once. One of them draws and gets the presses. A tap on the deck switches to another.',
      ],
      [
        'Do I need the web UI?',
        'No. The deck has its own status screen and settings menu. Without a network, it opens a setup WLAN and shows a QR code for it.',
      ],
      ['Is it open source?', 'Yes. Firmware, web UI and tools are free software under the AGPL-3.0.'],
    ],
    more: 'More in the docs',
  },
  cta: {
    title: 'Put your deck on the network',
    text: 'Three ways in. Pick one.',
  },
}

const de: typeof en = {
  eyebrow: 'Freie Firmware · ESP32-P4 · PoE',
  title: ['Dein Stream Deck,', 'im Netzwerk.'],
  lead: 'Eine kleine PoE-Platine steuert das Deck über USB. Companion, die Elgato-App oder dein eigener Server erreichen es übers Netzwerk.',
  flash: 'Platine flashen',
  demo: 'Live-Demo',
  docs: 'Zur Anleitung',
  note: 'Frei unter der AGPL-3.0. In wenigen Minuten aus Chrome oder Edge geflasht.',
  heroAlt:
    'Ein Stream Deck + an einer DeckConnector-Platine und einem PoE-Switch. Companion schickt Tastenbilder übers Netzwerk, ein Tastendruck geht zurück, Companion antwortet.',
  stats: [
    ['0,5', 'ms', 'pro Tastenbild'],
    ['1,7', 'ms', 'Vollbild, 9 KB'],
    ['≤ 6', 's', 'LAN-zu-WLAN-Wechsel'],
    ['8', '', 'Hosts gleichzeitig'],
  ],
  measured: 'Gemessen an einem Stream Deck + im LAN.',
  why: {
    label: 'Warum',
    title: ['Das Deck bleibt.', 'Der PC daneben nicht.'],
    art: ['Strom', 'Daten', 'Companion', 'Elgato', 'Server', 'zeichnet'],
    items: [
      {
        title: 'Kein PC am Deck',
        text: 'Die Platine spricht USB mit dem Deck und Netzwerk mit dem Rest. Companion läuft auf irgendeinem Rechner, auch in Docker.',
      },
      {
        title: 'Ein PoE-Kabel',
        text: 'Strom und Daten über ein Netzwerkkabel. Die Platine versorgt das Deck über ihren eigenen USB-Port.',
      },
      {
        title: 'Passt zu dem, was du hast',
        text: 'Kein Plugin, keine Zusatzsoftware. Hosts finden die Platine im Netzwerk wie ein Elgato Network Dock.',
      },
    ],
  },
  proof: {
    label: 'Offen',
    title: 'Alles nachprüfbar',
    items: [
      ['AGPL-3.0', 'Firmware, Web-UI und Werkzeuge. Code lesen, selbst bauen.'],
      ['Getestete Hardware', 'Läuft täglich an einem Stream Deck + mit Companion und der Elgato-App.'],
      ['Messwerte vom Prüfstand', 'Die Zeiten auf dieser Seite stammen aus den Messwerkzeugen im Repository.'],
    ],
    source: 'Quellcode auf GitHub',
  },
  actions: {
    flash: ['Flashen', 'Firmware aus dem Browser aufspielen, WLAN und Passwort setzen.'],
    demo: ['Demo', 'Durch das echte Web-UI klicken, mit einem virtuellen Deck.'],
    docs: ['Anleitung', 'Einrichtung für Companion, die Elgato-App, Home Assistant und eigene Server.'],
  },
  how: {
    label: 'So geht’s',
    title: 'In drei Schritten zur ersten Taste',
    steps: [
      {
        n: '01',
        title: 'Flashen',
        text: 'Platine per USB anschließen und den Flasher in Chrome oder Edge öffnen. Er spielt die Firmware auf und setzt WLAN und Admin-Passwort.',
        alt: 'Der Flasher verbindet sich per USB mit der Platine, spielt die Firmware auf und setzt dann WLAN, Passwort und Namen.',
      },
      {
        n: '02',
        title: 'Anstecken',
        text: 'Das Stream Deck an die Platine, die Platine an einen PoE-Port. Das Deck startet und zeigt seine Adresse.',
        alt: 'Stream Deck + per USB an der Platine, die Platine mit einem Netzwerkkabel am PoE-Switch.',
      },
      {
        n: '03',
        title: 'Host wählen',
        text: 'Jeder verbundene Host erscheint auf dem Deck. Ein Tipp legt fest, wer zeichnet, und sein Bild ist sofort da.',
        alt: 'Das Deck wechselt mit einem Tipp zwischen Companion, der Elgato-App und einem eigenen Server.',
      },
    ],
  },
  wiring: { deck: 'Stream Deck', usb: 'USB', board: 'DeckConnector', poe: 'PoE', switch: 'Switch' },
  features: {
    label: 'Funktionen',
    title: 'Gebaut wie ein Netzwerkgerät',
    speed: {
      title: 'So schnell wie das Kabel zum Rechner',
      text: 'Ein Tastenbild braucht 0,5 ms vom Server bis zum Ende der USB-Übertragung, ein Vollbild mit 9 KB 1,7 ms. Pro Bereich zählt nur das neueste Bild, so staut sich auch bei langsamer Verbindung nichts.',
      queue: ['Musik', 'verworfen', 'angezeigt'],
    },
    failover: {
      title: 'LAN und WLAN gleichzeitig',
      text: 'Verbindungen laufen über LAN. Ist ein Host dort nicht erreichbar, geht der nächste Versuch über WLAN, und sobald das LAN wieder antwortet, zurück. Ein totes LAN fällt nach spätestens 6 s auf.',
      points: ['Immer zuerst LAN', 'WLAN übernimmt nach höchstens 6 s', 'Von selbst zurück ins LAN'],
      alt: 'Der LAN-Uplink hinter dem PoE-Switch fällt aus, PoE versorgt die Platine weiter. Das Deck zeichnet über WLAN weiter und wechselt zurück, sobald das LAN wieder da ist.',
    },
    list: [
      {
        title: 'Gibt sich als Elgato Network Dock aus',
        text: 'Companion und die Elgato-App finden die Platine per mDNS wie ein Elgato Network Dock.',
      },
      {
        title: 'Holt Companion auf Satellite',
        text: 'Antwortet die Satellite-API von Companion, stellt die Platine selbst um. Auch aus Docker heraus.',
      },
      {
        title: 'Signierte Updates mit Rückfall',
        text: 'Jede Datei ist signiert. Startet eine neue Firmware nicht sauber, kommt die alte zurück.',
      },
      {
        title: 'Holt ein hängendes Deck zurück',
        text: 'Nimmt das Deck 3 s lang keine Bilder an, verbindet die Platine es von selbst neu.',
      },
      {
        title: 'Das Deck live im Browser',
        text: 'Klicken, halten, wischen und drehen wie am echten Gerät. Auf Deutsch und Englisch.',
      },
      {
        title: 'JSON-API und Live-Events',
        text: 'Ein Websocket meldet jeden Tastendruck und jedes Bild. Home Assistant liest die Platine über die REST-API aus.',
      },
    ],
  },
  works: {
    label: 'Arbeitet mit',
    items: ['Bitfocus Companion', 'Elgato-App', 'Home Assistant', 'Eigener Server'],
  },
  hw: {
    label: 'Hardware',
    title: 'Was du brauchst',
    parts: [
      [
        'Waveshare ESP32-P4-WIFI6-POE-ETH',
        'ESP32-P4 ab Chip-Revision v3.1, mit ESP32-C6 für WLAN, PoE und einem USB-A-Port, der das Deck mit Strom versorgt.',
      ],
      ['Ein Elgato Stream Deck', 'Angeschlossen mit seinem eigenen USB-Kabel. Am Deck selbst ändert sich nichts.'],
      ['PoE-Switch oder PoE-Injektor', 'Ein Netzwerkkabel für Strom und Daten. Ohne PoE bekommt die Platine Strom über USB-C.'],
      ['Ein USB-C-Kabel', 'Fürs erste Flashen und als Stromversorgung ohne PoE.'],
    ],
    buy: 'Platine bei Waveshare kaufen',
    ports: ['PoE · LAN', 'USB-A · Deck', 'USB-C · Flash'],
    decks: 'Unterstützte Decks',
    list: ['Stream Deck +', 'Stream Deck MK.2', 'Stream Deck XL', 'Stream Deck Neo', 'Stream Deck (V2)'],
    tested: 'getestet',
    untested: 'Im Treiber, sollten laufen, noch nicht getestet',
  },
  faq: {
    label: 'FAQ',
    title: 'Häufige Fragen',
    items: [
      [
        'Brauche ich noch einen Rechner neben dem Deck?',
        'Nein. Companion oder die Elgato-App laufen auf irgendeinem Rechner im Netzwerk, Companion auch in Docker. Das Deck braucht nur die Platine und ein Netzwerkkabel.',
      ],
      [
        'Welchen Browser braucht der Flasher?',
        'Einen Desktop-Browser mit Web Serial, also etwa Chrome oder Edge. Firefox und Safari können das nicht.',
      ],
      [
        'Kann ich die Platine beim Flashen kaputt machen?',
        'Nein. Der Chip startet immer seinen USB-Loader, und den kann nichts überschreiben. Neu flashen geht also jederzeit.',
      ],
      [
        'Braucht es WLAN?',
        'Nein, LAN reicht. WLAN springt ein, wenn das LAN ausfällt, und dient bei der Einrichtung als Zugang. Der WLAN-Chip kann kein 5 GHz.',
      ],
      [
        'Können mehrere Rechner das Deck nutzen?',
        'Bis zu vier Server und vier Dock-Hosts sind gleichzeitig verbunden. Einer zeichnet und bekommt die Tastendrücke, ein Tipp aufs Deck wechselt.',
      ],
      [
        'Brauche ich das Web-UI?',
        'Nein. Das Deck hat eine eigene Statusanzeige und ein Einstellungsmenü. Ohne Netzwerk öffnet es ein Einrichtungs-WLAN und zeigt dafür einen QR-Code.',
      ],
      ['Ist das Open Source?', 'Ja. Firmware, Web-UI und Werkzeuge sind freie Software unter der AGPL-3.0.'],
    ],
    more: 'Mehr in der Anleitung',
  },
  cta: {
    title: 'Bring dein Deck ins Netzwerk',
    text: 'Drei Wege hinein. Such dir einen aus.',
  },
}

export const copy: Record<Lang, typeof en> = { en, de }
