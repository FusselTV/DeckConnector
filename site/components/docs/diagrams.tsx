import type { ReactNode } from 'react'
import Image from 'next/image'
import {
  AppWindow,
  ArrowDown,
  Check,
  Cpu,
  Globe,
  House,
  KeyRound,
  Lock,
  MonitorSmartphone,
  Network,
  RotateCcw,
  Router,
  Server,
  SlidersHorizontal,
  Usb,
  Wifi,
} from 'lucide-react'
import { Figure, Flow, type Lang } from './figure'

const box = 'rounded-xl border border-line bg-panel px-3 py-2.5 shadow-panel'
const mono = 'font-mono text-xs uppercase tracking-wider text-fg-3'
const pick = <T,>(lang: Lang | undefined, en: T, de: T) => (lang === 'de' ? de : en)

function Node({
  icon,
  title,
  sub,
  tone,
  slim,
}: {
  icon: ReactNode
  title: ReactNode
  sub?: ReactNode
  tone?: 'amber'
  slim?: boolean
}) {
  return (
    <div className={`${box} flex items-center gap-3 ${tone === 'amber' ? 'border-amber/60' : ''}`}>
      <span
        className={`grid size-9 shrink-0 place-items-center rounded-lg [&_svg]:size-[18px] ${slim ? '@lg:@max-2xl:hidden' : ''} ${tone === 'amber' ? 'bg-amber-soft text-amber-fg' : 'bg-panel-2 text-fg-2'}`}
      >
        {icon}
      </span>
      <span className="min-w-0">
        <span className="block text-sm font-semibold leading-tight">{title}</span>
        {sub && <span className="block text-xs leading-snug text-fg-3">{sub}</span>}
      </span>
    </div>
  )
}

export function Topology({ lang }: { lang?: Lang }) {
  const t = pick(
    lang,
    {
      title: 'How Deck Connector connects',
      desc: 'The Stream Deck plugs into the connector over USB. The connector joins your network over Wi-Fi, Ethernet or both. Companion, the Elgato app, your own server and a browser reach the deck through the network. Home Assistant and Matter apps get the key presses but draw nothing.',
      deck: 'Your deck, unchanged',
      board: 'Deck Connector',
      boardSub: 'ESP32-P4, USB-C or PoE power',
      net: 'Wi-Fi and Ethernet',
      netSub: 'both at once',
      hosts: 'On any computer in the network',
      listen: 'Only get the presses',
      ha: 'Home Assistant',
      haSub: 'triggers and entities',
      matter: 'Matter',
      matterSub: 'Apple Home, Google Home',
      app: 'Elgato app',
      appSub: 'as a Network Dock',
      comp: 'Companion',
      compSub: 'Satellite or Network Dock',
      srv: 'Your server',
      srvSub: 'WebSocket protocol',
      web: 'Browser',
      webSub: 'web UI and API',
    },
    {
      title: 'So verbindet sich Deck Connector',
      desc: 'Das Stream Deck steckt per USB am Connector. Der Connector ist per WLAN, LAN oder beidem im Netzwerk. Companion, die Elgato-App, ein eigener Server und ein Browser erreichen das Deck über das Netzwerk. Home Assistant und Matter-Apps bekommen die Tastendrücke, zeichnen aber nichts.',
      deck: 'Dein Deck, unverändert',
      board: 'Deck Connector',
      boardSub: 'ESP32-P4, Strom per USB-C oder PoE',
      net: 'WLAN und LAN',
      netSub: 'beides zugleich',
      hosts: 'Auf jedem Rechner im Netz',
      listen: 'Bekommen nur die Tastendrücke',
      ha: 'Home Assistant',
      haSub: 'Auslöser und Entitäten',
      matter: 'Matter',
      matterSub: 'Apple Home, Google Home',
      app: 'Elgato-App',
      appSub: 'als Network Dock',
      comp: 'Companion',
      compSub: 'Satellite oder Network Dock',
      srv: 'Eigener Server',
      srvSub: 'WebSocket-Protokoll',
      web: 'Browser',
      webSub: 'Weboberfläche und API',
    },
  )
  return (
    <Figure title={t.title} desc={t.desc}>
      <div className="flex flex-col items-stretch gap-2 @lg:grid @lg:grid-cols-[1fr_auto_1fr_auto_1fr] @lg:items-center">
        <Node slim icon={<SlidersHorizontal />} title="Stream Deck +" sub={t.deck} />
        <Flow label="USB" />
        <Node slim icon={<Cpu />} title={t.board} sub={t.boardSub} tone="amber" />
        <Flow />
        <Node slim icon={<Wifi />} title={t.net} sub={t.netSub} />
        <ArrowDown aria-hidden className="mx-auto mb-1 size-4 text-fg-3 @lg:col-start-5" />
      </div>
      <p className={`${mono} mb-1.5`}>{t.hosts}</p>
      <div className="grid grid-cols-2 gap-2 @2xl:grid-cols-4">
        <Node icon={<AppWindow />} title={t.comp} sub={t.compSub} />
        <Node icon={<AppWindow />} title={t.app} sub={t.appSub} />
        <Node icon={<Server />} title={t.srv} sub={t.srvSub} />
        <Node icon={<Globe />} title={t.web} sub={t.webSub} />
      </div>
      <p className={`${mono} mb-1.5 mt-4`}>{t.listen}</p>
      <div className="grid grid-cols-2 gap-2">
        <Node icon={<House />} title={t.ha} sub={t.haSub} />
        <Node icon={<MonitorSmartphone />} title={t.matter} sub={t.matterSub} />
      </div>
    </Figure>
  )
}

