import { spring, useCurrentFrame, useVideoConfig } from 'remotion'
import { AMBER, at, Board, BOARD_USB, Cable, Deck, GREEN, INK, MONO, Pool, SANS, rounded, Trail, type Key, type Pt, showKeys, showStrip } from './hw'
import { camAt, Caption, Scene, type Shot } from './scene'
import { lin, pulse, ramp } from './time'
import { TEXT, type LangProps, type Text } from './text'

const W = 1280
const H = 720
// The browser is drawn at 700 x 608 and scaled up, so its UI reads like a real page.
const WIN = { x: 0, y: 0, w: 700, h: 608 }
const WS = 1.3
const BOARD = { x: 1070, y: 70, s: 1.5 }
const DECK = { x: 1180, y: 380, s: 3.4 }
const USBC = rounded([[WIN.w * WS + 58, at(BOARD, [0, 115])[1]], at(BOARD, [-6, 115])])
const USBA = rounded([at(BOARD, BOARD_USB), [1030, at(BOARD, BOARD_USB)[1]], [1030, 30], [1560, 30], [1560, 420]])
const SSID = 'Studio'
const PASS = 10
const NAME = 'deck-stage'
const T = {
  move1: [14, 30],
  click1: 32,
  chooser: [36, 48],
  move2: [46, 60],
  click2: 64,
  flash: [76, 160],
  form: 168,
  ssid: [180, 196],
  pass: [200, 214],
  move3: [216, 230],
  click3: 234,
  boot: [240, 256],
  lit: 262,
  comp: 292,
  out: [318, 330],
} as const

const SHOTS: Shot[] = [
  [0, { x: 455, y: 250, z: 1.3, rx: 3, ry: 2 }],
  [30, { x: 455, y: 262, z: 1.33, rx: 3, ry: 2 }],
  [70, { x: 455, y: 262, z: 1.33, rx: 3, ry: 2 }],
  [94, { x: 720, y: 300, z: 0.95, rx: 6, ry: -2 }],
  [118, { x: 1180, y: 190, z: 2.1, rx: 4, ry: -10 }],
  [156, { x: 1190, y: 200, z: 2.2, rx: 4, ry: -10 }],
  [176, { x: 455, y: 460, z: 1.2, rx: 3, ry: 2 }],
  [232, { x: 455, y: 465, z: 1.22, rx: 3, ry: 2 }],
  [262, { x: 1418, y: 522, z: 1.8, rx: 10, ry: -8 }],
  [316, { x: 1418, y: 520, z: 1.86, rx: 9, ry: -5 }],
  [330, { x: 455, y: 250, z: 1.3, rx: 3, ry: 2 }],
]
// On a phone the browser shots move closer, so the page text stays readable.
const SHOTS_NARROW: Shot[] = SHOTS.map(([f, c]) => [f, c.x === 455 ? { ...c, x: 440, z: c.z * 1.12 } : c])

