'use client'
import { useEffect, useRef, useState } from 'react'
import { Bug, Check, Mail } from 'lucide-react'
import { Btn, Field, Note, Spinner, card, input } from './flash/ui'
import type { Lang } from '~/lib/i18n'
import { EMAIL, MAX } from '~/contact-worker/mail'
import { turnstileKey } from '~/site.config'

const t = {
  en: {
    title: 'Contact',
    lead: 'Questions, ideas or feedback? Write me.',
    name: 'Name',
    optional: 'optional',
    email: 'Email',
    message: 'Message',
    consent: ['I use your details only to answer you. More in the ', 'privacy policy', '.'],
    send: 'Send message',
    sending: 'Sending',
    sent: 'Message sent',
    thanks: 'Thank you. I will answer you by email.',
    again: 'Write another message',
    badEmail: 'Enter a valid email address.',
    noMessage: 'Write a message.',
    rate: 'Too many messages. Wait a minute and try again.',
    failed: 'Sending did not work. Try again, or write to',
    noJs: 'This form needs JavaScript. Write to',
    bug: 'Found a bug?',
    bugText: 'Open an issue on GitHub. Then others find the answer too.',
    bugLink: 'Open an issue',
    mail: 'Email',
    mailText: 'You can also write to me directly.',
  },
  de: {
    title: 'Kontakt',
    lead: 'Fragen, Ideen oder Feedback? Schreib mir.',
    name: 'Name',
    optional: 'optional',
    email: 'E-Mail',
    message: 'Nachricht',
    consent: ['Ich nutze deine Angaben nur, um dir zu antworten. Mehr dazu in der ', 'Datenschutzerklärung', '.'],
    send: 'Nachricht senden',
    sending: 'Wird gesendet',
    sent: 'Nachricht gesendet',
    thanks: 'Danke! Ich antworte dir per E-Mail.',
    again: 'Neue Nachricht schreiben',
    badEmail: 'Gib eine gültige E-Mail-Adresse ein.',
    noMessage: 'Schreib eine Nachricht.',
    rate: 'Zu viele Nachrichten. Warte eine Minute und versuch es dann noch einmal.',
    failed: 'Das Senden hat nicht geklappt. Versuch es noch einmal oder schreib an',
    noJs: 'Das Formular braucht JavaScript. Schreib stattdessen an',
    bug: 'Fehler gefunden?',
    bugText: 'Eröffne ein Issue auf GitHub. Dann finden auch andere die Antwort.',
    bugLink: 'Issue eröffnen',
    mail: 'E-Mail',
    mailText: 'Du kannst mir auch direkt schreiben.',
  },
}

type Turnstile = {
  render(el: HTMLElement, o: object): string
  reset(id: string): void
}
type Widget = { token(): Promise<string | null>; reset(): void }

// Loaded on the first focus in the form, so a visit alone sends nothing to Cloudflare.
const turnstile = (el: HTMLElement) =>
  new Promise<Widget>((ok, bad) => {
    const s = document.createElement('script')
    s.src = 'https://challenges.cloudflare.com/turnstile/v0/api.js?render=explicit'
    s.onerror = bad
    s.onload = () => {
      try {
        const ts = (window as unknown as { turnstile: Turnstile }).turnstile
        let token: string | null = null
        let wait: ((t: string | null) => void) | null = null
        const give = (t: string | null) => ((token = t), wait?.(t), (wait = null))
        const id = ts.render(el, {
          sitekey: turnstileKey,
          appearance: 'interaction-only',
          callback: give,
          'expired-callback': () => (token = null),
          'error-callback': () => (give(null), true),
          'timeout-callback': () => give(null),
        })
        ok({
          // An interactive check left alone never answers; after a minute the form offers the email address.
          token: () =>
            token
              ? Promise.resolve(token)
              : new Promise((r) => ((wait = r), setTimeout(() => wait === r && give(null), 60_000))),
          reset: () => ((token = null), ts.reset(id)),
        })
      } catch (e) {
        bad(e instanceof Error ? e : new Error(String(e)))
      }
    }
    document.head.append(s)
  })

