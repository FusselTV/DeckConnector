import type { Lang } from './i18n'

// The server builds the index and the browser parses queries with this, so both must match.
export const tokenizer = {
  en: { language: 'english', stemming: true },
  de: { language: 'german' },
} as const

const synonyms: Record<Lang, Record<string, string>> = {
  en: {
    wifi: 'wi-fi',
    wlan: 'wi-fi',
    static: 'fixed',
    forgot: 'lost',
    forgotten: 'lost',
    ota: 'update',
    timeserver: 'ntp',
  },
  de: {
    wifi: 'wlan',
    'wi-fi': 'wlan',
    werkseinstellungen: 'werkszustand',
    werkseinstellung: 'werkszustand',
    ota: 'update',
    satellit: 'satellite',
    statisch: 'fest',
    statische: 'feste',
    zeitserver: 'ntp',
  },
}

export const stop = new Set(
  'a an the to of in on is are do does not no can my i how what why with der die das ein eine nicht kein keine geht klappt funktioniert ist sind wie was warum mit im am zu'.split(
    ' ',
  ),
)

export function words(query: string, lang: Lang) {
  const all = query
    .toLowerCase()
    .split(/\s+/)
    .filter(Boolean)
    .map((w) => synonyms[lang][w] ?? w)
  const kept = all.filter((w) => !stop.has(w))
  return [...new Set(kept.length ? kept : all)]
}