export default function Flash({ lang, narrow }: LangProps) {
  const f = useCurrentFrame()
  const { fps } = useVideoConfig()
  const t = TEXT[lang]
  const cap = narrow ? 76 : 56
  const out = ramp(f, T.out[0], T.out[1])
  const progress = lin(f, T.flash[0], T.flash[1])
  const flashing = f >= T.flash[0] && f < T.flash[1]
  const booted = f >= T.boot[1]
  const power = f >= T.boot[0] && f < T.boot[0] + 8 ? 0 : 1
  const first = firstPicture(t)
  const page = showKeys(t, f, 0)
  const swept = (i: number) => f >= T.comp + ((i % 4) + Math.floor(i / 4)) * 3
  const keys = first.keys.map((k, i) => (swept(i) ? (page[i] ?? null) : k))
  const lit = keys.map((_, i) => (1 - out) * (swept(i) ? ramp(f, T.comp + ((i % 4) + Math.floor(i / 4)) * 3, T.comp + ((i % 4) + Math.floor(i / 4)) * 3 + 6) : spring({ frame: f - T.lit - i * 2, fps, config: { damping: 18, stiffness: 140 } })))
  const flash = keys.map((_, i) => 0.4 * Math.max(pulse(f, T.lit + i * 2, T.lit + i * 2 + 14, 2, 11), pulse(f, T.comp + ((i % 4) + Math.floor(i / 4)) * 3, T.comp + ((i % 4) + Math.floor(i / 4)) * 3 + 12, 1, 10)))

  return (
    <Scene
      w={W}
      h={H}
      cam={camAt(f, narrow ? SHOTS_NARROW : SHOTS)}
      over={
        <>
          <Caption f={f} from={T.flash[0] + 30} to={T.flash[1] + 6} text={progress >= 1 ? t.verifying : t.flashing} big={`${Math.round(progress * 100)}%`} dot={progress >= 1 ? GREEN : AMBER} size={cap} />
          <Caption f={f} from={T.lit + 4} to={T.comp + 2} text={t.flashReady} dot={GREEN} size={cap} />
          <Caption f={f} from={T.comp + 2} to={T.out[0] + 6} text={t.flashComp} dot={GREEN} size={cap} />
        </>
      }
    >
      <Pool x={1418} y={760} rx={460} ry={200} c={AMBER} o={0.1 + 0.25 * (lit[0] ?? 0)} />
      <Cable pts={USBA} glow={Math.max(pulse(f, T.lit - 8, T.lit + 20, 4, 10), pulse(f, T.comp - 14, T.comp + 10, 4, 10))} opacity={ramp(f, T.form, T.form + 12) * (1 - out)} />
      <Cable pts={USBC} w={10} glow={flashing ? 0.6 : 0} />
      <g transform={`translate(${WIN.w * WS} ${at(BOARD, [0, 115])[1]})`}>
        <rect x={0} y={-13} width={40} height={26} rx={6} fill="#25292f" />
        <rect x={40} y={-8} width={18} height={16} rx={5} fill="#b9bec5" />
      </g>
      <Board {...BOARD} link={0} act={flashing ? Math.floor(f / 3) % 2 : 0} power={power} hot={flashing ? 0.4 + 0.6 * pulse(f, T.flash[0], T.flash[1], 10, 10) : booted ? pulse(f, T.boot[1], T.lit + 20, 4, 12) : 0} ring={flashing ? progress : 0} wifi={booted ? ramp(f, T.boot[1], T.boot[1] + 10) * (1 - out) : 0} sheen={Math.sin((f / 330) * Math.PI * 2)} />
      {flashing && [0, 1, 2, 3].map((k) => <Trail key={k} pts={USBC} t={(((f - T.flash[0]) / 16 + k / 4) % 1) * 1.3} w={8} len={0.35} />)}
      {[0, 6, 12].map((d) => (
        <g key={d}>
          <Trail pts={USBA} t={lin(f, T.lit - 22 + d, T.lit - 2 + d, 0, 1.3)} w={10} len={0.2} />
          <Trail pts={USBA} t={lin(f, T.comp - 22 + d, T.comp - 2 + d, 0, 1.3)} w={12} len={0.25} color={d === 6 ? '#ffffff' : AMBER} />
        </g>
      ))}
      <Deck {...DECK} edge={AMBER} keys={keys} under={f >= T.comp ? first.keys : undefined} strip={f >= T.comp + 12 ? showStrip(t, 40) : first.strip} lit={lit} flash={flash} stripLit={ramp(f, T.lit + 12, T.lit + 20) * (1 - out)} sheen={0.4} />
      <g transform={`scale(${WS})`}>
        <Browser t={t} f={f} out={out} progress={progress} />
      </g>
    </Scene>
  )
}

