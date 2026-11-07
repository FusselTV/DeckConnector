import {
  Check,
  ChevronDown,
  EyeOff,
  Info,
  LoaderCircle,
  Lock,
  RefreshCw,
  Wifi,
  WifiHigh,
  WifiLow,
  WifiZero,
} from 'lucide-react'
import { useCallback, useEffect, useMemo, useState } from 'react'
import { api } from '../api/client'
import { wifiScanAtom } from '../app/state'
import { useUi } from '../app/ui'
import { fold } from '../lib/rank'
import type { WifiAp } from '../api/types'
import { groupScan } from '../lib/wifi'
import { cx } from '../lib/util'
import { describedBy, Field, SIcon } from './controls'
import { PwInput } from './InputAction'

export interface WifiChoice {
  ssid: string
  pass: string
  bssid: string
}
const sigIcon = (r: number) => (r >= -60 ? Wifi : r >= -70 ? WifiHigh : r >= -80 ? WifiLow : WifiZero)

/** Wi-Fi list grouped by name; experts expand a name to its access points (rules 26, 30). */
export function WifiPicker(p: {
  id: string
  value: WifiChoice
  onChange: (v: WifiChoice) => void
  current?: string
  curBssid?: string
  err?: string | null
  expert?: boolean
}) {
  const { t } = useUi()
  const [q, setQ] = useState('')
  const [scan, setScan] = useState<WifiAp[] | null>(null)
  const [failed, setFailed] = useState(false)
  const [hidden, setHidden] = useState(false)
  const [open, setOpen] = useState<Record<string, boolean>>({})
  const rescan = useCallback(() => {
    setScan(null)
    setFailed(false)
    api.wifiScan().then(
      (x) => {
        setScan(x)
        wifiScanAtom.set(x)
      },
      () => {
        setScan([])
        setFailed(true)
      },
    )
  }, [])
  useEffect(() => {
    let off = false
    api.wifiScan().then(
      (x) => {
        if (off) return
        setScan(x)
        wifiScanAtom.set(x)
      },
      () => {
        if (off) return
        setScan([])
        setFailed(true)
      },
    )
    return () => {
      off = true
    }
  }, [])
  const current = p.current
  const groups = useMemo(() => groupScan(scan ?? [], current), [scan, current])
  const qq = fold(q)
  const qm = qq.replace(/-/g, ':')
  const list = groups.filter(
    (g) => !qq || fold(g.ssid).includes(qq) || (p.expert && qm.length >= 2 && g.aps.some((a) => a.bssid.includes(qm))),
  )
  const { ssid, pass, bssid } = p.value
  const known = groups.some((g) => g.ssid === ssid)
  return (
    <div className="wifi" id={p.id}>
      <div className="inrow">
        <input
          id={`${p.id}-q`}
          className="in"
          type="search"
          placeholder={t(p.expert ? 'wifi.search_x' : 'wifi.search')}
          aria-label={t(p.expert ? 'wifi.search_x' : 'wifi.search')}
          value={q}
          onChange={(e) => setQ(e.target.value)}
        />
        <button
          type="button"
          className="ibtn"
          aria-label={t('wifi.rescan')}
          title={t('wifi.rescan')}
          onClick={rescan}
          disabled={scan === null}
        >
          <RefreshCw size={16} className={scan === null ? 'spin' : ''} aria-hidden />
        </button>
      </div>
      <p className="small muted row" style={{ gap: 6 }}>
        <Info size={14} aria-hidden />
        {t('wifi.band')}
      </p>
      {p.err && !hidden && !groups.some((g) => g.ssid === ssid && g.ssid !== p.current && !g.open) && (
        <span className="err" id={`${p.id}-err`} role="alert">
          {p.err}
        </span>
      )}
      <ul className="wlist" aria-label={t('wifi.list')} aria-busy={scan === null}>
        {scan === null && (
          <li className="wnet empty">
            <LoaderCircle size={16} className="spin" aria-hidden />
            {t('wifi.scanning')}
          </li>
        )}
        {list.map((g) => {
          const sel = g.ssid === ssid
          const cur = g.ssid === p.current
          const tone = g.rssi >= -70 ? 'ok' : g.rssi >= -80 ? 'warn' : 'bad'
          const many = !!p.expert && g.aps.length > 1
          const isOpen =
            many && (open[g.ssid] ?? ((qm.length >= 2 && g.aps.some((a) => a.bssid.includes(qm))) || (sel && !!bssid)))
          const pick = (b: string) => {
            setHidden(false)
            p.onChange({ ssid: g.ssid, pass: sel ? pass : '', bssid: b })
          }
          return (
            <li key={g.ssid} className={cx('wgrp', sel && 'sel')}>
              <div className="wrow">
                <button type="button" className="wnet" aria-pressed={sel} onClick={() => pick(sel ? bssid : '')}>
                  <SIcon icon={sigIcon(g.rssi)} tone={tone} label={`${g.rssi} dBm`} />
                  <span className="grow">
                    {g.ssid}
                    {cur && <span className="tag ok">{t('st.connected')}</span>}
                    {p.expert && (
                      <span className="bss">
                        {g.aps.length > 1
                          ? t('wifi.aps', { n: g.aps.length }) +
                            (sel && bssid ? ` · ${t('wifi.locked', { b: bssid })}` : '')
                          : g.aps[0]?.bssid}
                      </span>
                    )}
                  </span>
                  <span className="dbm">{g.rssi} dBm</span>
                  {g.open ? <span className="tag">{t('wifi.open')}</span> : <SIcon icon={Lock} size={14} label="WPA" />}
                  {sel && <Check size={16} className="ic acc" aria-hidden />}
                </button>
                {many && (
                  <button
                    type="button"
                    className="wexp"
                    aria-expanded={isOpen}
                    aria-label={t('wifi.aps_of', { s: g.ssid })}
                    title={t('wifi.aps_of', { s: g.ssid })}
                    onClick={() => setOpen((o) => ({ ...o, [g.ssid]: !isOpen }))}
                  >
                    <ChevronDown size={16} className={isOpen ? 'chev up' : 'chev'} aria-hidden />
                  </button>
                )}
              </div>
              {isOpen && (
                <ul className="wsubs" role="radiogroup" aria-label={t('wifi.aps_of', { s: g.ssid })}>
                  {[
                    { b: '', label: t('wifi.any'), detail: t('wifi.any_h'), dbm: null as number | null },
                    ...g.aps.map((a) => ({
                      b: a.bssid,
                      label: a.bssid,
                      detail: `${t('wifi.ch', { c: a.channel })} · 2,4 GHz`,
                      dbm: a.rssi,
                    })),
                  ].map((s) => (
                    <li key={s.b || 'any'}>
                      <button
                        type="button"
                        role="radio"
                        className="wsub"
                        aria-checked={sel && bssid === s.b}
                        onClick={() => pick(s.b)}
                      >
                        <span className="rdot" aria-hidden />
                        <span className="grow">
                          <span className={s.b ? 'mono' : 'wsl'}>{s.label}</span>
                          {s.b && s.b === p.curBssid && <span className="tag ok">{t('st.connected')}</span>}
                          <span className="bss">{s.detail}</span>
                        </span>
                        {s.dbm !== null && <span className="dbm">{s.dbm} dBm</span>}
                      </button>
                    </li>
                  ))}
                </ul>
              )}
              {sel && !cur && !g.open && (
                <div className="wjoin">
                  <Field id={`${p.id}-pass`} label={t('net.pass')} err={p.err}>
                    <PwInput
                      {...describedBy(`${p.id}-pass`, p.err)}
                      value={pass}
                      onChange={(e) => p.onChange({ ssid, pass: e.target.value, bssid })}
                    />
                  </Field>
                </div>
              )}
            </li>
          )
        })}
        {scan && !list.length && <li className="wnet empty">{failed ? t('c.not_sent') : t('wifi.none')}</li>}
        <li>
          <button
            type="button"
            className={cx('wnet', (hidden || (!!ssid && !known && ssid !== p.current)) && 'sel')}
            aria-expanded={hidden}
            onClick={() => setHidden(!hidden)}
          >
            <SIcon icon={EyeOff} />
            <span className="grow">{t('wifi.hidden')}</span>
            <ChevronDown size={16} className={hidden ? 'chev up' : 'chev'} aria-hidden />
          </button>
          {hidden && (
            <div className="wjoin grid2">
              <Field id={`${p.id}-ssid`} label={t('net.ssid')}>
                <input
                  {...describedBy(`${p.id}-ssid`)}
                  className="in"
                  maxLength={32}
                  value={known ? '' : ssid}
                  onChange={(e) => p.onChange({ ssid: e.target.value, pass, bssid: '' })}
                />
              </Field>
              <Field id={`${p.id}-hpass`} label={t('net.pass')} err={p.err}>
                <PwInput
                  {...describedBy(`${p.id}-hpass`, p.err)}
                  value={pass}
                  onChange={(e) => p.onChange({ ssid, pass: e.target.value, bssid: '' })}
                />
              </Field>
            </div>
          )}
        </li>
      </ul>
    </div>
  )
}
