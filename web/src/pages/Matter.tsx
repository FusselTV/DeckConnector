import { House, Monitor, Plus, RotateCcw } from 'lucide-react'
import { useEffect, useState } from 'react'
import { api } from '../api/client'
import type { MatterCodes } from '../api/types'
import { expectRestart, refreshStatus } from '../app/connection'
import { toast } from '../app/state'
import { useStore } from '../app/store'
import { useUi } from '../app/ui'
import { Card } from '../components/Card'
import { Btn, Help, Switch } from '../components/controls'
import { CopyField } from '../components/InputAction'
import { qrPath } from '../lib/qr'

function Qr({ bits, label }: { bits: string; label: string }) {
  const { n, d } = qrPath(bits)
  if (!n) return null
  return (
    <svg className="mtqr" viewBox={`-4 -4 ${n + 8} ${n + 8}`} role="img" aria-label={label} shapeRendering="crispEdges">
      <rect x={-4} y={-4} width={n + 8} height={n + 8} fill="#fff" />
      <path d={d} fill="#000" />
    </svg>
  )
}

export function MatterCard() {
  const { t, act, ask } = useUi()
  const m = useStore((s) => s.status.matter)
  const [codes, setCodes] = useState<MatterCodes | null>(null)
  const running = !!m?.running
  useEffect(() => {
    if (running) void api.matter().then(setCodes, () => setCodes(null))
  }, [running])
  if (!m) return null
  const toggle = async (on: boolean) => {
    if (!on && !(await ask({ title: t('cf.mt_off'), body: t('cf.mt_off_b'), ok: t('btn.restart'), danger: true })))
      return
    await act(async () => {
      await api.setMatter({ on })
      if (on) await refreshStatus()
      else {
        await api.reboot()
        expectRestart()
      }
    })
  }
  const reset = async () => {
    if (!(await ask({ title: t('cf.mt_reset'), body: t('cf.mt_reset_b'), ok: t('mt.reset'), danger: true }))) return
    await act(async () => {
      await api.resetMatter()
      expectRestart()
    })
  }
  return (
    <Card
      id="card-matter"
      icon={House}
      title={t('mt.title')}
      right={<Help right label={t('mt.title')} text={t('mt.intro')} />}
    >
      <Switch id="matter-on" on={m.on} onChange={toggle} label={t('mt.on')} />
      {m.on && !running && <p className="warnline">{t('mt.failed')}</p>}
      {running && (
        <>
          <p className="small muted" id="matter-state">
            {m.controllers ? t('mt.paired', { n: m.controllers }) : t('mt.unpaired')}
            {m.pairing && ` ${t('mt.open')}`}
          </p>
          {codes && m.pairing && (
            <div className="mtcode">
              <Qr bits={codes.qr_bits} label={codes.qr} />
              <div className="field">
                <span className="flabel">{t('mt.code')}</span>
                <CopyField id="matter-code" value={codes.code} />
              </div>
            </div>
          )}
          <div className="row wrap acts">
            {!m.pairing && (
              <Btn icon={Plus} onClick={() => act(() => api.setMatter({ pair: true }).then(refreshStatus))}>
                {t(m.controllers ? 'mt.pair' : 'mt.pair_first')}
              </Btn>
            )}
            {m.pairing && (
              <Btn
                icon={Monitor}
                onClick={() => act(() => api.setMatter({ show: true }).then(() => toast(t('mt.shown'))))}
              >
                {t('mt.show')}
              </Btn>
            )}
            <Btn kind="danger" icon={RotateCcw} onClick={() => void reset()}>
              {t('mt.reset')}
            </Btn>
          </div>
        </>
      )}
    </Card>
  )
}
