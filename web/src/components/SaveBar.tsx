import { PenLine, Power } from 'lucide-react'
import { useEffect } from 'react'
import { useLatest } from '../lib/util'
import { draftsAtom, TABS, type Draft, type Tab } from '../app/state'
import { useStore } from '../app/store'
import { useUi } from '../app/ui'

/** Registers a tab's in-place settings with the one save bar (rule 4). A draft that restarts asks for itself. */
export function useDraft(tab: Tab, d: Draft) {
  const ref = useLatest(d)
  const { dirty, restart } = d
  useEffect(() => {
    draftsAtom.set((m) => ({
      ...m,
      [tab]: { dirty, restart, save: () => ref.current.save(), reset: () => ref.current.reset() },
    }))
  }, [tab, dirty, restart, ref])
  useEffect(
    () => () =>
      draftsAtom.set((m) => {
        const n = { ...m }
        n[tab] = undefined
        return n
      }),
    [tab],
  )
}

export const useDirtyTabs = () => {
  const key = draftsAtom.use((m) => TABS.filter((k) => m[k]?.dirty).join(','))
  return key ? (key.split(',') as Tab[]) : []
}

export function SaveBar() {
  const { t } = useUi()
  const dirty = useDirtyTabs()
  const locked = useStore((s) => s.conn !== 'live' || s.auth !== 'app' || !!s.restarting)
  if (!dirty.length) return null
  const drafts = draftsAtom.get()
  const restart = dirty.some((k) => drafts[k]?.restart)
  const save = async () => {
    // Settings that apply at once go first, so a restart never cuts them off.
    const order = [...dirty].sort((a, b) => Number(drafts[a]?.restart) - Number(drafts[b]?.restart))
    for (const k of order) if (!(await drafts[k]?.save())) return
  }
  return (
    <div className="savebar" role="region" aria-label={t('save.dirty')}>
      <PenLine size={16} className="ic" aria-hidden />
      <span className="sbtxt">
        <b>{t('save.dirty')}</b>
        <span className="hide-s"> · {dirty.map((k) => t(`tab.${k}`)).join(', ')}</span>
      </span>
      <span className="sp" />
      <button type="button" className="btn ghost" onClick={() => dirty.forEach((k) => drafts[k]?.reset())}>
        {t('save.discard')}
      </button>
      <button
        type="button"
        className="btn pri"
        disabled={locked}
        title={restart ? t('save.restart') : undefined}
        onClick={() => void save()}
      >
        {restart && <Power size={16} aria-hidden />}
        {t('save.save')}
        <span className="hide-s">{restart ? ` ${t('save.and_restart')}` : ''}</span>
      </button>
    </div>
  )
}
