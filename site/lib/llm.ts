import type { Node, Root } from 'fumadocs-core/page-tree'
import { withVersion } from './version'
import { compare } from '~/components/compare/data'
import { out as links } from '~/components/landing/links'
import { repo, site } from '~/site.config'
import { prefix, type Lang } from './i18n'
import { source } from './source'

type Page = NonNullable<ReturnType<typeof source.getPage>>

/** Where the Markdown copy of a docs page lives, e.g. /md/docs/flashing.md or /md/de/docs/index.md. */
export const mdPath = (lang: Lang, slugs: string[]) => `/md${prefix(lang)}/docs/${slugs.join('/') || 'index'}.md`

const abs = (url: string) => {
  if (!url.startsWith('/')) return url
  const i = url.search(/[?#]|$/)
  const path = url.slice(0, i)
  return `${site}${path.endsWith('/') || /\.\w+$/.test(path) ? path : `${path}/`}${url.slice(i)}`
}

const attr = (tag: string, name: string) => tag.match(new RegExp(`${name}=["']([^"']*)["']`))?.[1]

// Turns the MDX components the docs use into plain Markdown. Unknown components (diagrams) are dropped.
function mdx(body: string) {
  return body
    .split(/(^```[\s\S]*?^```)/m)
    .map((part, i) => {
      if (i % 2) return part
      return part
        .replace(/^<Callout([^>]*)>\n?([\s\S]*?)\n?<\/Callout>/gm, (_, a: string, text: string) => {
          const head = attr(a, 'title') ?? (attr(a, 'type') === 'warn' ? 'Warning' : 'Note')
          return [`**${head}**`, '', ...text.trim().split('\n')].map((l) => `> ${l}`.trimEnd()).join('\n')
        })
        .replace(/^[ \t]*<Tab value=["']([^"']+)["']>[ \t]*$/gm, '**$1**')
        .replace(/^[ \t]*<Card\b([^>]*?)\/>[ \t]*$/gm, (_, a: string) => {
          const d = attr(a, 'description')
          return `- [${attr(a, 'title')}](${attr(a, 'href')})${d ? `: ${d}` : ''}`
        })
        .replace(/^[ \t]*<Next\b([\s\S]*?)\/>[ \t]*$/gm, (_, a: string) => {
          const links = [...a.matchAll(/\[\s*['"]([^'"]+)['"]\s*,\s*['"]([^'"]+)['"]\s*\]/g)]
          return links.length ? `Next: ${links.map(([, t, u]) => `[${t}](${u})`).join(', ')}` : ''
        })
        .replace(/^[ \t]*<Buttons\b([\s\S]*?)\/>[ \t]*$/gm, (_, a: string) =>
          [...a.matchAll(/\[\s*['"]([^'"]+)['"]\s*,\s*['"]([^'"]+)['"]\s*\]/g)]
            .map(([, t, u]) => `- [${t}](${u})`)
            .join('\n'),
        )
        .replace(/^[ \t]*<BenchBars\b([\s\S]*?)\/>[ \t]*$/gm, (_, a: string) => {
          const de = attr(a, 'lang') === 'de'
          const unit = attr(a, 'unit') ?? ''
          const n = (v: string) => `${de ? v.replace('.', ',') : v} ${unit}`
          const rows = [...a.matchAll(/\[\s*['"]([^'"]+)['"]\s*,\s*([\d.]+)\s*,\s*([\d.]+)\s*\]/g)]
          return [
            `**${attr(a, 'title') ?? ''}**`,
            '',
            '| | Deck Connector | Elgato Network Dock |',
            '|---|---|---|',
            ...rows.map(([, l, x, y]) => `| ${l} | ${n(x ?? '')} | ${n(y ?? '')} |`),
          ].join('\n')
        })
        .replace(/^[ \t]*<[A-Z][\s\S]*?\/>[ \t]*$/gm, '')
        .replace(/^[ \t]*<\/?[A-Z]\w*[^>]*>[ \t]*$/gm, '')
        .replace(/\]\((\/[^)\s]*)\)/g, (_, u: string) => `](${abs(u)})`)
    })
    .join('')
    .replace(/\n{3,}/g, '\n\n')
    .trim()
}

