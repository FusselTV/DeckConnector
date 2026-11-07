import { ChevronLeft, ChevronRight, Hand, MousePointer2 } from 'lucide-react'
import { Fragment, memo, useEffect, useRef, useState, type CSSProperties, type PointerEvent as RPE } from 'react'
import type { InputBody, InputEvent, Rect, Screen } from '../api/types'
import { useUi } from '../app/ui'
import { cx, useLatest } from '../lib/util'
import { MODELS, type ModelKey } from './models'

type Hit = 'web' | 'deck'
const mm = (o: { x: number; y: number; w: number; h: number }): CSSProperties => ({
  left: `calc(${o.x}*var(--mm))`,
  top: `calc(${o.y}*var(--mm))`,
  width: `calc(${o.w}*var(--mm))`,
  height: `calc(${o.h}*var(--mm))`,
})
const overlaps = (a: Rect, b: Rect) => a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h

/** The part of the deck picture that falls on one key or strip, scaled to its box. */
function Pixels({ screen, rect }: { screen: Screen; rect: Rect | undefined }) {
  const W = screen.width
  const H = screen.height
  if (!rect || !W || !H) return null
  const imgs = screen.images.filter((i) => overlaps(i, rect))
  if (!imgs.length) return null
  return (
    <span className="px" aria-hidden>
      <span
        style={{
          width: `${(W / rect.w) * 100}%`,
          height: `${(H / rect.h) * 100}%`,
          left: `${(-rect.x / rect.w) * 100}%`,
          top: `${(-rect.y / rect.h) * 100}%`,
        }}
      >
        {imgs.map((i) => (
          <img
            key={i.url}
            src={i.url}
            alt=""
            draggable={false}
            style={{
              left: `${(i.x / W) * 100}%`,
              top: `${(i.y / H) * 100}%`,
              width: `${(i.w / W) * 100}%`,
              height: `${(i.h / H) * 100}%`,
            }}
          />
        ))}
      </span>
    </span>
  )
}

export interface DeckFrontProps {
  model: ModelKey
  label: string
  screen: Screen | null
  brightness: number
  armed: boolean
  /** The last input from the store, so presses on the real deck light up here. */
  last: InputEvent | undefined
  /** Called for a press while not armed: the remote-control pill shakes (rule 23). */
  onLocked: () => void
  send: (body: InputBody) => void
}

interface Dial {
  delta: number
  rot: number
  live: boolean
}
interface Drag {
  x: number
  acc: number
  turned: boolean
  z: number
  rep?: boolean
  tm?: number
}

