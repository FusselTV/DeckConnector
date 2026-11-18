// Drives the bridge in dock mode with @elgato-stream-deck/tcp, the library inside Bitfocus Companion.
// Usage: node check.mjs <bridge ip>[:dock port, which skips mDNS] [seconds to listen for input]
import { StreamDeckTcpConnectionManager, StreamDeckTcpDiscoveryService } from '@elgato-stream-deck/tcp'

const [host, port] = process.argv[2].split(':')
const listen = Number(process.argv[3] ?? 0)
const fail = (msg) => {
	console.error('FAIL', msg)
	process.exit(1)
}

const discovery = new StreamDeckTcpDiscoveryService()
const found = port ? { name: 'by address', modelName: '-', serialNumber: '-' } : await new Promise((resolve) => {
	const t = setTimeout(() => resolve(null), 8000)
	discovery.on('up', (svc) => {
		if (svc.address === host) {
			clearTimeout(t)
			resolve(svc)
		}
	})
	discovery.query()
})
discovery.destroy()
if (!found) fail(`mDNS: ${host} not found`)
console.log(`mDNS: ${found.name} model=${found.modelName} serial=${found.serialNumber}`)

const manager = new StreamDeckTcpConnectionManager({ autoConnectToSecondaries: true })
manager.on('error', (e) => console.log('manager error', e))
const deck = await new Promise((resolve) => {
	const t = setTimeout(() => resolve(null), 10000)
	manager.on('connected', (sd) => {
		console.log(`connected: ${sd.PRODUCT_NAME} (${sd.MODEL})`)
		if (sd.MODEL !== 'network-dock') {
			clearTimeout(t)
			resolve(sd)
		}
	})
	manager.connectTo(host, port ? Number(port) : undefined)
})
if (!deck) fail('device port did not open')

console.log('model', deck.MODEL, 'keys', deck.CONTROLS.filter((c) => c.type === 'button').length)
console.log('serial', await deck.getSerialNumber(), 'firmware', await deck.getFirmwareVersion())
await deck.setBrightness(70)

const colors = [
	[200, 30, 30],
	[30, 160, 60],
	[40, 80, 220],
	[220, 180, 20],
]
let t0 = performance.now()
for (let k = 0; k < deck.CONTROLS.filter((c) => c.type === 'button').length; k++) {
	await deck.fillKeyColor(k, ...colors[k % colors.length])
}
console.log(`8 key colours in ${(performance.now() - t0).toFixed(1)} ms`)

const lcd = deck.CONTROLS.find((c) => c.type === 'lcd-segment')
if (lcd) {
	const { width, height } = lcd.pixelSize
	const buf = Buffer.alloc(width * height * 3)
	for (let x = 0; x < width; x++)
		for (let y = 0; y < height; y++) buf.set([(x * 255) / width, 60, 255 - (x * 255) / width], (y * width + x) * 3)
	t0 = performance.now()
	await deck.fillLcd(lcd.id, buf, { format: 'rgb' })
	console.log(`strip ${width}x${height} in ${(performance.now() - t0).toFixed(1)} ms`)
}

if (listen > 0) {
	const seen = {}
	const note = (kind) => (seen[kind] = (seen[kind] ?? 0) + 1)
	deck.on('down', (c) => note(`${c.type}-down`))
	deck.on('up', (c) => note(`${c.type}-up`))
	deck.on('rotate', () => note('rotate'))
	deck.on('lcdShortPress', () => note('tap'))
	deck.on('lcdLongPress', () => note('long'))
	deck.on('lcdSwipe', () => note('swipe'))
	console.log(`listening ${listen} s for input`)
	await new Promise((r) => setTimeout(r, listen * 1000))
	console.log('input', JSON.stringify(seen))
}
await deck.close().catch(() => {})
manager.disconnectFromAll()
console.log('OK')
process.exit(0)
