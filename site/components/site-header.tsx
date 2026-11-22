'use client'
import { Fragment, useEffect, useSyncExternalStore, type ReactNode } from 'react'
import { flushSync } from 'react-dom'
import { BookOpen, Check, Languages, Menu, Moon, MonitorPlay, Search, Sun } from 'lucide-react'
import { useTheme } from 'next-themes'
import { useI18n } from 'fumadocs-ui/contexts/i18n'
import { useSearchContext } from 'fumadocs-ui/contexts/search'
import { Popover, PopoverContent, PopoverTrigger } from 'fumadocs-ui/components/ui/popover'
import { useSelectedLayoutSegments } from 'next/navigation'
import { prefix, type Lang } from '~/lib/i18n'
import { repo } from '~/site.config'
import { github } from './github'

const t = {
  en: {
    docs: 'Docs',
    flash: 'Set it up',
    demo: 'Demo',
    home: 'DeckConnector home',
    search: 'Search',
    language: 'Language',
    light: 'Light mode',
    dark: 'Dark mode',
    start: 'Home',
    menu: 'Menu',
    nav: 'Main',
    starOn: 'Star on GitHub',
  },
  de: {
    docs: 'Anleitung',
    flash: 'Einrichten',
    demo: 'Demo',
    home: 'Startseite von DeckConnector',
    search: 'Suchen',
    language: 'Sprache',
    light: 'Helles Design',
    dark: 'Dunkles Design',
    start: 'Startseite',
    menu: 'Menü',
    nav: 'Hauptmenü',
    starOn: 'Star auf GitHub',
  },
}

export const noop = () => () => undefined

export const icon =
  'inline-flex size-11 lg:size-9 shrink-0 items-center justify-center rounded-lg transition-colors hover:bg-fd-accent [&_svg]:size-4'
const link =
  'relative flex h-11 lg:h-9 items-center rounded-lg px-3 text-sm font-medium text-fd-muted-foreground transition-colors hover:text-fd-foreground aria-[current=page]:text-fd-foreground'