function firstPicture(t: Text) {
  const wifi = (
    <g transform="translate(60 72) scale(1.35)" fill="none" stroke={INK} strokeWidth={7} strokeLinecap="round">
      <path d="M-15 -4 a21 21 0 0 1 30 0" />
      <path d="M-28 -17 a39 39 0 0 1 56 0" />
      <circle r={4} cy={9} fill={INK} stroke="none" />
    </g>
  )
  const keys: (Key | null)[] = [
    { bg: '#3a2a06', glow: AMBER, art: <Spinner /> },
    { bg: '#1c7a45', glow: GREEN, art: wifi },
    { bg: '#1c7a45', top: t.lan, big: 'OK', glow: GREEN },
    { bg: '#262b32', big: t.later, glow: '#5a6470' },
    { bg: '#262b32', big: t.menu, glow: '#5a6470' },
    { bg: '#1b1f24', top: t.light, big: '−', glow: '#5a6470' },
    { bg: '#1b1f24', top: t.light, big: '+', glow: '#5a6470' },
    { bg: '#1f4f86', top: 'Web', big: 'IP', glow: '#3d8bff' },
  ]
  return {
    keys,
    strip: (
      <>
        <text x={36} y={42} fontFamily={MONO} fontSize={26} fontWeight={700} fill={INK} opacity={0.65}>
          DeckConnector
        </text>
        <text x={36} y={86} fontFamily={MONO} fontSize={40} fontWeight={800} fill={AMBER}>
          http://{NAME}.local
        </text>
      </>
    ),
  }
}

const C = { bg: '#0f1113', panel: '#171a1d', panel2: '#1e2226', well: '#0b0d0f', line: '#2a2f35', line2: '#3b4148', fg: '#e8e6e1', fg2: '#a6acb3', fg3: '#858c94' }

