import { Heart, X } from 'lucide-react'
import { useEffect, useRef, useState } from 'react'
import { putConfig } from '../app/actions'
import { useIssues } from '../app/issues'
import { starClose, starNever, starShow } from '../app/star'
import { useStore } from '../app/store'
import { useUi } from '../app/ui'
import { REPO } from '../lib/util'
import { SIcon } from './controls'
import { Notice } from './Notice'

/** One floating card on every tab: a newer release first, the star request only when no release waits. */
export function Popup() {
  const { offer } = useIssues()
  const ref = useRef<HTMLDivElement>(null)
  useEffect(() => {
    const el = ref.current
    if (!el) return
    // the page and the toasts make room for it
    const ro = new ResizeObserver(() => document.documentElement.style.setProperty('--pop-h', `${el.offsetHeight}px`))
    ro.observe(el)
    return () => ro.disconnect()
  }, [])
  return (
    <div className="pop" ref={ref}>
      {offer ? <Notice n={offer} /> : <StarNote />}
    </div>
  )
}

function StarNote() {
  const { t, act } = useUi()
  const cfg = useStore((s) => s.config)
  const fw = useStore((s) => s.status.firmware)
  const up = useStore((s) => s.status.uptime_s)
  const [now] = useState(() => Math.floor(Date.now() / 1000))
  const first = !!fw && cfg?.star_fw === ''
  useEffect(() => {
    // no toast for a failed silent save: the next visit tries again
    if (first) putConfig({ star_fw: fw }).catch(() => {})
  }, [first, fw])
  if (!cfg || !fw || !starShow(cfg, fw, up, now)) return null
  const link = (href: string, label: string) => (
    <a className="btn sm" href={href} target="_blank" rel="noreferrer">
      {label}
    </a>
  )
  return (
    <div className="notice info star" id="ntc-star" role="status">
      <SIcon icon={Heart} size={20} />
      <span className="ntext">{t('star.text')}</span>
      <div className="nacts">
        {link(REPO, t('star.gh'))}
        {link('https://github.com/sponsors/FusselTV', 'GitHub Sponsors')}
        {link('https://ko-fi.com/fusseltv', 'Ko-fi')}
        <button type="button" className="btn sm ghost" onClick={() => void act(() => putConfig(starNever(fw)))}>
          {t('star.never')}
        </button>
      </div>
      <button
        type="button"
        className="ibtn ghost sm"
        aria-label={t('star.close')}
        title={t('star.close')}
        onClick={() => void act(() => putConfig(starClose(fw, Math.floor(Date.now() / 1000))))}
      >
        <X size={16} aria-hidden />
      </button>
    </div>
  )
}
