import { useLayoutEffect, useRef, type HTMLAttributes, type PointerEvent as PE, type Ref } from 'react'

const AXES = [
  { k: 'y', pos: 'scrollTop', size: 'scrollHeight', view: 'clientHeight', page: 'clientY' },
  { k: 'x', pos: 'scrollLeft', size: 'scrollWidth', view: 'clientWidth', page: 'clientX' },
] as const

/** A box that scrolls with thin bars of its own over the native ones it hides. Wheel, keys and touch stay native. */
export function ScrollArea({
  ref,
  className,
  children,
  ...rest
}: HTMLAttributes<HTMLDivElement> & { ref?: Ref<HTMLDivElement> }) {
  const vp = useRef<HTMLDivElement | null>(null)
  const bars = useRef<(HTMLDivElement | null)[]>([])

  const paint = () => {
    const el = vp.current
    if (!el) return
    AXES.forEach((a, i) => {
      const bar = bars.current[i]
      const thumb = bar?.firstElementChild as HTMLElement | null
      if (!bar || !thumb) return
      const over = el[a.size] - el[a.view]
      bar.hidden = over <= 1
      if (over <= 1) return
      const track = a.k === 'y' ? bar.clientHeight : bar.clientWidth
      const len = Math.max(24, (el[a.view] / el[a.size]) * track)
      const at = (el[a.pos] / over) * (track - len)
      thumb.style[a.k === 'y' ? 'height' : 'width'] = `${len}px`
      thumb.style.transform = a.k === 'y' ? `translateY(${at}px)` : `translateX(${at}px)`
    })
  }

  useLayoutEffect(() => {
    const el = vp.current
    if (!el) return
    const ro = new ResizeObserver(paint)
    ro.observe(el)
    // the content grows without the box changing size: new log rows, an opened row
    const mo = new MutationObserver(paint)
    mo.observe(el, { childList: true, subtree: true })
    paint()
    return () => {
      ro.disconnect()
      mo.disconnect()
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps -- paint reads refs only
  }, [])

  const drag = (i: number) => (e: PE<HTMLDivElement>) => {
    const el = vp.current
    const a = AXES[i]
    const bar = bars.current[i]
    if (!el || !a || !bar || e.button !== 0) return
    e.preventDefault()
    const thumb = e.currentTarget
    const track = a.k === 'y' ? bar.clientHeight : bar.clientWidth
    const len = a.k === 'y' ? thumb.offsetHeight : thumb.offsetWidth
    const per = (el[a.size] - el[a.view]) / Math.max(1, track - len)
    const from = e[a.page]
    const start = el[a.pos]
    thumb.setPointerCapture(e.pointerId)
    thumb.onpointermove = (m) => (el[a.pos] = start + (m[a.page] - from) * per)
    thumb.onlostpointercapture = () => (thumb.onpointermove = null)
  }
  return (
    <div className="sa">
      <div
        {...rest}
        ref={(n) => {
          vp.current = n
          if (typeof ref === 'function') ref(n)
          else if (ref) ref.current = n
        }}
        className={`sa-vp ${className ?? ''}`}
        onScroll={(e) => {
          paint()
          rest.onScroll?.(e)
        }}
      >
        {children}
      </div>
      {AXES.map((a, i) => (
        <div key={a.k} className={`sa-bar ${a.k}`} aria-hidden ref={(n) => void (bars.current[i] = n)}>
          <div className="sa-thumb" onPointerDown={drag(i)} />
        </div>
      ))}
    </div>
  )
}
