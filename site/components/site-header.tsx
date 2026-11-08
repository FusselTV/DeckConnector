'use client'
import { useSyncExternalStore, type ReactNode } from 'react'
import { createPortal } from 'react-dom'
import { ArrowLeft, BookOpen, Check, Languages, Menu, PanelLeft, Moon, MonitorPlay, Search, Sun } from 'lucide-react'
import { useTheme } from 'next-themes'
import { useI18n } from 'fumadocs-ui/contexts/i18n'
import { useSearchContext } from 'fumadocs-ui/contexts/search'
import { Popover, PopoverContent, PopoverTrigger } from 'fumadocs-ui/components/ui/popover'
import { DocsLayout } from 'fumadocs-ui/layouts/docs'
import { SidebarTrigger } from 'fumadocs-ui/layouts/docs/slots/sidebar'
import type * as PageTree from 'fumadocs-core/page-tree'
import { useSelectedLayoutSegments } from 'next/navigation'
import { prefix, type Lang } from '~/lib/i18n'
import { repo } from '~/site.config'
import { github } from './github'

const t = {
  en: { docs: 'Docs', flash: 'Flash', demo: 'Demo', home: 'DeckConnector home', search: 'Search', language: 'Language', light: 'Light mode', dark: 'Dark mode', start: 'Home', menu: 'Menu', nav: 'Main' },
  de: { docs: 'Anleitung', flash: 'Flashen', demo: 'Demo', home: 'DeckConnector Startseite', search: 'Suchen', language: 'Sprache', light: 'Hell darstellen', dark: 'Dunkel darstellen', start: 'Startseite', menu: 'Menü', nav: 'Hauptmenü' },
}

export const icon = 'inline-flex size-9 shrink-0 items-center justify-center rounded-lg transition-colors hover:bg-fd-accent [&_svg]:size-4'
const link = 'flex h-9 items-center rounded-lg px-3 text-sm font-medium text-fd-muted-foreground transition-colors hover:bg-fd-accent hover:text-fd-foreground aria-[current=page]:bg-fd-accent aria-[current=page]:text-fd-foreground'

export function SiteHeader({ lang }: { lang: Lang }) {
  const s = t[lang]
  const p = prefix(lang)
  // The route, not the address: a 404 page hydrates under any address and must render as it was built.
  const path = `/${useSelectedLayoutSegments().join('/')}/`
  const docs = path.includes('/docs/')
  const flash = path.startsWith(`${p}/flash/`)
  const { setOpenSearch } = useSearchContext()
  const links: [string, string, ReactNode][] = [
    [s.docs, `${p}/docs/`, <BookOpen key="d" />],
    [s.demo, `${p}/demo/`, <MonitorPlay key="m" />],
  ]
  const current = (href: string) => (path.startsWith(href) ? 'page' : undefined)
  return (
    <header className="sticky top-0 z-40 h-14 border-b bg-fd-background/85 backdrop-blur-md">
      <div className="mx-auto flex h-full max-w-(--fd-layout-width) items-center gap-1 px-4 sm:px-6">
        <a href={`${p}/`} aria-label={s.home} title={s.home} className="me-auto flex items-center gap-2 rounded-lg transition-opacity hover:opacity-80 md:me-4">
          <Logo />
          <span className="text-[15px] font-semibold tracking-tight">DeckConnector</span>
        </a>
        <nav aria-label={s.nav} className="flex items-center gap-1 max-md:hidden">
          {links.map(([text, href]) => (
            <a key={href} href={href} aria-current={current(href)} className={link}>
              {text}
            </a>
          ))}
          <a href={repo} className={link}>
            GitHub
          </a>
        </nav>
        <span className="flex-1 max-md:hidden" />
        <button type="button" onClick={() => setOpenSearch(true)} aria-label={s.search} title={`${s.search} (Ctrl K)`} className={`${icon} lg:w-44 lg:justify-start lg:gap-2 lg:border lg:bg-fd-secondary/50 lg:px-2.5 lg:text-sm lg:text-fd-muted-foreground`}>
          <Search aria-hidden />
          <span className="max-lg:hidden">{s.search}</span>
          <kbd className="ms-auto font-mono text-[11px] max-lg:hidden">Ctrl K</kbd>
        </button>
        <span className="contents max-md:hidden">
          <LanguageButton lang={lang} label={s.language} />
          <ThemeButton light={s.light} dark={s.dark} />
        </span>
        <MobileMenu lang={lang} links={links} current={current} />
        {docs && <span id="docs-menu" className="md:hidden" />}
        <a href={`${p}/flash/`} aria-current={flash ? 'page' : undefined} className="ms-1 inline-flex h-9 items-center gap-1.5 rounded-lg bg-amber px-3.5 text-sm font-semibold text-amber-ink shadow-[inset_0_1px_0_rgb(255_255_255/0.35)] transition-[filter] hover:brightness-95 focus-visible:outline-2 focus-visible:outline-offset-2 focus-visible:outline-(--focus) [&_svg]:size-4">
          {s.flash}
        </a>
      </div>
    </header>
  )
}

