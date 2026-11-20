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
// stop(t0) holds the first frame from t0 on, so two recorders of one browser share a timeline.
export async function recorder(ctx, page, dir) {
  rmSync(dir, { recursive: true, force: true })
  mkdirSync(dir, { recursive: true })
  const cdp = await ctx.newCDPSession(page)
  const frames = []
  const speeds = [{ i: 0, n: 1 }]
  cdp.on('Page.screencastFrame', async (f) => {
    frames.push({ t: f.metadata.timestamp, at: Date.now(), data: f.data })
    await cdp.send('Page.screencastFrameAck', { sessionId: f.sessionId }).catch(() => {})
  })
  await cdp.send('Page.startScreencast', { format: 'png', everyNthFrame: 1 })
  return {
    speed: (n) => speeds.push({ i: frames.length, n }),
    first: () => frames[0]?.t,
    async stop(t0 = frames[0].t) {
      await cdp.send('Page.stopScreencast')
      let list = t0 < frames[0].t ? `file '${dir}/0000.png'\nduration ${(frames[0].t - t0).toFixed(3)}\n` : ''
      frames.forEach((f, i) => {
        const name = `${dir}/${String(i).padStart(4, '0')}.png`
        writeFileSync(name, Buffer.from(f.data, 'base64'))
        // a still page sends no frames: the last one lasts until stop()
        const next = frames[i + 1]?.t ?? f.t + (Date.now() - f.at) / 1000
        list += `file '${name}'\nduration ${((next - f.t) / speeds.findLast((s) => s.i <= i).n).toFixed(3)}\n`
      })
      list += `file '${dir}/${String(frames.length - 1).padStart(4, '0')}.png'\n`
      writeFileSync(`${dir}/list.txt`, list)
      console.log(frames.length, 'frames')
    },
  }
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