function Browser({ t, f, out, progress }: { t: Text; f: number; out: number; progress: number }) {
  const { x, y, w, h } = WIN
  const px = x + 56
  const step = f < T.flash[0] ? 0 : f < T.form ? 1 : f < T.click3 + 4 ? 2 : 3
  const typedSsid = SSID.slice(0, Math.round(lin(f, T.ssid[0], T.ssid[1], 0, SSID.length)))
  const typedPass = Math.round(lin(f, T.pass[0], T.pass[1], 0, PASS))
  const chooser = f < T.click2 + 4 ? pulse(f, T.chooser[0], T.click2 + 4, 6, 4) : 0
  const cursor = cursorAt(f)
  const press = Math.max(pulse(f, T.click1, T.click1 + 6, 1, 3), pulse(f, T.click2, T.click2 + 6, 1, 3), pulse(f, T.click3, T.click3 + 6, 1, 3))
  const reset = out > 0.5
  const s = reset ? 0 : step

  return (
    <g>
      <rect x={x + 4} y={y + 14} width={w} height={h} rx={14} fill="#000" opacity={0.45} />
      <rect x={x} y={y} width={w} height={h} rx={14} fill={C.bg} stroke={C.line2} strokeWidth={1.5} />
      <path d={`M${x} ${y + 48} H${x + w}`} stroke={C.line} strokeWidth={1.5} />
      {['#ff5f57', '#febc2e', '#28c840'].map((c, i) => (
        <circle key={c} cx={x + 24 + i * 20} cy={y + 24} r={6} fill={c} opacity={0.85} />
      ))}
      <rect x={x + 100} y={y + 11} width={w - 200} height={26} rx={13} fill={C.panel2} />
      <text x={x + w / 2} y={y + 29} textAnchor="middle" fontFamily={MONO} fontSize={15} fill={C.fg3}>
        /flash
      </text>
      <g opacity={out > 0 && !reset ? 1 - out : reset ? ramp(out, 0.5, 1) : 1}>
        <text x={px} y={y + 112} fontFamily={SANS} fontSize={40} fontWeight={600} fill={C.fg} letterSpacing={-1}>
          {t.flashTitle}
        </text>
        {[t.connect, t.flashing, t.setup].map((label, i) => {
          const done = s > i
          const cur = s === i
          const sx = px + i * 200
          return (
            <g key={i}>
              <circle cx={sx + 15} cy={y + 162} r={15} fill={done ? GREEN : cur ? AMBER : C.panel2} stroke={done || cur ? 'none' : C.line2} />
              {done ? (
                <path d={`M${sx + 8} ${y + 162} l5 5 l9 -10`} stroke="#0b0d0f" strokeWidth={3} fill="none" strokeLinecap="round" strokeLinejoin="round" />
              ) : (
                <text x={sx + 15} y={y + 168} textAnchor="middle" fontFamily={SANS} fontSize={16} fontWeight={700} fill={cur ? '#1a1200' : C.fg3}>
                  {i + 1}
                </text>
              )}
              <text x={sx + 40} y={y + 168} fontFamily={SANS} fontSize={19} fontWeight={600} fill={cur ? C.fg : C.fg3}>
                {label}
              </text>
            </g>
          )
        })}
        {s === 0 && (
          <g>
            <foreignObject x={px} y={y + 214} width={w - 112} height={60}>
              <div style={{ font: `400 21px/1.4 ${SANS}`, color: C.fg2 }}>{t.plugIn}</div>
            </foreignObject>
            <Btn x={px} y={y + 284} w={220} h={56} label={t.connect} primary press={f < T.chooser[0] ? press : 0} />
          </g>
        )}
        {s === 1 && (
          <g>
            <text x={px} y={y + 248} fontFamily={MONO} fontSize={20} fontWeight={700} fill={C.fg2}>
              {progress >= 1 ? t.verifying : t.flashing}
            </text>
            <text x={px + w - 112} y={y + 248} textAnchor="end" fontFamily={MONO} fontSize={26} fontWeight={800} fill={C.fg}>
              {Math.round(progress * 100)}%
            </text>
            <rect x={px} y={y + 266} width={w - 112} height={24} rx={12} fill={C.panel2} />
            <rect x={px} y={y + 266} width={(w - 112) * progress} height={24} rx={12} fill={progress >= 1 ? GREEN : AMBER} />
            <text x={px} y={y + 330} fontFamily={MONO} fontSize={16} fill={C.fg3}>
              {`0x${Math.floor(progress * 0x1d4000).toString(16).padStart(6, '0')} / 0x1d4000`}
            </text>
          </g>
        )}
        {s >= 2 && (
          <g>
            <Field x={px} y={y + 214} w={w - 112} label={t.ssid} value={typedSsid} caret={f >= T.ssid[0] && f < T.pass[0]} />
            <Field x={px} y={y + 304} w={w - 112} label={t.password} value={'•'.repeat(typedPass)} caret={f >= T.pass[0] && f < T.move3[1]} />
            <Field x={px} y={y + 394} w={w - 112} label={t.name} value={NAME} />
            {s === 2 ? (
              <Btn x={px} y={y + 500} w={360} h={56} label={t.save} primary press={f > T.move3[0] ? press : 0} />
            ) : (
              <g>
                <circle cx={px + 15} cy={y + 528} r={15} fill={f >= T.lit ? GREEN : C.panel2} />
                {f >= T.lit ? (
                  <path d={`M${px + 8} ${y + 528} l5 5 l9 -10`} stroke="#0b0d0f" strokeWidth={3} fill="none" strokeLinecap="round" strokeLinejoin="round" />
                ) : (
                  <circle cx={px + 15} cy={y + 528} r={8} fill="none" stroke={AMBER} strokeWidth={3} strokeDasharray="30 30" transform={`rotate(${f * 12} ${px + 15} ${y + 528})`} />
                )}
                <text x={px + 42} y={y + 535} fontFamily={SANS} fontSize={20} fontWeight={700} fill={C.fg}>
                  {f >= T.lit ? `${t.ready} ` : `${t.rebooting}…`}
                  {f >= T.lit && <tspan fill={AMBER}>{NAME}.local</tspan>}
                </text>
              </g>
            )}
          </g>
        )}
      </g>
      {chooser > 0 && <rect x={x} y={y + 49} width={w} height={h - 49} rx={14} fill="#050607" opacity={chooser * 0.6} />}
      {chooser > 0 && (
        <g opacity={chooser} transform={`translate(0 ${(1 - chooser) * -8})`}>
          <rect x={x + 210} y={y + 56} width={440} height={220} rx={12} fill={C.panel} stroke={C.line2} strokeWidth={1.5} />
          <foreignObject x={x + 230} y={y + 70} width={400} height={60}>
            <div style={{ font: `600 17px/1.35 ${SANS}`, color: C.fg }}>{t.pickPort}</div>
          </foreignObject>
          <rect x={x + 230} y={y + 136} width={400} height={44} rx={8} fill={f > T.move2[1] - 8 ? 'rgba(255,176,32,0.15)' : C.panel2} stroke={f > T.move2[1] - 8 ? AMBER : 'none'} />
          <text x={x + 248} y={y + 164} fontFamily={SANS} fontSize={17} fontWeight={600} fill={C.fg}>
            {t.port}
          </text>
          <Btn x={x + 500} y={y + 204} w={130} h={46} label={t.connect} primary press={f > T.chooser[1] ? press : 0} small />
        </g>
      )}
      {cursor && <Cursor at={cursor.at} o={cursor.o * (1 - out)} down={press} />}
    </g>
  )
}