export function SiteHeader({ lang }: { lang: Lang }) {
  const s = t[lang]
  const p = prefix(lang)
  // The route, not the address: a 404 page hydrates under any address and must render as it was built.
  const path = `/${useSelectedLayoutSegments().join('/')}/`
  const docs = path.includes('/docs/')
  const flash = path.startsWith(`${p}/flash/`)
  const { setOpenSearch, hotKey } = useSearchContext()
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      const el = e.target as HTMLElement
      if (
        e.key !== '/' ||
        e.ctrlKey ||
        e.metaKey ||
        e.altKey ||
        e.defaultPrevented ||
        el.isContentEditable ||
        el.closest('input, textarea, select, [role=dialog]')
      )
        return
      e.preventDefault()
      setOpenSearch(true)
    }
    window.addEventListener('keydown', onKey)
    return () => window.removeEventListener('keydown', onKey)
  }, [setOpenSearch])
  useEffect(() => {
    let last = scrollY
    const onScroll = () => {
      const moved = scrollY - last
      if (Math.abs(moved) < 8) return
      document.documentElement.toggleAttribute('data-header-hidden', moved > 0 && scrollY > 120)
      last = scrollY
    }
    addEventListener('scroll', onScroll, { passive: true })
    return () => removeEventListener('scroll', onScroll)
  }, [])
  const links: [string, string, ReactNode][] = [
    [s.docs, `${p}/docs/`, <BookOpen key="d" />],
    [s.demo, `${p}/demo/`, <MonitorPlay key="m" />],
  ]
  const current = (href: string) => (path.startsWith(href) ? 'page' : undefined)
  return (
    <header className="sticky top-0 z-40 h-14 max-md:h-24 border-b bg-fd-background/95 backdrop-blur-md">
      <div className="mx-auto flex h-14 max-w-(--fd-layout-width) items-center gap-1 px-4 sm:px-6">
        <a
          href={`${p}/`}
          aria-label={s.home}
          title={s.home}
          className="me-auto flex h-11 min-w-11 items-center gap-2 rounded-lg transition-opacity hover:opacity-80 md:me-4"
        >
          <Logo />
          <span data-wordmark className="text-[15px] font-semibold tracking-tight max-[380px]:sr-only">
            DeckConnector
          </span>
        </a>
        <nav aria-label={s.nav} className="flex items-center gap-1 max-md:hidden">
          {links.map(([text, href]) => (
            <a key={href} href={href} aria-current={current(href)} data-nav className={link}>
              {text}
            </a>
          ))}
          <a
            href={repo}
            aria-label={s.starOn}
            title={s.starOn}
            className="ms-1 inline-flex h-11 items-center gap-1.5 rounded-lg border bg-fd-secondary/50 px-2.5 text-sm font-medium text-fd-muted-foreground transition-colors hover:text-fd-foreground lg:h-9 [&_svg]:size-4"
          >
            {github}
            Star
          </a>
        </nav>
        <span className="flex-1 max-md:hidden" />
        {docs && <span id="docs-menu" className="size-11 md:hidden" />}
        <button
          type="button"
          onClick={() => setOpenSearch(true)}
          aria-label={s.search}
          title={`${s.search} (/)`}
          className={`${icon} lg:w-44 lg:justify-start lg:gap-2 lg:border lg:bg-fd-secondary/50 lg:px-2.5 lg:text-sm lg:text-fd-muted-foreground`}
        >
          <Search aria-hidden />
          <span className="max-lg:hidden">{s.search}</span>
          <kbd className="ms-auto flex gap-1 font-mono text-[11px] max-lg:hidden">
            {hotKey.map((k, i) => (
              <Fragment key={i}>{k.display}</Fragment>
            ))}
          </kbd>
        </button>
        <span className="contents max-md:hidden">
          <LanguageButton lang={lang} label={s.language} />
          <ThemeButton light={s.light} dark={s.dark} />
        </span>
        <MobileMenu lang={lang} links={links} current={current} />
        <a
          href={`${p}/flash/`}
          aria-current={flash ? 'page' : undefined}
          className="relative ms-1 inline-flex h-9 min-w-24 items-center justify-center before:absolute before:-inset-y-1 before:inset-x-0 gap-1.5 whitespace-nowrap rounded-lg bg-amber px-3.5 max-[380px]:px-2.5 text-sm font-semibold text-amber-ink shadow-[inset_0_1px_0_rgb(255_255_255/0.35)] transition-[filter] hover:brightness-95 focus-visible:outline-2 focus-visible:outline-offset-2 focus-visible:outline-(--focus) [&_svg]:size-4"
        >
          {s.flash}
        </a>
      </div>
      <nav aria-label={s.nav} data-subnav className="flex h-10 items-stretch justify-center gap-8 border-t md:hidden">
        {links.map(([text, href, svg]) => (
          <a
            key={href}
            href={href}
            aria-current={current(href)}
            data-nav
            className="relative flex items-center gap-2 px-2 text-sm font-medium text-fd-muted-foreground aria-[current=page]:text-fd-foreground [&_svg]:size-4"
          >
            {svg}
            {text}
          </a>
        ))}
      </nav>
    </header>
  )
}

// The same mark as favicon.svg and the web UI's Logo: fixed colours, one version for light and dark.
export function Logo() {
  return (
    <svg viewBox="0 0 32 32" className="size-7 shrink-0" aria-hidden>
      <rect x=".5" y=".5" width="31" height="31" rx="7.5" fill="#1b1e22" stroke="#3b4148" />
      {[0, 1].map((r) =>
        [0, 1, 2].map((c) => (
          <rect
            key={`${r}${c}`}
            x={6 + c * 7.2}
            y={5 + r * 7.2}
            width="5.2"
            height="5.2"
            rx="1.2"
            fill={r + c ? '#e8e6e1' : '#ffb020'}
          />
        )),
      )}
      <rect x="13.5" y="19.8" width="5" height="5" rx="1" fill="#ffb020" />
      <path d="M16 24.8V32" stroke="#ffb020" strokeWidth="2.4" />
    </svg>
  )
}

