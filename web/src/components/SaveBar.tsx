import { PenLine, Power } from 'lucide-react'
import { Fragment, useEffect, useState } from 'react'
import { putConfig } from '../app/actions'
import { draftsAtom, TABS, toast, type Draft, type Tab } from '../app/state'
import { useStore } from '../app/store'
import { useUi } from '../app/ui'
import { useLatest } from '../lib/util'

/** Registers a tab's in-place settings with the one save bar (rule 4). */
export function useDraft(tab: Tab, d: Draft) {
  const ref = useLatest(d)
  const { dirty, restart } = d
  useEffect(() => {
    // The bar reads the latest draft at save time; only dirty and restart need a re-render.
    draftsAtom.set((m) => ({ ...m, [tab]: { dirty, restart, ref } }))
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
  const { t, act, ask } = useUi()
  const dirty = useDirtyTabs()
  const [saving, setSaving] = useState(false)
  const locked = useStore((s) => s.conn !== 'live' || s.auth !== 'app' || !!s.restarting)
  useEffect(() => {
    if (!dirty.length) return
    const f = (e: BeforeUnloadEvent) => e.preventDefault()
    window.addEventListener('beforeunload', f)
    return () => window.removeEventListener('beforeunload', f)
  }, [dirty.length])
  if (!dirty.length) return null
  const all = draftsAtom.get()
  const restart = dirty.some((k) => all[k]?.restart)
  const save = async () => {
    const ds = dirty.flatMap((k) => all[k]?.ref.current ?? [])
    if (!ds.every((d) => d.check())) return
    const rs = ds.flatMap((d) => (d.restart && d.confirm ? [d.confirm] : []))
    const confirm =
      !rs.length ||
      (await ask({
        title: rs.length === 1 && rs[0] ? rs[0].title : t('cf.net'),
        body: rs.map((c, i) => <Fragment key={i}>{typeof c.body === 'string' ? <p>{c.body}</p> : c.body}</Fragment>),
        ok: t('c.save_restart'),
      }))
    if (!confirm) return
    const ok = await act(async () => {
      for (const d of ds) if (d.prepare && !(await d.prepare())) return false
      const patch = Object.assign({}, ...ds.map((d) => d.patch)) as Draft['patch']
      if (Object.keys(patch).length) await putConfig(patch, ds.find((d) => d.where)?.where)
      return true
    })
    if (!ok) return
    ds.forEach((d) => d.saved())
    if (!rs.length) toast(t('c.saved'))
  }
  return (
    <div className="savebar" role="region" aria-label={t('save.dirty')} aria-busy={saving}>
      <PenLine size={16} className="ic" aria-hidden />
      <span className="sbtxt">
        <b>{t('save.dirty')}</b>
        <span className="hide-s"> · {dirty.map((k) => t(`tab.${k}`)).join(', ')}</span>
      </span>
      <span className="sp" />
      <button
        type="button"
        className="btn ghost"
        disabled={saving}
        onClick={() => dirty.forEach((k) => all[k]?.ref.current.reset())}
      >
        {t('save.discard')}
      </button>
      <button
        type="button"
        className="btn pri"
        disabled={locked || saving}
        title={restart ? t('save.restart') : undefined}
        onClick={() => {
          setSaving(true)
          void save().finally(() => setSaving(false))
        }}
      >
        {restart && <Power size={16} aria-hidden />}
        {t('save.save')}
        <span className="hide-s">{restart ? ` ${t('save.and_restart')}` : ''}</span>
      </button>
    </div>
  )
}
