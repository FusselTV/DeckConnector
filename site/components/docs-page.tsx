import type { Metadata } from 'next'
import { notFound } from 'next/navigation'
import defaultMdxComponents, { createRelativeLink } from 'fumadocs-ui/mdx'
import { DocsBody, DocsDescription, DocsPage, DocsTitle } from 'fumadocs-ui/layouts/docs/page'
import type { Lang } from '~/lib/i18n'
import { source } from '~/lib/source'

export function DocPage({ lang, slug }: { lang: Lang; slug?: string[] }) {
  const page = source.getPage(slug, lang)
  if (!page) notFound()
  const MDX = page.data.body
  return (
    <DocsPage toc={page.data.toc}>
      <DocsTitle>{page.data.title}</DocsTitle>
      <DocsDescription>{page.data.description}</DocsDescription>
      <DocsBody>
        <MDX components={{ ...defaultMdxComponents, a: createRelativeLink(source, page) }} />
      </DocsBody>
    </DocsPage>
  )
}

export const docParams = (lang: Lang) => source.getPages(lang).map((p) => ({ slug: p.slugs }))

export function docMetadata(lang: Lang, slug?: string[]): Metadata {
  const page = source.getPage(slug, lang)
  if (!page) notFound()
  return { title: page.data.title, description: page.data.description }
}