export function SetupFlow({ lang }: { lang?: Lang }) {
  const p = lang === 'de' ? '/de' : ''
  const t = pick(
    lang,
    {
      title: 'From a new board to a working deck',
      desc: 'Four steps. Flash the board over USB-C in the browser. Plug in the deck and the network. Run the setup wizard. Connect Companion, the Elgato app or your own server.',
      steps: [
        ['Flash', 'USB-C, Chrome or Edge, about a minute', '/flash/'],
        ['Plug in', 'Deck in USB-A, Wi-Fi or network cable', `${p}/docs/first-setup/`],
        ['Set up', 'Name, password, network, source', `${p}/docs/first-setup/`],
        ['Connect', 'Companion, Elgato app or your server', `${p}/docs/companion/`],
      ],
    },
    {
      title: 'Von der neuen Platine zum fertigen Deck',
      desc: 'Vier Schritte. Die Platine per USB-C im Browser flashen. Deck und Netzwerk einstecken. Den Einrichtungsassistenten durchgehen. Companion, die Elgato-App oder einen eigenen Server verbinden.',
      steps: [
        ['Flashen', 'USB-C, Chrome oder Edge, etwa eine Minute', '/de/flash/'],
        ['Einstecken', 'Deck in USB-A, WLAN oder Netzwerkkabel', `${p}/docs/first-setup/`],
        ['Einrichten', 'Name, Passwort, Netzwerk, Quelle', `${p}/docs/first-setup/`],
        ['Verbinden', 'Companion, Elgato-App oder eigener Server', `${p}/docs/companion/`],
      ],
    },
  )
  return (
    <Figure title={t.title} desc={t.desc}>
      <ol className="grid grid-cols-2 gap-2 @lg:grid-cols-4">
        {t.steps.map(([name, sub, href], i) => (
          <li key={name}>
            <a href={href} className={`${box} group flex h-full flex-col gap-1 transition-colors hover:border-amber`}>
              <span className="font-mono text-xs text-amber-fg">{String(i + 1).padStart(2, '0')}</span>
              <span className="font-semibold group-hover:text-amber-fg">{name}</span>
              <span className="text-xs leading-snug text-fg-3">{sub}</span>
            </a>
          </li>
        ))}
      </ol>
    </Figure>
  )
}

