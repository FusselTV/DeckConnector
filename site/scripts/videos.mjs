// Renders every landing stage to public/videos/{id}-{lang}-{theme}[-port].{webm,mp4}. Run by hand: npm run videos (needs ffmpeg with libsvtav1 and libx264).
import { execFileSync } from 'node:child_process'
import { mkdirSync, statSync } from 'node:fs'
import { bundle } from '@remotion/bundler'
import { getCompositions, renderMedia, selectComposition } from '@remotion/renderer'

const path = (p) => new URL(p, import.meta.url).pathname
const out = path('../public/videos/')
const work = path('../node_modules/.cache/videos/')
mkdirSync(out, { recursive: true })
mkdirSync(work, { recursive: true })

const serveUrl = await bundle({ entryPoint: path('../remotion/root.tsx'), publicDir: path('../public/fonts/') })
const only = process.argv[2]
for (const { id: comp } of await getCompositions(serveUrl)) {
  const [id, port = ''] = comp.split(/(?=-port$)/)
  if (only && id !== only) continue
  for (const lang of ['en', 'de'])
    for (const theme of ['dark', 'light']) {
      const name = `${id}-${lang}-${theme}${port}`
      const src = `${work}${name}.mp4`
      const inputProps = { lang, light: theme === 'light' }
      const composition = await selectComposition({ serveUrl, id: comp, inputProps })
      await renderMedia({ serveUrl, composition, inputProps, codec: 'h264', crf: 1, muted: true, outputLocation: src })
      const ff = (...a) => execFileSync('ffmpeg', ['-v', 'error', '-y', '-i', src, '-an', ...a], { stdio: 'inherit' })
      ff('-c:v', 'libsvtav1', '-preset', '4', '-crf', '50', '-g', '300', '-pix_fmt', 'yuv420p', '-cues_to_front', '1', `${out}${name}.webm`)
      ff('-c:v', 'libx264', '-preset', 'veryslow', '-tune', 'animation', '-crf', '30', '-pix_fmt', 'yuv420p', '-movflags', '+faststart', `${out}${name}.mp4`)
      console.log(name, ...['webm', 'mp4'].map((e) => `${e} ${Math.round(statSync(`${out}${name}.${e}`).size / 1024)} KB`))
    }
}
