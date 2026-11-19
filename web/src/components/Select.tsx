import { Check, ChevronDown } from 'lucide-react'
import { useEffect, useId, useLayoutEffect, useRef, useState, type KeyboardEvent, type ReactNode } from 'react'
import { cx } from '../lib/util'

/** More than four exclusive options: a button that opens a list. Focus stays on the button (aria-activedescendant). */
export function Select<V extends string>(p: {
  id: string
  value: V
  options: readonly (readonly [V, ReactNode])[]
  onChange: (v: V) => void
  className?: string
}) {
  const lid = useId()
  const btn = useRef<HTMLButtonElement>(null)
  const list = useRef<HTMLDivElement>(null)
  const [open, setOpen] = useState(false)
  const [up, setUp] = useState(false)
  const at = Math.max(
    0,
    p.options.findIndex(([k]) => k === p.value),
  )
  const [active, setActive] = useState(at)
  const last = p.options.length - 1

  const show = (i = at) => {
    setActive(i)
    setOpen(true)
  }
  const pick = (i: number) => {
    const o = p.options[i]
    if (o && o[0] !== p.value) p.onChange(o[0])
    setOpen(false)
  }

  useLayoutEffect(() => {
    if (!open) return
    const b = btn.current?.getBoundingClientRect()
    const h = list.current?.offsetHeight ?? 0
    if (b) setUp(innerHeight - b.bottom < h + 8 && b.top > innerHeight - b.bottom)
  }, [open])
  useEffect(() => {
    if (open) list.current?.children[active]?.scrollIntoView({ block: 'nearest' })
  }, [open, active])

  const key = (e: KeyboardEvent) => {
    const move: Record<string, number> = {
      ArrowDown: Math.min(last, active + 1),
      ArrowUp: Math.max(0, active - 1),
      Home: 0,
      End: last,
    }
    if (!open) {
      if (['ArrowDown', 'ArrowUp', 'Enter', ' '].includes(e.key)) show()
      else if (e.key === 'Home' || e.key === 'End') show(move[e.key])
      else return
    } else if (e.key in move) setActive(move[e.key] ?? active)
    else if (e.key === 'Enter' || e.key === ' ') pick(active)
    else if (e.key === 'Escape') setOpen(false)
    else if (e.key === 'Tab') return pick(active)
    else return
    e.preventDefault()
  }

  return (
    <span className={cx('sel', p.className)}>
      <button
        ref={btn}
        type="button"
        id={p.id}
        className="in seltrig"
        role="combobox"
        aria-haspopup="listbox"
        aria-expanded={open}
        aria-controls={lid}
        aria-activedescendant={open ? `${lid}-${active}` : undefined}
        onClick={() => {
          btn.current?.focus() // Safari does not focus a clicked button: no blur to close on, no keys
          if (open) setOpen(false)
          else show()
        }}
        onKeyDown={key}
        onBlur={() => setOpen(false)}
      >
        <span>{p.options[at]?.[1]}</span>
        <ChevronDown size={16} className="ic" aria-hidden />
      </button>
      <div ref={list} id={lid} role="listbox" className={cx('selpop', up && 'up')} hidden={!open} tabIndex={-1}>
        {p.options.map(([k, l], i) => (
          // eslint-disable-next-line jsx-a11y/click-events-have-key-events, jsx-a11y/interactive-supports-focus -- keys and focus stay on the combobox, which points here with aria-activedescendant
          <div
            key={k}
            id={`${lid}-${i}`}
            role="option"
            aria-selected={k === p.value}
            className={cx(i === active && 'act')}
            onPointerMove={() => setActive(i)}
            // keeps focus on the button, so its blur does not close the list before the click lands
            onPointerDown={(e) => e.preventDefault()}
            onClick={() => pick(i)}
          >
            {l}
            {k === p.value && <Check size={16} className="ic" aria-hidden />}
          </div>
        ))}
      </div>
    </span>
  )
}
