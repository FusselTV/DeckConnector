'use client'
import { useEffect, useRef } from 'react'
import { useRouter } from 'next/navigation'
import { prefix, type Lang } from '~/lib/i18n'

// The theme follows on its own (both pages read next-themes' "theme" key); the language comes in the URL and goes
// back by message when it is changed inside the demo.
export function DemoFrame({ lang, title }: { lang: Lang; title: string }) {
  const ref = useRef<HTMLIFrameElement>(null)
  const router = useRouter()
  const src = `/demo/app/?lang=${lang}`
  useEffect(() => {
    // The page's own src starts loading before hydration; only extra options (?setup, ?login, a #tab) reload it.
    const q = new URLSearchParams(location.search)
    q.delete('lang')
    if (q.size || location.hash)
      ref.current?.setAttribute(
        'src',
        `${src}${q.size ? '&' : ''}${q.toString().replace(/=(?=&|$)/g, '')}${location.hash}`,
      )
    const onMessage = (e: MessageEvent<unknown>) => {
      const l = (e.data as { demoLang?: unknown } | null)?.demoLang
      if (e.origin === location.origin && (l === 'en' || l === 'de') && l !== lang)
        router.push(`${prefix(l)}/demo/${location.search}`)
    }
    addEventListener('message', onMessage)
    return () => removeEventListener('message', onMessage)
  }, [lang, router, src])
  return <iframe ref={ref} src={src} title={title} className="w-full flex-1 border-0" />
}
