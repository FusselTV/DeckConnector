'use client'
import { useEffect, useRef, useState, type ReactNode } from 'react'
import { Check, ChevronRight, CircleAlert, Eye, EyeOff, Info, TriangleAlert } from 'lucide-react'
import { AMBER, at, Board, BOARD_USB, BOARD_RJ45, Glow, GREEN, RED } from '~/remotion/board'
import { fill, type Copy } from './copy'

export const card = 'rounded-xl border border-line bg-panel shadow-panel'
export const mono = 'font-mono text-[0.8125rem] tracking-tight'

const btnBase =
  'inline-flex h-11 items-center justify-center gap-2 rounded-lg px-5 text-sm font-semibold transition-colors focus-visible:outline-2 focus-visible:outline-offset-2 focus-visible:outline-(--focus) disabled:cursor-not-allowed disabled:border-transparent disabled:bg-panel-2 disabled:text-fg-3 disabled:brightness-100 [&_svg]:size-4'

export function Btn({
  kind = 'pri',
  children,
  ...p
}: { kind?: 'pri' | 'ghost' | 'bad' } & React.ButtonHTMLAttributes<HTMLButtonElement>) {
  const k = {
    pri: 'bg-amber text-amber-ink hover:brightness-110',
    ghost: 'border border-line-2 text-fg hover:bg-panel-2',
    bad: 'bg-bad text-white hover:brightness-110',
  }[kind]
  return (
    <button type="button" className={`${btnBase} ${k}`} {...p}>
      {children}
    </button>
  )
}

export function LinkBtn({ kind = 'pri', children, ...p }: { kind?: 'pri' | 'ghost' } & React.AnchorHTMLAttributes<HTMLAnchorElement>) {
  const k = kind === 'pri' ? 'bg-amber text-amber-ink hover:brightness-110' : 'border border-line-2 text-fg hover:bg-panel-2'
  return (
    <a className={`${btnBase} ${k}`} {...p}>
      {children}
    </a>
  )
}

export function Note({ lvl, title, children }: { lvl: 'info' | 'warn' | 'bad' | 'ok'; title?: ReactNode; children?: ReactNode }) {
  const s = {
    info: ['border-line-2 bg-panel-2', 'text-fg-2', Info],
    warn: ['border-amber/50 bg-amber-soft', 'text-amber-fg', TriangleAlert],
    bad: ['border-bad/50 bg-bad-soft', 'text-bad-fg', CircleAlert],
    ok: ['border-ok/40 bg-ok-soft', 'text-ok-fg', Check],
  } as const
  const [box, ic, Icon] = s[lvl]
  return (
    <div role={lvl === 'bad' ? 'alert' : undefined} className={`flex gap-3 rounded-lg border p-3.5 text-sm ${box}`}>
      <Icon className={`mt-0.5 size-4 shrink-0 ${ic}`} aria-hidden />
      <div className="min-w-0 space-y-1">
        {title && <p className="font-semibold text-fg">{title}</p>}
        {children && <div className="text-fg-2">{children}</div>}
      </div>
    </div>
  )
}

export function Fix({ c, id }: { c: Copy; id: string }) {
  return (
    <a href={`${c.fixUrl}#${id}`} className="mt-1 block w-fit font-medium text-fg underline decoration-line-2 underline-offset-4 hover:decoration-current">
      {c.fix}
    </a>
  )
}

export function Spinner({ className = '' }: { className?: string }) {
  return <span aria-hidden className={`inline-block size-4 animate-spin rounded-full border-2 border-current border-t-transparent ${className}`} />
}

