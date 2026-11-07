// Draws colour cycles on a Network Dock (the bridge or a real Elgato dock) with @elgato-stream-deck/tcp, the library
// inside Bitfocus Companion, and prints one JSON line per update with its send time, for dbtools.deckmeter.
// Usage: node drive.mjs <dock ip> key|page <rates per second, comma separated> <seconds per rate>
//   key   key 8 only; page  all 8 keys and the strip, what a page switch sends
// Colour class c (0 red, 1 green, 2 blue) is the same as in the bench Companion's page set.
import { StreamDeckTcpConnectionManager } from '@elgato-stream-deck/tcp'

const [host, kind, rateList, secs] = process.argv.slice(2)
const COLORS = [
	[176, 0, 0],
	[0, 160, 0],
	[0, 0, 192],
]
const now = () => Date.now() / 1000 // the same clock as time.time() in Python; performance.timeOrigin drifts from it
const sleep = (ms) => new Promise((r) => setTimeout(r, ms))
const out = (o) => console.log(JSON.stringify(o))

const manager = new StreamDeckTcpConnectionManager({ autoConnectToSecondaries: true })
manager.on('error', (e) => console.error('manager error', e?.message ?? e))
const deck = await new Promise((resolve) => {
	const t = setTimeout(() => resolve(null), 15000)
	manager.on('connected', (sd) => {
		if (sd.MODEL !== 'network-dock') {
			clearTimeout(t)
			resolve(sd)
		}
	})
	manager.connectTo(host)
})
if (!deck) {
	console.error(`no deck behind ${host}:5343`)
	process.exit(1)
}
const lcd = deck.CONTROLS.find((c) => c.type === 'lcd-segment')
const strips = COLORS.map(([r, g, b]) => {
	if (!lcd) return null
	const buf = Buffer.alloc(lcd.pixelSize.width * lcd.pixelSize.height * 3)
	for (let i = 0; i < buf.length; i += 3) buf.set([r, g, b], i)
	return buf
})
const keys = deck.CONTROLS.filter((c) => c.type === 'button').map((c) => c.index)

async function draw(c) {
	const [r, g, b] = COLORS[c]
	if (kind === 'key') return deck.fillKeyColor(keys[keys.length - 1], r, g, b)
	await Promise.all(keys.map((k) => deck.fillKeyColor(k, r, g, b)))
	if (lcd) await deck.fillLcd(lcd.id, strips[c], { format: 'rgb' })
}

// The bridge sometimes shows a new dock host's pictures only seconds after it connected
await sleep(3000)
for (const c of [0, 1, 2]) {
	await draw(c)
	out({ learn: c, t: now() })
	await sleep(1500)
}
for (const rate of rateList.split(',').map(Number)) {
	const start = now()
	const n = Math.round(rate * Number(secs))
	for (let i = 0; i < n; i++) {
		const due = start + i / rate
		if (due > now()) await sleep((due - now()) * 1000)
		const t = now()
		await draw(i % 3)
		out({ rate, i, c: i % 3, t, done: now() })
	}
	await sleep(1000)
}
await deck.close().catch(() => {})
manager.disconnectFromAll()
process.exit(0)
