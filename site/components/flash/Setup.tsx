'use client'
import { useEffect, useRef, useState } from 'react'
import { Check, Lock, RefreshCw, RotateCcw } from 'lucide-react'
import { fill, type Copy } from './copy'
import { ImprovError, PROVISIONED, type AccessPoint, type Improv } from './improv'
import { Btn, Field, Fix, Note, Secret, Spinner, Toggle, input, mono } from './ui'

export interface Result {
  urls: string[]
  name: string
  later?: boolean
}

const bytes = (v: string) => new TextEncoder().encode(v).length

type Err = { text: string; fix?: string }

export function improvErr(c: Copy, e: unknown, ssid = ''): Err {
  if (!(e instanceof ImprovError)) return { text: e instanceof Error ? e.message : String(e), fix: 'setup-no-answer' }
  const t: Record<number, string> = { 1: c.err1, 2: c.err2, 3: c.err3, 4: c.err4, 255: c.errFF, [-1]: c.errTimeout }
  const fix = e.code === 1 || e.code === 3 ? 'wifi-join' : 'setup-no-answer'
  return { text: fill(t[e.code] ?? c.errFF, { ssid }), fix }
}

function Signal({ rssi }: { rssi: number }) {
  const bars = rssi >= -55 ? 4 : rssi >= -65 ? 3 : rssi >= -75 ? 2 : 1
  return (
    <svg viewBox="0 0 16 12" className="h-3 w-4 shrink-0" role="img" aria-label={`${rssi} dBm`}>
      {[0, 1, 2, 3].map((i) => (
        <rect key={i} x={i * 4} y={9 - i * 3} width="3" height={3 + i * 3} rx="0.5" fill={i < bars ? 'var(--fg-2)' : 'var(--line-2)'} />
      ))}
    </svg>
  )
}

