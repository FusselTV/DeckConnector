import { CircleCheck, Gauge, LayoutGrid, SquareMenu, TriangleAlert, Turtle, Unplug, Usb } from 'lucide-react'
import { useState } from 'react'
import type { Config } from '../api/types'
import { putConfig } from '../app/actions'
import { decksOf, deckName } from '../app/derive'
import { deckSelAtom, toast } from '../app/state'
import { useStore } from '../app/store'
import { useUi } from '../app/ui'
import { Card } from '../components/Card'
import { describedBy, Field, Help, Seg, SIcon } from '../components/controls'
import { useDraft } from '../components/SaveBar'
import { MODELS, type Model, type ModelKey } from '../deck/models'
import type { T } from '../i18n'
import { cx } from '../lib/util'

type Chord = Config['menu_chord']
interface Hl {
  keys: number[]
  dials: number[]
}
export function chordHl(o: Chord, M: Model): Hl {
  const nk = M.cols * M.rows
  const dials = M.dials?.n ?? 0
  const enc = { keys: [], dials: [0, Math.min(3, dials - 1)] }
  const key = { keys: [0, nk - 1], dials: [] }
  return o === 'encoders' ? enc : o === 'keys' ? key : o === 'auto' ? (dials ? enc : key) : { keys: [], dials: [] }
}
export function chordSentence(t: T, o: Chord, M: Model, holdS: string) {
  const hl = chordHl(o, M)
  if (o === 'off') return t('chord.s_off')
  const [a = 0, b = 0] = hl.dials.length ? hl.dials : hl.keys
  return t(hl.dials.length ? 'chord.s_dials' : 'chord.s_keys', { a: a + 1, b: b + 1, s: holdS })
}

export function MiniDeck({ kind, hl }: { kind: ModelKey; hl: Hl }) {
  const M: Model = MODELS[kind]
  const nk = M.cols * M.rows
  const nd = M.dials?.n ?? 0
  return (
    <span className="minideck" aria-hidden>
      <span className="mkeys" style={{ gridTemplateColumns: `repeat(${M.cols},1fr)` }}>
        {Array.from({ length: nk }, (_, i) => (
          <i key={i} className={hl.keys.includes(i) ? 'on' : ''} />
        ))}
      </span>
      {M.strip && <span className="mstrip" />}
      {nd > 0 && (
        <span className="mdials">
          {Array.from({ length: nd }, (_, i) => (
            <i key={i} className={hl.dials.includes(i) ? 'on' : ''} />
          ))}
        </span>
      )}
    </span>
  )
}

