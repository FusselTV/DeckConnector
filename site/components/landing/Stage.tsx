'use client'
import { startTransition, useEffect, useRef, useState, type CSSProperties } from 'react'
import { preload } from 'react-dom'
import type { Lang } from '~/lib/i18n'
import { compositions, type CompositionId } from '~/remotion'
import { RemotionStage } from '~/remotion/RemotionStage'

export function Stage({
  id,
  lang,
  label,
  eager,
  className = '',
}: {
  id: CompositionId
  lang: Lang
  label: string
  eager?: boolean
  className?: string
}) {
  const ref = useRef<HTMLDivElement>(null)
  const [near, setNear] = useState(!!eager)
  const [on, setOn] = useState(false)

  useEffect(() => {
    const ac = new AbortController()
    const once = { once: true, passive: true, signal: ac.signal }
    let timer = 0
    let seen = !!eager
    let ready = false
    // A transition, so input that arrives during the mount goes first.
    const play = () => seen && ready && startTransition(() => setOn(true))
    const io = new IntersectionObserver(
      ([e]) => {
        if (!e?.isIntersecting) return
        seen = true
        setNear(true)
        play()
        io.disconnect()
      },
      { rootMargin: '150% 0px' },
    )
    if (!eager && ref.current) io.observe(ref.current)
    // The player's code is heavy: it waits for the first input after load, or a few seconds, so startup stays free.
    const go = () => {
      ac.abort()
      clearTimeout(timer)
      ready = true
      play()
    }
    const wait = () => {
      timer = window.setTimeout(go, 3000)
      for (const e of ['pointerdown', 'keydown', 'scroll', 'touchstart', 'wheel']) addEventListener(e, go, once)
    }
    if (document.readyState === 'complete') wait()
    else addEventListener('load', wait, once)
    return () => {
      ac.abort()
      clearTimeout(timer)
      io.disconnect()
    }
  }, [eager])

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
      {near && (
        <div
          aria-hidden
          className="poster absolute inset-0"
          data-port={'portrait' in compositions[id] || undefined}
          style={poster}
        />
      )}
      {on && <RemotionStage id={id} className="absolute inset-0" />}
    </div>
  )
}
