import type { Face, KeyFace, StripFace } from './model'
import type { Paint } from './server'

const SANS = 'system-ui, -apple-system, "Segoe UI", Roboto, sans-serif'
const cache = new Map<string, string>()

function canvas(w: number, h: number) {
  const c = document.createElement('canvas')
  c.width = w
  c.height = h
  return [c, c.getContext('2d') as CanvasRenderingContext2D] as const
}

/** Equal faces give the same URL, so the live deck keeps a key whose picture did not change. */
function memo(key: string, draw: () => string) {
  let url = cache.get(key)
  if (!url) {
    url = draw()
    if (cache.size > 300) cache.clear()
    cache.set(key, url)
  }
  return url
}

function fit(g: CanvasRenderingContext2D, text: string, max: number, size: number, weight = 700) {
  let s = size
  do g.font = `${weight} ${s}px ${SANS}`
  while (g.measureText(text).width > max && --s > 10)
}

function icon(g: CanvasRenderingContext2D, k: KeyFace, cx: number, cy: number) {
  const fg = k.fg ?? '#e8e6e1'
  g.strokeStyle = fg
  g.fillStyle = fg
  g.lineWidth = 4
  g.lineCap = 'round'
  g.lineJoin = 'round'
  g.beginPath()
  switch (k.icon) {
    case 'mic':
      g.roundRect(cx - 7, cy - 18, 14, 24, 7)
      g.stroke()
      g.beginPath()
      g.arc(cx, cy - 2, 13, 0.15 * Math.PI, 0.85 * Math.PI)
      g.moveTo(cx, cy + 11)
      g.lineTo(cx, cy + 17)
      g.stroke()
      if (!k.on) {
        g.beginPath()
        g.moveTo(cx - 16, cy - 18)
        g.lineTo(cx + 16, cy + 16)
        g.stroke()
      }
      return
    case 'live':
      g.arc(cx, cy, 6, 0, 2 * Math.PI)
      g.fill()
      for (const r of [13, 20]) {
        g.beginPath()
        g.arc(cx, cy, r, -0.3 * Math.PI, 0.3 * Math.PI)
        g.stroke()
        g.beginPath()
        g.arc(cx, cy, r, 0.7 * Math.PI, 1.3 * Math.PI)
        g.stroke()
      }
      return
    case 'rec':
    case 'dot':
      g.arc(cx, cy, k.icon === 'rec' ? 13 : 6, 0, 2 * Math.PI)
      if (k.on || k.icon === 'dot') g.fill()
      else g.stroke()
      return
    case 'cam':
      g.roundRect(cx - 20, cy - 12, 28, 24, 4)
      g.moveTo(cx + 10, cy - 4)
      g.lineTo(cx + 20, cy - 10)
      g.lineTo(cx + 20, cy + 10)
      g.lineTo(cx + 10, cy + 4)
      g.stroke()
      return
    case 'light':
    case 'sun':
      g.arc(cx, cy, 9, 0, 2 * Math.PI)
      if (k.on || k.icon === 'sun') g.fill()
      else g.stroke()
      for (let i = 0; i < 8; i++) {
        const a = (i * Math.PI) / 4
        g.moveTo(cx + Math.cos(a) * 14, cy + Math.sin(a) * 14)
        g.lineTo(cx + Math.cos(a) * 19, cy + Math.sin(a) * 19)
      }
      g.stroke()
      return
    case 'next':
    case 'back': {
      const d = k.icon === 'next' ? 1 : -1
      g.moveTo(cx - 7 * d, cy - 14)
      g.lineTo(cx + 7 * d, cy)
      g.lineTo(cx - 7 * d, cy + 14)
      g.stroke()
      return
    }
    case 'play':
      g.moveTo(cx - 9, cy - 14)
      g.lineTo(cx + 13, cy)
      g.lineTo(cx - 9, cy + 14)
      g.fill()
      return
    case 'pause':
      g.fillRect(cx - 11, cy - 14, 8, 28)
      g.fillRect(cx + 3, cy - 14, 8, 28)
      return
    case 'skip':
      g.moveTo(cx - 13, cy - 13)
      g.lineTo(cx + 5, cy)
      g.lineTo(cx - 13, cy + 13)
      g.fill()
      g.fillRect(cx + 7, cy - 13, 5, 26)
      return
    case 'host':
      g.roundRect(cx - 18, cy - 14, 36, 22, 3)
      g.moveTo(cx - 8, cy + 15)
      g.lineTo(cx + 8, cy + 15)
      g.stroke()
      return
    case 'x':
      g.moveTo(cx - 12, cy - 12)
      g.lineTo(cx + 12, cy + 12)
      g.moveTo(cx + 12, cy - 12)
      g.lineTo(cx - 12, cy + 12)
      g.stroke()
      return
    case 'info':
      g.arc(cx, cy, 16, 0, 2 * Math.PI)
      g.stroke()
      g.fillRect(cx - 2, cy - 3, 4, 12)
      g.fillRect(cx - 2, cy - 10, 4, 4)
      return
    case 'brb':
      g.roundRect(cx - 14, cy - 10, 22, 22, 4)
      g.moveTo(cx + 8, cy - 4)
      g.arc(cx + 10, cy + 1, 5, -0.5 * Math.PI, 0.5 * Math.PI)
      g.stroke()
      return
  }
}

