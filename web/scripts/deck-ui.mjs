// Builds src/demo/deck-ui.wasm: the firmware's db_ui code (pages, input, drawing, font) for the demo's deck.
// Uses zig's clang from PyPI through uv, so nothing but uv has to be installed. Skips when nothing changed.
import { execFileSync } from 'node:child_process'
import { readdirSync, statSync } from 'node:fs'
import { join } from 'node:path'

const root = new URL('../../', import.meta.url).pathname
const ui = join(root, 'firmware/components/db_ui')
const core = join(root, 'firmware/components/db_core')
const out = join(root, 'web/src/demo/deck-ui.wasm')
const sources = [
  join(root, 'web/src/demo/deck-ui.c'),
  ...['screens.c', 'render.c', 'canvas.c', 'font_data.c'].map((f) => join(ui, f)),
  ...['db_driver.c', 'db_device.c', 'db_screen.c'].map((f) => join(core, f)),
  ...readdirSync(join(core, 'drivers'))
    .filter((f) => f.endsWith('.c'))
    .map((f) => join(core, 'drivers', f)),
]
const headers = [ui, join(core, 'include'), join(core, 'drivers')].flatMap((d) =>
  readdirSync(d)
    .filter((f) => f.endsWith('.h'))
    .map((f) => join(d, f)),
)
const mtime = (f) => statSync(f, { throwIfNoEntry: false })?.mtimeMs ?? 0
if (mtime(out) > Math.max(...[...sources, ...headers, new URL(import.meta.url).pathname].map(mtime))) process.exit(0)

execFileSync(
  'uvx',
  [
    ...['--from', 'ziglang==0.14.1', 'python', '-m', 'ziglang', 'cc'],
    ...['-target', 'wasm32-wasi', '-mexec-model=reactor', '-std=gnu2x', '-O2', '-s'],
    ...[`-I${ui}`, `-I${core}/include`, `-I${core}/drivers`, ...sources, '-o', out],
  ],
  { stdio: 'inherit' },
)
