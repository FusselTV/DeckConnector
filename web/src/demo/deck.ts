import { renderUi } from './deck-ui'
import { HEIGHT, KEYS, STRIP, WIDTH, type Face, type KeyFace, type SiteIcon, type StripZone } from './model'
import type { Paint } from './server'

const SANS = '"Geist Variable", system-ui, -apple-system, "Segoe UI", Roboto, sans-serif'
const cache = new Map<string, string>()

// lucide 1.71 paths on their 24 px grid, as the website draws them (circles as two arcs).
const SITE_ICONS: Record<Exclude<SiteIcon, 'rec'>, string[]> = {
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
  'mic-off': [
    'M12 19v3',
    'M15 9.34V5a3 3 0 0 0-5.68-1.33',
    'M16.95 16.95A7 7 0 0 1 5 12v-2',
    'M18.89 13.23A7 7 0 0 0 19 12v-2',
    'm2 2 20 20',
    'M9 9v3a3 3 0 0 0 5.12 2.12',
  ],
  title: ['M12 4v16', 'M4 7V5a1 1 0 0 1 1-1h14a1 1 0 0 1 1 1v2', 'M9 20h6'],
  next: ['m9 18 6-6-6-6'],
  back: ['m15 18-6-6 6-6'],
  light: [
    'M15 14c.2-1 .7-1.7 1.5-2.5 1-.9 1.5-2.2 1.5-3.5A6 6 0 0 0 6 8c0 1 .2 2.2 1.5 3.5.7.7 1.3 1.5 1.5 2.5',
    'M9 18h6',
    'M10 22h4',
  ],
  coffee: [
    'M10 2v2',
    'M14 2v2',
    'M16 8a1 1 0 0 1 1 1v8a4 4 0 0 1-4 4H7a4 4 0 0 1-4-4V9a1 1 0 0 1 1-1h14a4 4 0 1 1 0 8h-1',
    'M6 2v2',
  ],
  palette: [
    'M12 22a1 1 0 0 1 0-20 10 9 0 0 1 10 9 5 5 0 0 1-5 5h-2.25a1.75 1.75 0 0 0-1.4 2.8l.3.4a1.75 1.75 0 0 1-1.4 2.8z',
    'M13.5 6.5h.01',
    'M17.5 10.5h.01',
    'M6.5 12.5h.01',
    'M8.5 7.5h.01',
  ],
  power: ['M12 2v10', 'M18.4 6.6a9 9 0 1 1-12.77.04'],
  play: ['M5 5a2 2 0 0 1 3.008-1.728l11.997 6.998a2 2 0 0 1 .003 3.458l-12 7A2 2 0 0 1 5 19z'],
  skip: ['M21 4v16', 'M6.029 4.285A2 2 0 0 0 3 6v12a2 2 0 0 0 3.029 1.715l9.997-5.998a2 2 0 0 0 .003-3.432z'],
  camera: [
    'M13.997 4a2 2 0 0 1 1.76 1.05l.486.9A2 2 0 0 0 18.003 7H20a2 2 0 0 1 2 2v9a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V9a2 2 0 0 1 2-2h1.997a2 2 0 0 0 1.759-1.048l.489-.904A2 2 0 0 1 10.004 4z',
    'M9 13a3 3 0 1 0 6 0a3 3 0 1 0 -6 0',
  ],
  moon: [
    'M20.985 12.486a9 9 0 1 1-9.473-9.472c.405-.022.617.46.402.803a6 6 0 0 0 8.268 8.268c.344-.215.825-.004.803.401',
  ],
  sun: [
    'M8 12a4 4 0 1 0 8 0a4 4 0 1 0 -8 0',
    'M12 2v2',
    'M12 20v2',
    'm4.93 4.93 1.41 1.41',
    'm17.66 17.66 1.41 1.41',
    'M2 12h2',
    'M20 12h2',
    'm6.34 17.66-1.41 1.41',
    'm19.07 4.93-1.41 1.41',
  ],
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
    } else if (k.clock) {
      fit(g, k.t, 108, 38)
      g.fillText(k.t, 60, 54)
    } else if (k.t) {
      fit(g, k.t, 108, 26)
      g.fillText(k.t, 60, 54)
    }
    if (k.sub) {
      g.globalAlpha = 0.7
      fit(g, k.sub, 108, 14, 500)
      g.fillText(k.sub, 60, k.clock ? 88 : 104)
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
