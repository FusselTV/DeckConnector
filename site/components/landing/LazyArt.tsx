'use client'
import dynamic from 'next/dynamic'
import { useEffect, useRef, useState } from 'react'
import type { ArtProps } from './art'

const Art = dynamic(() => import('./art'), { ssr: false, loading: () => null })

// The drawings are heavy SVG, so they render in the browser once near the screen instead of in the HTML.
export function LazyArt({ className = '', ...art }: ArtProps & { className?: string }) {
  const ref = useRef<HTMLDivElement>(null)
  const [on, setOn] = useState(false)
  useEffect(() => {
    const io = new IntersectionObserver(([e]) => e?.isIntersecting && (setOn(true), io.disconnect()), { rootMargin: '600px' })
    if (ref.current) io.observe(ref.current)
    return () => io.disconnect()
  }, [])
  return (
    <div ref={ref} className={className}>
      {on && <Art {...art} />}
    </div>
  )
}
