// Compares the deck screens host_test/test_ui.c saves (build/ui/*.ppm) between main and a pull request.
// Writes before | after | diff as PNG for each changed screen and prints the Markdown for the PR comment,
// with {{preview}} where the URL of the images goes.
// node screen-diff.mjs <main ui dir> <pr ui dir> <png out dir>
import { existsSync, mkdirSync, readdirSync, readFileSync, writeFileSync } from 'node:fs'
import { join } from 'node:path'
import { crc32, deflateSync } from 'node:zlib'

const [before, after, out] = process.argv.slice(2)
const SHOWN = 12
const GAP = 8

const ppm = (file) => {
  const b = readFileSync(file)
  const [, w, h] = b.toString('latin1', 0, 32).match(/^P6\s+(\d+)\s+(\d+)\s+255\s/)
  return { w: +w, h: +h, px: b.subarray(b.length - w * h * 3) }
}

const png = (w, h, rgb) => {
  const raw = Buffer.alloc((w * 3 + 1) * h)
  for (let y = 0; y < h; y++) rgb.copy(raw, y * (w * 3 + 1) + 1, y * w * 3, (y + 1) * w * 3)
  const chunk = (type, data) => {
    const body = Buffer.concat([Buffer.from(type), data])
    const n = Buffer.alloc(4)
    n.writeUInt32BE(data.length)
    const c = Buffer.alloc(4)
    c.writeUInt32BE(crc32(body))
    return Buffer.concat([n, body, c])
  }
  const head = Buffer.alloc(13)
  head.writeUInt32BE(w, 0)
  head.writeUInt32BE(h, 4)
  head[8] = 8 // bits per channel
  head[9] = 2 // RGB
  return Buffer.concat([
    Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]),
    chunk('IHDR', head),
    chunk('IDAT', deflateSync(raw)),
    chunk('IEND', Buffer.alloc(0)),
  ])
}

// before | after | after dimmed with the changed pixels in magenta; a new screen is just after.
const sheet = (a, b) => {
  const panels = a ? 3 : 1
  const w = b.w * panels + GAP * (panels - 1)
  const rgb = Buffer.alloc(w * b.h * 3, 0x40)
  const put = (x0, img, px) => {
    for (let y = 0; y < img.h; y++)
      for (let x = 0; x < img.w; x++) {
        const i = (y * img.w + x) * 3
        px(i).forEach((v, c) => (rgb[(y * w + x0 + x) * 3 + c] = v))
      }
  }
  const at = (img) => (i) => [img.px[i], img.px[i + 1], img.px[i + 2]]
  if (!a) return { w, rgb: (put(0, b, at(b)), rgb) }
  put(0, a, at(a))
  put(b.w + GAP, b, at(b))
  put(2 * (b.w + GAP), b, (i) =>
    a.px.compare(b.px, i, i + 3, i, i + 3) ? [255, 0, 255] : [b.px[i] >> 2, b.px[i + 1] >> 2, b.px[i + 2] >> 2],
  )
  return { w, rgb }
}

const names = (dir) => (existsSync(dir) ? readdirSync(dir).filter((f) => f.endsWith('.ppm')) : []).sort()
const now = names(after)
if (!now.length) throw new Error(`no screens in ${after}`)
const old = new Set(names(before))
// The Stream Deck + first: the demo and the docs show it.
const order = (f) => (f.startsWith('plus_') ? 0 : 1)
const changed = now
  .filter((f) => {
    if (!old.has(f)) return true
    const a = ppm(join(before, f))
    const b = ppm(join(after, f))
    return a.w !== b.w || a.h !== b.h || !a.px.equals(b.px)
  })
  .sort((x, y) => order(x) - order(y) || x.localeCompare(y))
const gone = [...old].filter((f) => !now.includes(f))

if (!old.size) {
  console.log('### Deck screens\n\nNo screens of main to compare with.')
  process.exit(0)
}
console.log(`### Deck screens: ${changed.length} of ${now.length} changed\n`)
if (!changed.length && !gone.length) process.exit(0)
mkdirSync(out, { recursive: true })
for (const f of changed.slice(0, SHOWN)) {
  const b = ppm(join(after, f))
  const a = old.has(f) ? ppm(join(before, f)) : null
  const s = sheet(a, b)
  const file = f.replace('.ppm', '.png')
  writeFileSync(join(out, file), png(s.w, b.h, s.rgb))
  console.log(`**${f.replace('.ppm', '')}**${a ? '' : ' (new)'}\n\n![${file}]({{preview}}/${file})\n`)
}
if (changed.length) console.log('Before, after, and the changed pixels in magenta.\n')
const rest = changed.slice(SHOWN).map((f) => f.replace('.ppm', ''))
if (rest.length) console.log(`<details><summary>${rest.length} more</summary>\n\n${rest.join(', ')}\n</details>\n`)
if (gone.length) console.log(`Removed: ${gone.map((f) => f.replace('.ppm', '')).join(', ')}\n`)
