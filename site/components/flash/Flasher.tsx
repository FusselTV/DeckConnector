'use client'
import { useCallback, useEffect, useState, useSyncExternalStore } from 'react'
import { ArrowRight, BookOpen, Check, ChevronRight, Copy as CopyIcon, Cpu, ExternalLink, FolderOpen, Laptop, Monitor, RotateCcw, Smartphone, Usb } from 'lucide-react'
import { prefix, type Lang } from '~/lib/i18n'
import { copy, fill, type Copy } from './copy'
import { Console, Esp, pickPort, serial, type Chip, type Progress, type WebSerialPort } from './device'
import { FirmwareError, loadBuild, localSource, siteSource, totalSize, type Image, type Manifest, type Mode, type Source } from './firmware'
import { Improv } from './improv'
import { Setup, type Result } from './Setup'
import { BoardArt, Bar, Btn, Fix, LinkBtn, LogPanel, Note, Spinner, Stepper, card, duration, kib, mono } from './ui'

type Support = 'ok' | 'mobile' | 'browser' | 'insecure'

function detect(): Support {
  const ua = navigator.userAgent
  const uaData = (navigator as Navigator & { userAgentData?: { mobile?: boolean } }).userAgentData
  if (uaData?.mobile || /Android|iPhone|iPad|iPod|Mobile/i.test(ua) || (/Macintosh/.test(ua) && navigator.maxTouchPoints > 1)) return 'mobile'
  if (!window.isSecureContext) return 'insecure'
  return serial() ? 'ok' : 'browser'
}
const noop = () => () => undefined

type Step = 'connect' | 'install' | 'flash' | 'setup' | 'ready'
type ConnErr = { title: string; text?: string; fix: string; buttons?: boolean }
type PartState = { status: 'wait' | 'write' | 'verify' | 'ok'; done: number; total: number }
interface Flash {
  erase: 'run' | 'ok' | null
  parts: PartState[]
  start: number
  bytesAt: number
  err: string | null
}

function FwErr({ c, err }: { c: Copy; err: string }) {
  return (
    <Note lvl="bad" title={err}>
      {err !== c.fwLocalBad && <Fix c={c} id="firmware-download" />}
    </Note>
  )
}

const rev = (r: number) => `${Math.floor(r / 100)}.${r % 100}`
const partName = (c: Copy, path: string) => (path.includes('factory') ? c.partFactory : path.includes('ota_data') ? c.partOta : c.partApp)

function connErr(c: Copy, e: unknown): ConnErr {
  const msg = e instanceof Error ? `${e.name} ${e.message}` : String(e)
  if (/InvalidStateError|Failed to open|NetworkError.*open|already open/i.test(msg)) return { title: c.errBusy, text: c.errBusyDo, fix: 'port-busy' }
  if (/lost|disconnected/i.test(msg)) return { title: c.errLost, fix: 'no-port' }
  if (/Failed to connect|Serial data stream stopped|No serial data|Timeout/i.test(msg)) return { title: c.errNoAnswer, text: c.errNoAnswerDo, fix: 'no-answer', buttons: true }
  if (/UnexpectedChip|Unexpected chip/i.test(msg)) return { title: c.wrongChipT, text: fill(c.wrongChip, { chip: msg.replace(/^.*?(0x[0-9a-f]+|\d+).*$/i, 'unknown chip ($1)') }), fix: 'wrong-chip' }
  return { title: c.errOther, text: e instanceof Error ? e.message : msg, fix: 'no-answer' }
}

