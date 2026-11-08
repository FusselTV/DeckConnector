'use client'
import { useSyncExternalStore, type ReactNode } from 'react'
import { createPortal } from 'react-dom'
import { ArrowLeft, BookOpen, Check, Languages, LayoutGrid, Menu, Moon, MonitorPlay, Search, Sun, Usb } from 'lucide-react'
import { useTheme } from 'next-themes'
import { useI18n } from 'fumadocs-ui/contexts/i18n'
import { useSearchContext } from 'fumadocs-ui/contexts/search'
import { Popover, PopoverContent, PopoverTrigger } from 'fumadocs-ui/components/ui/popover'
import { DocsLayout } from 'fumadocs-ui/layouts/docs'
import { SidebarTrigger } from 'fumadocs-ui/layouts/docs/slots/sidebar'
import type * as PageTree from 'fumadocs-core/page-tree'
import { usePathname } from 'next/navigation'
import { prefix, type Lang } from '~/lib/i18n'
import { repo } from '~/site.config'
import { github } from './github'

const t = {
  en: { docs: 'Docs', flash: 'Flash', demo: 'Demo', home: 'DeckConnector home', search: 'Search', language: 'Language', light: 'Light mode', dark: 'Dark mode', start: 'Home' },
  de: { docs: 'Anleitung', flash: 'Flashen', demo: 'Demo', home: 'DeckConnector Startseite', search: 'Suchen', language: 'Sprache', light: 'Hell darstellen', dark: 'Dunkel darstellen', start: 'Startseite' },
}

export const icon = 'inline-flex size-8 shrink-0 sm:size-9 items-center justify-center rounded-md transition-colors hover:bg-fd-accent [&_svg]:size-4'

export function SiteHeader({ lang }: { lang: Lang }) {
  const s = t[lang]
  const p = prefix(lang)
  const path = usePathname()
  const docs = path.includes('/docs/')
  const { setOpenSearch } = useSearchContext()
  const links: [string, string, ReactNode][] = [
    [s.docs, `${p}/docs/`, <BookOpen key="d" />],
    [s.flash, `${p}/flash/`, <Usb key="f" />],
    [s.demo, `${p}/demo/`, <MonitorPlay key="m" />],
  ]
  return (
    <header className="sticky top-0 z-40 h-14 border-b bg-fd-background/90 backdrop-blur">
      <div className="mx-auto flex h-full max-w-(--fd-layout-width) items-center gap-0.5 px-3 sm:gap-2 sm:px-4">
        <a href={`${p}/`} aria-label={s.home} title={s.home} className="me-1 flex items-center gap-2 rounded-md transition-opacity hover:opacity-80 sm:me-3">
          <LayoutGrid className="size-5 text-fd-primary" aria-hidden />
          <span className="font-semibold tracking-tight max-sm:hidden">DeckConnector</span>
        </a>
        <nav className="flex items-center gap-0.5">
          {links.map(([text, href, svg]) => (
            <a key={href} href={href} data-active={path.startsWith(href)} className="flex h-9 items-center gap-2 rounded-md px-1.5 text-sm font-medium text-fd-muted-foreground transition-colors hover:bg-fd-accent hover:text-fd-foreground data-[active=true]:text-fd-primary sm:px-2.5 [&_svg]:size-4 [&_svg]:max-md:hidden">
              {svg}
              {text}
            </a>
          ))}
        </nav>
        <span className="flex-1" />
        <button type="button" onClick={() => setOpenSearch(true)} aria-label={s.search} title={`${s.search} (Ctrl K)`} className={`${icon} md:w-40 md:justify-start md:gap-2 md:border md:bg-fd-secondary/50 md:px-2.5 md:text-sm md:text-fd-muted-foreground`}>
          <Search aria-hidden />
          <span className="max-md:hidden">{s.search}</span>
        </button>
        <LanguageButton lang={lang} label={s.language} />
        <ThemeButton light={s.light} dark={s.dark} />
        <a href={repo} aria-label="GitHub" title="GitHub" className={`${icon} max-sm:hidden`}>
          {github}
        </a>
        {docs && <span id="docs-menu" className="md:hidden" />}
      </div>
    </header>
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
      <Menu aria-hidden />
    </SidebarTrigger>,
    target,
  )
}
