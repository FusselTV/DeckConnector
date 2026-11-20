import type { Metadata } from 'next'
import type { ReactNode } from 'react'
import type { Lang } from '~/lib/i18n'
import { owner } from '~/site.config'

export const legalPath = {
  imprint: { en: '/imprint/', de: '/de/impressum/' },
  privacy: { en: '/privacy/', de: '/de/datenschutz/' },
  contact: { en: '/contact/', de: '/de/kontakt/' },
}
const titles = {
  imprint: { en: 'Imprint', de: 'Impressum' },
  privacy: { en: 'Privacy policy', de: 'Datenschutzerklärung' },
  contact: { en: 'Contact', de: 'Kontakt' },
}

export function legalMeta(lang: Lang, page: keyof typeof legalPath): Metadata {
  const p = legalPath[page]
  return {
    title: titles[page][lang],
    robots: { index: false },
    alternates: { canonical: p[lang], languages: { en: p.en, de: p.de, 'x-default': p.en } },
  }
}

const DPA = 'https://www.cloudflare.com/cloudflare-customer-dpa/'
const CF_PRIVACY = 'https://www.cloudflare.com/privacypolicy/'
const APPLE_PRIVACY = {
  de: 'https://www.apple.com/legal/privacy/de-ww/',
  en: 'https://www.apple.com/legal/privacy/en-ww/',
}
const TURNSTILE_PRIVACY = 'https://www.cloudflare.com/turnstile-privacy-policy/'
const CF_ANALYTICS = 'https://developers.cloudflare.com/analytics/account-and-zone-analytics/zone-analytics/'

function Page({ lang, page, children }: { lang: Lang; page: keyof typeof legalPath; children: ReactNode }) {
  return (
    <main id="main" lang={lang} className="prose mx-auto w-full max-w-3xl px-4 py-16 sm:px-6">
      <h1 className="text-3xl font-semibold tracking-tight sm:text-4xl">{titles[page][lang]}</h1>
      {lang === 'en' && (
        <p>
          This is a translation. Only the{' '}
          <a href={legalPath[page].de} hrefLang="de">
            German version
          </a>{' '}
          is legally binding.
        </p>
      )}
      {children}
    </main>
  )
}

function Address({ lang }: { lang: Lang }) {
  return (
    <p>
      {owner.name}
      <br />
      {owner.street}
      <br />
      {owner.city}
      <br />
      {lang === 'de' ? 'Deutschland' : 'Germany'}
      <br />
      {lang === 'de' ? 'E-Mail' : 'Email'}: <a href={`mailto:${owner.email}`}>{owner.email}</a>
    </p>
  )
}

export function Imprint({ lang }: { lang: Lang }) {
  return lang === 'de' ? (
    <Page lang="de" page="imprint">
      <p>Angaben gemäß § 5 DDG</p>
      <Address lang="de" />
      <p>
        Oder über das <a href={legalPath.contact.de}>Kontaktformular</a>.
      </p>
      <h2>Verantwortlich für den Inhalt nach § 18 Abs. 2 MStV</h2>
      <p>{owner.name}, Anschrift wie oben</p>
    </Page>
  ) : (
    <Page lang="en" page="imprint">
      <p>Information under § 5 DDG (German Digital Services Act)</p>
      <Address lang="en" />
      <p>
        Or use the <a href={legalPath.contact.en}>contact form</a>.
      </p>
      <h2>Responsible for the content under § 18 (2) MStV</h2>
      <p>{owner.name}, address as above</p>
    </Page>
  )
}