export function Setup({ c, im, version, lang, onDone }: { c: Copy; im: Improv; version: string; lang?: string; onDone: (r: Result) => void }) {
  const [phase, setPhase] = useState<'boot' | 'noboot' | 'form' | 'busy'>('boot')
  const [msg, setMsg] = useState('')
  const [board, setBoard] = useState<{ name: string; version: string; urls: string[]; lan: boolean } | null>(null)
  const [aps, setAps] = useState<AccessPoint[] | null>(null)
  const [scanning, setScanning] = useState(false)
  const [useWifi, setUseWifi] = useState(false)
  const [joined, setJoined] = useState<string | null>(null)
  const [ssid, setSsid] = useState('')
  const [other, setOther] = useState(false)
  const [wpass, setWpass] = useState('')
  const [tried, setTried] = useState(false)
  const [err, setErr] = useState<Err | null>(null)
  const [wifiErr, setWifiErr] = useState<Err | null>(null)
  const started = useRef(false)

  const secured = !other && !!aps?.some((a) => a.ssid === ssid && a.secured)
  const wifiE =
    !useWifi || joined
      ? null
      : !ssid.trim() || bytes(ssid) > 32
        ? c.wifiPick
        : !wpass && secured
          ? c.wifiPassNeed
          : wpass && !(wpass.length >= 8 && wpass.length <= 63) && !/^[0-9a-f]{64}$/i.test(wpass)
            ? c.wifiPassLen
            : null

  const scan = async () => {
    setScanning(true)
    try {
      const list = await im.scan()
      setAps(list)
      setSsid((s) => s || list[0]?.ssid || '')
      if (!list.length) setOther(true)
    } catch (e) {
      setAps([])
      setErr(improvErr(c, e))
    } finally {
      setScanning(false)
    }
  }

  const read = async () => {
    const info = await im.info()
    const st = await im.state()
    const net = await im.network().catch(() => null)
    const urls = net?.urls.length ? net.urls : st.url ? [st.url] : []
    return { name: info.name, version: info.version, urls, lan: net ? net.lan : st.state === PROVISIONED, online: st.state === PROVISIONED }
  }

  const boot = async () => {
    setPhase('boot')
    setErr(null)
    try {
      await im.waitReady(15000)
      if (lang) await im.setup('', '', false, lang).catch(() => undefined)
      const b = await read()
      setBoard(b)
      setUseWifi(!b.online)
      setPhase('form')
      if (!b.online) void scan()
    } catch {
      setPhase('noboot')
    }
  }

  useEffect(() => {
    if (started.current) return
    started.current = true
    void boot()
  })

  const save = async () => {
    setTried(true)
    if (wifiE || !board) {
      setTimeout(() => document.querySelector<HTMLElement>('#flash-setup [aria-invalid="true"]')?.focus(), 0)
      return
    }
    setErr(null)
    setWifiErr(null)
    setPhase('busy')
    try {
      if (useWifi && !joined) {
        setMsg(fill(c.joining, { ssid }))
        const w = wpass
        setWpass('')
        try {
          await im.wifi(ssid, w)
        } catch (e) {
          setWifiErr(improvErr(c, e, ssid))
          setPhase('form')
          setTimeout(() => document.getElementById('f-wpass')?.focus(), 0)
          return
        }
        setJoined(ssid)
      }
      setMsg(c.reading)
      const b = await read()
      onDone({ urls: b.urls, name: b.name })
    } catch (e) {
      setErr(improvErr(c, e, ssid))
      setPhase('form')
    }
  }

  if (phase === 'boot' || phase === 'noboot')
    return (
      <div className="space-y-5">
        <Note lvl="ok" title={fill(c.installed, { v: version })} />
        <h2 className="text-xl font-semibold">{c.setupT}</h2>
        {phase === 'boot' ? (
          <div className="flex items-start gap-3">
            <Spinner className="mt-1 text-amber" />
            <div>
              <p className="font-medium">{c.booting}</p>
              <p className="text-sm text-fg-3">{c.bootingHint}</p>
            </div>
          </div>
        ) : (
          <>
            <Note lvl="bad" title={c.noBoot}>
              {c.noBootDo}
              <Fix c={c} id="setup-no-answer" />
            </Note>
            <div className="flex flex-wrap gap-3">
              <Btn onClick={() => void boot()}>
                <RotateCcw aria-hidden />
                {c.retry}
              </Btn>
              <Btn kind="ghost" onClick={() => onDone({ urls: [], name: '', later: true })}>
                {c.later}
              </Btn>
            </div>
          </>
        )}
      </div>
    )

  const busy = phase === 'busy'
  const inv = (e: string | null) => ({ 'aria-invalid': tried && !!e, 'aria-describedby': undefined })
  return (
    <form
      id="flash-setup"
      className="space-y-7"
      onSubmit={(e) => {
        e.preventDefault()
        void save()
      }}
      noValidate
    >
      <div className="space-y-4">
        <Note lvl="ok" title={fill(c.installed, { v: version })} />
        <div className="flex flex-wrap items-center justify-between gap-3 rounded-xl border border-line bg-well px-4 py-3">
          <div className="min-w-0">
            <p className={`${mono} font-semibold`}>{board?.name}</p>
            <p className={`${mono} text-fg-3`}>{board?.version}</p>
          </div>
          <span className={`flex items-center gap-2 text-sm ${board?.urls.length ? 'text-ok-fg' : 'text-fg-3'}`}>
            <span className={`size-2 rounded-full ${board?.urls.length ? 'bg-ok' : 'bg-fg-3'}`} aria-hidden />
            {board?.urls.length ? `${c.online} · ${board.urls[0]?.replace(/^https?:\/\//, '') ?? ''}` : c.offline}
          </span>
        </div>
        <h2 className="text-xl font-semibold">{c.setupT}</h2>
        <p className="text-fg-2">{c.setupLead}</p>
      </div>

      <fieldset className="space-y-4" disabled={busy}>
        {joined ? (
          <Note lvl="ok" title={`${c.wifi} · ${joined}`} />
        ) : (
          <>
            <Toggle
              id="f-usewifi"
              label={c.wifiUse}
              hint={board?.lan ? c.wifiUseLan : c.wifiUseNone}
              checked={useWifi}
              onChange={(e) => {
                setUseWifi(e.target.checked)
                if (e.target.checked && !aps) void scan()
              }}
            />
            {useWifi && (
              <div className="space-y-4 border-s-2 border-line ps-4 sm:ms-2">
                <div className="space-y-2">
                  <div className="flex items-center justify-between gap-3">
                    <span className="text-sm font-medium" id="wl-l">
                      {c.network}
                    </span>
                    <button type="button" onClick={() => void scan()} disabled={scanning} className="inline-flex items-center gap-1.5 text-sm text-fg-2 hover:text-fg disabled:opacity-60 [&_svg]:size-3.5">
                      <RefreshCw className={scanning ? 'animate-spin' : ''} aria-hidden />
                      {scanning ? c.scanning : c.scan}
                    </button>
                  </div>
                  <div role="radiogroup" aria-labelledby="wl-l" className="max-h-60 overflow-auto rounded-lg border border-line-2 bg-well">
                    {aps === null && scanning && (
                      <p className="flex items-center gap-2 p-3 text-sm text-fg-3">
                        <Spinner /> {c.scanning}
                      </p>
                    )}
                    {aps?.length === 0 && !scanning && <p className="p-3 text-sm text-fg-3">{c.scanNone}</p>}
                    {aps?.map((a) => (
                      <label key={a.ssid} className="flex cursor-pointer items-center gap-3 border-b border-line px-3 py-2.5 last:border-0 hover:bg-panel-2 has-checked:bg-amber-soft">
                        <input type="radio" name="ssid" checked={!other && ssid === a.ssid} onChange={() => (setOther(false), setSsid(a.ssid))} className="size-4 accent-(--amber)" />
                        <span className="min-w-0 flex-1 truncate">{a.ssid}</span>
                        {a.secured ? <Lock className="size-3.5 text-fg-3" aria-hidden /> : <span className="text-xs text-fg-3">{c.openNet}</span>}
                        <Signal rssi={a.rssi} />
                      </label>
                    ))}
                    <label className="flex cursor-pointer items-center gap-3 px-3 py-2.5 hover:bg-panel-2 has-checked:bg-amber-soft">
                      <input type="radio" name="ssid" checked={other} onChange={() => (setOther(true), setSsid(''))} className="size-4 accent-(--amber)" />
                      <span className="text-fg-2">{c.other}</span>
                    </label>
                  </div>
                </div>
                {other && (
                  <Field id="f-ssid" label={c.ssid} err={tried && wifiE === c.wifiPick ? wifiE : null}>
                    <input id="f-ssid" className={input} value={ssid} maxLength={32} onChange={(e) => setSsid(e.target.value)} autoComplete="off" spellCheck={false} {...inv(wifiE === c.wifiPick ? wifiE : null)} />
                  </Field>
                )}
                {!other && tried && wifiE === c.wifiPick && <p className="text-sm text-bad-fg">{wifiE}</p>}
                <Field id="f-wpass" label={c.wifiPass} err={
                    (tried && wifiE !== c.wifiPick ? wifiE : null) ??
                    (wifiErr && (
                      <>
                        {wifiErr.text}
                        {wifiErr.fix && <Fix c={c} id={wifiErr.fix} />}
                      </>
                    ))
                  }>
                  <Secret
                    c={c}
                    id="f-wpass"
                    value={wpass}
                    maxLength={64}
                    onChange={(e) => setWpass(e.target.value)}
                    aria-invalid={!!wifiErr || (tried && !!wifiE && wifiE !== c.wifiPick)}
                  />
                </Field>
              </div>
            )}
          </>
        )}
      </fieldset>

      {err && (
        <Note lvl="bad" title={err.text}>
          {err.fix && <Fix c={c} id={err.fix} />}
        </Note>
      )}

      <div className="flex flex-wrap items-center gap-x-5 gap-y-3">
        <Btn type="submit" disabled={busy}>
          {busy ? <Spinner /> : <Check aria-hidden />}
          {busy ? msg : c.save}
        </Btn>
        {!busy && (
          <button type="button" onClick={() => onDone({ urls: board?.urls ?? [], name: board?.name ?? '', later: true })} className="text-sm text-fg-2 underline decoration-line-2 underline-offset-4 hover:text-fg">
            {c.later}
          </button>
        )}
      </div>
    </form>
  )
}
