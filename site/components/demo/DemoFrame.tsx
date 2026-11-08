'use client'
import { useEffect, useRef } from 'react'
import { useTheme } from 'next-themes'
import { useRouter } from 'next/navigation'
import { prefix, type Lang } from '~/lib/i18n'

// The theme follows on its own (both pages read next-themes' "theme" key); the language comes in the URL and goes
// back by message when it is changed inside the demo.
export function DemoFrame({ lang, title }: { lang: Lang; title: string }) {
  const ref = useRef<HTMLIFrameElement>(null)
  const { resolvedTheme } = useTheme()
  const router = useRouter()
  const theme = useRef(resolvedTheme)
  useEffect(() => {
    const q = new URLSearchParams(location.search)
    q.set('lang', lang)
    if (theme.current === 'light' || theme.current === 'dark') q.set('theme', theme.current)
    ref.current?.setAttribute('src', `/demo/app/?${q.toString().replace(/=(?=&|$)/g, '')}${location.hash}`)
    const onMessage = (e: MessageEvent<unknown>) => {
      const l = (e.data as { demoLang?: unknown } | null)?.demoLang
      if (e.origin === location.origin && (l === 'en' || l === 'de') && l !== lang) router.push(`${prefix(l)}/demo/${location.search}`)
    }
    addEventListener('message', onMessage)
    return () => removeEventListener('message', onMessage)
  }, [lang, router])
  return <iframe ref={ref} title={title} className="w-full flex-1 border-0" />
}