export function SetupPaths({ lang }: { lang?: Lang }) {
  const t = pick(
    lang,
    {
      title: 'Three ways into the setup',
      desc: 'With a network cable, scan the QR code on the deck. Without a network, join the setup Wi-Fi the connector opens. Or skip the web UI and enter the address shown on the deck in Companion or the Elgato app. All three end with a source that draws on the deck.',
      ways: [
        ['Cable and QR code', 'The deck shows its address and a QR code. Scan it with a phone.', <Network key="n" />],
        ['Setup Wi-Fi', 'No network yet? The connector opens its own Wi-Fi.', <Wifi key="w" />],
        ['Deck only', 'Enter the address from the deck in Companion or the Elgato app.', <SlidersHorizontal key="s" />],
      ],
      end: 'Then a source draws on the deck',
    },
    {
      title: 'Drei Wege in die Einrichtung',
      desc: 'Mit Netzwerkkabel den QR-Code auf dem Deck scannen. Ohne Netzwerk ins Setup-WLAN des Connectors gehen. Oder ohne Weboberfläche die Adresse vom Deck in Companion oder der Elgato-App eintragen. Alle drei enden mit einer Quelle, die aufs Deck zeichnet.',
      ways: [
        ['Kabel und QR-Code', 'Das Deck zeigt Adresse und QR-Code. Mit dem Handy scannen.', <Network key="n" />],
        ['Setup-WLAN', 'Noch kein Netz? Dann öffnet der Connector ein eigenes WLAN.', <Wifi key="w" />],
        [
          'Nur am Deck',
          'Die Adresse vom Deck in Companion oder der Elgato-App eintragen.',
          <SlidersHorizontal key="s" />,
        ],
      ],
      end: 'Dann zeichnet eine Quelle aufs Deck',
    },
  )
  return (
    <Figure title={t.title} desc={t.desc}>
      <div className="grid gap-2 @lg:grid-cols-3">
        {t.ways.map(([name, sub, icon]) => (
          <Node key={name as string} icon={icon} title={name} sub={sub} />
        ))}
      </div>
      <div aria-hidden className="@max-lg:hidden">
        <div className="grid h-3 grid-cols-3 gap-2">
          {[0, 1, 2].map((i) => (
            <span key={i} className="mx-auto w-px bg-line-2" />
          ))}
        </div>
        <div className="mx-[calc((100%-1rem)/6)] h-px bg-line-2" />
        <div className="mx-auto h-2 w-px bg-line-2" />
      </div>
      <ArrowDown aria-hidden className="mx-auto my-1 size-4 text-fg-3 @lg:-mt-1 @lg:mb-0.5" />
      <div className={`${box} border-amber/60 text-center text-sm font-semibold`}>{t.end}</div>
    </Figure>
  )
}

export function Failover({ lang }: { lang?: Lang }) {
  const t = pick(
    lang,
    {
      title: 'What happens when the LAN fails',
      desc: 'Connections go over LAN. The connector pings the gateway once per second. After three missed pings in a row, about 3 seconds, the LAN counts as dead. A connection that hears nothing for 6 seconds starts again, now over Wi-Fi. While on Wi-Fi the connector tries the LAN every 5 seconds and moves back as soon as the LAN reaches the host.',
      phases: [
        ['LAN', 'Connections go over the cable'],
        ['LAN fails', '3 gateway pings missed, about 3 s'],
        ['Wi-Fi', 'On Wi-Fi within 6 s. The deck keeps working'],
        ['LAN back', 'LAN tried every 5 s, the connection moves back'],
      ],
    },
    {
      title: 'Was passiert, wenn das LAN ausfällt',
      desc: 'Verbindungen laufen über LAN. Der Connector pingt das Gateway einmal pro Sekunde. Fehlen drei Antworten hintereinander, gilt das LAN nach etwa 3 Sekunden als tot. Eine Verbindung, die 6 Sekunden lang nichts hört, startet neu, jetzt über WLAN. Im WLAN probiert der Connector alle 5 Sekunden das LAN und wechselt zurück, sobald es das Ziel erreicht.',
      phases: [
        ['LAN', 'Verbindungen laufen über das Kabel'],
        ['LAN fällt aus', '3 Gateway-Pings ohne Antwort, etwa 3 s'],
        ['WLAN', 'Nach höchstens 6 s im WLAN. Das Deck läuft weiter'],
        ['LAN wieder da', 'LAN alle 5 s geprüft, Verbindung wechselt zurück'],
      ],
    },
  )
  const tone = [
    ['bg-ok', 'text-ok-fg'],
    ['bg-bad', 'text-bad-fg'],
    ['bg-amber', 'text-amber-fg'],
    ['bg-ok', 'text-ok-fg'],
  ]
  return (
    <Figure title={t.title} desc={t.desc}>
      <ol className="grid gap-2 @lg:grid-cols-4 @lg:gap-0">
        {t.phases.map(([name, sub], i) => {
          const c = tone[i] ?? []
          return (
            <li key={name} className="flex gap-3 @lg:flex-col @lg:gap-2">
              <span
                aria-hidden
                className={`w-1.5 shrink-0 rounded-full @lg:h-1.5 @lg:w-auto ${c[0]} ${i === 0 ? '@lg:rounded-e-none' : i === 3 ? '@lg:rounded-s-none' : '@lg:rounded-none'}`}
              />
              <span className="@lg:pe-3">
                <span className={`flex items-center gap-1.5 text-sm font-semibold ${c[1]}`}>
                  {i === 2 ? <Wifi aria-hidden className="size-4" /> : <Network aria-hidden className="size-4" />}
                  {name}
                </span>
                <span className="block text-[13px] leading-snug text-fg-2">{sub}</span>
              </span>
            </li>
          )
        })}
      </ol>
    </Figure>
  )
}

