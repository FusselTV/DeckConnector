// The deck's own screen (menu, password, reset question, login request, update) from the firmware's db_ui code,
// built to WebAssembly by scripts/deck-ui.mjs from deck-ui.c.

interface Exports {
  memory: WebAssembly.Memory
  _initialize(): void
  deck_init(): number
  deck_buf(): number
  deck_begin(nowMs: number): void
  deck_set(): number
  deck_source(kind: number, via: number, chosen: number): void
  deck_lan_cfg(field: number, a: number, b: number, c: number, d: number): void
  deck_menu(): number
  deck_menu_size(): number
  deck_menu_reset(): void
  deck_open(): void
  deck_close(): void
  deck_is_open(): number
  deck_tick(): number
  deck_input(kind: number, index: number, value: number, x: number, y: number, x2: number, fromDeck: number): number
  deck_act_value(): number
  deck_screen(update: number): number
  deck_render(): number
}

/** ui_model_t by its C field names. */
export interface UiModel {
  fields: Record<string, string | number | boolean>
  /** kind: UI_CTRL_* (0 server, 1 Companion, 2 app); via: UI_VIA_* (0 server, 1 Satellite, 2 dock). */
  sources: { kind: number; via: number; chosen: boolean; label: string; id: string }[]
  lan_cfg: number[][]
}

/** db_input_kind_t */
export const INPUT = { keyDown: 1, keyUp: 2, encDown: 3, encUp: 4, turn: 5, tap: 6, long: 7, swipe: 8 } as const

/** ui_do_t */
export const DO = [
  'nothing',
  'source',
  'answer',
  'brightness',
  'ap_start',
  'ap_stop',
  'restart',
  'reset',
  'save_lan',
  'setup_done',
  'offer',
  'hide_password',
  'cancel_reset',
  'login',
] as const

let x: Exports
const wasm = new URL('./deck-ui.wasm', import.meta.url)
// Node (the unit tests, the contract test) reads the file; the page gets it inlined by the build.
const fs = 'node:fs/promises'
const bytes: Promise<BufferSource> =
  wasm.protocol === 'file:'
    ? import(/* @vite-ignore */ fs).then((m: { readFile(u: URL): Promise<Uint8Array<ArrayBuffer>> }) =>
        m.readFile(wasm),
      )
    : fetch(wasm).then((r) => r.arrayBuffer())
export const deckUiReady = bytes.then(async (b) => {
  x = (await WebAssembly.instantiate(b)).instance.exports as unknown as Exports
  x._initialize()
  if (!x.deck_init()) throw new Error('deck-ui: no Stream Deck + driver')
})

const enc = new TextEncoder()
function put(...parts: string[]) {
  const bytes = enc.encode(parts.map((p) => p + '\0').join(''))
  new Uint8Array(x.memory.buffer, x.deck_buf(), 256).set(bytes.subarray(0, 255))
}

/** Puts `menu` (empty: a fresh one) and `m` into the module for the calls in `run`, and returns the menu after. */
export function withUi<T>(menu: number[], m: UiModel, nowMs: number, run: (u: Exports) => T): [T, number[]] {
  const at = x.deck_menu()
  const size = x.deck_menu_size()
  if (menu.length === size) new Uint8Array(x.memory.buffer, at, size).set(menu)
  else x.deck_menu_reset()
  x.deck_begin(nowMs)
  for (const [k, v] of Object.entries(m.fields)) {
    put(k, typeof v === 'boolean' ? (v ? '1' : '0') : String(v))
    if (!x.deck_set()) throw new Error(`deck-ui: ui_model_t has no ${k}`)
  }
  for (const s of m.sources) {
    put(s.label, s.id)
    x.deck_source(s.kind, s.via, s.chosen ? 1 : 0)
  }
  m.lan_cfg.forEach((o, i) => x.deck_lan_cfg(i, o[0] ?? 0, o[1] ?? 0, o[2] ?? 0, o[3] ?? 0))
  const r = run(x)
  return [r, [...new Uint8Array(x.memory.buffer, at, size)]]
}

/** The screen deck_screen built last, as a PNG data URL. */
export function renderUi(w: number, h: number) {
  const bgr = new Uint8Array(x.memory.buffer, x.deck_render(), w * h * 3)
  const c = document.createElement('canvas')
  c.width = w
  c.height = h
  const g = c.getContext('2d') as CanvasRenderingContext2D
  const img = g.createImageData(w, h)
  for (let i = 0, j = 0; i < bgr.length; i += 3, j += 4) {
    img.data[j] = bgr[i + 2] ?? 0
    img.data[j + 1] = bgr[i + 1] ?? 0
    img.data[j + 2] = bgr[i] ?? 0
    img.data[j + 3] = 255
  }
  g.putImageData(img, 0, 0)
  return c.toDataURL('image/png')
}