const link = 'font-medium text-fg underline decoration-line-2 underline-offset-4 hover:decoration-current'

export function Contact({
  lang,
  email,
  issues,
  privacy,
}: {
  lang: Lang
  email: string
  issues: string
  privacy: string
}) {
  const c = t[lang]
  const opened = useRef(0)
  const form = useRef<HTMLFormElement>(null)
  const box = useRef<HTMLDivElement>(null)
  const widget = useRef<Promise<Widget> | null>(null)
  const start = () => {
    if (!widget.current && box.current)
      widget.current = turnstile(box.current).catch((e: unknown) => {
        widget.current = null
        throw e
      })
  }
  const done = useRef<HTMLHeadingElement>(null)
  const [state, setState] = useState<'idle' | 'sending' | 'sent' | 'rate' | 'failed'>('idle')
  const [tried, setTried] = useState(false)
  const [v, setV] = useState({ name: '', email: '', message: '' })
  useEffect(() => {
    opened.current = Date.now()
  }, [])
  const again = useRef(false)
  useEffect(() => {
    if (state === 'sent') done.current?.focus()
    if (state === 'idle' && again.current) document.getElementById('c-name')?.focus()
  }, [state])

  const err = {
    email: EMAIL.test(v.email.trim()) ? null : c.badEmail,
    message: v.message.trim() ? null : c.noMessage,
  }
  const mailto = (
    <a href={`mailto:${email}`} className={link}>
      {email}
    </a>
  )

  async function submit() {
    if (state === 'sending') return
    setTried(true)
    const bad = (['email', 'message'] as const).find((k) => err[k])
    if (bad) return form.current?.querySelector<HTMLElement>(`#c-${bad}`)?.focus()
    setState('sending')
    const website = (form.current?.elements.namedItem('c-hp') as HTMLInputElement | null)?.value ?? ''
    try {
      start()
      const w = await widget.current
      const token = await w?.token()
      w?.reset()
      if (!token) return setState('failed')
      const r = await fetch('/api/contact', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ ...v, website, t: Date.now() - opened.current, turnstile: token }),
      })
      setState(r.ok ? 'sent' : r.status === 429 ? 'rate' : 'failed')
    } catch {
      setState('failed')
    }
  }

  const inv = (k: 'email' | 'message') => ({
    'aria-invalid': tried && !!err[k],
    'aria-describedby': tried && err[k] ? `c-${k}-e` : undefined,
  })

  return (
    <main id="main" className="mx-auto w-full max-w-5xl px-4 pt-10 pb-20 sm:px-6 sm:pt-14">
      <header className="max-w-2xl">
        <h1 className="text-3xl font-semibold tracking-tight text-balance sm:text-4xl">{c.title}</h1>
        <p className="mt-3 text-base text-pretty text-fg-2 sm:text-lg">{c.lead}</p>
      </header>

      <div className="mt-10 grid gap-6 lg:grid-cols-[minmax(0,1fr)_300px]">
        <section className={`${card} min-w-0 p-5 sm:p-7`}>
          {state === 'sent' ? (
            <div className="flex flex-col items-center py-10 text-center">
              <span className="flex size-14 items-center justify-center rounded-full bg-ok-soft text-ok-fg ring-8 ring-ok-soft/40">
                <Check className="size-7" strokeWidth={2.5} aria-hidden />
              </span>
              <h2 ref={done} tabIndex={-1} className="mt-5 text-xl font-semibold outline-none">
                {c.sent}
              </h2>
              <p className="mt-2 text-fg-2">{c.thanks}</p>
              <div className="mt-8">
                <Btn
                  kind="ghost"
                  onClick={() => (
                    (again.current = true),
                    setV({ name: '', email: '', message: '' }),
                    setTried(false),
                    setState('idle')
                  )}
                >
                  {c.again}
                </Btn>
              </div>
            </div>
          ) : (
            <form ref={form} onSubmit={(e) => (e.preventDefault(), void submit())} noValidate className="space-y-5">
              <noscript>
                <Note lvl="info">
                  {c.noJs} {mailto}.
                </Note>
              </noscript>
              <div className="grid gap-5 sm:grid-cols-2">
                <Field
                  id="c-name"
                  label={
                    <>
                      {c.name} <span className="font-normal text-fg-3">({c.optional})</span>
                    </>
                  }
                >
                  <input
                    id="c-name"
                    className={input}
                    value={v.name}
                    maxLength={MAX.name}
                    autoComplete="name"
                    onFocus={start}
                    onChange={(e) => setV({ ...v, name: e.target.value })}
                  />
                </Field>
                <Field id="c-email" label={c.email} err={tried && err.email}>
                  <input
                    id="c-email"
                    type="email"
                    className={input}
                    value={v.email}
                    maxLength={MAX.email}
                    autoComplete="email"
                    onFocus={start}
                    required
                    onChange={(e) => setV({ ...v, email: e.target.value })}
                    {...inv('email')}
                  />
                </Field>
              </div>
              <Field id="c-message" label={c.message} err={tried && err.message}>
                <textarea
                  id="c-message"
                  className={`${input} h-auto min-h-44 resize-y py-2.5 leading-relaxed`}
                  rows={7}
                  value={v.message}
                  maxLength={MAX.message}
                  onFocus={start}
                  required
                  onChange={(e) => setV({ ...v, message: e.target.value })}
                  {...inv('message')}
                />
              </Field>
              <div aria-hidden className="absolute -left-[9999px] h-0 w-0 overflow-hidden">
                <label htmlFor="c-hp">Leave empty</label>
                <input id="c-hp" name="c-hp" tabIndex={-1} autoComplete="off" />
              </div>
              <div role="status">
                {(state === 'rate' || state === 'failed') && (
                  <Note lvl="bad">
                    {state === 'rate' ? (
                      c.rate
                    ) : (
                      <>
                        {c.failed} {mailto}.
                      </>
                    )}
                  </Note>
                )}
              </div>
              <div className="flex flex-col-reverse gap-4 border-t border-line pt-5 sm:flex-row sm:items-center sm:justify-between">
                <p className="text-sm text-fg-3">
                  {c.consent[0]}
                  <a href={privacy} className={link}>
                    {c.consent[1]}
                  </a>
                  {c.consent[2]}
                </p>
                <div className="shrink-0 [&>button]:w-full">
                  <Btn type="submit" aria-disabled={state === 'sending'} aria-busy={state === 'sending'}>
                    {state === 'sending' ? (
                      <>
                        <Spinner /> {c.sending}
                      </>
                    ) : (
                      c.send
                    )}
                  </Btn>
                </div>
              </div>
            </form>
          )}
          <div ref={box} />
        </section>

        <aside className="grid content-start gap-4">
          <div className={`${card} p-5`}>
            <Bug className="size-5 text-amber-fg" aria-hidden />
            <h2 className="mt-3 font-semibold">{c.bug}</h2>
            <p className="mt-1 text-sm text-fg-2">{c.bugText}</p>
            <a href={issues} className={`${link} mt-3 inline-block text-sm`}>
              {c.bugLink}
            </a>
          </div>
          <div className={`${card} p-5`}>
            <Mail className="size-5 text-amber-fg" aria-hidden />
            <h2 className="mt-3 font-semibold">{c.mail}</h2>
            <p className="mt-1 text-sm text-fg-2">{c.mailText}</p>
            <p className="mt-3 text-sm break-all">{mailto}</p>
          </div>
        </aside>
      </div>
    </main>
  )
}