function Shot({ src, alt, w, h }: { src: string; alt: string; w: number; h: number }) {
  return <Image src={src} alt={alt} width={w / 2} height={h / 2} className="block h-auto w-full" />
}

export function HostSwitch({ lang }: { lang?: Lang }) {
  const t = pick(
    lang,
    {
      title: 'Switching sources on the deck',
      a: 'Home screen, Companion away',
      aAlt: 'Top row of the deck. Companion in amber says offline, waiting. Next to it the keys Elgato App STUDIO-PC Dock, Server regie and Menu settings.',
      b: 'First tap on the Elgato app',
      bAlt: 'The same row after one tap. The Elgato app key is blue and says Switch?, tap again.',
      c: 'Second tap within 3 s',
      draws: 'The Elgato app draws',
    },
    {
      title: 'Quelle am Deck wechseln',
      a: 'Startbildschirm, Companion fehlt',
      aAlt: 'Obere Reihe des Decks. Companion in Gelb mit getrennt, wartet. Daneben die Tasten Elgato App STUDIO-PC Dock, Server regie und Menü Einstellungen.',
      b: 'Erstes Tippen auf die Elgato-App',
      bAlt: 'Dieselbe Reihe nach einem Tippen. Die Taste der Elgato-App ist blau und zeigt Wechseln?, nochmal tippen.',
      c: 'Zweites Tippen in 3 s',
      draws: 'Die Elgato-App zeichnet',
    },
  )
  return (
    <Figure title={t.title}>
      <div className="grid gap-4">
        {(
          [
            [t.a, t.aAlt, 'away'],
            [t.b, t.bAlt, 'asking'],
          ] as const
        ).map(([name, alt, img]) => (
          <div key={img}>
            <p className={`${mono} mb-1.5`}>{name}</p>
            <div className="overflow-hidden rounded-xl">
              <Shot src={`/docs/deck-${img}-${lang ?? 'en'}.webp`} alt={alt} w={912} h={186} />
            </div>
          </div>
        ))}
        <div>
          <p className={`${mono} mb-1.5`}>{t.c}</p>
          <p className="flex items-center gap-2 text-sm font-semibold text-ok-fg">
            <Check aria-hidden className="size-4" />
            {t.draws}
          </p>
        </div>
      </div>
    </Figure>
  )
}