export function Stepper({ c, at, failed }: { c: Copy; at: number; failed?: boolean }) {
  return (
    <nav aria-label={fill(c.step, { n: Math.min(at + 1, c.steps.length), all: c.steps.length })}>
      <ol className="flex items-start">
        {c.steps.map((label, i) => {
          const state = i < at ? 'done' : i === at ? (failed ? 'bad' : 'now') : 'next'
          const dot = {
            done: 'border-ok bg-ok text-bg',
            now: 'border-amber bg-amber text-amber-ink ring-4 ring-amber-soft',
            bad: 'border-bad bg-bad text-white ring-4 ring-bad-soft',
            next: 'border-line-2 bg-panel text-fg-3',
          }[state]
          return (
            <li key={label} className="relative flex flex-1 flex-col items-center gap-2" aria-current={i === at ? 'step' : undefined}>
              {i > 0 && <span aria-hidden className={`absolute top-4 right-1/2 -z-0 h-0.5 w-full ${i <= at ? 'bg-ok' : 'bg-line'}`} />}
              <span className={`relative z-10 flex size-8 items-center justify-center rounded-full border-2 font-mono text-sm font-semibold transition-colors ${dot}`}>
                {state === 'done' ? <Check className="size-4" strokeWidth={3} aria-hidden /> : i + 1}
              </span>
              <span className={`text-center text-xs font-medium max-sm:hidden ${i === at ? 'text-fg' : 'text-fg-3'}`}>{label}</span>
            </li>
          )
        })}
      </ol>
      <p className="mt-3 text-center text-sm font-medium sm:hidden">
        <span className="text-fg-3">{fill(c.step, { n: Math.min(at + 1, c.steps.length), all: c.steps.length })}</span> · {c.steps[Math.min(at, c.steps.length - 1)]}
      </p>
    </nav>
  )
}

export function Bar({ value, tone = 'amber', label }: { value: number | null; tone?: 'amber' | 'ok' | 'bad'; label: string }) {
  const color = { amber: 'bg-amber', ok: 'bg-ok', bad: 'bg-bad' }[tone]
  return (
    <div
      role="progressbar"
      aria-label={label}
      aria-valuemin={0}
      aria-valuemax={100}
      aria-valuenow={value === null ? undefined : Math.round(value * 100)}
      className="relative h-2 overflow-hidden rounded-full bg-well"
    >
      {value === null ? (
        <span className={`absolute inset-y-0 w-1/3 animate-[flash-indet_1.4s_ease-in-out_infinite] rounded-full motion-reduce:animate-none ${color}`} />
      ) : (
        <span className={`absolute inset-y-0 left-0 rounded-full transition-[width] duration-300 ${color}`} style={{ width: `${Math.max(0, Math.min(1, value)) * 100}%` }} />
      )}
      <style>{'@keyframes flash-indet{0%{left:-35%}100%{left:100%}}'}</style>
    </div>
  )
}

export function Field({ id, label, hint, err, children }: { id: string; label: string; hint?: ReactNode; err?: ReactNode; children: ReactNode }) {
  return (
    <div className="space-y-1.5">
      <label htmlFor={id} className="block text-sm font-medium">
        {label}
      </label>
      {children}
      {err ? (
        <p id={`${id}-e`} className="text-sm text-bad-fg">
          {err}
        </p>
      ) : (
        hint && (
          <p id={`${id}-h`} className="text-sm text-fg-3">
            {hint}
          </p>
        )
      )}
    </div>
  )
}

export const input =
  'h-11 w-full rounded-lg border border-line-2 bg-well px-3 [.light_&]:bg-white text-sm text-fg placeholder:text-fg-3 focus-visible:border-amber focus-visible:outline-2 focus-visible:outline-offset-0 focus-visible:outline-amber-soft aria-[invalid=true]:border-bad'

export function Secret({ c, ...p }: { c: Copy } & React.InputHTMLAttributes<HTMLInputElement>) {
  const [show, setShow] = useState(false)
  return (
    <div className="relative">
      <input {...p} type={show ? 'text' : 'password'} className={`${input} pe-11 font-mono`} autoComplete="new-password" spellCheck={false} />
      <button
        type="button"
        onClick={() => setShow(!show)}
        aria-label={show ? c.hide : c.show}
        title={show ? c.hide : c.show}
        className="absolute inset-y-0 end-0 flex w-11 items-center justify-center text-fg-3 hover:text-fg [&_svg]:size-4"
      >
        {show ? <EyeOff aria-hidden /> : <Eye aria-hidden />}
      </button>
    </div>
  )
}

export function LogPanel({ c, lines }: { c: Copy; lines: string[] }) {
  const box = useRef<HTMLPreElement>(null)
  useEffect(() => {
    const el = box.current
    if (el && el.scrollHeight - el.scrollTop - el.clientHeight < 80) el.scrollTop = el.scrollHeight
  }, [lines])
  return (
    <details className={`${card} group overflow-hidden`}>
      <summary className="flex cursor-pointer list-none items-center gap-2 px-4 py-3 text-sm font-medium select-none hover:bg-panel-2 [&::-webkit-details-marker]:hidden">
        <ChevronRight className="size-4 text-fg-3 transition-transform group-open:rotate-90" aria-hidden />
        {c.log}
        <span className={`${mono} rounded bg-panel-2 px-1.5 text-fg-3`}>{lines.length}</span>
        <span className="ms-auto text-xs text-fg-3 max-sm:hidden">{c.logNote}</span>
      </summary>
      <pre ref={box} className={`${mono} max-h-72 overflow-auto border-t border-line bg-lcd p-4 leading-relaxed text-lcd-fg/85`}>
        {lines.length ? lines.join('\n') : c.logEmpty}
      </pre>
    </details>
  )
}

