import { EmailMessage } from 'cloudflare:email'
import { check, mime, type Form } from './mail'

type Env = {
  EMAIL: { send(m: EmailMessage): Promise<unknown> }
  LIMIT: { limit(o: { key: string }): Promise<{ success: boolean }> }
  CONTACT_TO: string
  ORIGIN: string
}

const FROM = 'kontakt@deckconnector.com'
const json = (body: object, status = 200) => Response.json(body, { status })

export default {
  async fetch(req: Request, env: Env): Promise<Response> {
    if (req.method !== 'POST') return json({ error: 'method' }, 405)
    if (req.headers.get('Origin') !== env.ORIGIN) return json({ error: 'origin' }, 403)
    const { success } = await env.LIMIT.limit({
      key: req.headers.get('CF-Connecting-IP') ?? '',
    })
    if (!success) return json({ error: 'rate' }, 429)
    const raw = await req.text()
    if (raw.length > 20_000) return json({ error: 'message' }, 413)
    let form: Form | null
    try {
      form = JSON.parse(raw) as Form | null
    } catch {
      return json({ error: 'bad' }, 400)
    }
    const f = check(form ?? {})
    if ('problem' in f) return json({ error: f.problem }, 400)
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
      await env.EMAIL.send(new EmailMessage(FROM, env.CONTACT_TO, mail))
    } catch (e) {
      console.error('send failed', e)
      return json({ error: 'send' }, 502)
    }
    return json({ ok: true })
  },
}
