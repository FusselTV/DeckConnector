import { createSearchAPI } from 'fumadocs-core/search/server'
import { i18n, type Lang } from '~/lib/i18n'
import { tokenizer } from '~/lib/search'
import { source } from '~/lib/source'
import { withVersion } from '~/lib/version'

export const revalidate = false
export const dynamicParams = false
export const generateStaticParams = () => i18n.languages.map((lang) => ({ lang }))

export async function GET(_: Request, { params }: { params: Promise<{ lang: string }> }) {
  const lang = (await params).lang as Lang
  return createSearchAPI('advanced', {
    tokenizer: tokenizer[lang],
    indexes: source.getPages(lang).map((page) => ({
      id: page.url,
      title: page.data.title,
      description: page.data.description,
      url: page.url,
      structuredData: {
        ...page.data.structuredData,
        contents: page.data.structuredData.contents.map((c) => ({ ...c, content: withVersion(c.content) })),
      },
    })),
  }).staticGET()
}
