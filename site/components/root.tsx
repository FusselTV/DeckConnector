import type { ReactNode } from 'react'
import { preload } from 'react-dom'
import { RootProvider } from 'fumadocs-ui/provider/next'
import type { Lang } from '~/lib/i18n'
import { ui } from '~/lib/translations'
import { Search } from './search-lazy'
import { SiteFooter } from './site-footer'
import { SiteHeader } from './site-header'
import '~/app/global.css'

const skip = { en: 'Skip to content', de: 'Zum Inhalt springen' }

export function Root({ lang, children }: { lang: Lang; children: ReactNode }) {
  for (const f of ['geist', 'jetbrains-mono']) preload(`/fonts/${f}.woff2`, { as: 'font', type: 'font/woff2', crossOrigin: 'anonymous' })
  return (
    <html lang={lang} suppressHydrationWarning>
      <body className="flex min-h-screen flex-col">
        <RootProvider i18n={ui.provider(lang)} search={{ SearchDialog: Search }} theme={{ defaultTheme: 'dark', enableSystem: false }}>
          <a href="#main" className="sr-only rounded-lg bg-amber px-4 py-2 font-semibold text-amber-ink focus:not-sr-only focus:fixed focus:start-3 focus:top-2 focus:z-50">
            {skip[lang]}
          </a>
          <SiteHeader lang={lang} />
          {children}
          <SiteFooter lang={lang} />
        </RootProvider>
      </body>
    </html>
  )
}
