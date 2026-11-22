import { Heart, Star, X } from 'lucide-react'
import { useEffect, useRef, useState } from 'react'
import { putConfig } from '../app/actions'
import { useIssues } from '../app/issues'
import { starClose, starShow } from '../app/star'
import { useStore } from '../app/store'
import { useUi } from '../app/ui'
import { REPO } from '../lib/util'
import { ActBtn, type PopData } from './Notice'

const SPONSORS = 'https://github.com/sponsors/FusselTV'
const open = (url: string) => () => window.open(url, '_blank', 'noopener')

/** One floating card on every tab: a newer release first, the star request only when no release waits. */
export function Popup() {
  const { offer } = useIssues()
  const star = useStar()
  const ref = useRef<HTMLDivElement>(null)
  useEffect(() => {
    const el = ref.current
    if (!el) return
    // the page and the toasts make room for it
    const ro = new ResizeObserver(() => document.documentElement.style.setProperty('--pop-h', `${el.offsetHeight}px`))
    ro.observe(el)
    return () => ro.disconnect()
  }, [])
  const p = offer ?? star
  return (
    <div className="pop" ref={ref}>
      {p && <PopCard p={p} />}
    </div>
  )
}

export function PopCard({ p }: { p: PopData }) {
  const { t } = useUi()
  return (
    <section className="popcard" id={`ntc-${p.id}`} role="status">
      <div className="pophead">
        <b>{p.title}</b>
        <button
          type="button"
          className="ibtn ghost sm"
          aria-label={t('c.close')}
          title={t('c.close')}
          onClick={p.close}
        >
          <X size={16} aria-hidden />
        </button>
      </div>
      {p.text && <p>{p.text}</p>}
      <div className="popacts">
        {p.acts.map((a) => (
          <ActBtn key={a.label} a={a} sm={false} />
        ))}
      </div>
    </section>
  )
}

function useStar(): PopData | null {
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
  return {
    id: 'star',
    title: t('star.title'),
    text: t('star.text'),
    acts: [
      { label: t('star.gh'), icon: Star, pri: true, run: open(REPO) },
      { label: t('star.support'), icon: Heart, run: open(SPONSORS) },
    ],
    close: () => void act(() => putConfig(starClose(fw, Math.floor(Date.now() / 1000)))),
  }
}
