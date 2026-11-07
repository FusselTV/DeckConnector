import { Copy, Eye, EyeOff, FolderOpen } from 'lucide-react'
import { useRef, useState, type InputHTMLAttributes, type ReactNode } from 'react'
import { toast } from '../app/state'
import { useUi } from '../app/ui'
import { copyText } from '../lib/util'

/** An input or value box and its button in one row, same height. */
export const InputAction = ({ children }: { children: ReactNode }) => <div className="inrow">{children}</div>

export function PwInput(p: InputHTMLAttributes<HTMLInputElement> & { id: string }) {
  const { t } = useUi()
  const [show, setShow] = useState(p.autoComplete !== 'current-password')
  return (
    <InputAction>
      <input {...p} className="in" type={show ? 'text' : 'password'} autoComplete={p.autoComplete ?? 'new-password'} />
      <button
        type="button"
        className="ibtn"
        aria-pressed={show}
        aria-label={t(show ? 'pw.hide' : 'pw.show')}
        title={t(show ? 'pw.hide' : 'pw.show')}
        onClick={() => setShow(!show)}
      >
        {show ? <EyeOff size={16} aria-hidden /> : <Eye size={16} aria-hidden />}
      </button>
    </InputAction>
  )
}

export function CopyField({ id, value, link }: { id?: string; value: string; link?: boolean }) {
  const { t } = useUi()
  const ref = useRef<HTMLElement>(null)
  const copy = async () => toast(t((await copyText(value, ref.current)) ? 'c.copied' : 'c.copy_fail'))
  return (
    <InputAction>
      {link ? (
        <a ref={ref as React.Ref<HTMLAnchorElement>} id={id} href={value} className="in linkbox">
          <span className="mono">{value}</span>
        </a>
      ) : (
        <code ref={ref} id={id} className="in codebox wrap">
          {value}
        </code>
      )}
      <button
        type="button"
        className={link ? 'ibtn' : 'btn'}
        aria-label={`${t('c.copy')}: ${value}`}
        title={t('c.copy')}
        onClick={() => void copy()}
      >
        <Copy size={16} aria-hidden />
        {!link && t('c.copy')}
      </button>
    </InputAction>
  )
}

export function FilePick(p: {
  id: string
  accept?: string
  file: File | null
  onFile: (f: File | null) => void
  label?: string
}) {
  const { t } = useUi()
  const ref = useRef<HTMLInputElement>(null)
  return (
    <InputAction>
      <span className="in codebox" id={`${p.id}-name`}>
        {p.file?.name ?? '–'}
      </span>
      <input
        ref={ref}
        id={p.id}
        type="file"
        accept={p.accept}
        hidden
        onChange={(e) => p.onFile(e.target.files?.[0] ?? null)}
      />
      <button type="button" className="btn" aria-describedby={`${p.id}-name`} onClick={() => ref.current?.click()}>
        <FolderOpen size={16} aria-hidden />
        {p.label ?? t('upd.choose')}
      </button>
    </InputAction>
  )
}
