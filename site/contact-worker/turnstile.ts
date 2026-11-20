/** Whether Cloudflare Turnstile vouches for `token`, solved on `host`. */
export async function human(secret: string, token: unknown, ip: string, host: string) {
  if (typeof token !== 'string' || !token || token.length > 2048) return false
  try {
    const r = await fetch('https://challenges.cloudflare.com/turnstile/v0/siteverify', {
      method: 'POST',
      body: new URLSearchParams({ secret, response: token, remoteip: ip }),
    })
    const v = (await r.json()) as { success?: boolean; hostname?: string; 'error-codes'?: string[] }
    if (v.success === true && v.hostname === host) return true
    console.error('turnstile refused', r.status, v.hostname, v['error-codes'])
  } catch (e) {
    console.error('turnstile failed', e)
  }
  return false
}
