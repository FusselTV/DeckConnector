import { check, limitKey, mime, type Form } from './mail'
import { send } from './smtp'
import { human } from './turnstile'

type Env = {
  LIMIT: { limit(o: { key: string }): Promise<{ success: boolean }> }
  CONTACT_TO: string
  SMTP_USER: string
  SMTP_PASS: string
  ORIGIN: string
  TURNSTILE_SECRET: string
}

// iCloud+ custom domain: SMTP_USER is the account's @icloud.com address, FROM one of its domain addresses.
const FROM = 'info@deckconnector.com'
const json = (body: object, status = 200) => Response.json(body, { status })

export default {
  async fetch(req: Request, env: Env): Promise<Response> {
    if (req.method !== 'POST') return json({ error: 'method' }, 405)
    if (req.headers.get('Origin') !== env.ORIGIN) return json({ error: 'origin' }, 403)
    // Content-Length first: req.text() would buffer any size. Missing counts as too big.
    if (!(Number(req.headers.get('Content-Length') ?? Infinity) <= 20_000)) return json({ error: 'size' }, 413)
    const ip = req.headers.get('CF-Connecting-IP') ?? ''
    const { success } = await env.LIMIT.limit({ key: limitKey(ip) })
    if (!success) return json({ error: 'rate' }, 429)
    const raw = await req.text()
    if (raw.length > 20_000) return json({ error: 'size' }, 413)
    let form: Form | null
    try {
      form = JSON.parse(raw) as Form | null
    } catch {
      return json({ error: 'bad' }, 400)
    }
    const f = check(form ?? {})
    if ('problem' in f) return json({ error: f.problem }, 400)
    if (!(await human(env.TURNSTILE_SECRET, form?.turnstile, ip, new URL(env.ORIGIN).hostname)))
      return json({ error: 'turnstile' }, 403)
    if (f.bot) return json({ ok: true }) // the honeypot field was filled: look sent, send nothing
    const id = `<${crypto.randomUUID()}@deckconnector.com>`
    const mail = mime({
      from: FROM,
      to: env.CONTACT_TO,
      replyTo: f.email,
      name: f.name,
      message: f.message,
      id,
      date: new Date(),
    })
    try {
      await send({
        host: 'smtp.mail.me.com',
        user: env.SMTP_USER,
        pass: env.SMTP_PASS,
        from: FROM,
        to: env.CONTACT_TO,
        mail,
      })
    } catch (e) {
      console.error('send failed', e)
      return json({ error: 'send' }, 502)
    }
    return json({ ok: true })
  },
}
