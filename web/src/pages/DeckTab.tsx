import { CircleCheck, Gauge, LayoutGrid, SquareMenu, TriangleAlert, Turtle, Unplug, Usb } from 'lucide-react'
import { useState } from 'react'
import { decksOf, deckName } from '../app/derive'
import { deckSelAtom } from '../app/state'
import { useStore } from '../app/store'
import { useUi } from '../app/ui'
import { Card } from '../components/Card'
import { describedBy, Field, Help, Seg, SIcon } from '../components/controls'
import { useDraft } from '../components/SaveBar'
import { chordHl, chordSentence, type Hl } from '../deck/chord'
import { MODELS, type Model, type ModelKey } from '../deck/models'
import { cx, roam } from '../lib/util'

const OFF_PRESETS = ['0', '60', '300', '900', '1800', '3600']
const duration = (s: number) => (s % 60 ? `${s} s` : `${s / 60} min`)

function MiniDeck({ kind, hl }: { kind: ModelKey; hl: Hl }) {
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
  const { t, fmt, go } = useUi()
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
    max: String(cfg?.max_decks) as '1' | '2' | '3' | '4',
    off: String(cfg?.screen_off_s ?? 0),
  }
  // Only the fields the person touched, over the live values: a save never sends back a stale copy.
  const [d, setD] = useState<Partial<typeof base>>({})
  const v = { ...base, ...d }
  const changed = (k: keyof typeof base) => k in d && d[k] !== base[k]
  const dirty = (Object.keys(base) as (keyof typeof base)[]).some(changed)
  const restart = changed('usb')
  const holdErr = v.chord !== 'off' && (v.hold < 0.5 || v.hold > 10) ? t('deck.hold_h') : null
  const set = (x: Partial<typeof base>) => setD((o) => ({ ...o, ...x }))
  useDraft('deck', {
    dirty,
    restart,
    reset: () => setD({}),
    saved: () => setD({}),
    check: () => {
      if (!holdErr) return true
      go('deck')
      setTimeout(() => document.getElementById('hold')?.focus(), 80)
      return false
    },
    confirm: { title: t('cf.usb'), body: t('cf.usb_b') },
    patch: {
      ...(changed('chord') ? { menu_chord: v.chord } : {}),
      ...(changed('hold') ? { menu_hold_ms: Math.round(v.hold * 1000) } : {}),
      ...(changed('usb') ? { usb_speed: v.usb } : {}),
      ...(changed('max') ? { max_decks: Number(v.max) } : {}),
      ...(changed('off') ? { screen_off_s: Number(v.off) } : {}),
    },
  })
  const anyDials = decks.some((x) => MODELS[x.kind].dials) || !decks.length
  const offs = OFF_PRESETS.includes(base.off) ? OFF_PRESETS : [...OFF_PRESETS, base.off]
  return (
    <div className="cards">
      <Card
        id="card-decks"
        icon={LayoutGrid}
        title={multi ? t('deck.list') : t('tab.deck')}
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
        <fieldset className="gate field" disabled={!cfg}>
          <span className="flabel">
            {t('deck.speed')}
            <Help label={t('deck.speed')} text={`${t('deck.speed_h')} ${t('is.full_h')}`} />
          </span>
          <Seg
            id="usb-speed"
            label={t('deck.speed')}
            value={v.usb}
            onChange={(usb) => set({ usb })}
            options={[
              ['auto', t('chord.auto'), Gauge],
              ['full', t('meter.full'), Turtle],
            ]}
          />
          {multi && <span className="hint">{t('deck.settings_for')}</span>}
        </fieldset>
        {(multi || !!st.decks_over_limit) && (
          <fieldset className="gate field" disabled={!cfg}>
            <span className="flabel">
              {t('deck.max')}
              <Help label={t('deck.max')} text={t('deck.max_h')} />
            </span>
            <Seg
              id="max-decks"
              label={t('deck.max')}
              value={v.max}
              onChange={(max) => set({ max })}
              options={(['1', '2', '3', '4'] as const).map((n) => [n, n] as const)}
            />
          </fieldset>
        )}
        <fieldset className="gate field" disabled={!cfg}>
          <span className="flabel">
            {t('deck.off')}
            <Help label={t('deck.off')} text={t('deck.off_h')} />
          </span>
          <Seg
            id="screen-off"
            label={t('deck.off')}
            value={v.off}
            onChange={(off) => set({ off })}
            options={offs.map((s) => [s, s === '0' ? t('deck.never') : duration(Number(s))] as const)}
          />
        </fieldset>
      </Card>
      <fieldset className="gate" disabled={!cfg}>
        <Card id="card-dmenu" icon={SquareMenu} title={t('deck.menu')}>
          <div className="chordnow">
            <MiniDeck kind={kind} hl={chordHl(v.chord, MODELS[kind], deck)} />
            <p>
              {chordSentence(
                t,
                v.chord,
                MODELS[kind],
                fmt.dec(holdErr ? (cfg?.menu_hold_ms ?? 2000) / 1000 : v.hold),
                deck,
              )}
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
            <div
              className="choices chordopts"
              role="radiogroup"
              id="chord"
              aria-labelledby="chord-l"
              onKeyDown={roam}
              tabIndex={-1}
            >
              {(['auto', 'keys', 'encoders', 'off'] as const).map((o) => {
                const dis = o === 'encoders' && !anyDials
                return (
                  <button
                    key={o}
                    type="button"
                    role="radio"
                    className="choice"
                    aria-checked={v.chord === o}
                    tabIndex={v.chord === o ? 0 : -1}
                    disabled={dis}
                    onClick={() => set({ chord: o })}
                  >
                    <MiniDeck kind={kind} hl={chordHl(o, MODELS[kind], deck)} />
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
                onChange={(e) => set({ hold: Number(e.target.value) })}
              />
            </Field>
          )}
        </Card>
      </fieldset>
    </div>
  )
}