export function DeckTab() {
  const { t, fmt, act, ask } = useUi()
  const st = useStore((s) => s.status)
  const cfg = useStore((s) => s.config)
  const sel = deckSelAtom.use()
  const decks = decksOf(st)
  const multi = decks.length > 1
  const deck = decks[Math.min(sel, decks.length - 1)]
  const kind: ModelKey = deck?.kind ?? 'plus'
  const base = {
    chord: cfg?.menu_chord ?? 'auto',
    hold: (cfg?.menu_hold_ms ?? 2000) / 1000,
    usb: cfg?.usb_speed ?? 'auto',
  }
  const [d, setD] = useState<typeof base | null>(null)
  const v = d ?? base
  const dirty = !!d && JSON.stringify(d) !== JSON.stringify(base)
  const restart = dirty && v.usb !== base.usb
  const holdErr = v.hold < 0.5 || v.hold > 10 ? t('deck.hold_h') : null
  useDraft('deck', {
    dirty,
    restart,
    reset: () => setD(null),
    save: async () => {
      if (holdErr) {
        document.getElementById('hold')?.focus()
        return false
      }
      if (restart && !(await ask({ title: t('cf.usb'), body: t('cf.usb_b'), ok: t('c.save_restart') }))) return false
      const ok = await act(async () => {
        await putConfig({ menu_chord: v.chord, menu_hold_ms: Math.round(v.hold * 1000), usb_speed: v.usb })
        if (!restart) toast(t('c.saved'))
        return true
      })
      if (ok) setD(null)
      return !!ok
    },
  })
  const anyDials = decks.some((x) => MODELS[x.kind].dials) || !decks.length
  return (
    <div className="cards">
      <Card
        id="card-decks"
        icon={LayoutGrid}
        title={multi ? t('deck.list') : t('deck.one')}
        right={multi && <Help right label={t('deck.list')} text={t('deck.map')} />}
      >
        {!deck ? (
          <div className="empty">
            <Unplug size={18} aria-hidden />
            {t('is.nodeck_h')}
          </div>
        ) : (
          <div className="list" role={multi ? 'radiogroup' : undefined} aria-label={multi ? t('deck.pick') : undefined}>
            {decks.map((x, i) => {
              const body = (
                <>
                  <SIcon
                    icon={multi && sel === i ? CircleCheck : LayoutGrid}
                    tone={x.speed === 'high' ? 'ok' : 'warn'}
                  />
                  <div className="grow">
                    <b>{deckName(x, i, decks.length, t)}</b>
                    <br />
                    <span className="small muted">
                      {multi && `${x.model} · `}
                      <span className="mono">{x.serial}</span>
                      {x.keys !== undefined && ` · ${t('deck.layout_v', { k: x.keys, e: x.encoders ?? 0 })}`}
                      {x.firmware && (
                        <>
                          {' · fw '}
                          <span className="mono">{x.firmware}</span>
                        </>
                      )}
                      {multi && x.surface && ` · ${t('deck.surface', { n: x.surface })}`}
                    </span>
                  </div>
                  <span className={cx('tag', x.speed === 'high' ? 'ok' : 'warn')}>
                    {x.speed === 'high' ? <Usb size={12} aria-hidden /> : <TriangleAlert size={12} aria-hidden />}
                    {t(x.speed === 'high' ? 'meter.high' : 'meter.full')}
                  </span>
                </>
              )
              return multi ? (
                <button
                  key={x.serial}
                  type="button"
                  role="radio"
                  aria-checked={sel === i}
                  className={cx('li deckrow', sel === i && 'sel')}
                  onClick={() => deckSelAtom.set(i)}
                >
                  {body}
                </button>
              ) : (
                <div key={x.serial} className="li deckrow">
                  {body}
                </div>
              )
            })}
          </div>
        )}
        <div className="field">
          <span className="flabel">
            {t('deck.speed')}
            <Help label={t('deck.speed')} text={`${t('deck.speed_h')} ${t('is.full_h')}`} />
          </span>
          <Seg
            id="usb-speed"
            label={t('deck.speed')}
            value={v.usb}
            onChange={(usb) => setD({ ...v, usb })}
            options={[
              ['auto', t('chord.auto'), Gauge],
              ['full', t('meter.full'), Turtle],
            ]}
          />
          {multi && <span className="hint">{t('deck.settings_for')}</span>}
        </div>
      </Card>
      <Card id="card-dmenu" icon={SquareMenu} title={t('deck.menu')}>
        <div className="chordnow">
          <MiniDeck kind={kind} hl={chordHl(v.chord, MODELS[kind])} />
          <p>
            {chordSentence(t, v.chord, MODELS[kind], fmt.dec(v.hold))}
            {multi && deck && (
              <>
                <br />
                <span className="small muted">
                  {t('deck.chord_all', { deck: deckName(deck, sel, decks.length, t) })}
                </span>
              </>
            )}
          </p>
        </div>
        <div className="field">
          <span className="flabel" id="chord-l">
            {t('deck.chord')}
          </span>
          <div className="choices chordopts" role="radiogroup" id="chord" aria-labelledby="chord-l">
            {(['auto', 'keys', 'encoders', 'off'] as const).map((o) => {
              const dis = o === 'encoders' && !anyDials
              return (
                <button
                  key={o}
                  type="button"
                  role="radio"
                  className="choice"
                  aria-checked={v.chord === o}
                  disabled={dis}
                  onClick={() => setD({ ...v, chord: o })}
                >
                  <MiniDeck kind={kind} hl={chordHl(o, MODELS[kind])} />
                  <div>
                    <b>{t(`chord.${o}`)}</b>
                    <span>{dis ? t('deck.no_dials') : t(`chord.d_${o}`)}</span>
                  </div>
                </button>
              )
            })}
          </div>
        </div>
        {v.chord !== 'off' && (
          <Field id="hold" label={`${t('deck.hold')} (s)`} err={holdErr}>
            <input
              {...describedBy('hold', holdErr)}
              className="in mono short"
              type="number"
              min={0.5}
              max={10}
              step={0.5}
              value={v.hold}
              onChange={(e) => setD({ ...v, hold: Number(e.target.value) })}
            />
          </Field>
        )}
      </Card>
    </div>
  )
}
