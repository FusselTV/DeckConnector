import type { ReactNode } from 'react'
import { RootProvider } from 'fumadocs-ui/provider/next'
import type { Lang } from '~/lib/i18n'
import { ui } from '~/lib/translations'
import { Search } from './search-lazy'
import { SiteFooter } from './site-footer'
import { SiteHeader } from './site-header'
import '~/app/global.css'

export function Root({ lang, children }: { lang: Lang; children: ReactNode }) {
  return (
    <html lang={lang} suppressHydrationWarning>
      <body className="flex min-h-screen flex-col">
        <RootProvider i18n={ui.provider(lang)} search={{ SearchDialog: Search }} theme={{ defaultTheme: 'dark', enableSystem: false }}>
          <SiteHeader lang={lang} />
          {children}
          <SiteFooter lang={lang} />
        </RootProvider>
      </body>
    </html>
  )
}
