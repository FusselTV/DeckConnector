import { ChevronLeft, ChevronRight, Hand, MousePointer2 } from 'lucide-react'
import { Fragment, memo, useEffect, useRef, useState, type CSSProperties, type PointerEvent as RPE } from 'react'
import type { InputBody, InputEvent, Rect, Screen } from '../api/types'
import { useUi } from '../app/ui'
import { cx, useLatest } from '../lib/util'
import { MODELS, stripKeyInput, type ModelKey } from './models'

type Hit = 'web' | 'deck'
const mm = (o: { x: number; y: number; w: number; h: number }): CSSProperties => ({
  left: `calc(${o.x}*var(--mm))`,
  top: `calc(${o.y}*var(--mm))`,
  width: `calc(${o.w}*var(--mm))`,
  height: `calc(${o.h}*var(--mm))`,
})
const overlaps = (a: Rect, b: Rect) => a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h

type PixelsProps = { screen: Screen; rect: Rect | undefined }
const box = (r: Rect) => `${r.x},${r.y},${r.w},${r.h}`
const facePrint = ({ screen: s, rect: r }: PixelsProps) =>
  r ? `${s.width}x${s.height} ${box(r)} ${s.images.map((i) => (overlaps(i, r) ? i.url + box(i) : '')).join(' ')}` : ''

/** The part of the deck picture that falls on one key or strip, scaled to its box. A key whose images did not
 *  change keeps its DOM. */
const Pixels = memo(
  function Pixels({ screen, rect }: PixelsProps) {
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
              onError={(e) => (e.currentTarget.style.visibility = 'hidden')}
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
  },
  (a, b) => facePrint(a) === facePrint(b),
)

