import { afterEach, describe, expect, it, vi } from 'vitest'
import { api, ApiError, onAuthLost, pressReal, request } from './client'

const reply = (status: number, body: unknown, type = 'application/json') =>
  new Response(typeof body === 'string' ? body : JSON.stringify(body), { status, headers: { 'content-type': type } })

function mockFetch(...replies: (Response | Error)[]) {
  const f = vi.fn<typeof fetch>()
  for (const r of replies) f.mockImplementationOnce(() => (r instanceof Error ? Promise.reject(r) : Promise.resolve(r)))
  vi.stubGlobal('fetch', f)
  return f
}

afterEach(() => vi.unstubAllGlobals())

describe('request', () => {
  it('sends the CSRF header and JSON body, and parses JSON', async () => {
    const f = mockFetch(reply(200, { ok: true, restart: true }))
    expect(await api.putConfig({ brightness: 40 })).toEqual({ ok: true, restart: true })
    const [url, init] = f.mock.calls[0] ?? []
    expect(url).toBe('/api/config')
    expect(init?.method).toBe('PUT')
    expect((init?.headers as Record<string, string> | undefined)?.['X-DeckConnector']).toBe('1')
    expect(init?.body).toBe('{"brightness":40}')
  })

  it('returns text for a text reply', async () => {
    mockFetch(reply(200, 'I (1) boot\n', 'text/plain'))
    expect(await api.log()).toBe('I (1) boot\n')
  })

  it('turns an error reply into an ApiError with status and message', async () => {
    mockFetch(reply(400, { ok: false, error: 'admin_pass: at least 6 characters', code: 'short' }))
    const e = await request('/api/config', { method: 'PUT', body: {} }).catch((x: unknown) => x)
    expect(e).toBeInstanceOf(ApiError)
    expect(e).toMatchObject({ status: 400, message: 'admin_pass: at least 6 characters', code: 'short' })
  })

  it('tells listeners about a lost session and a lost network', async () => {
    const seen: number[] = []
    const off = onAuthLost((c) => seen.push(c))
    mockFetch(reply(401, { ok: false, error: 'login required' }), new TypeError('network'))
    await api.status().catch(() => undefined)
    await api.status().catch(() => undefined)
    off()
    expect(seen).toEqual([401, 0])
  })

  it('keeps login failures quiet', async () => {
    const seen: number[] = []
    const off = onAuthLost((c) => seen.push(c))
    mockFetch(reply(401, { ok: false, error: 'wrong' }))
    await api.login('x').catch(() => undefined)
    off()
    expect(seen).toEqual([])
  })
})

describe('pressReal', () => {
  it('marks presses from the page as real and names the deck', async () => {
    const f = mockFetch(reply(200, { ok: true }))
    await pressReal({ key: 2, action: 'down' }, 'EL27')
    const [url, init] = f.mock.calls[0] ?? []
    expect(url).toBe('/api/input?deck=EL27')
    expect(JSON.parse(init?.body as string)).toEqual({ key: 2, action: 'down', real: true })
  })
})

describe('inspect', () => {
  it('sends only the 144-byte header for inspection', async () => {
    const f = mockFetch(reply(200, { ok: true, target: 'p4', version: 'v1' }))
    await api.inspect(new Blob([new Uint8Array(4096)]))
    expect((f.mock.calls[0]?.[1]?.body as Blob | undefined)?.size).toBe(144)
  })
})

describe('replies the page cannot use', () => {
  it('drops a proxy HTML page from the message', async () => {
    mockFetch(reply(500, '<html><body><h1>500 Internal Server Error</h1></body></html>', 'text/html'))
    const e = await request('/api/config', { method: 'PUT', body: {} }).catch((x: unknown) => x)
    expect(e).toMatchObject({ status: 500, message: '' })
  })
  it('fails an empty JSON reply instead of handing on null', async () => {
    mockFetch(reply(200, 'null'))
    await expect(api.config()).rejects.toMatchObject({ code: 'badreply' })
  })
  it('fails a list reply that is not a list', async () => {
    mockFetch(reply(200, { x: 1 }), reply(200, 'null'))
    await expect(api.wifiScan()).rejects.toMatchObject({ code: 'badreply' })
    await expect(api.tokens()).rejects.toMatchObject({ code: 'badreply' })
  })
})

describe('a hung request', () => {
  it('ends as a timeout error without telling listeners the network is gone', async () => {
    const seen: number[] = []
    const off = onAuthLost((c) => seen.push(c))
    vi.stubGlobal(
      'fetch',
      vi.fn(() => Promise.reject(new DOMException('slow', 'TimeoutError'))),
    )
    await expect(api.putConfig({ brightness: 1 })).rejects.toMatchObject({ code: 'timeout' })
    expect(seen).toEqual([])
    off()
  })
})
