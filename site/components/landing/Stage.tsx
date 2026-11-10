'use client'
import { useEffect, useRef, useState, type CSSProperties } from 'react'
import { preload } from 'react-dom'
import type { Lang } from '~/lib/i18n'
import { compositions, type CompositionId } from '~/remotion'
import { RemotionStage } from '~/remotion/RemotionStage'

const idle = (fn: () => void) => ('requestIdleCallback' in window ? requestIdleCallback(fn) : setTimeout(fn, 200))

export function Stage({ id, lang, label, eager, className = '' }: { id: CompositionId; lang: Lang; label: string; eager?: boolean; className?: string }) {
  const ref = useRef<HTMLDivElement>(null)
  const [near, setNear] = useState(!!eager)
  const [on, setOn] = useState(false)

  useEffect(() => {
    // After load and idle, so the player never competes with first paint; then every stage's code is warm before it scrolls in.
    const go = () =>
      idle(() => {
        void import('~/remotion/Stage')
        void compositions[id].load()
        if (eager) setOn(true)
      })
    if (document.readyState === 'complete') go()
    else window.addEventListener('load', go, { once: true })
    if (eager) return () => window.removeEventListener('load', go)
    const io = new IntersectionObserver(([e]) => e?.isIntersecting && (setNear(true), setOn(true), io.disconnect()), { rootMargin: '150% 0px' })
    if (ref.current) io.observe(ref.current)
    return () => {
      window.removeEventListener('load', go)
      io.disconnect()
    }
  }, [id, eager])

  const file = (v: string) => `/stills/${id}-${lang}-${v}.webp`
  const src = (v: string) => `url(${file(v)})`
  // The first picture is a CSS background, which the browser finds late; tell it now. Dark is the default theme.
  if (eager) {
    const port = 'portrait' in compositions[id]
    preload(file('dark'), { as: 'image', fetchPriority: 'high', media: port ? '(min-width: 640px)' : undefined })
    if (port) preload(file('dark-port'), { as: 'image', fetchPriority: 'high', media: '(max-width: 639.98px)' })
  }
  const poster = {
    '--p-dark': src('dark'),
    '--p-light': src('light'),
    ...('portrait' in compositions[id] && { '--p-dark-port': src('dark-port'), '--p-light-port': src('light-port') }),
  } as CSSProperties

  return (
    <div ref={ref} role="img" aria-label={label} className={`relative w-full overflow-hidden ${className}`}>
      {near && <div aria-hidden className="poster absolute inset-0" data-port={'portrait' in compositions[id] || undefined} style={poster} />}
      {on && <RemotionStage id={id} className="absolute inset-0" />}
    </div>
  )
}