function cursorAt(f: number): { at: Pt; o: number } | null {
  const { x, y } = WIN
  const P = {
    start: [x + 480, y + 440],
    connect: [x + 160, y + 316],
    chooser: [x + 570, y + 230],
    save: [x + 150, y + 532],
  } satisfies Record<string, Pt>
  const go = (a: Pt, b: Pt, r: readonly [number, number]) => {
    const k = ramp(f, r[0], r[1])
    return [a[0] + (b[0] - a[0]) * k, a[1] + (b[1] - a[1]) * k] as Pt
  }
  if (f < T.flash[0]) return { at: f < T.move2[0] ? go(P.start, P.connect, T.move1) : go(P.connect, P.chooser, T.move2), o: ramp(f, 4, 12) * (1 - ramp(f, T.flash[0] - 8, T.flash[0])) }
  if (f >= T.pass[1] - 6 && f < T.boot[0]) return { at: go([x + 520, y + 470], P.save, T.move3), o: ramp(f, T.pass[1] - 6, T.pass[1]) * (1 - ramp(f, T.click3 + 4, T.boot[0])) }
  return null
}

function Cursor({ at: p, o, down }: { at: Pt; o: number; down: number }) {
  return (
    <g transform={`translate(${p[0]} ${p[1]}) scale(${1.25 - down * 0.15})`} opacity={o}>
      {down > 0 && <circle r={18 + down * 10} fill="#fff" opacity={0.25 * down} />}
      <path d="M0 0 L0 26 L7 20 L12 31 L17 29 L12 18 L21 18 Z" fill="#fff" stroke="#000" strokeWidth={1.6} strokeLinejoin="round" />
    </g>
  )
}

function Btn({ x, y, w, h = 50, label, primary, press = 0, small }: { x: number; y: number; w: number; h?: number; label: string; primary?: boolean; press?: number; small?: boolean }) {
  return (
    <g transform={`translate(${x + w / 2} ${y + h / 2}) scale(${1 - press * 0.04}) translate(${-w / 2} ${-h / 2})`}>
      {primary && <rect x={-10} y={-10} width={w + 20} height={h + 20} rx={18} fill={AMBER} opacity={0.12 + press * 0.2} />}
      <rect width={w} height={h} rx={10} fill={primary ? AMBER : C.panel2} />
      <text x={w / 2} y={h / 2 + (small ? 6 : 7)} textAnchor="middle" fontFamily={SANS} fontSize={small ? 17 : 21} fontWeight={700} fill={primary ? '#1a1200' : C.fg}>
        {label}
      </text>
    </g>
  )
}

function Field({ x, y, w, label, value, caret }: { x: number; y: number; w: number; label: string; value: string; caret?: boolean }) {
  return (
    <g>
      <text x={x} y={y} fontFamily={SANS} fontSize={17} fontWeight={700} fill={C.fg2}>
        {label}
      </text>
      <rect x={x} y={y + 12} width={w} height={54} rx={10} fill={C.well} stroke={caret ? AMBER : C.line2} strokeWidth={caret ? 2.5 : 1.2} />
      <text x={x + 18} y={y + 48} fontFamily={SANS} fontSize={22} fontWeight={600} fill={C.fg}>
        {value}
        {caret && <tspan fill={AMBER}>|</tspan>}
      </text>
    </g>
  )
}

/** The welcome key: Companion is not there yet. */
function Spinner() {
  const f = useCurrentFrame()
  return (
    <g transform={`translate(60 60) rotate(${f * 9})`} fill="none" strokeLinecap="round">
      <circle r={30} stroke={AMBER} strokeOpacity={0.25} strokeWidth={10} />
      <path d="M0 -30 a30 30 0 0 1 30 30" stroke={AMBER} strokeWidth={10} />
    </g>
  )
}