export function Flasher({ lang, manifest }: { lang: Lang; manifest: Manifest | null }) {
  const c = copy[lang]
  const support = useSyncExternalStore(noop, detect, () => null)
  const [src, setSrc] = useState<Source | null>(() => (manifest ? siteSource(manifest) : null))
  const [lines, setLines] = useState<string[]>([])
  const [step, setStep] = useState<Step>('connect')
  const [busy, setBusy] = useState(false)
  const [cErr, setCErr] = useState<ConnErr | null>(null)
  const [bad, setBad] = useState<{ chip: Chip; why: 'chip' | 'rev' } | null>(null)
  const [esp, setEsp] = useState<Esp | null>(null)
  const [factory, setFactory] = useState<Image[] | null>(null)
  const [sameLayout, setSameLayout] = useState<boolean | null>(null)
  const [mode, setMode] = useState<Mode>('factory')
  const [sure, setSure] = useState(false)
  const [fwErr, setFwErr] = useState<string | null>(null)
  const [flash, setFlash] = useState<Flash | null>(null)
  const [link, setLink] = useState<{ im: Improv; con: Console } | null>(null)
  const [lost, setLost] = useState(false)
  const [result, setResult] = useState<Result | null>(null)
  const m = src?.manifest

  const log = useCallback((l: string) => {
    setLines((ls) => {
      const next = ls.length > 1500 ? ls.slice(-1000) : ls.slice()
      // esptool's baud rate advice only worries people; a failed baud change says so itself.
      if (l.startsWith('If the chip does not respond')) return ls
      if (l.startsWith('Writing at') && next[next.length - 1]?.startsWith('Writing at')) next[next.length - 1] = l
      else next.push(l)
      return next
    })
  }, [])

  useEffect(() => {
    if (step !== 'flash' || flash?.err) return
    const stay = (e: BeforeUnloadEvent) => e.preventDefault()
    addEventListener('beforeunload', stay)
    return () => removeEventListener('beforeunload', stay)
  }, [step, flash?.err])

  const fwText = (e: unknown) =>
    e instanceof FirmwareError ? fill(e.kind === 'fetch' ? c.fwFetch : c.fwSha, { file: e.path }) : e instanceof Error ? e.message : String(e)

  const startOver = async () => {
    await esp?.close()
    await link?.con.close()
    setEsp(null)
    setLink(null)
    setLost(false)
    setBad(null)
    setCErr(null)
    setFlash(null)
    setFactory(null)
    setSameLayout(null)
    setSure(false)
    setFwErr(null)
    setResult(null)
    setStep('connect')
  }

  const connect = async (all = false) => {
    if (!src) return
    const m = src.manifest
    setCErr(null)
    setBad(null)
    let port: WebSerialPort | null
    try {
      port = await pickPort(all)
    } catch (e) {
      return setCErr(connErr(c, e))
    }
    if (!port) return
    setBusy(true)
    const ok = (ch: Chip) => ch.id === m.chip_id && ch.revision >= m.chip_revision.min && ch.revision <= m.chip_revision.max
    try {
      const e = await Esp.connect(port, m.esptool.baud, log, ok)
      if (!ok(e.chip)) {
        await e.close()
        return setBad({ chip: e.chip, why: e.chip.id === m.chip_id ? 'rev' : 'chip' })
      }
      setEsp(e)
      setStep('install')
      setFwErr(null)
      try {
        const imgs = await loadBuild(src, 'factory')
        setFactory(imgs)
        const first = imgs[0]
        const same = !!m.builds.update && imgs.length === 1 && !!first && first.offset === 0 && (await e.sameLayout(first))
        setSameLayout(same)
        setMode(same ? 'update' : 'factory')
      } catch (x) {
        setSameLayout(false)
        setFwErr(fwText(x))
      }
    } catch (x) {
      log(String(x))
      setCErr(connErr(c, x))
    } finally {
      setBusy(false)
    }
  }

  const install = async () => {
    if (!esp || !src) return
    const m = src.manifest
    setBusy(true)
    let imgs: Image[]
    try {
      imgs = mode === 'factory' && factory ? factory : await loadBuild(src, mode)
    } catch (x) {
      setBusy(false)
      return setFwErr(fwText(x))
    }
    setBusy(false)
    const eraseAll = !!m.builds[mode]?.erase_all
    setStep('flash')
    setFlash({ erase: eraseAll ? 'run' : null, parts: imgs.map((i) => ({ status: 'wait', done: 0, total: i.data.length })), start: 0, bytesAt: 0, err: null })
    const on = (p: Progress) =>
      setFlash((f) => {
        if (!f) return f
        if (p.phase === 'erase') return { ...f, erase: 'run' }
        const parts = f.parts.slice()
        const cur = parts[p.part]
        if (!cur) return f
        if (p.phase === 'verified') {
          parts[p.part] = { ...cur, status: 'ok', done: cur.total }
          return { ...f, parts }
        }
        parts[p.part] = { ...cur, status: p.phase, done: p.done }
        const fresh = !f.start
        return { ...f, erase: f.erase && 'ok', parts, start: fresh ? Date.now() : f.start, bytesAt: fresh ? p.done : f.bytesAt }
      })
    try {
      await esp.flash(imgs, eraseAll, on)
      log(c.restarting)
      const port = await esp.boot(m.console.baud)
      setEsp(null)
      if (!m.console.improv) {
        await port.close()
        setResult({ urls: [], name: '', later: true })
        return setStep('ready')
      }
      let con: Console | null = null
      const im = new Improv((b) => (con ? con.write(b) : Promise.reject(new Error('closed'))), log)
      con = new Console(port, (b) => im.feed(b), () => setLost(true))
      setLink({ im, con })
      setStep('setup')
    } catch (x) {
      log(String(x))
      const md5 = /MD5|md5/.test(String(x))
      setFlash((f) => f && { ...f, err: md5 ? c.flashMd5 : c.flashErr })
    }
  }

  const pickLocal = async (files: FileList | null) => {
    if (!files?.length) return
    try {
      setSrc(await localSource([...files]))
      setFwErr(null)
    } catch {
      setFwErr(c.fwLocalBad)
    }
  }

  const unsupported = support !== null && support !== 'ok'
  const at = unsupported ? 0 : { connect: 1, install: 2, flash: 2, setup: 3, ready: 5 }[step]
  const failed = unsupported || !!bad || !!flash?.err || lost
  const focus = step === 'connect' && !bad ? (cErr?.buttons ? 'buttons' : 'usb') : 'none'

  return (
    <main id="main" className="mx-auto w-full max-w-6xl px-4 pt-10 pb-20 sm:px-6 sm:pt-14">
      <header className="max-w-2xl">
        <h1 className="text-3xl font-semibold tracking-tight text-balance sm:text-4xl">{c.title}</h1>
        <p className="mt-3 text-base text-pretty text-fg-2 sm:text-lg">{c.lead}</p>
      </header>

      <div className="mt-10">
        <Stepper c={c} at={at} failed={failed} />
      </div>

      <div className="mt-8 grid gap-6 lg:grid-cols-[minmax(0,1fr)_340px]">
        <section className={`${card} min-w-0 p-5 sm:p-7`} aria-live="polite">
          {support === 'mobile' || support === 'browser' || support === 'insecure' ? (
            <Unsupported c={c} kind={support} lang={lang} />
          ) : !m ? (
            <NoFirmware c={c} err={fwErr} onFiles={pickLocal} />
          ) : step === 'connect' ? (
            bad ? (
              <WrongChip c={c} m={m} bad={bad} onRetry={startOver} />
            ) : (
              <ConnectStep c={c} busy={busy || support === null} err={cErr} focus={focus} onConnect={connect} />
            )
          ) : step === 'install' && esp ? (
            <InstallStep c={c} m={m} chip={esp.chip} same={sameLayout} mode={mode} setMode={setMode} sure={sure} setSure={setSure} busy={busy} err={fwErr} onInstall={install} onOther={startOver} />
          ) : step === 'flash' && flash ? (
            <FlashStep c={c} f={flash} names={m.builds[mode]?.parts.map((p) => p.path) ?? []} onRetry={startOver} />
          ) : step === 'setup' && link ? (
            lost ? (
              <div className="space-y-5">
                <Note lvl="bad" title={c.errLostSerial}>
                  <Fix c={c} id="setup-no-answer" />
                </Note>
                <Btn onClick={() => void startOver()}>
                  <RotateCcw aria-hidden />
                  {c.again}
                </Btn>
              </div>
            ) : (
              <Setup
                c={c}
                im={link.im}
                version={m.version}
                onDone={(r) => {
                  void link.con.close()
                  setResult(r)
                  setStep('ready')
                }}
              />
            )
          ) : step === 'ready' && result ? (
            <Ready c={c} r={result} lang={lang} onNext={startOver} />
          ) : null}
        </section>

        <aside className="min-w-0 space-y-6">
          {!unsupported && (
            <div className={`${card} overflow-hidden max-lg:hidden`}>
              <BoardArt focus={focus} className="block w-full bg-well px-4 pt-4 pb-2" />
            </div>
          )}
          <FirmwareCard c={c} lang={lang} src={src} canChange={step === 'connect' && !busy && !unsupported} err={step === 'connect' && m ? fwErr : null} onFiles={pickLocal} onSite={manifest ? () => setSrc(siteSource(manifest)) : undefined} />
        </aside>
      </div>

      {!unsupported && (
        <div className="mt-6">
          <LogPanel c={c} lines={lines} />
        </div>
      )}
    </main>
  )
}

