import { CircleCheck, CircleHelp, type LucideIcon } from 'lucide-react'
import { useEffect, useId, useRef, useState, type ReactNode } from 'react'
import { useUi } from '../app/ui'
import { cx, roam } from '../lib/util'

export type Tone = 'ok' | 'warn' | 'bad' | 'acc' | ''

/** Status icon for a thing (Ethernet, deck, source), coloured by state (rule 2). */
export function SIcon({
  icon: I,
  tone = '',
  size = 18,
  label,
}: {
  icon: LucideIcon
  tone?: Tone
  size?: number
  label?: string
}) {
  return (
    <span
      className={cx('sicon', tone)}
      role={label ? 'img' : undefined}
      aria-label={label}
      aria-hidden={label ? undefined : true}
    >
      <I size={size} className="ic" aria-hidden />
    </span>
  )
}

/** The one LED: the live connection in the status strip (rule 2). */
export const Led = ({ tone, pulse }: { tone: Tone; pulse?: boolean }) => (
  <span className={cx('led', tone, pulse && 'pulse')} aria-hidden />
)

export function Tag({ tone, icon: I, children }: { tone?: Tone | 'amber'; icon?: LucideIcon; children: ReactNode }) {
  return (
    <span className={cx('tag', tone)}>
      {I && <I size={12} className="ic" aria-hidden />}
      {children}
    </span>
  )
}

export function Help({ label, text, right }: { label: string; text: ReactNode; right?: boolean }) {
  const { t } = useUi()
  const [open, setOpen] = useState(false)
  const id = useId()
  const ref = useRef<HTMLSpanElement>(null)
  useEffect(() => {
    if (!open) return
    const out = (e: PointerEvent) => !ref.current?.contains(e.target as Node) && setOpen(false)
    const esc = (e: globalThis.KeyboardEvent) => e.key === 'Escape' && setOpen(false)
    document.addEventListener('pointerdown', out)
    document.addEventListener('keydown', esc)
    return () => {
      document.removeEventListener('pointerdown', out)
      document.removeEventListener('keydown', esc)
    }
  }, [open])
  return (
    <span className="help" ref={ref}>
      <button
        type="button"
        aria-label={t('c.help', { x: label })}
        aria-expanded={open}
        aria-controls={id}
        onClick={(e) => {
          e.preventDefault()
          setOpen(!open)
        }}
      >
        <CircleHelp size={14} className="ic" aria-hidden />
      </button>
      {open && (
        <span className={cx('bubble', right && 'r')} id={id} role="note">
          {text}
        </span>
      )}
    </span>
  )
}

export const describedBy = (id: string, err?: string | null | false, hint?: ReactNode) =>
  ({
    id,
    'aria-invalid': err ? true : undefined,
    'aria-describedby': [err && `${id}-err`, hint && `${id}-hint`].filter(Boolean).join(' ') || undefined,
  }) as const

export function Field(p: {
  id: string
  label: ReactNode
  help?: ReactNode
  err?: string | null
  hint?: ReactNode
  children: ReactNode
}) {
  return (
    <div className="field">
      <label htmlFor={p.id}>
        {p.label}
        {p.help && <Help label={typeof p.label === 'string' ? p.label : ''} text={p.help} />}
      </label>
      {p.children}
      {p.err && (
        <span className="err" id={`${p.id}-err`} role="alert">
          {p.err}
        </span>
      )}
      {p.hint && (
        <span className="hint" id={`${p.id}-hint`}>
          {p.hint}
        </span>
      )}
    </div>
  )
}

export function Switch(p: {
  id?: string
  on: boolean
  onChange: (v: boolean) => void
  label: ReactNode
  hint?: ReactNode
  disabled?: boolean
}) {
  return (
    <button
      type="button"
      id={p.id}
      className="sw"
      role="switch"
      aria-checked={p.on}
      disabled={p.disabled}
      onClick={() => p.onChange(!p.on)}
    >
      <span className="track" />
      <span className="t">
        <b>{p.label}</b>
        {p.hint && <span>{p.hint}</span>}
      </span>
    </button>
  )
}

/** Up to four exclusive options (rule 6). Arrow keys move the choice, as a radio group does. */
export function Seg<V extends string>(p: {
  id?: string
  label: string
  value: V
  options: readonly (readonly [V, ReactNode, LucideIcon?])[]
  onChange: (v: V) => void
}) {
  return (
    <div className="seg" role="radiogroup" aria-label={p.label} id={p.id} onKeyDown={roam} tabIndex={-1}>
      {p.options.map(([k, l, I]) => (
        <button
          key={k}
          type="button"
          role="radio"
          aria-checked={p.value === k}
          tabIndex={p.value === k ? 0 : -1}
          onClick={() => p.onChange(k)}
        >
          {I && <I size={15} className="ic" aria-hidden />}
          {l}
        </button>
      ))}
    </div>
  )
}

export function IconBtn(p: {
  icon: LucideIcon
  label: string
  onClick: () => void
  disabled?: boolean
  size?: number
  ghost?: boolean
  id?: string
}) {
  const I = p.icon
  return (
    <button
      type="button"
      id={p.id}
      className={cx('ibtn', p.ghost && 'ghost')}
      aria-label={p.label}
      title={p.label}
      onClick={p.onClick}
      disabled={p.disabled}
    >
      <I size={p.size ?? 16} className="ic" aria-hidden />
    </button>
  )
}

/** Runs a click handler once at a time. While its promise is pending the button is busy; it stays focusable
 *  (aria-disabled, not disabled), so focus can come back to it from a dialog the handler opened. */
export function useRun(fn: (() => unknown) | undefined) {
  const [busy, setBusy] = useState(false)
  const on = useRef(false)
  const run = () => {
    if (on.current || !fn) return
    const r = fn()
    if (!(r instanceof Promise)) return
    on.current = true
    setBusy(true)
    void r.finally(() => {
      on.current = false
      setBusy(false)
    })
  }
  return [busy, run] as const
}

export function Btn(p: {
  icon?: LucideIcon
  children: ReactNode
  onClick?: () => unknown
  kind?: 'pri' | 'ghost' | 'danger'
  sm?: boolean
  disabled?: boolean
  id?: string
  type?: 'button' | 'submit'
}) {
  const I = p.icon
  const [busy, run] = useRun(p.onClick)
  return (
    <button
      type={p.type ?? 'button'}
      id={p.id}
      className={cx('btn', p.kind, p.sm && 'sm')}
      onClick={run}
      disabled={p.disabled}
      aria-disabled={busy || undefined}
      aria-busy={busy || undefined}
    >
      {I && <I size={p.sm ? 15 : 16} className="ic" aria-hidden />}
      {p.children}
    </button>
  )
}

export const Empty = ({ icon: I, children }: { icon: LucideIcon; children: ReactNode }) => (
  <div className="emptystate">
    <SIcon icon={I} size={28} />
    {children}
  </div>
)

export function Choice(p: {
  on: boolean
  icon: LucideIcon
  label: string
  hint?: string
  onPick: () => void
  first?: boolean
}) {
  const I = p.on ? CircleCheck : p.icon
  return (
    <button
      type="button"
      role="radio"
      className="choice"
      aria-checked={p.on}
      tabIndex={p.on || p.first ? 0 : -1}
      onClick={p.onPick}
    >
      <I size={22} className="ic" aria-hidden />
      <div>
        <b>{p.label}</b>
        {p.hint && <span>{p.hint}</span>}
      </div>
    </button>
  )
}
