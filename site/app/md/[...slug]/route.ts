import { i18n } from '~/lib/i18n'
import { mdPath, pageMarkdown } from '~/lib/llm'
import { source } from '~/lib/source'

export const dynamicParams = false

const pages = () =>
  i18n.languages.flatMap((lang) => source.getPages(lang).map((page) => ({ page, path: mdPath(lang, page.slugs) })))

export const generateStaticParams = () => pages().map(({ path }) => ({ slug: path.split('/').slice(2) }))

export async function GET(_: Request, { params }: { params: Promise<{ slug: string[] }> }) {
  const path = `/md/${(await params).slug.join('/')}`
  const hit = pages().find((p) => p.path === path)
  if (!hit) return new Response(null, { status: 404 })
  return new Response(await pageMarkdown(hit.page), { headers: { 'Content-Type': 'text/markdown; charset=utf-8' } })
}
