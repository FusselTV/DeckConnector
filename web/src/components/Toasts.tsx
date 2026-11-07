import { Check, TriangleAlert } from 'lucide-react'
import { useEffect, useRef } from 'react'
import { dismissToast, toastsAtom, type Toast } from '../app/state'
import { useUi } from '../app/ui'
import { cx } from '../lib/util'

function ToastItem({ x }: { x: Toast }) {
  const { t } = useUi()
  const ref = useRef<HTMLDivElement>(null)
  useEffect(() => {
    let tm = 0
    const tick = () => {
      const el = ref.current
      if (el && (el.matches(':hover') || el.contains(document.activeElement))) tm = window.setTimeout(tick, 1000)
      else dismissToast(x.id)
    }
    tm = window.setTimeout(tick, x.undo ? 10_000 : 4000)
    return () => clearTimeout(tm)
  }, [x])
  return (
    <div ref={ref} className={cx('toast', x.bad && 'bad')} role={x.bad ? 'alert' : undefined}>
      {x.bad ? <TriangleAlert size={16} className="ic" aria-hidden /> : <Check size={16} className="ic" aria-hidden />}
      <span>{x.msg}</span>
      {x.undo && (
        <button
          type="button"
          onClick={() => {
            x.undo?.()
            dismissToast(x.id)
          }}
        >
          {t('c.undo')}
        </button>
      )}
    </div>
  )
}

export function Toasts() {
  const list = toastsAtom.use()
  return (
    <div className="toasts" aria-live="polite">
      {list.map((x) => (
        <ToastItem key={x.id} x={x} />
      ))}
    </div>
  )
}
