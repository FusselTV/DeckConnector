import { llmsFullTxt } from '~/lib/llm'

export const dynamic = 'force-static'

export const GET = async () => new Response(await llmsFullTxt(), { headers: { 'Content-Type': 'text/plain; charset=utf-8' } })