function MobileMenu({
  lang,
  links,
  current,
}: {
  lang: Lang
  links: [string, string, ReactNode][]
  current: (href: string) => 'page' | undefined
}) {
  const s = t[lang]
  const { locales = [], onChange } = useI18n()
  const { resolvedTheme, setTheme } = useTheme()
  const row =
    'flex h-11 items-center gap-3 rounded-lg px-3 text-[15px] font-medium transition-colors hover:bg-fd-accent [&_svg]:size-4 [&_svg]:text-fd-muted-foreground'
  return (
    <Popover>
      <PopoverTrigger aria-label={s.menu} title={s.menu} className={`${icon} md:hidden`}>
        <Menu aria-hidden />
      </PopoverTrigger>
      <PopoverContent align="end" className="flex w-60 min-w-0 bg-fd-popover backdrop-blur-none flex-col gap-0.5 p-1.5">
        {links.map(([text, href, svg]) => (
          <a key={href} href={href} aria-current={current(href)} className={`${row} aria-[current=page]:bg-fd-accent`}>
            {svg}
            {text}
          </a>
        ))}
        <a href={repo} className={row}>
          {github}
          {s.starOn}
        </a>
        <div className="my-1 h-px bg-fd-border" />
        <div className="flex gap-1 px-1 py-1" role="group" aria-label={s.language}>
          {locales.map((l) => (
            <button
              key={l.locale}
              type="button"
              aria-pressed={l.locale === lang}
              onClick={() => onChange?.(l.locale)}
              className="h-11 flex-1 rounded-lg border text-sm font-medium transition-colors aria-pressed:border-transparent aria-pressed:bg-fd-accent aria-pressed:text-fd-foreground"
            >
              {l.name}
            </button>
          ))}
        </div>
        <button
          type="button"
          onClick={(e) => switchTheme(e.currentTarget, resolvedTheme === 'light' ? 'dark' : 'light', setTheme)}
          className={row}
        >
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
      <PopoverContent align="end" className="flex w-44 min-w-0 bg-fd-popover backdrop-blur-none flex-col gap-0.5 p-1">
        {locales.map((l) => (
          <button
            key={l.locale}
            type="button"
            onClick={() => onChange?.(l.locale)}
            className="flex items-center rounded-md px-2 py-1.5 text-start text-sm transition-colors hover:bg-fd-accent"
          >
            {l.name}
            {l.locale === lang && <Check className="ms-auto size-4" aria-hidden />}
          </button>
        ))}
      </PopoverContent>
    </Popover>
  )
}

// The new theme grows from the button that switched it; without View Transitions or with reduced motion it switches at once.
function switchTheme(from: HTMLElement, next: string, setTheme: (t: string) => void) {
  document.documentElement.toggleAttribute('data-theme-switched', true)
  if (!('startViewTransition' in document) || matchMedia('(prefers-reduced-motion: reduce)').matches)
    return setTheme(next)
  const r = from.getBoundingClientRect()
  const x = r.left + r.width / 2
  const y = r.top + r.height / 2
  const root = document.documentElement.style
  root.setProperty('--vt-x', `${x}px`)
  root.setProperty('--vt-y', `${y}px`)
  root.setProperty('--vt-r', `${Math.hypot(Math.max(x, innerWidth - x), Math.max(y, innerHeight - y))}px`)
  // next-themes puts the class on <html> in an effect, and the transition must see it before it takes the new snapshot
  document.startViewTransition(() => flushSync(() => setTheme(next)))
}

function ThemeButton({ light, dark }: { light: string; dark: string }) {
  const { resolvedTheme, setTheme } = useTheme()
  // The server does not know the theme, so the label waits for the client to avoid a hydration mismatch.
  const mounted = useSyncExternalStore(
    noop,
    () => true,
    () => false,
  )
  const label = mounted && resolvedTheme === 'light' ? dark : light
  return (
    <button
      type="button"
      onClick={(e) => switchTheme(e.currentTarget, resolvedTheme === 'light' ? 'dark' : 'light', setTheme)}
      aria-label={label}
      title={label}
      className={icon}
    >
      <Sun className="theme-icon hidden dark:block" aria-hidden />
      <Moon className="theme-icon dark:hidden" aria-hidden />
    </button>
  )
}

// Logo, search, language and theme are in the site header, so the sidebar shows only the page tree.