/** The board from above. `focus` lights up the USB-C port or the two buttons for download mode. */
export function BoardArt({ focus, className = '' }: { focus: 'usb' | 'buttons' | 'none'; className?: string }) {
  // The landing's board drawing. Its text has fixed light colours; text beside it follows the theme.
  const board = { x: 100, y: 34, s: 1.1 }
  const [cx, cy] = at(board, [2, 115])
  const [ax, ay] = at(board, BOARD_USB)
  const [rx, ry] = at(board, BOARD_RJ45)
  const keys = [
    ['BOOT', at(board, [86, 126])],
    ['RST', at(board, [106, 126])],
  ] as const
  const ping = 'animate-ping [transform-box:fill-box] [transform-origin:center] motion-reduce:animate-none'
  const label = 'font-mono text-[10px] font-semibold tracking-wide'
  const ring = { fill: 'none', stroke: 'var(--focus)', strokeWidth: 2 }
  const usb = focus === 'usb'
  const btn = focus === 'buttons'
  return (
    <svg viewBox="0 0 360 256" className={className} role="img" aria-label="ESP32-P4 PoE board: USB-C port, BOOT and RST buttons">
      <defs>
        {[AMBER, GREEN, RED].map((c) => (
          <Glow key={c} c={c} />
        ))}
      </defs>
      <Board {...board} />
      <text x={ax - 8} y={ay - 26} textAnchor="end" className={label} fill="var(--fg-3)">
        USB-A · Deck
      </text>
      <text x={rx} y={ry - 92} textAnchor="end" className={label} fill="var(--fg-3)">
        LAN · PoE
      </text>
      {usb && (
        <>
          <circle cx={cx} cy={cy} r={20} {...ring} />
          <circle cx={cx} cy={cy} r={20} {...ring} className={ping} />
        </>
      )}
      <text x={cx} y={cy + 66} textAnchor="middle" className={label} fill={usb ? 'var(--focus)' : 'var(--fg-3)'}>
        USB-C · Computer
      </text>
      {keys.map(([name, [x, y]]) => (
        <g key={name}>
          {btn && (
            <>
              <circle cx={x} cy={y} r={11} fill="none" stroke={AMBER} strokeWidth={2} />
              <circle cx={x} cy={y} r={11} fill="none" stroke={AMBER} strokeWidth={2} className={ping} />
            </>
          )}
          <rect x={x - 6} y={y - 6} width={12} height={12} rx={2} fill="#c9ced4" stroke="#6c737b" />
          <circle cx={x} cy={y} r={3.5} fill="#2a2f35" />
          <path d={`M${x} ${y + 8} V${y + 64}`} stroke="var(--edge)" strokeWidth={1} />
          <text x={name === 'BOOT' ? x - 3 : x + 3} y={y + 76} textAnchor={name === 'BOOT' ? 'end' : 'start'} className={label} fill={btn ? 'var(--focus)' : 'var(--fg-3)'}>
            {name}
          </text>
        </g>
      ))}
    </svg>
  )
}

export function Toggle({ id, label, hint, ...p }: { id: string; label: string; hint?: string } & React.InputHTMLAttributes<HTMLInputElement>) {
  return (
    <div className="flex items-start gap-3">
      <input type="checkbox" id={id} aria-describedby={hint ? `${id}-h` : undefined} className="mt-1 size-4 shrink-0 accent-(--amber)" {...p} />
      <div>
        <label htmlFor={id} className="block cursor-pointer font-medium">
          {label}
        </label>
        {hint && (
          <p id={`${id}-h`} className="text-sm text-fg-3">
            {hint}
          </p>
        )}
      </div>
    </div>
  )
}

export const kib = (n: number) => (n >= 1 << 20 ? `${(n / (1 << 20)).toFixed(1)} MB` : `${Math.round(n / 1024)} KB`)

export const duration = (s: number) => (s >= 60 ? `${Math.floor(s / 60)}:${String(Math.round(s % 60)).padStart(2, '0')} min` : `${Math.max(1, Math.round(s))} s`)
