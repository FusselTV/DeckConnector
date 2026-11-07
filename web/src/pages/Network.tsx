import { CircleAlert, EthernetPort, Network as NetIcon, Tag as TagIcon, Unplug, Wifi } from 'lucide-react'
import { useState } from 'react'
import { api } from '../api/client'
import type { IpConf } from '../api/types'
import { refreshConfig, refreshStatus } from '../app/connection'
import { reachList, sigWord } from '../app/derive'
import { getState, useStore } from '../app/store'
import { useUi } from '../app/ui'
import { Card } from '../components/Card'
import { describedBy, Field, Seg, SIcon, Switch } from '../components/controls'
import { CopyField } from '../components/InputAction'
import { Row } from '../components/Row'
import { useDraft } from '../components/SaveBar'
import { WifiPicker, type WifiChoice } from '../components/WifiPicker'
import type { Key } from '../i18n/types'
import { ls } from '../lib/util'
import { ipErrors, NAME_RE, nameError, splitCidr, wifiError, type IpErrors } from '../lib/validate'

export const EMPTY: IpConf = { dhcp: true, ip: '', mask: '', gateway: '', dns: '' }
const LABEL: Record<'ip' | 'mask' | 'gateway' | 'dns', Key> = {
  ip: 'net.ip',
  mask: 'net.mask',
  gateway: 'net.gw',
  dns: 'net.dns',
}

export function NetBlock({ id, v, set, err }: { id: string; v: IpConf; set: (v: IpConf) => void; err: IpErrors }) {
  const { t } = useUi()
  return (
    <div className="stack" style={{ gap: 10 }}>
      <div className="field">
        <span className="flabel">{t('net.ipmode')}</span>
        <Seg
          id={`${id}-dhcp`}
          label={t('net.ipmode')}
          value={v.dhcp ? 'auto' : 'static'}
          onChange={(m) => set({ ...v, dhcp: m === 'auto' })}
          options={[
            ['auto', t('net.dhcp')],
            ['static', t('net.static')],
          ]}
        />
      </div>
      {!v.dhcp && (
        <div className="grid2">
          {(['ip', 'mask', 'gateway', 'dns'] as const).map((k) => {
            const e = err[k] ? t(err[k]) : null
            return (
              <Field
                key={k}
                id={`${id}-${k}`}
                label={t(LABEL[k])}
                err={e}
                help={k === 'ip' ? t('net.cidr') : undefined}
              >
                <input
                  {...describedBy(`${id}-${k}`, e)}
                  className="in mono"
                  inputMode="decimal"
                  value={v[k]}
                  onChange={(ev) => set({ ...v, [k]: ev.target.value })}
                  onBlur={(ev) => {
                    if (k !== 'ip') return
                    const c = splitCidr(ev.target.value)
                    if (c.mask) set({ ...v, ip: c.ip, mask: c.mask })
                  }}
                />
              </Field>
            )
          })}
        </div>
      )}
    </div>
  )
}

/** POST /api/wifi/join, then the status until the device says ok or failed (at most 20 s). 'lost' means the
 *  page lost the device on the way, which happens when it is reached over the WLAN being changed. */
export async function joinWifi(w: WifiChoice): Promise<string | null> {
  await api.wifiJoin(w.ssid, w.pass, w.bssid)
  let seen = false
  for (let i = 0; i < 20; i++) {
    await new Promise((r) => setTimeout(r, 1000))
    seen = (await refreshStatus()) || seen
    const j = getState().status.network?.wlan
    if (j?.join === 'ok') return null
    if (j?.join === 'failed') return j.join_error || 'failed'
  }
  return seen ? 'timeout' : 'lost'
}