export function DeckMenu({ lang }: { lang?: Lang }) {
  const t = pick(
    lang,
    {
      title: 'The deck menu on a Stream Deck +',
      alt: "The deck menu in the web UI's live deck view. Top row: Deutsch Language, two empty keys, Menu close. Second row: LAN 192.168.1.50 automatic, Setup Wi-Fi start, Firmware with its version, Restart 5 s: factory reset. Touch strip: Brightness 80 %, Web access http://192.168.1.50 and the password, Close.",
      rows: [
        ['Deutsch', 'Switches the deck to German. In German it says English'],
        ['LAN', 'Fixed or automatic address'],
        ['Setup Wi-Fi', 'Starts the setup Wi-Fi'],
        ['Restart', 'Tap twice: restart. Hold 5 s: factory reset'],
        ['Strip', 'Brightness, turn dial 1, at least 20 % while the menu is open. Web address, password. Close'],
      ],
    },
    {
      title: 'Das Deck-Menü auf einem Stream Deck +',
      alt: 'Das Deck-Menü in der Live-Ansicht der Weboberfläche. Obere Reihe: English Sprache, zwei leere Tasten, Menü schließen. Zweite Reihe: LAN 192.168.1.50 automatisch, Setup-WLAN starten, Firmware mit ihrer Version, Neustart 5 s: Werkszustand. Touchleiste: Helligkeit 80 %, Web-Zugang http://192.168.1.50 mit Passwort, Schließen.',
      rows: [
        ['English', 'Stellt das Deck auf Englisch. Auf Englisch steht dort Deutsch'],
        ['LAN', 'Feste oder automatische Adresse'],
        ['Setup-WLAN', 'Startet das Setup-WLAN'],
        ['Neustart', 'Zweimal tippen: Neustart. 5 s halten: Werkszustand'],
        ['Leiste', 'Helligkeit, Drehregler 1 drehen, im Menü mindestens 20 %. Web-Adresse, Passwort. Schließen'],
      ],
    },
  )
  return (
    <Figure title={t.title}>
      <div className="grid gap-4">
        <a href={`/docs/deck-menu-${lang ?? 'en'}.webp`} className="mx-auto block w-full max-w-lg">
          <Shot src={`/docs/deck-menu-${lang ?? 'en'}.webp`} alt={t.alt} w={1146} h={1092} />
        </a>
        <dl className="grid gap-2 text-sm">
          {t.rows.map(([k, v]) => (
            <div key={k} className="grid grid-cols-[6.5rem_1fr] gap-2">
              <dt className="font-mono text-xs leading-5 text-amber-fg">{k}</dt>
              <dd className="leading-5 text-fg-2">{v}</dd>
            </div>
          ))}
        </dl>
      </div>
    </Figure>
  )
}

export function OtaFlow({ lang }: { lang?: Lang }) {
  const t = pick(
    lang,
    {
      title: 'How an update is checked and kept',
      desc: 'The connector checks the signature, the security level and the SHA-256, then writes the new firmware into the slot after the running one and restarts into it. The new firmware must reach the network within 2 minutes and then stay up for 20 seconds. If it does not, or it crashes, the old slot starts again.',
      checks: [
        'Signature',
        'Security level',
        'SHA-256',
        'Restart into the new slot',
        'Network within 2 min, then 20 s up',
      ],
      ok: 'Kept',
      fail: 'Any step fails, or the new firmware crashes? The old slot starts again.',
      slots: 'Four slots',
      slot: 'slot',
      run: 'running',
      nxt: 'update goes here',
      prev: 'older firmware',
    },
    {
      title: 'So wird ein Update geprüft und behalten',
      desc: 'Der Connector prüft Signatur, Sicherheitsstufe und SHA-256, schreibt die neue Firmware in den Platz nach dem laufenden und startet ihn. Die neue Firmware muss in 2 Minuten das Netzwerk erreichen und dann 20 Sekunden laufen. Sonst, oder nach einem Absturz, startet wieder der alte Platz.',
      checks: [
        'Signatur',
        'Sicherheitsstufe',
        'SHA-256',
        'Neustart in den neuen Platz',
        'Netz in 2 min, dann 20 s stabil',
      ],
      ok: 'Behalten',
      fail: 'Scheitert ein Schritt oder stürzt die neue Firmware ab, startet wieder der alte Platz.',
      slots: 'Vier Plätze',
      slot: 'Platz',
      run: 'läuft',
      nxt: 'Update kommt hierhin',
      prev: 'ältere Firmware',
    },
  )
  return (
    <Figure title={t.title} desc={t.desc}>
      <ol className="relative grid gap-1.5 before:absolute before:inset-y-3 before:start-[0.6875rem] before:w-px before:bg-line-2">
        {[...t.checks, t.ok].map((c, i) => {
          const last = i === t.checks.length
          return (
            <li key={c} className="relative flex items-center gap-3">
              <span
                className={`z-10 grid size-6 shrink-0 place-items-center rounded-full border font-mono text-xs ${last ? 'border-ok bg-ok text-bg' : 'border-line-2 bg-panel text-fg-2'}`}
              >
                {last ? <Check aria-hidden className="size-3.5" /> : i + 1}
              </span>
              <span className={`text-sm ${last ? 'font-semibold text-ok-fg' : ''}`}>{c}</span>
            </li>
          )
        })}
      </ol>
      <p className="mt-3 flex items-center gap-2 border-t border-line pt-3 text-sm text-bad-fg">
        <RotateCcw aria-hidden className="size-4 shrink-0" />
        {t.fail}
      </p>
      <p className={`${mono} mb-1.5 mt-4`}>{t.slots}</p>
      <div className="grid grid-cols-4 gap-1.5">
        {[
          ['0', t.prev, ''],
          ['1', t.run, 'border-ok/70 bg-ok-soft'],
          ['2', t.nxt, 'border-amber border-dashed'],
          ['3', '', ''],
        ].map(([n, sub, cls]) => (
          <div key={n} className={`rounded-lg border-2 border-line bg-panel p-2 ${cls}`}>
            <span className="block font-mono text-xs text-fg-3">
              {t.slot} {n}
            </span>
            <span className="block text-xs leading-snug">{sub}</span>
          </div>
        ))}
      </div>
    </Figure>
  )
}

