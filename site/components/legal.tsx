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
  return { title: titles[page][lang], robots: { index: false }, alternates: { canonical: p[lang], languages: { en: p.en, de: p.de, 'x-default': p.en } } }
}

const DPA = 'https://www.cloudflare.com/cloudflare-customer-dpa/'
const CF_PRIVACY = 'https://www.cloudflare.com/privacypolicy/'

function Page({ lang, page, children }: { lang: Lang; page: keyof typeof legalPath; children: ReactNode }) {
  return (
    <main id="main" lang={lang} className="prose mx-auto w-full max-w-3xl px-4 py-16 sm:px-6">
      <h1 className="text-3xl font-semibold tracking-tight sm:text-4xl">{titles[page][lang]}</h1>
      {lang === 'en' && (
        <p>
          This is a translation. Only the <a href={legalPath[page].de} hrefLang="de">German version</a> is legally binding.
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
      <p>Diese Website liegt bei Cloudflare Pages. Anbieter ist Cloudflare, Inc., 101 Townsend St, San Francisco, CA 94107, USA.</p>
      <p>
        Wenn du eine Seite aufrufst, verarbeitet Cloudflare deine IP-Adresse, den Zeitpunkt, die aufgerufene Adresse und die Kennung deines Browsers. Das ist nötig, um die Seite auszuliefern und
        vor Angriffen zu schützen. Rechtsgrundlage ist Art. 6 Abs. 1 lit. f DSGVO. Mein berechtigtes Interesse ist eine sichere und schnelle Website. Ich werte diese Daten nicht aus.
      </p>
      <p>
        Cloudflare verarbeitet die Daten in meinem Auftrag (Art. 28 DSGVO), auf Grundlage des <a href={DPA}>Data Processing Addendum</a> von Cloudflare. Dabei können Daten in die USA gehen.
        Cloudflare ist nach dem EU-US Data Privacy Framework zertifiziert (Art. 45 DSGVO). Zusätzlich gelten die Standardvertragsklauseln der EU-Kommission aus dem Data Processing Addendum
        (Art. 46 Abs. 2 lit. c DSGVO). Wie lange Cloudflare die Daten speichert, steht in der <a href={CF_PRIVACY}>Datenschutzerklärung von Cloudflare</a>.
      </p>

      <h2>Kontaktformular und E-Mail</h2>
      <p>
        Wenn du mir über das <a href={legalPath.contact.de}>Kontaktformular</a> oder per E-Mail schreibst, verarbeite ich deine E-Mail-Adresse, deine Nachricht und, wenn du ihn angibst, deinen
        Namen. Ich nutze sie nur, um dir zu antworten. Rechtsgrundlage ist Art. 6 Abs. 1 lit. b DSGVO, wenn es um einen Vertrag oder seine Anbahnung geht, sonst Art. 6 Abs. 1 lit. f DSGVO. Mein
        berechtigtes Interesse ist, deine Anfrage zu beantworten.
      </p>
      <p>
        Das Formular schickt deine Angaben an Cloudflare Workers. Cloudflare Email Routing leitet sie als E-Mail an mein Postfach weiter, genauso wie E-Mails an die Adresse oben. Dabei verarbeitet
        Cloudflare auch die Verbindungsdaten aus dem Abschnitt „Hosting bei Cloudflare“. Cloudflare handelt in meinem Auftrag, auf Grundlage desselben Data Processing Addendum.
      </p>
      <p>Ich lösche deine Nachricht, wenn deine Anfrage erledigt ist, außer ein Gesetz verlangt, sie länger aufzubewahren.</p>

      <h2>Keine Cookies, kein Tracking</h2>
      <p>Diese Website setzt keine Cookies und nutzt keine Analyse- oder Tracking-Dienste. Sie lädt nichts von fremden Servern.</p>

      <h2>Speicher in deinem Browser</h2>
      <p>Einige Einstellungen speichert die Website im lokalen Speicher deines Browsers (localStorage), sobald du sie änderst:</p>
      <ul>
        <li>
          <code>theme</code>: helles oder dunkles Design
        </li>
        <li>
          Schlüssel, die mit <code>dc.</code> beginnen: Einstellungen in der Demo, zum Beispiel Sprache, Design und geschlossene Hinweise
        </li>
      </ul>
      <p>
        Wenn du die Demo öffnest, hält sie ihren Zustand im Sitzungsspeicher (sessionStorage, Schlüssel mit <code>dc.</code>). Er endet, wenn du den Tab schließt oder auf „Zurücksetzen“ tippst.
      </p>
      <p>
        Diese Daten bleiben in deinem Browser und werden nicht übertragen. Rechtsgrundlage ist § 25 Abs. 2 Nr. 2 TDDDG, denn das Speichern ist für die Funktion nötig, die du ausdrücklich nutzt. Du
        kannst die Daten in den Einstellungen deines Browsers löschen.
      </p>

      <h2>Deine Rechte</h2>
      <p>
        Du hast das Recht auf Auskunft (Art. 15 DSGVO), Berichtigung (Art. 16), Löschung (Art. 17), Einschränkung der Verarbeitung (Art. 18) und Datenübertragbarkeit (Art. 20). Schreib mir dazu an
        die E-Mail-Adresse oben.
      </p>
      <p>
        <strong>Widerspruch:</strong> Soweit die Verarbeitung auf Art. 6 Abs. 1 lit. f DSGVO beruht, kannst du ihr jederzeit aus Gründen widersprechen, die sich aus deiner besonderen Situation
        ergeben (Art. 21 DSGVO).
      </p>
      <p>Du kannst dich bei einer Datenschutz-Aufsichtsbehörde beschweren (Art. 77 DSGVO). Für mich zuständig ist {authority}.</p>

      <h2>Pflicht zur Angabe von Daten</h2>
      <p>Du musst keine Daten angeben. Ohne die Verbindungsdaten kann Cloudflare die Website aber nicht ausliefern, und ohne E-Mail-Adresse kann ich dir nicht antworten.</p>
    </Page>
  ) : (
    <Page lang="en" page="privacy">
      <h2>Controller</h2>
      <Address lang="en" />

      <h2>Hosting by Cloudflare</h2>
      <p>This website is hosted on Cloudflare Pages. The provider is Cloudflare, Inc., 101 Townsend St, San Francisco, CA 94107, USA.</p>
      <p>
        When you open a page, Cloudflare processes your IP address, the time, the address of the page and your browser&apos;s user agent. Cloudflare needs this to deliver the page and to protect it
        from attacks. The legal basis is Art. 6(1)(f) GDPR. My legitimate interest is a safe and fast website. I do not analyse this data.
      </p>
      <p>
        Cloudflare processes the data on my behalf (Art. 28 GDPR) under Cloudflare&apos;s <a href={DPA}>Data Processing Addendum</a>. Data can go to the USA. Cloudflare is certified under the
        EU-US Data Privacy Framework (Art. 45 GDPR). The EU Standard Contractual Clauses in the Data Processing Addendum also apply (Art. 46(2)(c) GDPR). <a href={CF_PRIVACY}>Cloudflare&apos;s privacy
        policy</a> says how long it keeps the data.
      </p>

      <h2>Contact form and email</h2>
      <p>
        When you write to me with the <a href={legalPath.contact.en}>contact form</a> or by email, I process your email address, your message and your name if you give it. I use them only to
        answer you. The legal basis is Art. 6(1)(b) GDPR when it is about a contract or the steps before one, else Art. 6(1)(f) GDPR. My legitimate interest is to answer your question.
      </p>
      <p>
        The form sends your details to Cloudflare Workers. Cloudflare Email Routing forwards them as an email to my mailbox, the same as emails to the address above. Cloudflare also processes the
        connection data from the section on hosting. Cloudflare acts on my behalf under the same Data Processing Addendum.
      </p>
      <p>I delete your message when your question is done, unless a law says I must keep it longer.</p>

      <h2>No cookies, no tracking</h2>
      <p>This website sets no cookies and uses no analytics or tracking. It loads nothing from other servers.</p>

      <h2>Storage in your browser</h2>
      <p>The website keeps some settings in your browser&apos;s local storage (localStorage) once you change them.</p>
      <ul>
        <li>
          <code>theme</code> is the light or dark design.
        </li>
        <li>
          Keys that start with <code>dc.</code> are settings in the demo, for example language, design and closed notices.
        </li>
      </ul>
      <p>
        When you open the demo, it keeps its state in session storage (sessionStorage, keys that start with <code>dc.</code>). It ends when you close the tab or tap Reset.
      </p>
      <p>
        This data stays in your browser and is not sent anywhere. The legal basis is § 25(2) no. 2 TDDDG, because storing it is needed for a function you asked for. You can delete it in your
        browser&apos;s settings.
      </p>

      <h2>Your rights</h2>
      <p>
        You have the right of access (Art. 15 GDPR), correction (Art. 16), deletion (Art. 17), restriction of processing (Art. 18) and data portability (Art. 20). Write to the email address above.
      </p>
      <p>
        <strong>Right to object.</strong> Where the processing is based on Art. 6(1)(f) GDPR, you can object to it at any time, for reasons that come from your particular situation (Art. 21 GDPR).
      </p>
      <p>You can complain to a data protection authority (Art. 77 GDPR). The authority responsible for me is {authority}.</p>

      <h2>No duty to give data</h2>
      <p>You do not have to give any data. Without the connection data, Cloudflare cannot deliver the website, and without an email address I cannot answer you.</p>
    </Page>
  )
}