function paintKey(k: KeyFace) {
  return memo(JSON.stringify(k), () => {
    const [c, g] = canvas(120, 120)
    g.fillStyle = k.bg
    g.fillRect(0, 0, 120, 120)
    g.textAlign = 'center'
    g.textBaseline = 'middle'
    g.fillStyle = k.fg ?? '#e8e6e1'
    if (k.icon === 'clock') {
      fit(g, k.t, 108, 38)
      g.fillText(k.t, 60, 54)
    } else if (k.icon) {
      icon(g, k, 60, 40)
      g.fillStyle = k.fg ?? '#e8e6e1'
      fit(g, k.t, 108, 20)
      g.fillText(k.t, 60, k.sub ? 82 : 90)
    } else if (k.t) {
      fit(g, k.t, 108, 26)
      g.fillText(k.t, 60, k.sub ? 54 : 60)
    }
    if (k.sub) {
      g.globalAlpha = 0.7
      fit(g, k.sub, 108, 14, 500)
      g.fillText(k.sub, 60, k.icon === 'clock' ? 88 : 104)
      g.globalAlpha = 1
    }
    return c.toDataURL('image/png')
  })
}

function paintStrip(s: StripFace) {
  return memo(JSON.stringify(s), () => {
    const [c, g] = canvas(800, 100)
    g.fillStyle = '#040506'
    g.fillRect(0, 0, 800, 100)
    g.textBaseline = 'middle'
    if ('zones' in s) {
      s.zones.forEach((z, i) => {
        const x = i * 200
        g.fillStyle = '#0d1014'
        g.fillRect(x + 6, 6, 188, 88)
        g.globalAlpha = z.off ? 0.45 : 1
        g.fillStyle = '#9aa1a9'
        g.font = `600 15px ${SANS}`
        g.textAlign = 'left'
        g.fillText(z.t, x + 18, 26)
        g.fillStyle = z.off ? '#ff5b4f' : '#e8e6e1'
        g.font = `700 26px ${SANS}`
        g.textAlign = 'right'
        g.fillText(z.text, x + 182, 30)
        g.fillStyle = '#22272d'
        g.beginPath()
        g.roundRect(x + 18, 64, 164, 12, 6)
        g.fill()
        g.fillStyle = z.color
        g.beginPath()
        g.roundRect(x + 18, 64, Math.max(12, (164 * Math.max(0, Math.min(100, z.v))) / 100), 12, 6)
        g.fill()
        g.globalAlpha = 1
      })
    } else {
      g.textAlign = 'center'
      g.fillStyle = '#e8e6e1'
      fit(g, s.text, 760, 26)
      g.fillText(s.text, 400, s.sub || s.bar !== undefined ? 34 : 50)
      if (s.sub) {
        g.fillStyle = '#9aa1a9'
        g.font = `500 16px ${SANS}`
        g.fillText(s.sub, 400, 66)
      }
      if (s.bar !== undefined) {
        g.fillStyle = '#22272d'
        g.fillRect(200, 84, 400, 8)
        g.fillStyle = '#ffb020'
        g.fillRect(200, 84, 4 * s.bar, 8)
      }
    }
    return c.toDataURL('image/png')
  })
}

export const paint: Paint = (f: Face) => ({ keys: f.keys.map(paintKey), strip: paintStrip(f.strip) })
