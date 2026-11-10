import { armAtom, langAtom, themeAtom, toast } from '../app/state'
import { de } from '../i18n/de'
import { en } from '../i18n/en'
import { view } from './model'
import './demo.css'

// The site and the demo share an origin: the theme travels through next-themes' "theme" key (a storage event in
// the other document), the language through the URL of the site page around the demo.
addEventListener('storage', (e) => {
  if (e.key === 'theme' && (e.newValue === 'light' || e.newValue === 'dark')) themeAtom.set(e.newValue)
})
themeAtom.subscribe(() => {
  const v = themeAtom.get()
  if (v && localStorage.getItem('theme') !== v) localStorage.setItem('theme', v)
})

view.lang = langAtom.get()
langAtom.subscribe(() => {
  view.lang = langAtom.get()
  if (window.parent !== window) window.parent.postMessage({ demoLang: view.lang }, location.origin)
})

// The device's warning that presses reach a real host would alarm here: the demo deck is virtual.
Object.assign(en, {
  'ov.real': 'Remote control is on: taps go to the virtual deck.',
  'ov.real_ui': 'Remote control is on: taps go to the virtual deck menu.',
})
Object.assign(de, {
  'ov.real': 'Fernsteuerung ist an: Eingaben gehen an das virtuelle Deck.',
  'ov.real_ui': 'Fernsteuerung ist an: Eingaben gehen an das Menü des virtuellen Decks.',
})

// Remote control starts on and stays on while it is on: the demo has no host a stray press could reach.
armAtom.set(Date.now())
setInterval(() => armAtom.get() && armAtom.set(Date.now()), 20_000)

// The example addresses in the demo exist only on a real network.
const PRIVATE = /^(192\.168\.|10\.|172\.(1[6-9]|2\d|3[01])\.)|\.local$/
document.addEventListener(
  'click',
  (e) => {
    const a = (e.target as Element | null)?.closest('a[href]')
    if (!(a instanceof HTMLAnchorElement) || !/^https?:$/.test(a.protocol) || !PRIVATE.test(a.hostname)) return
    e.preventDefault()
    toast(
      langAtom.get() === 'de'
        ? `Demo: ${a.host} gibt es nur in deinem Netzwerk.`
        : `Demo: ${a.host} only exists on your own network.`,
    )
  },
  true,
)