export function Privacy({ lang }: { lang: Lang }) {
  const authority = <a href={owner.authority.url}>{owner.authority.name}</a>
  return lang === 'de' ? (
    <Page lang="de" page="privacy">
      <h2>Verantwortlicher</h2>
      <Address lang="de" />

      <h2>Hosting bei Cloudflare</h2>
      <p>
        Diese Website liegt bei Cloudflare Pages. Anbieter ist Cloudflare, Inc., 101 Townsend St, San Francisco, CA
        94107, USA.
      </p>
      <p>
        Wenn du eine Seite aufrufst, verarbeitet Cloudflare deine IP-Adresse, den Zeitpunkt, die aufgerufene Adresse und
        die Kennung deines Browsers. Das ist nötig, um die Seite auszuliefern und vor Angriffen zu schützen.
        Rechtsgrundlage ist Art. 6 Abs. 1 lit. f DSGVO. Mein berechtigtes Interesse ist eine sichere und schnelle
        Website.
      </p>
      <p>
        Cloudflare verarbeitet die Daten in meinem Auftrag (Art. 28 DSGVO), auf Grundlage des{' '}
        <a href={DPA}>Data Processing Addendum</a> von Cloudflare. Dabei können Daten in die USA gehen. Cloudflare ist
        nach dem EU-US Data Privacy Framework zertifiziert (Art. 45 DSGVO). Zusätzlich gelten die
        Standardvertragsklauseln der EU-Kommission aus dem Data Processing Addendum (Art. 46 Abs. 2 lit. c DSGVO). Wie
        lange Cloudflare die Daten speichert, steht in der <a href={CF_PRIVACY}>Datenschutzerklärung von Cloudflare</a>.
      </p>

      <h2>Cookies</h2>
      <p>Diese Website setzt keine Cookies.</p>

      <h2>Schutz von E-Mail-Adressen</h2>
      <p>
        Cloudflare ersetzt E-Mail-Adressen in den Seiten durch eine kodierte Form, damit Programme sie nicht einsammeln
        können. Das Skript <code>email-decode.min.js</code>, geladen von dieser Website, macht sie in deinem Browser
        wieder lesbar. Rechtsgrundlage ist Art. 6 Abs. 1 lit. f DSGVO. Mein berechtigtes Interesse ist Schutz vor Spam.
      </p>

      <h2>Besucherstatistik von Cloudflare</h2>
      <p>
        Cloudflare zählt die Anfragen, die es für diese Website ohnehin ausliefert. Daraus zeigt es mir Summen, zum
        Beispiel die Zahl der Anfragen und Besucher, die Datenmenge und die Länder, aus denen die Anfragen kommen. Für
        die Statistik lädt keine Seite ein Skript, und Cloudflare setzt dafür kein Cookie und liest dafür nichts von
        deinem Gerät. Ich sehe nur zusammengefasste Zahlen, keine einzelnen Besucher. Laut Cloudflare stehen die Zahlen
        mindestens 30 Tage lang zur Verfügung. Mehr dazu in der <a href={CF_ANALYTICS}>Dokumentation von Cloudflare</a>.
        Rechtsgrundlage ist Art. 6 Abs. 1 lit. f DSGVO. Mein berechtigtes Interesse ist zu sehen, wie viel die Website
        genutzt wird. Cloudflare handelt auch hier in meinem Auftrag, auf Grundlage des Data Processing Addendum.
      </p>

      <h2>Kontaktformular und E-Mail</h2>
      <p>
        Wenn du mir über das <a href={legalPath.contact.de}>Kontaktformular</a> oder per E-Mail schreibst, verarbeite
        ich deine E-Mail-Adresse, deine Nachricht und, wenn du ihn angibst, deinen Namen. Ich nutze sie nur, um dir zu
        antworten. Rechtsgrundlage ist Art. 6 Abs. 1 lit. b DSGVO, wenn es um einen Vertrag oder seine Anbahnung geht,
        sonst Art. 6 Abs. 1 lit. f DSGVO. Mein berechtigtes Interesse ist, deine Anfrage zu beantworten.
      </p>
      <p>
        Das Formular schickt deine Angaben an Cloudflare Workers. Von dort gehen sie als E-Mail an mein Postfach. Dabei
        verarbeitet Cloudflare auch die Verbindungsdaten aus dem Abschnitt „Hosting bei Cloudflare“. Cloudflare handelt
        in meinem Auftrag, auf Grundlage desselben Data Processing Addendum. Mein Postfach liegt bei iCloud Mail von
        Apple (Apple Distribution International Ltd., Hollyhill Industrial Estate, Hollyhill, Cork, Irland). Apple
        speichert die E-Mails als Anbieter meines Postfachs. Dort landen auch E-Mails an die Adresse oben.
        Rechtsgrundlage ist Art. 6 Abs. 1 lit. f DSGVO. Mein berechtigtes Interesse ist, E-Mails zu empfangen. Ob Apple
        Daten in die USA übermittelt, steht in der <a href={APPLE_PRIVACY.de}>Datenschutzerklärung von Apple</a>.
      </p>
      <p>
        Für jede Anfrage an das Formular schreibt Cloudflare einen Eintrag in die Workers Logs: Zeitpunkt, Adresse und
        Methode der Anfrage, die Header mit deiner IP-Adresse, Daten von Cloudflare zur Verbindung wie dein Land, und ob
        die Anfrage geklappt hat. Mein Code für das Formular schreibt nichts, was du eingibst, in die Logs, nur die
        Fehlermeldung des Mailservers, wenn das Senden scheitert. Ich nutze die Logs, um Fehler zu finden.
        Rechtsgrundlage ist Art. 6 Abs. 1 lit. f DSGVO. Cloudflare löscht sie nach {owner.logDays} Tagen.
      </p>
      <p>
        Damit niemand das Formular massenhaft nutzt, zählt Cloudflare die Anfragen je IP-Adresse (bei IPv6 je /64-Netz)
        eine Minute lang und lässt etwa fünf durch. Danach verfällt der Zähler. Rechtsgrundlage ist Art. 6 Abs. 1 lit. f
        DSGVO. Mein berechtigtes Interesse ist der Schutz vor Missbrauch.
      </p>
      <p>
        Gegen Spam nutzt das Formular Cloudflare Turnstile von Cloudflare, Inc. (Adresse oben). Das Skript lädt erst,
        sobald du ein Feld im Formular nutzt, von <code>challenges.cloudflare.com</code>. Es prüft im Hintergrund, ob
        ein Mensch das Formular nutzt. Dafür verarbeitet Cloudflare deine IP-Adresse, die Kennung deines Browsers und
        Merkmale deines Browsers und Geräts, zum Beispiel aus der TLS-Verbindung. Nur selten zeigt Turnstile ein
        Kästchen zum Anklicken. Turnstile setzt kein Cookie. Es legt aber im lokalen Speicher deines Browsers für
        challenges.cloudflare.com den Eintrag <code>cf.turnstile.u</code> an. Er bleibt, bis du ihn löschst. Das
        Speichern und Lesen erlaubt § 25 Abs. 2 Nr. 2 TDDDG, weil es für das Formular, das du nutzt, unbedingt nötig
        ist. Rechtsgrundlage ist Art. 6 Abs. 1 lit. f DSGVO. Mein berechtigtes Interesse ist ein Formular ohne Spam. Für
        die Prüfung handelt Cloudflare in meinem Auftrag. Nutzt Cloudflare die Daten, um die Erkennung von Bots zu
        verbessern, ist Cloudflare selbst verantwortlich. Die Übermittlung in die USA stützt sich auf das EU-US Data
        Privacy Framework (Art. 45 DSGVO). Mehr dazu im <a href={TURNSTILE_PRIVACY}>Turnstile Privacy Addendum</a> und
        in der <a href={CF_PRIVACY}>Datenschutzerklärung von Cloudflare</a>.
      </p>
      <p>
        Ich lösche deine Nachricht, wenn deine Anfrage erledigt ist, außer ein Gesetz verlangt, sie länger
        aufzubewahren.
      </p>

      <h2>Speicher in deinem Browser</h2>
      <p>
        Einige Einstellungen speichert die Website im lokalen Speicher deines Browsers (localStorage), sobald du sie
        änderst:
      </p>
      <ul>
        <li>
          <code>theme</code>: helles oder dunkles Design
        </li>
        <li>
          Schlüssel, die mit <code>dc.</code> beginnen: Einstellungen in der Demo, zum Beispiel Sprache, Design und
          geschlossene Hinweise
        </li>
      </ul>
      <p>
        Wenn du die Demo öffnest, hält sie ihren Zustand im Sitzungsspeicher (sessionStorage, Schlüssel mit{' '}
        <code>dc.</code>). Er endet, wenn du den Tab schließt oder auf „Zurücksetzen“ tippst.
      </p>
      <p>
        Diese Daten bleiben in deinem Browser und werden nicht übertragen. Rechtsgrundlage ist § 25 Abs. 2 Nr. 2 TDDDG,
        denn das Speichern ist für die Funktion nötig, die du ausdrücklich nutzt. Du kannst die Daten in den
        Einstellungen deines Browsers löschen.
      </p>

      <h2>Die Software DeckConnector</h2>
      <p>
        Die Firmware und die Home-Assistant-Integration schicken mir keine Daten. Mit welchen Servern ein Connector
        spricht, steht in der <a href="/de/docs/network/#internet">Anleitung</a>.
      </p>

      <h2>Deine Rechte</h2>
      <p>
        Du hast das Recht auf Auskunft (Art. 15 DSGVO), Berichtigung (Art. 16), Löschung (Art. 17), Einschränkung der
        Verarbeitung (Art. 18) und Datenübertragbarkeit (Art. 20). Schreib mir dazu an die E-Mail-Adresse oben.
      </p>
      <p>
        <strong>Widerspruch:</strong> Soweit die Verarbeitung auf Art. 6 Abs. 1 lit. f DSGVO beruht, kannst du ihr
        jederzeit aus Gründen widersprechen, die sich aus deiner besonderen Situation ergeben (Art. 21 DSGVO).
      </p>
      <p>
        Du kannst dich bei einer Datenschutz-Aufsichtsbehörde beschweren (Art. 77 DSGVO). Für mich zuständig ist{' '}
        {authority}.
      </p>

      <h2>Pflicht zur Angabe von Daten</h2>
      <p>
        Du musst keine Daten angeben. Ohne die Verbindungsdaten kann Cloudflare die Website aber nicht ausliefern, und
        ohne E-Mail-Adresse kann ich dir nicht antworten.
      </p>
    </Page>
  ) : (
    <Page lang="en" page="privacy">
      <h2>Controller</h2>
      <Address lang="en" />

      <h2>Hosting by Cloudflare</h2>
      <p>
        This website is hosted on Cloudflare Pages. The provider is Cloudflare, Inc., 101 Townsend St, San Francisco, CA
        94107, USA.
      </p>
      <p>
        When you open a page, Cloudflare processes your IP address, the time, the address of the page and your
        browser&apos;s user agent. Cloudflare needs this to deliver the page and to protect it from attacks. The legal
        basis is Art. 6(1)(f) GDPR. My legitimate interest is a safe and fast website.
      </p>
      <p>
        Cloudflare processes the data on my behalf (Art. 28 GDPR) under Cloudflare&apos;s{' '}
        <a href={DPA}>Data Processing Addendum</a>. Data can go to the USA. Cloudflare is certified under the EU-US Data
        Privacy Framework (Art. 45 GDPR). The EU Standard Contractual Clauses in the Data Processing Addendum also apply
        (Art. 46(2)(c) GDPR). <a href={CF_PRIVACY}>Cloudflare&apos;s privacy policy</a> says how long it keeps the data.
      </p>

      <h2>Cookies</h2>
      <p>This website sets no cookies.</p>

      <h2>Protection of email addresses</h2>
      <p>
        Cloudflare replaces email addresses in the pages with an encoded form, so programs cannot collect them. The
        script <code>email-decode.min.js</code>, loaded from this website, makes them readable again in your browser.
        The legal basis is Art. 6(1)(f) GDPR. My legitimate interest is protection from spam.
      </p>

      <h2>Visitor statistics from Cloudflare</h2>
      <p>
        Cloudflare counts the requests it serves for this website anyway. It shows me totals, for example the number of
        requests and visitors, the amount of data and the countries the requests come from. The statistics need no
        script. Cloudflare sets no cookie for them and reads nothing from your device for them. I only see totals, not
        single visitors. Cloudflare says the numbers are available for at least 30 days. See{' '}
        <a href={CF_ANALYTICS}>Cloudflare&apos;s documentation</a>. The legal basis is Art. 6(1)(f) GDPR. My legitimate
        interest is to see how much the website is used. Cloudflare acts on my behalf here too, under the Data
        Processing Addendum.
      </p>

      <h2>Contact form and email</h2>
      <p>
        When you write to me with the <a href={legalPath.contact.en}>contact form</a> or by email, I process your email
        address, your message and your name if you give it. I use them only to answer you. The legal basis is Art.
        6(1)(b) GDPR when it is about a contract or the steps before one, else Art. 6(1)(f) GDPR. My legitimate interest
        is to answer your question.
      </p>
      <p>
        The form sends your details to Cloudflare Workers. From there they go as an email to my mailbox. Cloudflare also
        processes the connection data from the section on hosting. Cloudflare acts on my behalf under the same Data
        Processing Addendum. My mailbox is at iCloud Mail by Apple (Apple Distribution International Ltd., Hollyhill
        Industrial Estate, Hollyhill, Cork, Ireland). Apple stores the emails as the provider of my mailbox. Emails to
        the address above end up there too. The legal basis is Art. 6(1)(f) GDPR. My legitimate interest is to receive
        email. <a href={APPLE_PRIVACY.en}>Apple&apos;s privacy policy</a> says whether Apple sends data to the USA.
      </p>
      <p>
        For each request to the form, Cloudflare writes an entry to its Workers Logs. It holds the time, the address and
        method of the request, the headers with your IP address, data from Cloudflare about the connection such as your
        country, and whether the request worked. My code behind the form writes nothing you typed into the logs, only
        the mail server&apos;s error message when sending fails. I use the logs to find faults. The legal basis is Art.
        6(1)(f) GDPR. Cloudflare deletes them after {owner.logDays} days.
      </p>
      <p>
        To stop mass use of the form, Cloudflare counts the requests per IP address (per /64 network for IPv6) for one
        minute and lets about five through. Then the count expires. The legal basis is Art. 6(1)(f) GDPR. My legitimate
        interest is protection from abuse.
      </p>
      <p>
        Against spam, the form uses Cloudflare Turnstile by Cloudflare, Inc. (address above). The script loads from{' '}
        <code>challenges.cloudflare.com</code> only once you use a field of the form. It checks in the background
        whether a person uses the form. For this, Cloudflare processes your IP address, your browser&apos;s identifier
        and signals from your browser and device, for example from the TLS connection. Only rarely does Turnstile show a
        box to click. Turnstile sets no cookie. It does keep the entry <code>cf.turnstile.u</code> in your
        browser&apos;s local storage for challenges.cloudflare.com. It stays until you delete it. Storing and reading
        are allowed under § 25(2) no. 2 TDDDG, because it is strictly necessary for the form you use. The legal basis is
        Art. 6(1)(f) GDPR. My legitimate interest is a form without spam. For the check, Cloudflare acts on my behalf.
        When Cloudflare uses the data to improve its bot detection, Cloudflare is the controller itself. The transfer to
        the USA is based on the EU-US Data Privacy Framework (Art. 45 GDPR). See the{' '}
        <a href={TURNSTILE_PRIVACY}>Turnstile Privacy Addendum</a> and{' '}
        <a href={CF_PRIVACY}>Cloudflare&apos;s privacy policy</a>.
      </p>
      <p>I delete your message when your question is done, unless a law says I must keep it longer.</p>

      <h2>Storage in your browser</h2>
      <p>The website keeps some settings in your browser&apos;s local storage (localStorage) once you change them.</p>
      <ul>
        <li>
          <code>theme</code> is the light or dark design.
        </li>
        <li>
          Keys that start with <code>dc.</code> are settings in the demo, for example language, design and closed
          notices.
        </li>
      </ul>
      <p>
        When you open the demo, it keeps its state in session storage (sessionStorage, keys that start with{' '}
        <code>dc.</code>). It ends when you close the tab or tap Reset.
      </p>
      <p>
        This data stays in your browser and is not sent anywhere. The legal basis is § 25(2) no. 2 TDDDG, because
        storing it is needed for a function you asked for. You can delete it in your browser&apos;s settings.
      </p>

      <h2>The DeckConnector software</h2>
      <p>
        The firmware and the Home Assistant integration send me no data. The <a href="/docs/network/#internet">docs</a>{' '}
        list the servers a connector talks to.
      </p>

      <h2>Your rights</h2>
      <p>
        You have the right of access (Art. 15 GDPR), correction (Art. 16), deletion (Art. 17), restriction of processing
        (Art. 18) and data portability (Art. 20). Write to the email address above.
      </p>
      <p>
        <strong>Right to object.</strong> Where the processing is based on Art. 6(1)(f) GDPR, you can object to it at
        any time, for reasons that come from your particular situation (Art. 21 GDPR).
      </p>
      <p>
        You can complain to a data protection authority (Art. 77 GDPR). The authority responsible for me is {authority}.
      </p>

      <h2>No duty to give data</h2>
      <p>
        You do not have to give any data. Without the connection data, Cloudflare cannot deliver the website, and
        without an email address I cannot answer you.
      </p>
    </Page>
  )
}