interface DeckFrontProps {
  model: ModelKey
  label: string
  screen: Screen | null
  brightness: number
  dark?: boolean
  armed: boolean
  /** The last input from the store, so presses on the real deck light up here. */
  last: InputEvent | undefined
  /** Called for a press while not armed: the remote-control pill shakes. */
  onLocked: () => void
  /** `auto` marks a turn from auto-repeat, which must not keep the remote control on. */
  send: (body: InputBody, auto?: boolean) => void
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
  t0: number
}
const HOLD_MS = 450
const REPEAT_MAX_MS = 5000
// One wheel notch is 100 px in Chrome and Safari; Firefox counts lines (deltaMode 1).
const WHEEL_PX = 100

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
  // Keys this page holds down on the device. Releases are decided from here, never from render state.
  const held = useRef(new Set<number>())
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
  const keyDown = (i: number) => {
    if (held.current.has(i) || blocked()) return
    held.current.add(i)
    setDown((d) => ({ ...d, [`k${i}`]: true }))
    flash(`k${i}`, 'web')
    send({ key: i, action: 'down' })
  }
  const keyUp = (i: number) => {
    if (!held.current.delete(i)) return
    setDown((d) => ({ ...d, [`k${i}`]: false }))
    send({ key: i, action: 'up' })
  }
  const tap = (i: number) => {
    if (blocked()) return
    flash(`k${i}`, 'web')
    send({ key: i })
  }
  // A blur, a hidden page, the remote control turning off or an unmount must never leave a key held or a dial
  // repeating on the device.
  const stop = useLatest(() => {
    for (const k of kd.current.values()) clearTimeout(k.tm)
    kd.current.clear()
    for (const g of dd.current.values()) clearTimeout(g.tm)
    dd.current.clear()
    for (const i of [...held.current]) keyUp(i)
  })
  useEffect(() => {
    const f = () => stop.current()
    const badges = dtm.current
    window.addEventListener('blur', f)
    document.addEventListener('visibilitychange', f)
    return () => {
      window.removeEventListener('blur', f)
      document.removeEventListener('visibilitychange', f)
      f()
      Object.values(badges).forEach(clearTimeout)
    }
  }, [stop])
  useEffect(() => {
    if (!armed) stop.current()
  }, [armed, stop])

  // Encoders are endless: show the turn as a +n badge, then spring back to rest.
  const turn = (e: number, d: number, auto = false) => {
    if (blocked()) return false
    flash(`e${e}`, 'web')
    send({ encoder: e, action: 'turn', ticks: d }, auto)
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
    // A trackpad fires dozens of small wheel events per gesture: the distance counts, and one turn per frame is sent.
    const hs = encRefs.current.map((el, i) => {
      if (!el) return null
      let acc = 0
      let ticks = 0
      let raf = 0
      const f = (ev: WheelEvent) => {
        if (document.activeElement !== el) return
        ev.preventDefault()
        const dy = ev.deltaY * (ev.deltaMode === 1 ? 33 : ev.deltaMode === 2 ? 800 : 1)
        if (Math.sign(dy) !== Math.sign(acc)) acc = 0
        acc += dy
        const n = Math.trunc(acc / WHEEL_PX)
        if (!n) return
        acc -= n * WHEEL_PX
        ticks += n
        raf ||= requestAnimationFrame(() => {
          raf = 0
          turnRef.current(i, ticks)
          ticks = 0
        })
      }
      el.addEventListener('wheel', f, { passive: false })
      return [el, f, () => cancelAnimationFrame(raf)] as const
    })
    return () =>
      hs.forEach((x) => {
        x?.[0].removeEventListener('wheel', x[1])
        x?.[2]()
      })
  }, [M.dials?.n, turnRef])

  const dDown = (i: number, e: RPE<HTMLButtonElement>) => {
    const r = e.currentTarget.getBoundingClientRect()
    const fx = (e.clientX - r.left) / r.width
    const g: Drag = { x: e.clientX, acc: 0, turned: false, z: fx < 0.35 ? -1 : fx > 0.65 ? 1 : 0, t0: e.timeStamp }
    dd.current.set(i, g)
    // Holding an arrow zone turns on and speeds up until release, for a few seconds at most.
    if (g.z) {
      const rep = (ms: number) => {
        g.rep = true
        const over = performance.now() - g.t0 > REPEAT_MAX_MS
        if (dd.current.get(i) !== g || over || !turnRef.current(i, g.z, true)) return
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
    const n = Math.trunc(g.acc / 10)
    if (!n) return
    clearTimeout(g.tm)
    g.acc -= n * 10
    g.turned = true
    turn(i, n)
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
  // On touch a key sends nothing until the finger lifts (a tap) or rests still for HOLD_MS (a hold), so a
  // scroll gesture that starts on a key never presses it.
  const kDown = (i: number, e: RPE<HTMLButtonElement>) => {
    if (e.pointerType === 'touch') {
      const k = { x: e.clientX, y: e.clientY, tm: 0, sent: false }
      k.tm = window.setTimeout(() => {
        k.sent = true
        keyDown(i)
      }, HOLD_MS)
      kd.current.set(i, k)
    } else {
      e.preventDefault()
      e.currentTarget.setPointerCapture(e.pointerId)
      keyDown(i)
    }
  }
  const kMove = (i: number, e: RPE) => {
    const k = kd.current.get(i)
    if (k && !k.sent && (Math.abs(e.clientY - k.y) > 8 || Math.abs(e.clientX - k.x) > 8)) {
      clearTimeout(k.tm)
      kd.current.delete(i)
    }
  }
  const kUp = (i: number, cancel = false) => {
    const k = kd.current.get(i)
    kd.current.delete(i)
    if (k) clearTimeout(k.tm)
    if (k && !k.sent && !cancel) tap(i)
    keyUp(i)
  }

  const zones = M.strip?.z ?? 0
  const zk = !!M.strip?.keys
  const touch = p.screen?.touch[0]
  const [z0, zn] = [p.screen?.keys[nk], p.screen?.keys[nk + zones - 1]]
  const stripRect = zk && z0 && zn ? { ...z0, w: zn.x + zn.w - z0.x } : touch
  const zoneSend = (z: number, dir: number) => {
    if (blocked()) return
    flash(`z${z}`, 'web')
    send(stripKeyInput(M, z, dir))
  }
  // Without the strip's place in the picture there is no point to send that the firmware would take.
  const stripPt = (fx: number): [number, number] =>
    touch ? [Math.round(touch.x + fx * touch.w), Math.round(touch.y + touch.h / 2)] : [0, 0]
  const stripUp = (e: RPE) => {
    const s = pd.current
    pd.current = null
    if (!s) return
    const dx = e.clientX - s.x
    const dy = e.clientY - s.y
    if (Math.abs(dy) > 12 && Math.abs(dy) > Math.abs(dx)) return
    const fx = (s.x - s.r.left) / s.r.width
    const z = Math.max(0, Math.min(zones - 1, Math.floor(fx * zones)))
    if (zk) return zoneSend(z, Math.abs(dx) > 20 ? Math.sign(dx) : 0)
    if (!touch || blocked()) return
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
    setKf(true)
    setHint('strip')
    if ((L || R) && !e.shiftKey) {
      e.preventDefault()
      setZone((z) => Math.max(0, Math.min(zones - 1, z + (R ? 1 : -1))))
      return
    }
    if (L || R || e.key === 'Enter' || e.key === ' ') {
      e.preventDefault()
      if (zk) return zoneSend(zone, L ? -1 : R ? 1 : 0)
      if (!touch || blocked()) return
      flash(`z${zone}`, 'web')
      const [x, y] = stripPt(mid)
      if (L || R) send({ touch: 'swipe', x, y, x2: stripPt(mid + (R ? 1 : -1) / zones)[0], y2: y })
      else send({ touch: e.shiftKey ? 'long' : 'tap', x, y })
    }
  }

  const bri = p.dark ? 0.12 : 0.35 + (0.65 * p.brightness) / 100
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
                onPointerCancel={() => kUp(i, true)}
                onBlur={() => kUp(i, true)}
                onKeyDown={(e) => {
                  if (e.key === 'Enter' || e.key === ' ') {
                    e.preventDefault()
                    if (!e.repeat) keyDown(i)
                  }
                }}
                onKeyUp={(e) => {
                  if (e.key === 'Enter' || e.key === ' ') {
                    e.preventDefault()
                    keyUp(i)
                  }
                }}
                // Screen readers in browse mode activate with a bare click (detail 0) and no pointer or key events.
                onClick={(e) => e.detail === 0 && tap(i)}
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
              aria-disabled={armed && (zk || touch) ? undefined : true}
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
              onFocus={(e) => {
                // A click focuses the strip too; the zone frame and the key help are for the keyboard only.
                if (!e.currentTarget.matches(':focus-visible')) return
                setKf(true)
                setHint('strip')
              }}
              onBlur={() => {
                setKf(false)
                setHint(null)
              }}
            >
              {p.screen && <Pixels screen={p.screen} rect={stripRect} />}
              {Array.from({ length: zones }, (_, i) => (
                <div key={i} className={cx('z', hits[`z${i}`] && 'hit', kf && zone === i && 'kz')} />
              ))}
            </div>
          )}
          {M.info && (
            <div className="finfo" style={mm(M.info)}>
              {p.screen && <Pixels screen={p.screen} rect={p.screen.strips?.[0] ?? p.screen.touch[0]} />}
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
                    onFocus={(e) => e.currentTarget.matches(':focus-visible') && setHint('knob')}
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
                        turn(i, -1, e.repeat)
                      } else if (e.key === 'ArrowRight' || e.key === 'ArrowUp') {
                        e.preventDefault()
                        turn(i, 1, e.repeat)
                      } else if (e.key === 'Enter' || e.key === ' ') {
                        e.preventDefault()
                        if (!e.repeat) press(i)
                      }
                    }}
                    onKeyUp={(e) => (e.key === 'Enter' || e.key === ' ') && e.preventDefault()}
                    onClick={(e) => e.detail === 0 && press(i)}
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

