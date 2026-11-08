'use client'
import { useEffect, useRef, useState, type ReactNode } from 'react'
import type { CompositionId } from '~/remotion'
import { RemotionStage } from '~/remotion/RemotionStage'

export function Stage({
  id,
  label,
  eager,
  children,
  className = '',
}: {
  id: CompositionId
  label: string
  eager?: boolean
  children?: ReactNode
  className?: string
}) {
  const ref = useRef<HTMLDivElement>(null)
  const [on, setOn] = useState(false)
  useEffect(() => {
    if (eager) {
      // After load and idle, so the player never competes with first paint.
      const go = () => ('requestIdleCallback' in window ? requestIdleCallback : setTimeout)(() => setOn(true))
      if (document.readyState === 'complete') go()
      else window.addEventListener('load', go, { once: true })
      return () => window.removeEventListener('load', go)
    }
    const io = new IntersectionObserver(([e]) => e?.isIntersecting && (setOn(true), io.disconnect()), {
      rootMargin: '400px',
    })
    if (ref.current) io.observe(ref.current)
    return () => io.disconnect()
  }, [eager])
  return (
    <div
      ref={ref}
      role="img"
      aria-label={label}
      className={`relative w-full overflow-hidden ${className}`}
    >
      {children}
      {on && <RemotionStage id={id} className="absolute inset-0" />}
    </div>
  )
}