export function AuthLevels({ lang }: { lang?: Lang }) {
  const t = pick(
    lang,
    {
      title: 'API access levels',
      desc: 'Four levels, each includes the one above. open: everyone. control: the password, or an API key with control or full access. full: the password, or a full access key. admin: the password only.',
      rows: [
        ['open', 'Everyone', 'Login, certificate, lost password help, first setup'],
        ['control', 'Password, control or full key', 'Status, deck picture, presses, menu, metrics'],
        ['full', 'Password, full key', 'Settings, restart, updates, slots, log, pairing, Wi-Fi'],
        ['admin', 'Password only', 'Password, API keys, backup, crash report, own certificate, factory reset'],
      ],
    },
    {
      title: 'Zugriffsstufen der API',
      desc: 'Vier Stufen, jede enthält die darüber. open: alle. control: das Passwort oder ein API-Schlüssel mit Steuern oder Vollzugriff. full: das Passwort oder ein Schlüssel mit Vollzugriff. admin: nur das Passwort.',
      rows: [
        ['open', 'Alle', 'Login, Zertifikat, Hilfe bei vergessenem Passwort, erste Einrichtung'],
        ['control', 'Passwort, Schlüssel Steuern oder Vollzugriff', 'Status, Deck-Bild, Tastendrücke, Menü, Messwerte'],
        [
          'full',
          'Passwort, Schlüssel Vollzugriff',
          'Einstellungen, Neustart, Updates, Firmware-Plätze, Log, Kopplung, WLAN',
        ],
        [
          'admin',
          'Nur das Passwort',
          'Passwort, API-Schlüssel, Backup, Absturzbericht, eigenes Zertifikat, Werkszustand',
        ],
      ],
    },
  )
  const icons = [<Globe key="g" />, <MonitorSmartphone key="m" />, <KeyRound key="k" />, <Lock key="l" />]
  return (
    <Figure title={t.title} desc={t.desc}>
      <ol className="grid gap-1.5">
        {t.rows.map(([lvl, who, what], i) => (
          <li
            key={lvl}
            className={`${box} grid gap-x-3 gap-y-0.5 @lg:grid-cols-[6.5rem_1fr] @lg:items-center`}
            style={{ marginInlineStart: `${i * 0.75}rem` }}
          >
            <span className="flex items-center gap-2 font-mono text-sm font-semibold text-amber-fg [&_svg]:size-4">
              {icons[i]}
              {lvl}
            </span>
            <span>
              <span className="block text-sm font-medium">{who}</span>
              <span className="block text-xs leading-snug text-fg-3">{what}</span>
            </span>
          </li>
        ))}
      </ol>
    </Figure>
  )
}

