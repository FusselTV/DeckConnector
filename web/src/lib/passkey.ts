import { api } from '../api/client'
import { getState } from '../app/store'

/** Browsers allow passkeys only in a secure context, and never for an IP address. */
export const passkeysHere = () =>
  window.isSecureContext && 'PublicKeyCredential' in window && !/^[\d.]+$|:/.test(location.hostname)

const bytes = (hex: string) => Uint8Array.from(hex.match(/../g) ?? [], (h) => parseInt(h, 16))
const b64u = (b: ArrayBuffer) =>
  btoa(String.fromCharCode(...new Uint8Array(b)))
    .replace(/\+/g, '-')
    .replace(/\//g, '_')
    .replace(/=+$/, '')

export async function addPasskey(name: string) {
  const { challenge } = await api.passkeyChallenge(false)
  const user = `Deck Connector ${getState().status.name ?? location.hostname}`
  const c = (await navigator.credentials.create({
    publicKey: {
      challenge: bytes(challenge),
      rp: { id: location.hostname, name: 'Deck Connector' },
      // one id per passkey: the same id again would replace an older passkey on that device
      user: { id: crypto.getRandomValues(new Uint8Array(16)), name: user, displayName: user },
      pubKeyCredParams: [{ type: 'public-key', alg: -7 }],
      authenticatorSelection: { residentKey: 'required', userVerification: 'required' },
      attestation: 'none',
    },
  })) as PublicKeyCredential
  const r = c.response as AuthenticatorAttestationResponse
  const key = r.getPublicKey()
  return api.addPasskey({
    name,
    id: b64u(c.rawId),
    clientDataJSON: b64u(r.clientDataJSON),
    authenticatorData: b64u(r.getAuthenticatorData()),
    publicKey: key ? b64u(key) : '',
    publicKeyAlgorithm: r.getPublicKeyAlgorithm(),
  })
}

export async function passkeyLogin() {
  const { challenge } = await api.passkeyChallenge(true)
  const c = (await navigator.credentials.get({
    publicKey: { challenge: bytes(challenge), rpId: location.hostname, userVerification: 'required' },
  })) as PublicKeyCredential
  const r = c.response as AuthenticatorAssertionResponse
  return api.passkeyLogin({
    id: b64u(c.rawId),
    clientDataJSON: b64u(r.clientDataJSON),
    authenticatorData: b64u(r.authenticatorData),
    signature: b64u(r.signature),
  })
}
