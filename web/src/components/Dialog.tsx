import { CircleHelp, TriangleAlert, X, type LucideIcon } from 'lucide-react'
import { Dialog as D } from 'radix-ui'
import { useState, type ReactNode } from 'react'
import type { AskOpts } from '../app/ui'
import { useUi } from '../app/ui'
import { cx } from '../lib/util'
import { describedBy, Field } from './controls'

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
    <D.Root open onOpenChange={(o) => !o && p.close()}>
      <D.Portal>
        <D.Overlay className="scrim" />
        <D.Content className={cx('modal', p.wide && 'wide')} aria-describedby={undefined}>
          <div className="mhead">
            <I size={18} className="ic" aria-hidden />
            <D.Title asChild>
              <h2>{p.title}</h2>
            </D.Title>
            <span className="sp" />
            <D.Close className="ibtn ghost" aria-label={t('c.close')} title={t('c.close')}>
              <X size={18} aria-hidden />
            </D.Close>
          </div>
          <div className="mbody">{p.children}</div>
          {p.foot && <div className="mfoot">{p.foot}</div>}
        </D.Content>
      </D.Portal>
    </D.Root>
  )
}

/** Confirmations name the object and the effect. `typed` asks for a word before the danger button works. */
export function Confirm({ d, done }: { d: AskOpts; done: (ok: boolean) => void }) {
  const { t } = useUi()
  const [typed, setTyped] = useState('')
  return (
    <D.Root open onOpenChange={(o) => !o && done(false)}>
      <D.Portal>
        <D.Overlay className="scrim" />
        <D.Content className="modal confirm" role="alertdialog" aria-describedby="dlg-b">
          <div className={cx('dh', d.danger && 'danger')}>
            {d.danger ? (
              <TriangleAlert size={22} className="ic" aria-hidden />
            ) : (
              <CircleHelp size={22} className="ic" aria-hidden />
            )}
            <D.Title asChild>
              <h2>{d.title}</h2>
            </D.Title>
          </div>
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
            <D.Close className="btn ghost">{d.ok === null ? t('c.close') : t('c.cancel')}</D.Close>
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
        </D.Content>
      </D.Portal>
    </D.Root>
  )
}
