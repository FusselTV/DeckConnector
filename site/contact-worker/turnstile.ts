/** Whether Cloudflare Turnstile vouches for `token`, solved on `host`. */
export async function human(secret: string, token: unknown, ip: string, host: string) {
  if (typeof token !== 'string' || !token || token.length > 2048) return false
  const r = await fetch('https://challenges.cloudflare.com/turnstile/v0/siteverify', {
    method: 'POST',
    body: new URLSearchParams({ secret, response: token, remoteip: ip }),
  })
  if (!r.ok) return false
  const v = (await r.json()) as { success?: boolean; hostname?: string }
  return v.success === true && v.hostname === host
}
