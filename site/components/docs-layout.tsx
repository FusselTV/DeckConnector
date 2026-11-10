'use client'
import { useEffect, useSyncExternalStore, type ReactNode } from 'react'
import { createPortal } from 'react-dom'
import { ArrowLeft, PanelLeft } from 'lucide-react'
import { DocsLayout } from 'fumadocs-ui/layouts/docs'
import { SidebarTrigger } from 'fumadocs-ui/layouts/docs/slots/sidebar'
import type * as PageTree from 'fumadocs-core/page-tree'
import { prefix, type Lang } from '~/lib/i18n'
import { icon, noop } from './site-header'

// Apart from the site header, so the pages outside the docs do not load the docs layout.
const t = { en: { start: 'Home' }, de: { start: 'Startseite' } }

export function Docs({ lang, tree, children }: { lang: Lang; tree: PageTree.Root; children: ReactNode }) {
  const home = (
    <a href={`${prefix(lang)}/`} className="me-auto flex items-center gap-2 rounded-lg p-2 text-sm text-fd-muted-foreground transition-colors hover:bg-fd-accent/50 hover:text-fd-accent-foreground">
      <ArrowLeft className="size-4" aria-hidden />
      {t[lang].start}
    </a>
  )
  // The sidebar and TOC are sticky inside the docs grid, so a footer in view pushed them up under the header.
  useEffect(() => {
    const footer = document.querySelector('body > footer')
    if (!footer) return
    const root = document.documentElement.style
    const set = () => root.setProperty('--footer-in', `${Math.max(0, innerHeight - footer.getBoundingClientRect().top)}px`)
    set()
    addEventListener('scroll', set, { passive: true })
    addEventListener('resize', set)
    return () => {
      removeEventListener('scroll', set)
      removeEventListener('resize', set)
      root.removeProperty('--footer-in')
    }
  }, [])
  return (
    <DocsLayout tree={tree} nav={{ component: <DocsMenu />, children: home }} slots={{ navTitle: () => null, languageSelect: false, themeSwitch: false, searchTrigger: false }}>
      {children}
    </DocsLayout>
  )
}

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