function Unsupported({ c, kind, lang }: { c: Copy; kind: 'mobile' | 'browser' | 'insecure'; lang: Lang }) {
  const [done, setDone] = useState(false)
  const url = typeof location === 'undefined' ? '' : (location.href.split('#')[0] ?? '')
  const Icon = kind === 'mobile' ? Smartphone : Monitor
  return (
    <div className="space-y-5">
      <div className="flex items-center gap-3">
        <span className="flex size-11 items-center justify-center rounded-lg bg-amber-soft text-amber-fg">
          <Icon className="size-5" aria-hidden />
        </span>
        <h2 className="text-xl font-semibold">{kind === 'mobile' ? c.mobileT : c.browserT}</h2>
      </div>
      <p className="text-fg-2">
        {kind === 'mobile' ? c.mobile : kind === 'insecure' ? c.insecure : c.browser}
        {kind !== 'mobile' && <Fix c={c} id={kind === 'insecure' ? 'https' : 'browser'} />}
      </p>
      {kind === 'mobile' ? (
        <div className="space-y-2">
          <p className="text-sm font-medium">{c.mobileOpen}</p>
          <div className="flex items-center gap-2 rounded-lg border border-line bg-well p-1 ps-3">
            <Laptop className="size-4 shrink-0 text-fg-3" aria-hidden />
            <span className={`${mono} min-w-0 flex-1 break-all`}>{url.replace(/^https?:\/\//, '')}</span>
            <button
              type="button"
              onClick={(e) => {
                const text = e.currentTarget.previousElementSibling
                const legacy = () => {
                  if (text) getSelection()?.selectAllChildren(text)
                  // navigator.clipboard does not exist on plain http (a LAN address); the old API still copies the selection there.
                  // eslint-disable-next-line @typescript-eslint/no-deprecated
                  setDone(document.execCommand('copy'))
                }
                if (!isSecureContext) return legacy()
                navigator.clipboard.writeText(url).then(() => setDone(true), legacy)
              }}
              className="inline-flex h-11 items-center gap-1.5 rounded-md bg-panel-2 px-3 text-sm font-medium hover:bg-line [&_svg]:size-4"
            >
              {done ? <Check aria-hidden /> : <CopyIcon aria-hidden />}
              {done ? c.copied : c.copy}
            </button>
          </div>
        </div>
      ) : (
        <p className="font-medium">{c.browserDo}</p>
      )}
      <p className="text-sm text-fg-3">{c.readOnly}</p>
      <LinkBtn kind="ghost" href={`${prefix(lang)}/docs/flashing/`}>
        <BookOpen aria-hidden />
        {c.guide}
      </LinkBtn>
    </div>
  )
}

function NoFirmware({ c, err, onFiles }: { c: Copy; err: string | null; onFiles: (f: FileList | null) => Promise<void> }) {
  return (
    <div className="space-y-5">
      <Note lvl="warn" title={c.fwNone}>
        {c.fwNoneHint}
      </Note>
      <FilePick c={c} onFiles={onFiles} />
      {err && <FwErr c={c} err={err} />}
    </div>
  )
}

function FilePick({ c, onFiles }: { c: Copy; onFiles: (f: FileList | null) => Promise<void> }) {
  return (
    <div>
      <label className="inline-flex h-10 cursor-pointer items-center gap-2 rounded-lg border border-line-2 px-4 text-sm font-medium hover:bg-panel-2 has-focus-visible:outline-2 has-focus-visible:outline-(--focus) [&_svg]:size-4">
        <FolderOpen aria-hidden />
        {c.fwLocal}
        <input type="file" multiple accept=".json,.bin" className="sr-only" onChange={(e) => void onFiles(e.target.files)} />
      </label>
      <p className="mt-1.5 text-xs text-fg-3">{c.fwLocalHint}</p>
    </div>
  )
}

function ConnectStep({ c, busy, err, focus, onConnect }: { c: Copy; busy: boolean; err: ConnErr | null; focus: 'usb' | 'buttons' | 'none'; onConnect: (all?: boolean) => Promise<void> }) {
  return (
    <div className="space-y-6">
      <h2 className="text-xl font-semibold">{c.connectT}</h2>
      <ol className="space-y-3">
        {[c.c1, c.c2, c.c3].map((t, i) => (
          <li key={t} className="flex gap-3">
            <span className={`${mono} flex size-6 shrink-0 items-center justify-center rounded-md bg-panel-2 text-fg-2`}>{i + 1}</span>
            <span className="text-fg-2">{t}</span>
          </li>
        ))}
      </ol>
      <BoardArt focus={focus} className="block w-full rounded-lg bg-well px-2 pt-2 lg:hidden" />
      {err && (
        <Note lvl="bad" title={err.title}>
          {err.text}
          <Fix c={c} id={err.fix} />
        </Note>
      )}
      <div className="flex flex-wrap items-center gap-x-5 gap-y-3">
        <Btn onClick={() => void onConnect()} disabled={busy}>
          {busy ? <Spinner /> : <Usb aria-hidden />}
          {busy ? c.connecting : c.connect}
        </Btn>
        <button type="button" disabled={busy} onClick={() => void onConnect(true)} className="text-sm text-fg-2 underline decoration-line-2 underline-offset-4 hover:text-fg disabled:opacity-50">
          {c.showAll}
        </button>
      </div>
      <p className="text-sm text-fg-3">
        {c.cable}{' '}
        <a href={`${c.fixUrl}#no-port`} className="underline decoration-line-2 underline-offset-4 hover:text-fg">
          {c.noPort}
        </a>
        {' · '}
        <a href={`${c.fixUrl}#driver`} className="underline decoration-line-2 underline-offset-4 hover:text-fg">
          {c.driver}
        </a>
      </p>
      <details className="group rounded-lg border border-line" open={!!err?.buttons}>
        <summary className="flex cursor-pointer list-none items-center gap-2 px-4 py-3 text-sm font-medium hover:bg-panel-2 [&::-webkit-details-marker]:hidden">
          <ChevronRight className="size-4 text-fg-3 transition-transform group-open:rotate-90" aria-hidden />
          {c.dlT}
        </summary>
        <ol className="space-y-2 px-4 pb-4">
          {c.dl.map((t, i) => (
            <li key={t} className="flex items-center gap-3 text-sm text-fg-2">
              <span className={`${mono} flex size-6 shrink-0 items-center justify-center rounded-md bg-panel-2 text-fg-2`}>{i + 1}</span>
              {t}
            </li>
          ))}
        </ol>
      </details>
    </div>
  )
}

function WrongChip({ c, m, bad, onRetry }: { c: Copy; m: Manifest; bad: { chip: Chip; why: 'chip' | 'rev' }; onRetry: () => Promise<void> }) {
  const v = { chip: bad.chip.name, rev: rev(bad.chip.revision), min: rev(m.chip_revision.min), max: rev(m.chip_revision.max) }
  return (
    <div className="space-y-5">
      <Note lvl="bad" title={bad.why === 'chip' ? c.wrongChipT : c.wrongRevT}>
        {fill(bad.why === 'chip' ? c.wrongChip : c.wrongRev, v)}
        <Fix c={c} id={bad.why === 'chip' ? 'wrong-chip' : 'chip-revision'} />
      </Note>
      <ChipLine c={c} chip={bad.chip} />
      <Btn kind="ghost" onClick={() => void onRetry()}>
        <RotateCcw aria-hidden />
        {c.otherBoard}
      </Btn>
    </div>
  )
}

function ChipLine({ c, chip }: { c: Copy; chip: Chip }) {
  return (
    <div className="flex items-center gap-3 rounded-lg border border-line bg-well px-4 py-3">
      <Cpu className="size-5 shrink-0 text-fg-3" aria-hidden />
      <div className="min-w-0">
        <p className="font-medium">{fill(c.found, { chip: chip.name, rev: rev(chip.revision) })}</p>
        <p className={`${mono} text-fg-3`}>{fill(c.mac, { mac: chip.mac })}</p>
      </div>
    </div>
  )
}

function InstallStep(p: {
  c: Copy
  m: Manifest
  chip: Chip
  same: boolean | null
  mode: Mode
  setMode: (m: Mode) => void
  sure: boolean
  setSure: (b: boolean) => void
  busy: boolean
  err: string | null
  onInstall: () => Promise<void>
  onOther: () => Promise<void>
}) {
  const { c, m, mode } = p
  const canUpdate = !!m.builds.update && p.same === true
  const ready = p.same !== null && !p.err && (mode === 'update' ? canUpdate : p.sure)
  const opt = (value: Mode, title: string, text: string, disabled: boolean, extra?: React.ReactNode) => (
    <label
      className={`relative flex gap-3 rounded-xl border-2 p-4 transition-colors ${
        disabled ? 'cursor-not-allowed border-line opacity-60' : mode === value ? 'cursor-pointer border-amber bg-amber-soft' : 'cursor-pointer border-line hover:border-line-2'
      }`}
    >
      <input type="radio" name="mode" value={value} checked={mode === value} disabled={disabled} onChange={() => p.setMode(value)} className="mt-1 size-4 accent-(--amber)" />
      <span className="min-w-0 space-y-1">
        <span className="flex flex-wrap items-center gap-2 font-semibold">
          {title}
          {value === 'update' && canUpdate && <span className="rounded bg-ok-soft px-1.5 py-0.5 text-xs font-medium text-ok-fg">{c.recommended}</span>}
        </span>
        <span className="block text-sm text-fg-2">{text}</span>
        {extra}
      </span>
    </label>
  )
  return (
    <div className="space-y-6">
      <div className="flex flex-wrap items-center justify-between gap-3">
        <h2 className="text-xl font-semibold">{c.installT}</h2>
      </div>
      <ChipLine c={c} chip={p.chip} />
      {p.same === null && !p.err ? (
        <p className="flex items-center gap-2 text-sm text-fg-2">
          <Spinner /> {c.checking}
        </p>
      ) : (
        <div className="grid gap-3">
          {opt(
            'update',
            c.update,
            c.updateText,
            !canUpdate,
            !canUpdate && <span className="block text-sm text-fg-3">{m.builds.update ? c.updateNo : c.updateLocal}</span>,
          )}
          {opt('factory', c.fresh, c.freshText, false)}
        </div>
      )}
      {mode === 'factory' && p.same !== null && (
        <div className="space-y-3 rounded-xl border border-bad/50 bg-bad-soft p-4">
          <p className="text-sm font-medium text-bad-fg">{c.freshWarn}</p>
          <label className="flex cursor-pointer items-start gap-3 text-sm">
            <input type="checkbox" checked={p.sure} onChange={(e) => p.setSure(e.target.checked)} className="mt-0.5 size-4 accent-(--bad)" />
            <span>{c.confirm}</span>
          </label>
        </div>
      )}
      {p.err && <FwErr c={c} err={p.err} />}
      <div className="flex flex-wrap items-center gap-x-5 gap-y-3">
        <Btn kind={mode === 'factory' && ready ? 'bad' : 'pri'} disabled={!ready || p.busy} onClick={() => void p.onInstall()}>
          {p.busy ? <Spinner /> : <ArrowRight aria-hidden />}
          {p.busy ? c.loading : fill(c.install, { v: release(m.version) })}
        </Btn>
        <button type="button" onClick={() => void p.onOther()} className="text-sm text-fg-2 underline decoration-line-2 underline-offset-4 hover:text-fg">
          {c.otherBoard}
        </button>
      </div>
    </div>
  )
}

function FlashStep({ c, f, names, onRetry }: { c: Copy; f: Flash; names: string[]; onRetry: () => Promise<void> }) {
  const now = useNow(!f.err && f.parts.some((p) => p.status !== 'ok'))
  const total = f.parts.reduce((a, p) => a + p.total, 0)
  const done = f.parts.reduce((a, p) => a + p.done, 0)
  const secs = f.start ? (now - f.start) / 1000 : 0
  const speed = secs > 1 ? (done - f.bytesAt) / secs : 0
  const eta = speed > 0 ? (total - done) / speed : null
  const allOk = f.parts.every((p) => p.status === 'ok')
  return (
    <div className="space-y-6">
      <div className="flex flex-wrap items-baseline justify-between gap-2">
        <h2 className="text-xl font-semibold">{f.err ? c.flashErrT : c.flashT}</h2>
        {!f.err && !allOk && (
          <p className={`${mono} text-fg-2`}>
            {f.erase === 'run' ? c.erasing : `${Math.floor((done / total) * 100)}%`}
            {speed > 0 && ` · ${kib(speed)}/s`}
            {eta !== null && ` · ${fill(c.left, { t: duration(eta) })}`}
          </p>
        )}
      </div>
      {f.err ? (
        <Note lvl="bad" title={f.err}>
          <Fix c={c} id={f.err === c.flashMd5 ? 'read-back' : 'flash-stopped'} />
        </Note>
      ) : (
        <Note lvl="warn" title={c.flashKeep}>
          {c.flashSafe}
        </Note>
      )}
      <ul className="space-y-4">
        {f.erase && (
          <Row label={c.erase} hint={c.eraseHint} status={f.erase === 'ok' ? c.erased : f.err ? c.failed : c.erasing} done={f.erase === 'ok'}>
            <Bar value={f.erase === 'ok' ? 1 : f.err ? 0 : null} tone={f.erase === 'ok' ? 'ok' : f.err ? 'bad' : 'amber'} label={c.erase} />
          </Row>
        )}
        {f.parts.map((p, i) => (
          <Row
            key={names[i] ?? i}
            label={partName(c, names[i] ?? '')}
            sub={`${names[i] ?? ''} · ${kib(p.total)}`}
            status={
              f.err && (p.status === 'write' || p.status === 'verify')
                ? c.failed
                : { wait: c.wait, write: `${c.writing} ${Math.floor((p.done / p.total) * 100)}%`, verify: c.verifying, ok: c.verified }[p.status]
            }
            done={p.status === 'ok'}
          >
            <Bar value={p.status === 'verify' && !f.err ? null : p.done / p.total} tone={p.status === 'ok' ? 'ok' : f.err && p.status !== 'wait' ? 'bad' : 'amber'} label={partName(c, names[i] ?? '')} />
          </Row>
        ))}
      </ul>
      {allOk && !f.err ? (
        <p className="flex items-center gap-2 text-sm text-fg-2">
          <Spinner /> {c.restarting}
        </p>
      ) : (
        <p className="text-sm text-fg-3">{c.verifyNote}</p>
      )}
      {f.err && (
        <Btn onClick={() => void onRetry()}>
          <RotateCcw aria-hidden />
          {c.again}
        </Btn>
      )}
    </div>
  )
}

function Row({ label, sub, hint, status, done, children }: { label: string; sub?: string; hint?: string; status: string; done: boolean; children: React.ReactNode }) {
  return (
    <li className="space-y-2">
      <div className="flex items-baseline justify-between gap-3">
        <span className="min-w-0">
          <span className="font-medium">{label}</span>
          {sub && <span className={`${mono} ms-2 text-fg-3 max-sm:block max-sm:ms-0`}>{sub}</span>}
          {hint && <span className="block text-sm text-fg-3">{hint}</span>}
        </span>
        <span className={`flex shrink-0 items-center gap-1.5 text-sm ${done ? 'text-ok-fg' : 'text-fg-2'}`}>
          {done && <Check className="size-4" aria-hidden />}
          {status}
        </span>
      </div>
      {children}
    </li>
  )
}

function useNow(on: boolean) {
  const [now, setNow] = useState(() => Date.now())
  useEffect(() => {
    if (!on) return
    const id = setInterval(() => setNow(Date.now()), 500)
    return () => clearInterval(id)
  }, [on])
  return now
}

function Ready({ c, r, lang, onNext }: { c: Copy; r: Result; lang: Lang; onNext: () => Promise<void> }) {
  const first = r.urls[0]
  const scheme = first?.startsWith('https') ? 'https' : 'http'
  return (
    <div className="space-y-6">
      <div className="flex items-center gap-3">
        <span className="flex size-11 items-center justify-center rounded-full bg-ok-soft text-ok-fg">
          <Check className="size-5" strokeWidth={3} aria-hidden />
        </span>
        <h2 className="text-xl font-semibold">{c.readyT}</h2>
      </div>
      <p className="text-fg-2">
        {first ? c.readyText : r.later ? c.readyLater : c.readyOffline}
        {!first && !r.later && <Fix c={c} id="find-board" />}
      </p>
      <p className="flex items-center gap-2 text-sm text-fg-2">
        <Usb className="size-4 text-fg-3" aria-hidden />
        {c.unplug}
      </p>
      {first && (
        <div className="space-y-3 rounded-xl border border-line bg-well p-4">
          {r.urls.map((u, i) => (
            <p key={u} className="flex items-center justify-between gap-3">
              <span className="text-sm text-fg-3">{r.urls.length > 1 ? (i === 0 ? c.lan : c.wlan) : c.address}</span>
              <a href={u} className={`${mono} text-base text-amber-fg underline-offset-4 hover:underline`}>
                {u}
              </a>
            </p>
          ))}
          {r.name && (
            <p className="flex items-center justify-between gap-3 border-t border-line pt-3">
              <span className="text-sm text-fg-3">{c.mdns}</span>
              <a href={`${scheme}://${r.name}.local`} className={`${mono} text-amber-fg underline-offset-4 hover:underline`}>
                {r.name}.local
              </a>
            </p>
          )}
        </div>
      )}
      <div className="flex flex-wrap gap-3">
        {first && (
          <LinkBtn href={first} target="_blank" rel="noreferrer">
            <ExternalLink aria-hidden />
            {c.open}
          </LinkBtn>
        )}
        <LinkBtn kind="ghost" href={`${prefix(lang)}/docs/`}>
          <BookOpen aria-hidden />
          {c.docs}
        </LinkBtn>
        <Btn kind="ghost" onClick={() => void onNext()}>
          <RotateCcw aria-hidden />
          {c.next}
        </Btn>
      </div>
    </div>
  )
}

// A local build carries -dirty; CI builds from a clean tag, so the site shows that suffix as a badge.
const release = (v: string) => v.replace(/-dirty$/, '')

/** The C compiler's "Nov  8 2026 19:10:22" in the page's language. */
function builtAt(s: string, lang: Lang) {
  const d = new Date(s.replace(/\s+/g, ' '))
  return Number.isNaN(d.getTime()) ? s : new Intl.DateTimeFormat(lang, { dateStyle: 'medium', timeStyle: 'short' }).format(d)
}

function FirmwareCard(p: { c: Copy; lang: Lang; src: Source | null; canChange: boolean; err: string | null; onFiles: (f: FileList | null) => Promise<void>; onSite?: () => void }) {
  const { c, src } = p
  const m = src?.manifest
  if (!m) return null
  const rows: [string, string][] = [
    [c.fwVersion, release(m.version)],
    ...(m.built ? [[c.fwBuilt, builtAt(m.built, p.lang)] as [string, string]] : []),
    [c.fwSize, kib(totalSize(m.builds.factory))],
    [c.fwChip, fill(c.fwChipV, { min: rev(m.chip_revision.min), max: rev(m.chip_revision.max) })],
  ]
  return (
    <div className={`${card} p-5`}>
      <div className="flex items-center justify-between gap-2">
        <h2 className="text-sm font-semibold">{c.fw}</h2>
        {src.local && <span className={`${mono} rounded bg-amber-soft px-1.5 py-0.5 text-[0.6875rem] text-amber-fg`}>{c.fwLocalOn}</span>}
        {m.version.endsWith('-dirty') && <span className={`${mono} rounded bg-panel-2 px-1.5 py-0.5 text-[0.6875rem] text-fg-3`}>{c.fwDev}</span>}
      </div>
      <dl className="mt-3 space-y-2 text-sm">
        {rows.map(([k, v]) => (
          <div key={k} className="flex justify-between gap-3">
            <dt className="text-fg-3">{k}</dt>
            <dd className={`${mono} text-end break-all`}>{v}</dd>
          </div>
        ))}
      </dl>
      {p.canChange && (
        <div className="mt-4 border-t border-line pt-4">
          {src.local && p.onSite ? (
            <button type="button" onClick={p.onSite} className="text-sm text-fg-2 underline decoration-line-2 underline-offset-4 hover:text-fg">
              {c.fwSite}
            </button>
          ) : (
            <FilePick c={c} onFiles={p.onFiles} />
          )}
          {p.err && (
            <p className="mt-2 text-sm text-bad-fg">
              {p.err}
              {p.err !== c.fwLocalBad && <Fix c={c} id="firmware-download" />}
            </p>
          )}
        </div>
      )}
    </div>
  )
}
