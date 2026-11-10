import type { Metadata } from 'next'
import type { ComponentProps } from 'react'
import { notFound } from 'next/navigation'
import defaultMdxComponents, { createRelativeLink } from 'fumadocs-ui/mdx'
import { Step, Steps } from 'fumadocs-ui/components/steps'
import { Tab, Tabs } from 'fumadocs-ui/components/tabs'
import { DocsBody, DocsDescription, DocsPage, DocsTitle, MarkdownCopyButton, ViewOptionsPopover } from 'fumadocs-ui/layouts/docs/page'
import { BenchBars } from '~/components/docs/bench'
import { JsonLd } from '~/components/landing/ui'
import * as diagrams from '~/components/docs/diagrams'
import { Next } from '~/components/docs/figure'
import { mdPath } from '~/lib/llm'
import { prefix, type Lang } from '~/lib/i18n'
import { pageMeta } from '~/lib/meta'
import { source } from '~/lib/source'
import { site } from '~/site.config'

export function DocPage({ lang, slug }: { lang: Lang; slug?: string[] }) {
  const page = source.getPage(slug, lang)
  if (!page) notFound()
  const MDX = page.data.body
  const Link = createRelativeLink(source, page)
  const md = mdPath(lang, page.slugs)
  return (
    <DocsPage toc={page.data.toc} footer={{ enabled: false }} className="pt-10 md:pt-14">
      <JsonLd
        data={{
          '@context': 'https://schema.org',
          '@type': 'BreadcrumbList',
          itemListElement: [
            { '@type': 'ListItem', position: 1, name: 'DeckConnector', item: `${site}${prefix(lang)}/` },
            { '@type': 'ListItem', position: 2, name: lang === 'de' ? 'Anleitung' : 'Docs', item: `${site}${prefix(lang)}/docs/` },
            ...(page.slugs.length ? [{ '@type': 'ListItem', position: 3, name: page.data.title, item: `${site}${page.url}/` }] : []),
          ],
        }}
      />
      <DocsTitle id="main" className="text-3xl tracking-tight text-balance sm:text-4xl">
        {page.data.title}
      </DocsTitle>
      <DocsDescription>{page.data.description}</DocsDescription>
      <div className="flex items-center gap-2 border-b pb-6">
        <MarkdownCopyButton markdownUrl={md} />
        <ViewOptionsPopover markdownUrl={md} pageUrl={`${site}${page.url}/`} />
      </div>
      <DocsBody>
        <MDX
          components={{
            ...defaultMdxComponents,
            Step,
            Steps,
            Tab,
            Tabs,
            ...diagrams,
            BenchBars,
            Next,
            // a file such as THIRD_PARTY_LICENSES.txt is no page, and Next's prefetch of it 404s
            a: (p: ComponentProps<'a'>) => (/\.\w+$/.test(p.href ?? '') ? <a {...p}>{p.children}</a> : <Link {...p} />),
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
  const m = pageMeta(lang, `/docs/${page.slugs.map((x) => `${x}/`).join('')}`, { title: page.data.title, description: page.data.description })
  return { ...m, alternates: { ...m.alternates, types: { 'text/markdown': mdPath(lang, page.slugs) } } }
}