export function Ports({ lang }: { lang?: Lang }) {
  const t = pick(
    lang,
    {
      title: 'The ports and buttons on the board',
      desc: 'USB-C goes to the computer for flashing, setup over USB and power. USB-A takes the Stream Deck. The network port takes Ethernet and PoE power. BOOT and RST put the board into download mode.',
      rows: [
        [<Usb key="c" />, 'USB-C', 'To the computer: flashing, setup over USB, power without PoE'],
        [<Usb key="a" />, 'USB-A', 'The Stream Deck. Not for flashing'],
        [<Router key="r" />, 'Ethernet', 'Network, and power with PoE'],
        [<RotateCcw key="b" />, 'BOOT + RST', 'Download mode: hold BOOT, press and release RST, release BOOT'],
      ],
    },
    {
      title: 'Anschlüsse und Tasten der Platine',
      desc: 'USB-C geht zum Computer: Flashen, Einrichtung über USB und Strom. USB-A nimmt das Stream Deck. Die Netzwerkbuchse nimmt LAN und PoE-Strom. BOOT und RST bringen die Platine in den Download-Modus.',
      rows: [
        [<Usb key="c" />, 'USB-C', 'Zum Computer: Flashen, Einrichtung über USB, Strom ohne PoE'],
        [<Usb key="a" />, 'USB-A', 'Das Stream Deck. Nicht zum Flashen'],
        [<Router key="r" />, 'LAN', 'Netzwerk, und Strom per PoE'],
        [<RotateCcw key="b" />, 'BOOT + RST', 'Download-Modus: BOOT halten, RST drücken und loslassen, BOOT loslassen'],
      ],
    },
  )
  return (
    <Figure title={t.title} desc={t.desc}>
      <div className="grid gap-2 @lg:grid-cols-2">
        {t.rows.map(([icon, name, sub]) => (
          <Node key={name as string} icon={icon} title={name} sub={sub} tone={name === 'USB-C' ? 'amber' : undefined} />
        ))}
      </div>
    </Figure>
  )
}

export function CompanionModes({ lang }: { lang?: Lang }) {
  const t = pick(
    lang,
    {
      title: 'Two ways Companion connects',
      desc: 'Satellite: the connector opens the connection to Companion on TCP 16622, or 16623 for WebSocket. Network Dock: Companion opens the connection to the connector on TCP 5343, after it finds it over mDNS.',
      sat: 'Satellite',
      satSub: 'The default. Works through Docker and other networks',
      dock: 'Network Dock',
      dockSub: 'Companion finds the connector in the same network',
    },
    {
      title: 'Zwei Wege, wie Companion sich verbindet',
      desc: 'Satellite: Der Connector baut die Verbindung zu Companion auf, TCP 16622 oder 16623 für WebSocket. Network Dock: Companion baut die Verbindung zum Connector auf, TCP 5343, nachdem es ihn per mDNS gefunden hat.',
      sat: 'Satellite',
      satSub: 'Der Standard. Geht auch durch Docker und andere Netze',
      dock: 'Network Dock',
      dockSub: 'Companion findet den Connector im selben Netz',
    },
  )
  const lane = (name: string, sub: string, port: string, toCompanion: boolean) => (
    <div className="grid gap-1.5">
      <p className={mono}>
        <span className="text-fg">{name}</span> · {sub}
      </p>
      <div className="grid items-center gap-2 @lg:grid-cols-[1fr_auto_1fr]">
        <Node slim icon={<Cpu />} title="Deck Connector" tone="amber" />
        <div
          aria-hidden
          className="flex items-center justify-center gap-2 font-mono text-xs text-fg-3 @lg:flex-col @lg:gap-0"
        >
          <span>{port}</span>
          <span className="text-base leading-none @lg:hidden">{toCompanion ? '↓' : '↑'}</span>
          <span className="text-base leading-none @max-lg:hidden">{toCompanion ? '→' : '←'}</span>
        </div>
        <Node slim icon={<AppWindow />} title="Companion" />
      </div>
    </div>
  )
  return (
    <Figure title={t.title} desc={t.desc}>
      <div className="grid gap-4">
        {lane(t.sat, t.satSub, 'TCP 16622', true)}
        {lane(t.dock, t.dockSub, 'TCP 5343', false)}
      </div>
    </Figure>
  )
}
