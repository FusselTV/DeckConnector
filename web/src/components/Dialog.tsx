import { CircleHelp, TriangleAlert, X, type LucideIcon } from 'lucide-react'
import { useEffect, useId, useRef, useState, type ReactNode } from 'react'
import type { AskOpts } from '../app/ui'
import { useUi } from '../app/ui'
import { cx, useLatest } from '../lib/util'
import { describedBy, Field } from './controls'

/** A native modal dialog: top layer, focus trap, Escape, inert page. Focus goes back where it came from. */
export function Dialog(p: {
  className: string
  title: string
  titleClass?: string
  role?: 'alertdialog'
  describedBy?: string
  close: () => void
  head?: (title: ReactNode) => ReactNode
  children: ReactNode
}) {
  const ref = useRef<HTMLDialogElement>(null)
  const close = useLatest(p.close)
  const id = useId()
  // Taken before the dialog opens: by the time an effect runs, focus may already be inside it.
  const [back] = useState(() => document.activeElement)
  useEffect(() => {
    ref.current?.showModal()
    ref.current?.querySelector<HTMLElement>('[data-autofocus]')?.focus()
    return () => {
      if (back instanceof HTMLElement && back.isConnected) back.focus()
    }
  }, [back])
  const title = (
    <h2 id={id} className={p.titleClass}>
      {p.title}
    </h2>
  )
  return (
    // Escape (onCancel) is the keyboard twin of the backdrop click.
    // eslint-disable-next-line jsx-a11y/click-events-have-key-events, jsx-a11y/no-noninteractive-element-interactions
    <dialog
      ref={ref}
      className={p.className}
      role={p.role}
      aria-labelledby={id}
      aria-describedby={p.describedBy}
      onCancel={(e) => {
        e.preventDefault()
        close.current()
      }}
      onClick={(e) => e.target === e.currentTarget && close.current()}
    >
      {p.head ? p.head(title) : title}
      {p.children}
    </dialog>
  )
}

/** A dialog that edits or creates something and commits with its own button (rule 15). */
export function Modal(p: {
  title: string
  icon: LucideIcon
  children: ReactNode
  foot?: ReactNode
  close: () => void
  wide?: boolean
}) {
  const { t } = useUi()
  const I = p.icon
  return (
    <Dialog
      className={cx('modal', p.wide && 'wide')}
      title={p.title}
      close={p.close}
      head={(h) => (
        <div className="mhead">
          <I size={18} className="ic" aria-hidden />
          {h}
          <span className="sp" />
          <button type="button" className="ibtn ghost" aria-label={t('c.close')} title={t('c.close')} onClick={p.close}>
            <X size={18} aria-hidden />
          </button>
        </div>
      )}
    >
      <div className="mbody">{p.children}</div>
      {p.foot && <div className="mfoot">{p.foot}</div>}
    </Dialog>
  )
}

/** Confirmations name the object and the effect. `typed` asks for a word before the danger button works. */
export function Confirm({ d, done }: { d: AskOpts; done: (ok: boolean) => void }) {
  const { t } = useUi()
  const [typed, setTyped] = useState('')
  return (
    <Dialog
      className="modal confirm"
      role="alertdialog"
      describedBy="dlg-b"
      title={d.title}
      close={() => done(false)}
      head={(h) => (
        <div className={cx('dh', d.danger && 'danger')}>
          {d.danger ? (
            <TriangleAlert size={22} className="ic" aria-hidden />
          ) : (
            <CircleHelp size={22} className="ic" aria-hidden />
          )}
          {h}
        </div>
      )}
    >
      <div className="db" id="dlg-b">
        {typeof d.body === 'string' ? <p>{d.body}</p> : d.body}
        {d.typed && (
          <Field id="dlg-typed" label={t('cf.typed', { w: d.typed })}>
            <input
              {...describedBy('dlg-typed')}
              className="in mono"
              value={typed}
              onChange={(e) => setTyped(e.target.value)}
              autoComplete="off"
            />
          </Field>
        )}
      </div>
      <div className="df">
        <button type="button" className="btn ghost" onClick={() => done(false)}>
          {d.ok === null ? t('c.close') : t('c.cancel')}
        </button>
        {d.ok !== null && (
          <button
            type="button"
            className={cx('btn', d.danger ? 'danger' : 'pri')}
            disabled={!!d.typed && typed !== d.typed}
            onClick={() => done(true)}
          >
            {d.ok ?? t('c.confirm')}
          </button>
        )}
      </div>
    </Dialog>
  )
}