export function Network() {
  const { t, go } = useUi()
  const st = useStore((s) => s.status)
  const cfg = useStore((s) => s.config)
  const n = st.network
  const base = {
    name: cfg?.name ?? '',
    lan: cfg?.lan ?? EMPTY,
    wlan: cfg?.wlan ?? EMPTY,
    wifi: { ssid: cfg?.wifi_ssid ?? '', pass: '', bssid: cfg?.wifi_bssid ?? '' },
  }
  // Only the fields the person touched, over the live values: a save never sends back a stale copy.
  const [d, setD] = useState<Partial<typeof base>>({})
  const [expert, setExpert] = useState(() => ls.get('dc.bssid', false))
  const [joinErr, setJoinErr] = useState<string | null>(null)
  const v = { ...base, ...d }
  const set = (x: Partial<typeof base>) => setD((o) => ({ ...o, ...x }))
  const same = (a: unknown, b: unknown) => JSON.stringify(a) === JSON.stringify(b)
  const err = { lan: ipErrors(v.lan), wlan: ipErrors(v.wlan), name: nameError(v.name) }
  const wifiChanged = v.wifi.ssid !== base.wifi.ssid || v.wifi.bssid !== base.wifi.bssid || !!v.wifi.pass
  const patch = {
    ...(d.name !== undefined && v.name !== base.name ? { name: v.name } : {}),
    ...(d.lan && !same(v.lan, base.lan) ? { lan: v.lan } : {}),
    ...(d.wlan && !same(v.wlan, base.wlan) ? { wlan: v.wlan } : {}),
  }
  const restart = Object.keys(patch).length > 0
  const where = reachList(st, !!cfg?.https_only, v.name)
  useDraft('network', {
    dirty: restart || wifiChanged,
    restart,
    reset: () => {
      setD({})
      setJoinErr(null)
    },
    saved: () => setD({}),
    check: () => {
      const bad = (['lan', 'wlan'] as const).find((k) => Object.keys(err[k]).length)
      const wifiBad = wifiChanged && wifiError(v.wifi.ssid, v.wifi.pass)
      if (!bad && !err.name && !wifiBad) return true
      go('network')
      if (wifiBad) setJoinErr(t(wifiBad))
      setTimeout(() => {
        const r = bad ? document.getElementById(`row-${bad}`) : wifiBad ? document.getElementById('row-wlan') : null
        if (r instanceof HTMLDetailsElement) r.open = true
        document.querySelector<HTMLElement>('#card-conn [aria-invalid="true"], #name[aria-invalid="true"]')?.focus()
      }, 80)
      return false
    },
    confirm: {
      title: t('cf.net'),
      body: (
        <>
          <p>{t('cf.net_b')}</p>
          {where.map((w) => (
            <code key={w}>{w}</code>
          ))}
        </>
      ),
    },
    patch,
    // POST /api/wifi/join tests a new Wi-Fi and saves it only when it works, so a typo keeps the old network.
    prepare: async () => {
      if (!wifiChanged || !v.wifi.ssid) return true
      setJoinErr(null)
      const e = await joinWifi(v.wifi)
      if (e === null) {
        await refreshConfig()
        if (!restart) setD(({ wifi: _w, ...o }) => o)
        return true
      }
      setJoinErr(e === 'lost' ? t('wifi.lost') : t('wiz.test_bad') + ` (${e})`)
      return false
    },
  })
  const lanSt = !n?.lan?.cable ? (
    <>
      <SIcon icon={Unplug} size={15} />
      {t('meter.no_cable')}
    </>
  ) : n.lan.dead ? (
    <>
      <SIcon icon={CircleAlert} tone="bad" size={15} />
      {t('meter.dead')}
    </>
  ) : (
    <>
      <span className="mono">{n.lan.ip}</span> · {t(base.lan.dhcp ? 'net.addr_auto' : 'net.addr_static')}
    </>
  )
  const w = n?.wlan
  const wlanSt = w?.ip ? (
    <>
      <span className="mono">{w.ssid}</span>
      {w.rssi ? (
        <>
          {` · ${t(sigWord(w.rssi))}`}
          <span className="wide mono">{` (${w.rssi} dBm)`}</span>
        </>
      ) : null}
    </>
  ) : (
    t('arm.off')
  )
  return (
    <div className="cards">
      <Card id="card-conn" icon={NetIcon} title={t('net.conn')}>
        <Row
          id="row-lan"
          group="net"
          icon={EthernetPort}
          tone={!n?.lan?.cable ? '' : n.lan.dead ? 'bad' : 'ok'}
          title={t('net.lan')}
          status={lanSt}
          open={!base.lan.dhcp}
        >
          <NetBlock id="lan" v={v.lan} set={(lan) => set({ lan })} err={err.lan} />
        </Row>
        <Row
          id="row-wlan"
          group="net"
          icon={Wifi}
          tone={w?.ip ? ((w.rssi ?? 0) < -72 ? 'warn' : 'ok') : ''}
          title={t('net.wlan')}
          status={wlanSt}
          open={!base.wlan.dhcp}
        >
          <Switch
            id="wifi-x"
            on={expert}
            onChange={(x) => {
              setExpert(x)
              ls.set('dc.bssid', x)
            }}
            label={t('wifi.expert')}
            hint={t('wifi.expert_h')}
          />
          {expert && w?.bssid && (
            <p className="small muted">{t('wifi.joined', { b: w.bssid, c: w.channel ?? '', r: w.rssi ?? '' })}</p>
          )}
          <WifiPicker
            id="wifi"
            expert={expert}
            value={v.wifi}
            current={w?.ssid}
            curBssid={w?.bssid}
            err={joinErr}
            onChange={(wifi) => {
              setJoinErr(null)
              set({ wifi })
            }}
          />
          <NetBlock id="wlan" v={v.wlan} set={(wlan) => set({ wlan })} err={err.wlan} />
        </Row>
      </Card>
      <Card id="card-names" icon={TagIcon} title={t('net.names')}>
        <Field id="name" label={t('net.name')} err={err.name && t(err.name)}>
          <input
            {...describedBy('name', err.name && t(err.name))}
            className="in mono"
            maxLength={31}
            pattern={NAME_RE.source}
            value={v.name}
            onChange={(e) => set({ name: e.target.value.trim() })}
            spellCheck={false}
          />
        </Field>
        {(() => {
          const sch = cfg?.https_only ? 'https' : 'http'
          const n = st.network
          const rows: [string, string, string?][] = [
            ...(n?.lan?.cable && n.lan.ip ? [[t('net.at_lan'), `${sch}://${n.lan.ip}`] as [string, string]] : []),
            ...(n?.wlan?.ip ? [[t('net.at_wlan'), `${sch}://${n.wlan.ip}`] as [string, string]] : []),
            ...(base.name
              ? [[t('net.at_mdns'), `${sch}://${base.name}.local`, t('net.at_mdns_h')] as [string, string, string]]
              : []),
          ]
          return rows.map(([label, u, hint]) => (
            <div key={u} className="field">
              <span className="flabel">{label}</span>
              <CopyField value={u} link />
              {hint && <span className="hint">{hint}</span>}
            </div>
          ))
        })()}
      </Card>
    </div>
  )
}