// The mark printed on the case: Logo without its tile, in one colour.
function DeckMark() {
  return (
    <svg viewBox="4 3 24 26" aria-hidden="true" fill="currentColor">
      {[0, 1].map((r) =>
        [0, 1, 2].map((c) => (
          <rect
            key={`${r}${c}`}
            x={6 + c * 7.2}
            y={5 + r * 7.2}
            width="5.2"
            height="5.2"
            rx="1.2"
            opacity={r + c ? 0.7 : 1}
          />
        )),
      )}
      <rect x="13.5" y="19.8" width="5" height="5" rx="1" />
      <path d="M16 24.8V29" stroke="currentColor" strokeWidth="2.4" />
    </svg>
  )
}

// The same mark as the site's header and favicons: fixed colours, one version for light and dark.
export function Logo({ s = 34 }: { s?: number }) {
  return (
    <svg width={s} height={s} viewBox="0 0 32 32" aria-hidden="true">
      <rect x=".5" y=".5" width="31" height="31" rx="7.5" fill="#1b1e22" stroke="#3b4148" />
      {[0, 1].map((r) =>
        [0, 1, 2].map((c) => (
          <rect
            key={`${r}${c}`}
            x={6 + c * 7.2}
            y={5 + r * 7.2}
            width="5.2"
            height="5.2"
            rx="1.2"
            fill={r + c ? '#e8e6e1' : '#ffb020'}
          />
        )),
      )}
      <rect x="13.5" y="19.8" width="5" height="5" rx="1" fill="#ffb020" />
      <path d="M16 24.8V32" stroke="#ffb020" strokeWidth="2.4" />
    </svg>
  )
}