export function Logo() {
  return (
    <svg viewBox="0 0 24 24" className="size-6" aria-hidden>
      <rect x="1" y="3" width="22" height="18" rx="4.5" className="fill-fd-foreground" />
      {[0, 1, 2].map((c) =>
        [0, 1].map((r) => <rect key={`${c}${r}`} x={4.5 + c * 5.5} y={6.5 + r * 5.5} width="4" height="4" rx="1" className={c === 0 && r === 0 ? 'fill-amber' : 'fill-fd-background'} />),
      )}
    </svg>
  )
}

function MobileMenu({ lang, links, current }: { lang: Lang; links: [string, string, ReactNode][]; current: (href: string) => 'page' | undefined }) {
  const s = t[lang]
  const { locales = [], onChange } = useI18n()
  const { resolvedTheme, setTheme } = useTheme()
  const row = 'flex h-11 items-center gap-3 rounded-lg px-3 text-[15px] font-medium transition-colors hover:bg-fd-accent [&_svg]:size-4 [&_svg]:text-fd-muted-foreground'
  return (
    <Popover>
      <PopoverTrigger aria-label={s.menu} title={s.menu} className={`${icon} md:hidden`}>
        <Menu aria-hidden />
      </PopoverTrigger>
      <PopoverContent align="end" className="flex w-60 min-w-0 flex-col gap-0.5 p-1.5">
        {links.map(([text, href, svg]) => (
          <a key={href} href={href} aria-current={current(href)} className={`${row} aria-[current=page]:bg-fd-accent`}>
            {svg}
            {text}
          </a>
        ))}
        <a href={repo} className={row}>
          {github}
          GitHub
        </a>
        <div className="my-1 h-px bg-fd-border" />
        <div className="flex gap-1 px-1 py-1" role="group" aria-label={s.language}>
          {locales.map((l) => (
            <button key={l.locale} type="button" aria-pressed={l.locale === lang} onClick={() => onChange?.(l.locale)} className="h-10 flex-1 rounded-lg border text-sm font-medium transition-colors aria-pressed:border-transparent aria-pressed:bg-fd-accent aria-pressed:text-fd-foreground">
              {l.name}
            </button>
          ))}
        </div>
        <button type="button" onClick={() => setTheme(resolvedTheme === 'light' ? 'dark' : 'light')} className={row}>
          <Sun className="dark:hidden" aria-hidden />
          <Moon className="hidden dark:block" aria-hidden />
          {resolvedTheme === 'light' ? s.dark : s.light}
        </button>
      </PopoverContent>
    </Popover>
  )
}

function LanguageButton({ lang, label }: { lang: Lang; label: string }) {
  const { locales = [], onChange } = useI18n()
  return (
    <Popover>
      <PopoverTrigger aria-label={label} title={label} className={icon}>
        <Languages aria-hidden />
      </PopoverTrigger>
      <PopoverContent align="end" className="flex w-44 min-w-0 flex-col gap-0.5 p-1">
        {locales.map((l) => (
          <button key={l.locale} type="button" onClick={() => onChange?.(l.locale)} className="flex items-center rounded-md px-2 py-1.5 text-start text-sm transition-colors hover:bg-fd-accent">
            {l.name}
            {l.locale === lang && <Check className="ms-auto size-4" aria-hidden />}
          </button>
        ))}
      </PopoverContent>
    </Popover>
  )
}

function ThemeButton({ light, dark }: { light: string; dark: string }) {
  const { resolvedTheme, setTheme } = useTheme()
  const label = resolvedTheme === 'light' ? dark : light
  return (
    <button type="button" onClick={() => setTheme(resolvedTheme === 'light' ? 'dark' : 'light')} aria-label={label} title={label} className={`${icon} relative`}>
      <Sun className="scale-0 rotate-90 transition-transform duration-500 dark:scale-100 dark:rotate-0" aria-hidden />
      <Moon className="absolute scale-100 rotate-0 transition-transform duration-500 dark:scale-0 dark:-rotate-90" aria-hidden />
    </button>
  )
}

// Logo, search, language and theme are in the site header, so the sidebar shows only the page tree.
export function Docs({ lang, tree, children }: { lang: Lang; tree: PageTree.Root; children: ReactNode }) {
  const home = (
    <a href={`${prefix(lang)}/`} className="me-auto flex items-center gap-2 rounded-lg p-2 text-sm text-fd-muted-foreground transition-colors hover:bg-fd-accent/50 hover:text-fd-accent-foreground">
      <ArrowLeft className="size-4" aria-hidden />
      {t[lang].start}
    </a>
  )
  return (
    <DocsLayout tree={tree} nav={{ component: <DocsMenu />, children: home }} slots={{ navTitle: () => null, languageSelect: false, themeSwitch: false, searchTrigger: false }}>
      {children}
    </DocsLayout>
  )
}

const noop = () => () => undefined

// DocsLayout owns the sidebar state, so its trigger renders inside it and portals into the site header.
function DocsMenu() {
  const target = useSyncExternalStore(noop, () => document.getElementById('docs-menu'), () => null)
  if (!target) return null
  return createPortal(
    <SidebarTrigger className={icon}>
      <PanelLeft aria-hidden />
    </SidebarTrigger>,
    target,
  )
}
