import { renderUi } from './deck-ui'
import { HEIGHT, KEYS, STRIP, WIDTH, type Face, type KeyFace, type StripZone } from './model'
import type { Paint } from './server'

const SANS = '"Geist Variable", system-ui, -apple-system, "Segoe UI", Roboto, sans-serif'
const cache = new Map<string, string>()

// lucide 1.70 paths (video, presentation, pause, mic, type, clock, chevron-right) on their 24 px grid, as the website draws them.
const SITE_ICONS: Record<Exclude<NonNullable<KeyFace['site']>, 'rec'>, string[]> = {
  cam: [
    'm16 13 5.223 3.482a.5.5 0 0 0 .777-.416V7.87a.5.5 0 0 0-.752-.432L16 10.5',
    'M4 6h10a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2z',
  ],
  slides: ['M2 3h20', 'M21 3v11a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V3', 'm7 21 5-5 5 5'],
  pause: [
    'M15 3h3a1 1 0 0 1 1 1v16a1 1 0 0 1-1 1h-3a1 1 0 0 1-1-1V4a1 1 0 0 1 1-1z',
    'M6 3h3a1 1 0 0 1 1 1v16a1 1 0 0 1-1 1H6a1 1 0 0 1-1-1V4a1 1 0 0 1 1-1z',
  ],
  mic: ['M12 19v3', 'M19 10v2a7 7 0 0 1-14 0v-2', 'M12 2a3 3 0 0 1 3 3v7a3 3 0 0 1-6 0V5a3 3 0 0 1 3-3z'],
  title: ['M12 4v16', 'M4 7V5a1 1 0 0 1 1-1h14a1 1 0 0 1 1 1v2', 'M9 20h6'],
  clock: ['M2 12a10 10 0 1 0 20 0a10 10 0 1 0-20 0', 'M12 6v6l4 2'],
  next: ['m9 18 6-6-6-6'],
}

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
    case 'dot':
      g.arc(cx, cy, 6, 0, 2 * Math.PI)
      g.fill()
      return
    case 'light':
      g.arc(cx, cy, 9, 0, 2 * Math.PI)
      if (k.on) g.fill()
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
    if (k.site) {
      // The key look of the website (site/remotion/hw.tsx Face): a 24 px lucide icon over one bold word.
      g.save()
      g.translate(38, 16)
      g.scale(44 / 24, 44 / 24)
      g.strokeStyle = k.fg ?? '#fff'
      g.lineWidth = 2.4
      g.lineCap = 'round'
      g.lineJoin = 'round'
      if (k.site === 'rec') {
        g.fillStyle = k.dot ?? '#ff4b3e'
        g.beginPath()
        g.arc(12, 12, 8, 0, 2 * Math.PI)
        g.fill()
      } else for (const d of SITE_ICONS[k.site]) g.stroke(new Path2D(d))
      g.restore()
      g.fillStyle = k.fg ?? '#fff'
      g.textBaseline = 'alphabetic'
      fit(g, k.t, 104, 28, 800)
      g.fillText(k.t, 60, 98)
    } else if (k.icon === 'clock') {
      fit(g, k.t, 108, 38)
      g.fillText(k.t, 60, 54)
    } else if (k.icon) {
      icon(g, k, 60, 40)
      g.fillStyle = k.fg ?? '#e8e6e1'
      fit(g, k.t, 108, 20)
      g.fillText(k.t, 60, k.sub ? 82 : 90)
    } else if (k.t) {
      fit(g, k.t, 108, 26)
      g.fillText(k.t, 60, 54)
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

function paintStrip(zones: StripZone[]) {
  return memo(JSON.stringify(zones), () => {
    const [c, g] = canvas(800, 100)
    g.fillStyle = '#040506'
    g.fillRect(0, 0, 800, 100)
    g.textBaseline = 'middle'
    zones.forEach((z, i) => {
      const x = i * 200
      g.fillStyle = '#0d0f12'
      g.beginPath()
      g.roundRect(x + 4, 4, 192, 92, 8)
      g.fill()
      g.globalAlpha = z.off ? 0.45 : 1
      g.textAlign = 'left'
      g.fillStyle = '#a3a9b1'
      fit(g, z.t, 164, 18, 600)
      g.fillText(z.t, x + 18, 28)
      g.fillStyle = z.off ? '#ff5b4f' : '#fff'
      fit(g, z.text, 164, 34, 800)
      g.fillText(z.text, x + 18, 60)
      g.fillStyle = '#2a2e34'
      g.beginPath()
      g.roundRect(x + 18, 80, 164, 6, 3)
      g.fill()
      g.fillStyle = z.color
      g.beginPath()
      g.roundRect(x + 18, 80, Math.max(6, (164 * Math.max(0, Math.min(100, z.v))) / 100), 6, 3)
      g.fill()
      g.globalAlpha = 1
    })
    return c.toDataURL('image/png')
  })
}

/** The keys drew with a fallback font until Geist arrived. */
export const fontsReady = document.fonts.load(`700 20px ${SANS}`).then(() => cache.clear())

export const paint: Paint = (f: Face) =>
  'ui' in f
    ? [{ x: 0, y: 0, w: WIDTH, h: HEIGHT, url: memo(`ui${f.ui}`, () => renderUi(WIDTH, HEIGHT)) }]
    : [
        ...KEYS.map((r, i) => ({ ...r, url: paintKey(f.keys[i] ?? { t: '', bg: '#000' }) })),
        { ...STRIP, url: paintStrip(f.strip.zones) },
      ]
