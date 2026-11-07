import { useMemo } from 'react'
import { api, ApiError } from '../api/client'
import type { ConfigPatch, Source } from '../api/types'
import { atom } from '../lib/atom'
import { expectRestart, refreshConfig, refreshStatus, stopSession } from './connection'
import { hk, hostKey, hostLabel, hostName } from './derive'
import { loginReasonAtom, toast } from './state'
import { dispatch, getState } from './store'
import { useUi } from './ui'

/** The source being handed the deck, until the next status shows it drawing. */
export const pendingAtom = atom<string | null>(null)

/** PUT /api/config, then the stored values: the page never claims more than the device confirmed (api-gaps #11).
 *  After a restart the device's own `reach` list says where to find it; `where` is the page's guess for older
 *  firmware. */
export async function putConfig(patch: ConfigPatch, where?: string[]) {
  const r = await api.putConfig(patch)
  if (r.config) dispatch({ type: 'config', config: r.config })
  if (r.restart) expectRestart(r.reach?.length ? r.reach : where)
  else await Promise.all([r.config ? undefined : refreshConfig(), refreshStatus()])
  return r
}

export function useActions() {
  const { t, act, ask } = useUi()
  return useMemo(() => {
    // The answer names the host that asked; 409 means another host asks by now, or none.
    const answer = (accept: boolean, ip: string, done: string) =>
      act(async () => {
        try {
          await api.dock({ accept, ip })
          toast(done)
        } catch (e) {
          if (!(e instanceof ApiError && e.status === 409)) throw e
          toast(t('pair.gone'), { bad: true })
        }
        await refreshStatus()
      })
    const select = async (s: Source) => {
      const controller = s.kind === 'unknown' ? 'app' : s.kind
      pendingAtom.set(hk(s))
      try {
        await putConfig({ controller, host: hostKey(s) })
      } finally {
        pendingAtom.set(null)
      }
    }
    const pick = async (s: Source) => {
      if (s.kind === 'unknown' || s.chosen || pendingAtom.get()) return
      const cur = (getState().status.sources ?? []).find((x) => x.drawing)
      if (
        cur &&
        !(await ask({
          title: t('sw.title'),
          body: t('sw.body', { cur: hostLabel(cur, t), next: hostLabel(s, t) }),
          ok: t('sw.ok'),
        }))
      )
        return
      const prev = cur ?? (getState().status.sources ?? []).find((x) => x.chosen)
      await act(async () => {
        await select(s)
        toast(t('sw.done', { host: hostLabel(s, t) }), prev ? { undo: () => void act(() => select(prev)) } : {})
      })
    }
    return {
      pick,
      brightness: (n: number) => act(() => putConfig({ brightness: n })),
      menu: (serial?: string) =>
        act(async () => {
          await api.menu(serial)
          toast(t('ov.menu_done'))
        }),
      reconnect: async (n: number) => {
        if (
          !(await ask({
            title: t('cf.reconnect'),
            body: n > 1 ? t('cf.reconnect_b_n', { n }) : t('cf.reconnect_b'),
            ok: t('btn.reconnect'),
          }))
        )
          return
        await act(() => api.reconnect())
      },
      reboot: async () => {
        if (!(await ask({ title: t('cf.reboot'), body: t('cf.reboot_b'), ok: t('btn.restart'), danger: true }))) return
        await act(async () => {
          await api.reboot()
          expectRestart()
        })
      },
      accept: (ip: string, name: string) => answer(true, ip, t('ban.paired', { name })),
      deny: (ip: string) => answer(false, ip, t('ban.denied', { ip })),
      retry: (s: Source) =>
        act(async () => {
          toast(t('act.retrying', { host: hostName(s) }))
          await api.retrySource(s.host)
        }),
      toSatellite: (s: Source) =>
        act(async () => {
          await putConfig({ satellite: s.host })
          await api.dock({ forget: s.host })
          toast(t('src.to_sat_done', { host: s.host }))
        }),
      logout: async () => {
        await api.logout().catch(() => undefined)
        stopSession()
        loginReasonAtom.set('logout')
        dispatch({ type: 'auth', auth: 'login' })
      },
    }
  }, [t, act, ask])
}