export const DeckFront = memo(function DeckFront(p: DeckFrontProps) {
  const { t } = useUi()
  const M = MODELS[p.model]
  const nk = M.cols * M.rows
  const [hits, setHits] = useState<Record<string, Hit>>({})
  const [down, setDown] = useState<Record<string, boolean>>({})
  const [dial, setDial] = useState<Record<number, Dial>>({})
  const [zone, setZone] = useState(0)
  const [kf, setKf] = useState(false)
  const [hint, setHint] = useState<'strip' | 'knob' | null>(null)
  const kd = useRef(new Map<number, { x: number; y: number; tm: number; sent?: boolean }>())
  const dd = useRef(new Map<number, Drag>())
  const dtm = useRef<Record<number, number>>({})
  const pd = useRef<{ x: number; y: number; t: number; r: DOMRect } | null>(null)
  const stripRef = useRef<HTMLDivElement>(null)
  const encRefs = useRef<(HTMLButtonElement | null)[]>([])
  const { armed, onLocked, send } = p

  const flash = (id: string, kind: Hit) => {
    setHits((h) => ({ ...h, [id]: kind }))
    setTimeout(() => {
      setHits((h) => {
        const { [id]: _gone, ...n } = h
        return n
      })
    }, 700)
  }
  // Presses on the real deck arrive as store inputs: light the control up with the hand icon.
  const lastAt = useRef(0)
  useEffect(() => {
    const ev = p.last
    if (!ev || ev.origin !== 'deck' || ev.at === lastAt.current) return
    lastAt.current = ev.at
    const id =
      ev.kind === 'turn' || ev.kind === 'encoder' ? `e${ev.key ?? 0}` : ev.kind === 'touch' ? 'z0' : `k${ev.key ?? 0}`
    flash(id, 'deck')
  }, [p.last])

  const blocked = () => {
    if (!armed) {
      onLocked()
      return true
    }
    return false
  }
  const keyEv = (i: number, a: 'down' | 'up') => {
    if (a === 'down' && blocked()) return
    setDown((d) => ({ ...d, [`k${i}`]: a === 'down' }))
    if (a === 'down') flash(`k${i}`, 'web')
    send({ key: i, action: a })
  }
  // A blur or hidden page must never leave a key held on the device.
  const downRef = useLatest(down)
  useEffect(() => {
    const release = () => {
      for (const [k, v] of Object.entries(downRef.current))
        if (v && k.startsWith('k')) send({ key: Number(k.slice(1)), action: 'up' })
      setDown({})
    }
    window.addEventListener('blur', release)
    document.addEventListener('visibilitychange', release)
    return () => {
      window.removeEventListener('blur', release)
      document.removeEventListener('visibilitychange', release)
    }
  }, [send, downRef])

  // Encoders are endless: show the turn as a +n badge, then spring back to rest (rule 12).
  const turn = (e: number, d: number) => {
    if (blocked()) return false
    flash(`e${e}`, 'web')
    send({ encoder: e, action: 'turn', ticks: d })
    setDial((x) => {
      const c = x[e]?.live ? x[e] : { delta: 0, rot: 0, live: false }
      return { ...x, [e]: { delta: c.delta + d, rot: c.rot + d * 12, live: true } }
    })
    clearTimeout(dtm.current[e])
    dtm.current[e] = window.setTimeout(() => {
      setDial((x) => ({ ...x, [e]: { delta: x[e]?.delta ?? 0, rot: 0, live: false } }))
      setTimeout(() => setDial((x) => (x[e] && !x[e].live ? { ...x, [e]: { delta: 0, rot: 0, live: false } } : x)), 700)
    }, 450)
    return true
  }
  const press = (e: number) => {
    if (blocked()) return
    flash(`e${e}`, 'web')
    setDown((d) => ({ ...d, [`e${e}`]: true }))
    send({ encoder: e })
    setTimeout(() => setDown((d) => ({ ...d, [`e${e}`]: false })), 140)
  }
  const turnRef = useLatest(turn)
  useEffect(() => {
    const hs = encRefs.current.map((el, i) => {
      if (!el) return null
      const f = (ev: WheelEvent) => {
        if (document.activeElement !== el) return
        ev.preventDefault()
        turnRef.current(i, ev.deltaY > 0 ? 1 : -1)
      }
      el.addEventListener('wheel', f, { passive: false })
      return [el, f] as const
    })
    return () => hs.forEach((x) => x?.[0].removeEventListener('wheel', x[1]))
  }, [M.dials?.n, turnRef])

  const dDown = (i: number, e: RPE<HTMLButtonElement>) => {
    const r = e.currentTarget.getBoundingClientRect()
    const fx = (e.clientX - r.left) / r.width
    const g: Drag = { x: e.clientX, acc: 0, turned: false, z: fx < 0.35 ? -1 : fx > 0.65 ? 1 : 0 }
    dd.current.set(i, g)
    // Holding an arrow zone turns on and speeds up until release (rule 23).
    if (g.z) {
      const rep = (ms: number) => {
        g.rep = true
        if (dd.current.get(i) !== g || !turnRef.current(i, g.z)) return
        g.tm = window.setTimeout(rep, ms, Math.max(40, ms * 0.8))
      }
      g.tm = window.setTimeout(rep, 400, 160)
    }
    if (e.pointerType !== 'touch') {
      e.preventDefault()
      e.currentTarget.setPointerCapture(e.pointerId)
    }
  }
  const dMove = (i: number, e: RPE) => {
    const g = dd.current.get(i)
    if (!g) return
    g.acc += e.clientX - g.x
    g.x = e.clientX
    while (Math.abs(g.acc) >= 10) {
      clearTimeout(g.tm)
      const s = Math.sign(g.acc)
      g.acc -= s * 10
      g.turned = true
      turn(i, s)
    }
  }
  const dUp = (i: number) => {
    const g = dd.current.get(i)
    dd.current.delete(i)
    if (!g) return
    clearTimeout(g.tm)
    if (g.turned || g.rep) return
    if (g.z) turn(i, g.z)
    else press(i)
  }
  // On touch a key waits 90 ms so a scroll gesture never presses it.
  const kDown = (i: number, e: RPE<HTMLButtonElement>) => {
    if (e.pointerType === 'touch') {
      const k = { x: e.clientX, y: e.clientY, tm: 0, sent: false }
      k.tm = window.setTimeout(() => {
        k.sent = true
        keyEv(i, 'down')
      }, 90)
      kd.current.set(i, k)
    } else {
      e.preventDefault()
      e.currentTarget.setPointerCapture(e.pointerId)
      keyEv(i, 'down')
    }
  }
  const kMove = (i: number, e: RPE) => {
    const k = kd.current.get(i)
    if (k && !k.sent && (Math.abs(e.clientY - k.y) > 8 || Math.abs(e.clientX - k.x) > 8)) {
      clearTimeout(k.tm)
      kd.current.delete(i)
    }
  }
  const kUp = (i: number) => {
    const k = kd.current.get(i)
    if (k) {
      clearTimeout(k.tm)
      kd.current.delete(i)
      if (!k.sent) {
        if (!blocked()) {
          flash(`k${i}`, 'web')
          send({ key: i })
        }
        return
      }
    }
    if (down[`k${i}`]) keyEv(i, 'up')
  }

  const zones = M.strip?.z ?? 0
  const touch = p.screen?.touch[0]
  const stripPt = (fx: number): [number, number] =>
    touch ? [Math.round(touch.x + fx * touch.w), Math.round(touch.y + touch.h / 2)] : [Math.round(fx * 800), 430]
  const stripUp = (e: RPE) => {
    const s = pd.current
    pd.current = null
    if (!s) return
    const dx = e.clientX - s.x
    const dy = e.clientY - s.y
    if (Math.abs(dy) > 12 && Math.abs(dy) > Math.abs(dx)) return
    if (blocked()) return
    const fx = (s.x - s.r.left) / s.r.width
    const z = Math.max(0, Math.min(zones - 1, Math.floor(fx * zones)))
    flash(`z${z}`, 'web')
    const [x, y] = stripPt(fx)
    if (Math.abs(dx) > 20) {
      const [x2] = stripPt(Math.max(0, Math.min(1, fx + dx / s.r.width)))
      send({ touch: 'swipe', x, y, x2, y2: y })
    } else send({ touch: Date.now() - s.t > 500 ? 'long' : 'tap', x, y })
  }
  const stripKey = (e: React.KeyboardEvent) => {
    const L = e.key === 'ArrowLeft'
    const R = e.key === 'ArrowRight'
    const mid = (zone + 0.5) / zones
    if ((L || R) && !e.shiftKey) {
      e.preventDefault()
      setZone((z) => Math.max(0, Math.min(zones - 1, z + (R ? 1 : -1))))
      return
    }
    if (L || R || e.key === 'Enter' || e.key === ' ') {
      e.preventDefault()
      if (blocked()) return
      flash(`z${zone}`, 'web')
      const [x, y] = stripPt(mid)
      if (L || R) send({ touch: 'swipe', x, y, x2: stripPt(mid + (R ? 1 : -1) / zones)[0], y2: y })
      else send({ touch: e.shiftKey ? 'long' : 'tap', x, y })
    }
  }

  const bri = 0.35 + (0.65 * p.brightness) / 100
  const D = M.dials
  const dialX = (i: number) => (D ? D.x + (D.w / D.n) * (i + 0.5) : 0)
  const style = {
    '--W': M.W,
    '--H': M.H,
    '--R': M.R,
    '--lp': (M.k - 2 * M.li) / 120,
    '--li': M.li,
    '--kr': M.kr,
    '--bri': bri,
  } as CSSProperties
  return (
    <>
      <div className="fwrap">
        <div className={cx('front', !armed && 'safe')} style={style} role="group" aria-label={p.label}>
          {M.tray && <div className="ftray" style={{ ...mm(M.tray), borderRadius: `calc(${M.tray.r}*var(--mm))` }} />}
          <div className="fmark" style={{ top: `calc(${M.mark.y}*var(--mm))` }}>
            <DeckMark />
            <span>DeckConnector</span>
          </div>
          {Array.from({ length: nk }, (_, i) => {
            const c = i % M.cols
            const r = Math.floor(i / M.cols)
            const hit = hits[`k${i}`]
            return (
              <button
                key={i}
                type="button"
                className={cx('fkey', down[`k${i}`] && 'down', hit && `hit-${hit}`)}
                style={mm({ x: M.x0 + c * M.px, y: M.y0 + r * M.py, w: M.k, h: M.k })}
                aria-label={t('ov.key', { n: i + 1, r: r + 1 })}
                aria-disabled={armed ? undefined : true}
                onPointerDown={(e) => kDown(i, e)}
                onPointerMove={(e) => kMove(i, e)}
                onPointerUp={() => kUp(i)}
                onPointerCancel={() => {
                  const k = kd.current.get(i)
                  if (k) clearTimeout(k.tm)
                  kd.current.delete(i)
                  if (down[`k${i}`]) keyEv(i, 'up')
                }}
                onKeyDown={(e) => {
                  if ((e.key === 'Enter' || e.key === ' ') && !e.repeat) {
                    e.preventDefault()
                    keyEv(i, 'down')
                  }
                }}
                onKeyUp={(e) => {
                  if ((e.key === 'Enter' || e.key === ' ') && down[`k${i}`]) {
                    e.preventDefault()
                    keyEv(i, 'up')
                  }
                }}
              >
                <span className="face">{p.screen && <Pixels screen={p.screen} rect={p.screen.keys[i]} />}</span>
                {hit && <HitIcon kind={hit} />}
              </button>
            )
          })}
          {M.win && <div className="fwin" style={mm(M.win)} />}
          {M.strip && (
            <div
              ref={stripRef}
              role="button"
              tabIndex={0}
              aria-disabled={armed ? undefined : true}
              aria-describedby="kb-hint"
              className="fstrip"
              style={{ ...mm(M.strip), gridTemplateColumns: `repeat(${zones},1fr)` }}
              aria-label={t('ov.strip', { z: zone + 1 })}
              onPointerDown={(e) => {
                if (stripRef.current)
                  pd.current = {
                    x: e.clientX,
                    y: e.clientY,
                    t: Date.now(),
                    r: stripRef.current.getBoundingClientRect(),
                  }
              }}
              onPointerUp={stripUp}
              onPointerCancel={() => (pd.current = null)}
              onKeyDown={stripKey}
              onFocus={() => {
                setKf(true)
                setHint('strip')
              }}
              onBlur={() => {
                setKf(false)
                setHint(null)
              }}
            >
              {p.screen && <Pixels screen={p.screen} rect={touch} />}
              {Array.from({ length: zones }, (_, i) => (
                <div key={i} className={cx('z', hits[`z${i}`] && 'hit', kf && zone === i && 'kz')} />
              ))}
            </div>
          )}
          {M.info && (
            <div className="finfo" style={mm(M.info)}>
              {p.screen && <Pixels screen={p.screen} rect={p.screen.touch[0]} />}
            </div>
          )}
          {M.tp?.map((b, i) => (
            <button
              key={`tp${i}`}
              type="button"
              className={cx('ftp', hits[`tp${i}`] && 'hit-web')}
              style={mm(b)}
              aria-label={t('deck.tp', { n: i + 1 })}
              aria-disabled={armed ? undefined : true}
              onClick={() => {
                if (blocked()) return
                flash(`tp${i}`, 'web')
                send({ key: nk + i })
              }}
            >
              {i ? <ChevronRight className="ic" aria-hidden /> : <ChevronLeft className="ic" aria-hidden />}
            </button>
          ))}
          {D &&
            Array.from({ length: D.n }, (_, i) => {
              const s = dial[i] ?? { delta: 0, rot: 0, live: false }
              return (
                <Fragment key={`d${i}`}>
                  <button
                    type="button"
                    ref={(el) => {
                      encRefs.current[i] = el
                    }}
                    className={cx('fdial', down[`e${i}`] && 'down', hits[`e${i}`] === 'deck' && 'hit-deck')}
                    style={
                      {
                        left: `calc(${dialX(i) - D.d / 2}*var(--mm))`,
                        top: `calc(${D.cy - D.d / 2}*var(--mm))`,
                        width: `calc(${D.d}*var(--mm))`,
                        height: `calc(${D.d}*var(--mm))`,
                        '--dd': D.d,
                        '--rot': `${s.rot}deg`,
                      } as CSSProperties
                    }
                    aria-label={t('ov.enc_press', { n: i + 1 })}
                    aria-describedby="kb-hint"
                    title={t('ov.enc_press', { n: i + 1 })}
                    aria-disabled={armed ? undefined : true}
                    onFocus={() => setHint('knob')}
                    onBlur={() => setHint(null)}
                    onPointerDown={(e) => dDown(i, e)}
                    onPointerMove={(e) => dMove(i, e)}
                    onPointerUp={() => dUp(i)}
                    onPointerCancel={() => {
                      clearTimeout(dd.current.get(i)?.tm)
                      dd.current.delete(i)
                    }}
                    onKeyDown={(e) => {
                      if (e.key === 'ArrowLeft' || e.key === 'ArrowDown') {
                        e.preventDefault()
                        turn(i, -1)
                      } else if (e.key === 'ArrowRight' || e.key === 'ArrowUp') {
                        e.preventDefault()
                        turn(i, 1)
                      } else if ((e.key === 'Enter' || e.key === ' ') && !e.repeat) {
                        e.preventDefault()
                        press(i)
                      }
                    }}
                  >
                    <span className="dhit" aria-hidden>
                      <span className="dz">
                        <ChevronLeft className="ic" />
                      </span>
                      <span className="dz">
                        <ChevronRight className="ic" />
                      </span>
                    </span>
                  </button>
                  {s.delta !== 0 && (
                    <span
                      className={cx('fdelta', !s.live && 'out')}
                      aria-hidden
                      style={{ left: `calc(${dialX(i)}*var(--mm))`, top: `calc(${D.cy - D.d / 2 - 1}*var(--mm))` }}
                    >
                      {s.delta > 0 ? '+' : ''}
                      {s.delta}
                    </span>
                  )}
                </Fragment>
              )
            })}
        </div>
      </div>
      <p id="kb-hint" className="kbhint" hidden={!hint}>
        {hint === 'strip' ? t('kb.strip') : t('kb.knob')}
      </p>
    </>
  )
})

