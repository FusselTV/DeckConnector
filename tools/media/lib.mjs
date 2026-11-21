import { createRequire } from 'node:module'
import { mkdirSync, rmSync, writeFileSync } from 'node:fs'

const require = createRequire(new URL('../../web/package.json', import.meta.url))
export const { chromium } = require('playwright')

export const CURSOR = `(() => { const c = document.createElement('div');
  c.innerHTML = '<svg width="22" height="22" viewBox="0 0 24 24"><path d="M4 2l15 11-7 1-3 7z" fill="#fff" stroke="#111" stroke-width="1.6" stroke-linejoin="round"/></svg>';
  Object.assign(c.style, { position: 'fixed', left: '-40px', top: '-40px', zIndex: 2147483647, pointerEvents: 'none', transform: 'translate(-3px,-2px)', transition: 'scale .12s' });
  const add = () => document.body ? document.body.appendChild(c) : requestAnimationFrame(add); add();
  addEventListener('mousemove', (e) => { c.style.left = e.clientX + 'px'; c.style.top = e.clientY + 'px' }, true);
  addEventListener('mousedown', () => (c.style.scale = '0.8'), true); addEventListener('mouseup', () => (c.style.scale = '1'), true) })()`

// CDP screencast. speed(n) plays everything after it n times faster in the GIF; stop() writes an ffmpeg concat list.
// stop(t0, t1) cuts to that span of Date.now() seconds, so two recorders of one browser share a timeline.
// With a clip it takes screenshots of that part in a loop instead: the screencast has no device pixels
// unless the browser runs with --force-device-scale-factor.
// format 'jpeg' keeps up with a 2x page, where PNG frames come 100 ms or more apart.
export async function recorder(ctx, page, dir, clip, format = 'png') {
  rmSync(dir, { recursive: true, force: true })
  mkdirSync(dir, { recursive: true })
  const frames = []
  const ext = clip ? 'png' : format
  const speeds = [{ t: 0, n: 1 }]
  let running = true
  let cdp, loop
  if (clip)
    loop = (async () => {
      while (running) {
        const t = Date.now() / 1000 // the page is captured when the call starts, encoding takes the rest
        const data = await page.screenshot({ clip })
        if (!frames.at(-1)?.data.equals(data)) frames.push({ t, data })
        await page.waitForTimeout(40)
      }
    })()
  else {
    cdp = await ctx.newCDPSession(page)
    cdp.on('Page.screencastFrame', async (f) => {
      frames.push({ t: f.metadata.timestamp, data: Buffer.from(f.data, 'base64') })
      await cdp.send('Page.screencastFrameAck', { sessionId: f.sessionId }).catch(() => {})
    })
    await cdp.send('Page.startScreencast', { format, quality: 92, everyNthFrame: 1 })
  }
  return {
    speed: (n) => speeds.push({ t: Date.now() / 1000, n }),
    async stop(t0 = frames[0].t, t1 = Date.now() / 1000) {
      running = false
      await (loop ?? cdp.send('Page.stopScreencast'))
      while (frames.length > 1 && frames.at(-1).t > t1) frames.pop()
      // GIF seconds from t0 to t; a still page sends no frames, so the last one lasts until t1
      const out = (t) => speeds.reduce((o, s, i) => o + Math.max(0, Math.min(t, speeds[i + 1]?.t ?? t1) - Math.max(t0, s.t)) / s.n, 0)
      let list = ''
      frames.forEach((f, i) => {
        const name = `${dir}/${String(i).padStart(4, '0')}.${ext}`
        writeFileSync(name, f.data)
        list += `file '${name}'\nduration ${(out(frames[i + 1]?.t ?? t1) - out(i ? f.t : t0)).toFixed(6)}\n`
      })
      list += `file '${dir}/${String(frames.length - 1).padStart(4, '0')}.${ext}'\n`
      writeFileSync(`${dir}/list.txt`, list)
      console.log(frames.length, 'frames')
      return out
    },
  }
}

// in the page: the live deck as the firmware's screen.jpg, a data URL; null without a live deck
export async function deckJpeg([w, h]) {
  const c = Object.assign(document.createElement('canvas'), { width: w, height: h })
  const g = c.getContext('2d')
  g.fillRect(0, 0, w, h)
  const imgs = document.querySelectorAll('img[src^="data:"][style*="left"]')
  if (!imgs.length) return null
  for (const img of imgs) {
    await img.decode().catch(() => {})
    const s = img.style
    const [x, y, iw, ih] = [s.left, s.top, s.width, s.height].map((v) => parseFloat(v) / 100)
    g.drawImage(img, x * w, y * h, iw * w, ih * h)
  }
  return c.toDataURL('image/jpeg', 0.92)
}

const ease = (t) => (t < 0.5 ? 2 * t * t : 1 - (-2 * t + 2) ** 2 / 2)

export function mouse(page) {
  let x = 0
  let y = 0
  const move = async (tx, ty, ms = 340) => {
    const n = Math.max(2, Math.round(ms / 16))
    const [sx, sy] = [x, y]
    for (let i = 1; i <= n; i++) {
      const k = ease(i / n)
      await page.mouse.move(sx + (tx - sx) * k, sy + (ty - sy) * k)
      await page.waitForTimeout(16)
    }
    ;[x, y] = [tx, ty]
  }
  const click = async ([tx, ty], wait = 700) => {
    await move(tx, ty)
    await page.mouse.down()
    await page.waitForTimeout(90)
    await page.mouse.up()
    await page.waitForTimeout(wait)
  }
  const drag = async ([fx, fy], [tx, ty], wait = 500) => {
    await move(fx, fy)
    await page.mouse.down()
    await move(tx, ty, 600)
    await page.waitForTimeout(80)
    await page.mouse.up()
    await page.waitForTimeout(wait)
  }
  const park = async ([tx, ty]) => {
    ;[x, y] = [tx, ty]
    await page.mouse.move(tx, ty)
  }
  return { move, click, drag, park }
}

export async function center(locator) {
  const b = await locator.first().boundingBox()
  return [b.x + b.width / 2, b.y + b.height / 2]
}