const demote = (md: string) =>
  md
    .split(/(^```[\s\S]*?^```)/m)
    .map((part, i) => (i % 2 ? part : part.replace(/^(#+) /gm, '#$1 ')))
    .join('')

export async function pageMarkdown(page: Page) {
  const raw = withVersion(await page.data.getText('raw'))
  const body = raw.replace(/^---\n[\s\S]*?\n---\n/, '')
  return `# ${page.data.title}\n\n> ${page.data.description}\n\nSource: ${abs(`${page.url}/`)}\n\n${mdx(body)}\n`
}

/** Docs pages in sidebar order, grouped by the sidebar's separators. */
function sections(tree: Root, lang: Lang) {
  let cur: { name: string; pages: Page[] } = { name: 'Docs', pages: [] }
  const out = [cur]
  const walk = (nodes: Node[]) => {
    for (const n of nodes) {
      if (n.type === 'separator') out.push((cur = { name: typeof n.name === 'string' ? n.name : 'Docs', pages: [] }))
      else if (n.type === 'folder') walk(n.children)
      else {
        const page = source.getNodePage(n, lang)
        if (page) cur.pages.push(page)
      }
    }
  }
  walk(tree.children)
  const seen = new Set(out.flatMap((s) => s.pages))
  out.push({ name: 'More docs', pages: source.getPages(lang).filter((p) => !seen.has(p)) })
  return out.filter((s) => s.pages.length)
}

const c = compare.en

const about = `# Deck Connector

> Deck Connector is free, open source firmware (AGPL-3.0) for the Waveshare ESP32-P4-WIFI6-POE-ETH board. The board connects to an Elgato Stream Deck over USB and puts the deck on your network over Wi-Fi or Ethernet. Bitfocus Companion, the Elgato app and your own software then use the deck as if it were plugged into the computer. It is an alternative to the Elgato Network Dock, and adds Wi-Fi.

## Key facts

- Hardware: Waveshare ESP32-P4-WIFI6-POE-ETH board, about $25 to $35 (https://www.waveshare.com/esp32-p4-wifi6-poe-eth.htm). Needs ESP32-P4 chip revision v3.1 or newer. No case. The deck plugs into the board's USB-A port.
- Network: Wi-Fi (2.4 GHz only) and 100 Mbit/s Ethernet at the same time. If the cable fails, connections move to Wi-Fi by themselves.
- Power: a USB-C power supply, or PoE from the network cable. PoE is optional.
- Bitfocus Companion: over the Satellite API (Companion 5.2 tested), or as an emulated Elgato Network Dock.
- Elgato app: finds the board as a Network Dock by itself (app 7.6 on Windows tested).
- Your own software: open WebSocket protocol and a JSON HTTP API.
- Optional: a Home Assistant integration from HACS, and Matter for Apple Home, Google Home and other Matter apps. The deck works without them.
- Several computers at once: Companion over Satellite, Network Dock hosts (Companion, Elgato app) and your own servers share one list. Up to 2 servers and 4 dock hosts stay connected at once. One draws. Two taps on the deck switch between them.
- Languages: the web UI and the screens on the deck are in English and German.
- Decks: most Elgato Stream Decks, and with Companion also Mirabox and Ajazz Stream Docks. Only the Stream Deck + is tested on real hardware. The others pass tests on emulated decks and should work. Every model: ${site}/docs/hardware/#decks
- Install: open ${site}/flash/ in Chrome or Edge, connect the board's USB-C port, flash (about a minute), then join Wi-Fi over USB. No tools to install.
- Updates: signed firmware files, installed from the web UI. New firmware that does not start rolls back by itself.
- Speed: the same as the Elgato Network Dock or faster, measured on a bench with a Stream Deck +.
- Not affiliated with Elgato, Corsair or Bitfocus. Elgato and Stream Deck are trademarks of Corsair Memory, Inc. Community project, no official support.
- Source code: ${repo}
- Official sources: this site (${site}) and the GitHub repository. Firmware comes only from its releases (${repo}/releases) and the web flasher on this site. Other sites offering Deck Connector downloads are not from this project.

## How to install

1. Buy the Waveshare ESP32-P4-WIFI6-POE-ETH board.
2. Open the web flasher at ${site}/flash/ in Chrome or Edge, plug the board's USB-C port into the computer and flash. The flasher can join your Wi-Fi right away.
3. Plug the Stream Deck into the board's USB-A port. Power the board over USB-C or PoE. With Wi-Fi set in the flasher, or a network cable, the deck shows the board's address and a QR code. Without a network, the board opens its own setup Wi-Fi one minute after the start, and the deck shows how to join it.
4. Open that address in a browser. The setup wizard asks for name, password, network and the program that controls the deck.
5. Connect a program. Companion: in the web UI under Sources, enter the Companion address (Satellite), or add the board in Companion as a Network Dock. Elgato app: it finds the board as a Network Dock by itself.

## Deck Connector vs Elgato Network Dock

| | Deck Connector | Elgato Network Dock |
|---|---|---|
${c.rows.map((r) => `| ${r.k} | ${r.a[1]} | ${r.b[1]} |`).join('\n')}

Dock facts from the Elgato product page (${links('en').dock}), November 2026. Speed and power-cut results from the Deck Connector bench with a Stream Deck +.

Where the dock is better:
${c.dockWins.items.map(([t, d]) => `- ${t}: ${d}`).join('\n')}

## Questions

${c.faq.items.map(([q, a]) => `### ${q}\n\n${a}`).join('\n\n')}
`

const link = (p: Page, lang: Lang) => `- [${p.data.title}](${site}${mdPath(lang, p.slugs)}): ${p.data.description}`

export function llmsTxt() {
  const docs = sections(source.getPageTree('en'), 'en')
    .map((s) => `## ${s.name}\n\n${s.pages.map((p) => link(p, 'en')).join('\n')}`)
    .join('\n\n')
  const de = sections(source.getPageTree('de'), 'de').flatMap((s) => s.pages.map((p) => link(p, 'de')))
  return `${about}
## Pages

- [Compare with the Elgato Network Dock](${site}/compare/): the full comparison table, with sources.
- [Web flasher](${site}/flash/): install the firmware from Chrome or Edge.
- [Live demo](${site}/demo/): the real web UI with a virtual Stream Deck +, no device needed.
- [All docs as one file](${site}/llms-full.txt)

${docs}

## Optional

German pages (Deutsch):

- [Startseite](${site}/de/)
- [Vergleich mit dem Elgato Network Dock](${site}/de/compare/)
${de.join('\n')}
`
}

export async function llmsFullTxt() {
  const pages = sections(source.getPageTree('en'), 'en').flatMap((s) => s.pages)
  const docs = await Promise.all(pages.map((p) => pageMarkdown(p).then(demote)))
  return `${about}\n# Documentation\n\n${docs.join('\n')}`
}
