import type { Metadata } from 'next'
import { notFound } from 'next/navigation'
import defaultMdxComponents, { createRelativeLink } from 'fumadocs-ui/mdx'
import { Step, Steps } from 'fumadocs-ui/components/steps'
import { Tab, Tabs } from 'fumadocs-ui/components/tabs'
import { DocsBody, DocsDescription, DocsPage, DocsTitle } from 'fumadocs-ui/layouts/docs/page'
import type { Lang } from '~/lib/i18n'
import { pageMeta } from '~/lib/meta'
import { source } from '~/lib/source'

export function DocPage({ lang, slug }: { lang: Lang; slug?: string[] }) {
  const page = source.getPage(slug, lang)
  if (!page) notFound()
  const MDX = page.data.body
  const Link = createRelativeLink(source, page)
  return (
    <DocsPage toc={page.data.toc}>
      <DocsTitle id="main">
        {page.data.title}
      </DocsTitle>
      <DocsDescription>{page.data.description}</DocsDescription>
      <DocsBody>
        <MDX
          components={{
            ...defaultMdxComponents,
            Step,
            Steps,
            Tab,
            Tabs,
            // the demo is a static HTML file, not a Next route: a client-side Link would fetch a missing RSC payload
            a: ({ children, ...p }) =>
              p.href?.startsWith('/demo/app/') ? <a {...p}>{children}</a> : <Link {...p}>{children}</Link>,
          }}
        />
      </DocsBody>
    </DocsPage>
  )
}

export const docParams = (lang: Lang) => source.getPages(lang).map((p) => ({ slug: p.slugs }))

export function docMetadata(lang: Lang, slug?: string[]): Metadata {
  const page = source.getPage(slug, lang)
  if (!page) notFound()
  return pageMeta(lang, `/docs/${page.slugs.map((x) => `${x}/`).join('')}`, { title: page.data.title, description: page.data.description })
}
