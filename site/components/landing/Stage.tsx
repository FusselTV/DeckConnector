'use client'
import { useEffect, useRef, useState, useSyncExternalStore, type CSSProperties } from 'react'
import { preload } from 'react-dom'
import type { Lang } from '~/lib/i18n'
import { compositions, type CompositionId } from '~/remotion'

const PORT = '(max-width: 639.98px)'
const STILL = '(prefers-reduced-motion: reduce)'

function subscribe(cb: () => void) {
  const mo = new MutationObserver(cb)
  mo.observe(document.documentElement, { attributeFilter: ['class'] })
  const mq = [PORT, STILL].map((q) => matchMedia(q))
  for (const m of mq) m.addEventListener('change', cb)
  return () => {
    mo.disconnect()
    for (const m of mq) m.removeEventListener('change', cb)
  }
}

// Not autoPlay: React can build an element it then throws away, and that one would fetch the file a second time.
function play(v: HTMLVideoElement | null) {
  if (!v) return
  const io = new IntersectionObserver(([e]) => void (e?.isIntersecting ? v.play().catch(() => undefined) : v.pause()))
  io.observe(v)
  return () => io.disconnect()
}

export function Stage({ id, lang, label, eager, className = '' }: { id: CompositionId; lang: Lang; label: string; eager?: boolean; className?: string }) {
  const ref = useRef<HTMLDivElement>(null)
  const [near, setNear] = useState(!!eager)
  const [on, setOn] = useState(false)
  const c = compositions[id]
  const port = 'portrait' in c
  const variant = useSyncExternalStore(
    subscribe,
    () => (matchMedia(STILL).matches ? null : `${document.documentElement.classList.contains('light') ? 'light' : 'dark'}${port && matchMedia(PORT).matches ? '-port' : ''}`),
    () => null,
  )

  useEffect(() => {
    if (eager) {
      // After load, so the video never competes with the poster for first paint.
      const go = () => setOn(true)
      if (document.readyState === 'complete') go()
      else window.addEventListener('load', go, { once: true })
      return () => window.removeEventListener('load', go)
    }
    const io = new IntersectionObserver(([e]) => e?.isIntersecting && (setNear(true), setOn(true), io.disconnect()), { rootMargin: '150% 0px' })
    if (ref.current) io.observe(ref.current)
    return () => io.disconnect()
  }, [eager])

  const file = (v: string) => `/stills/${id}-${lang}-${v}.webp`
  const src = (v: string) => `url(${file(v)})`
  // The first picture is a CSS background, which the browser finds late; tell it now. Dark is the default theme.
  if (eager) {
    preload(file('dark'), { as: 'image', fetchPriority: 'high', media: port ? '(min-width: 640px)' : undefined })
    if (port) preload(file('dark-port'), { as: 'image', fetchPriority: 'high', media: PORT })
  }
  const poster = {
    '--p-dark': src('dark'),
    '--p-light': src('light'),
    ...(port && { '--p-dark-port': src('dark-port'), '--p-light-port': src('light-port') }),
  } as CSSProperties
  const video = `/videos/${id}-${lang}-${variant ?? ''}`
  const t = `#t=${c.start / c.fps}`

  return (
    <div ref={ref} role="img" aria-label={label} className={`relative w-full overflow-hidden ${className}`}>
      {near && <div aria-hidden className="poster absolute inset-0" data-port={port || undefined} style={poster} />}
      {on && variant && (
        <video key={video} ref={play} aria-hidden muted loop playsInline preload="none" className="absolute inset-0 size-full object-cover">
          <source src={`${video}.webm${t}`} type={'video/webm; codecs="av01.0.08M.08"'} />
          <source src={`${video}.mp4${t}`} type="video/mp4" />
        </video>
      )}
    </div>
  )
}