const HitIcon = ({ kind }: { kind: Hit }) => (
  <span className={cx('hico', kind)} aria-hidden>
    {kind === 'deck' ? <Hand className="ic" /> : <MousePointer2 className="ic" />}
  </span>
)

export function DeckMark() {
  return (
    <svg viewBox="0 0 32 32" aria-hidden="true">
      <g fill="currentColor">
        {[0, 1].map((r) =>
          [0, 1, 2, 3].map((c) => (
            <rect
              key={`${r}-${c}`}
              x={4.5 + c * 6.2}
              y={5 + r * 6.2}
              width="4.6"
              height="4.6"
              rx="1"
              opacity={r === 0 && c === 2 ? 1 : 0.7}
            />
          )),
        )}
      </g>
      <path
        d="M6.8 18v2.6h18.6V18M16.1 20.6v4.2"
        stroke="currentColor"
        strokeWidth="1.8"
        fill="none"
        strokeLinecap="round"
        strokeLinejoin="round"
      />
    </svg>
  )
}

export function Logo({ s = 34 }: { s?: number }) {
  return (
    <svg width={s} height={s} viewBox="0 0 32 32" aria-hidden="true">
      <rect x="0.5" y="0.5" width="31" height="31" rx="7" fill="var(--well)" stroke="var(--line-2)" />
      {[0, 1].map((r) =>
        [0, 1, 2, 3].map((c) => (
          <rect
            key={`${r}-${c}`}
            x={4.5 + c * 6.2}
            y={5 + r * 6.2}
            width="4.6"
            height="4.6"
            rx="1"
            fill={r === 0 && c === 2 ? 'var(--amber)' : 'var(--fg)'}
            opacity={r === 0 && c === 2 ? 1 : 0.86}
          />
        )),
      )}
      <path
        d="M6.8 18v2.6h18.6V18M16.1 20.6v4.2"
        stroke="var(--amber)"
        strokeWidth="1.6"
        fill="none"
        strokeLinecap="round"
        strokeLinejoin="round"
      />
      <circle cx="16.1" cy="26.4" r="1.9" fill="var(--amber)" />
    </svg>
  )
}
