import { useLayoutEffect, useRef, type KeyboardEvent } from 'react'
import type { UpdateCheck } from '../api/types'

export const cx = (...a: (string | false | null | undefined)[]) => a.filter(Boolean).join(' ')

export const store = (st: () => Storage) => ({
  get<V>(k: string, d: V): V {
    try {
      const v = st().getItem(k)
      return v == null ? d : (JSON.parse(v) as V)
    } catch {
      return d
    }
  },
  set(k: string, v: unknown) {
    try {
      if (v === undefined) st().removeItem(k)
      else st().setItem(k, JSON.stringify(v))
    } catch {
      /* private mode */
    }
  },
})
export const ls = store(() => localStorage)
export const ss = store(() => sessionStorage)

export async function copyText(text: string, el?: HTMLElement | null): Promise<boolean> {
  try {
    await navigator.clipboard.writeText(text)
    return true
  } catch {
    if (el) {
      const r = document.createRange()
      r.selectNodeContents(el)
      const s = getSelection()
      s?.removeAllRanges()
      s?.addRange(r)
    }
    return false
  }
}

export function download(name: string, data: BlobPart, type = 'application/json') {
  const a = document.createElement('a')
  a.href = URL.createObjectURL(new Blob([data], { type }))
  a.download = name
  document.body.appendChild(a)
  a.click()
  setTimeout(() => {
    URL.revokeObjectURL(a.href)
    a.remove()
  }, 500)
}

/** Arrow keys in a radio group: focus moves to the next enabled radio and, as in any radio group, picks it. */
export function roam(e: KeyboardEvent<HTMLElement>) {
  const d = ({ ArrowDown: 1, ArrowRight: 1, ArrowUp: -1, ArrowLeft: -1 } as Record<string, number>)[e.key]
  if (!d) return
  const b = [...e.currentTarget.querySelectorAll<HTMLButtonElement>('[role=radio]:not(:disabled)')]
  if (!b.length) return
  e.preventDefault()
  const i = b.indexOf(document.activeElement as HTMLButtonElement)
  const n = b[(i + d + b.length) % b.length]
  n?.focus()
  n?.click()
}

/** A ref that always holds the latest value, for listeners and timers set up once. */
export function useLatest<V>(v: V) {
  const ref = useRef(v)
  useLayoutEffect(() => {
    ref.current = v
  })
  return ref
}

/** What the deck shows about the browser that asks to log in, e.g. "iPhone · Safari". */
export function deviceLabel(ua: string) {
  const os = /iPhone/.test(ua)
    ? 'iPhone'
    : /iPad/.test(ua)
      ? 'iPad'
      : /Android/.test(ua)
        ? 'Android'
        : /Mac OS X/.test(ua)
          ? 'Mac'
          : /Windows/.test(ua)
            ? 'Windows'
            : /Linux/.test(ua)
              ? 'Linux'
              : ''
  const br = /Edg\//.test(ua)
    ? 'Edge'
    : /Firefox|FxiOS/.test(ua)
      ? 'Firefox'
      : /Chrome|CriOS/.test(ua)
        ? 'Chrome'
        : /Safari/.test(ua)
          ? 'Safari'
          : ''
  return [os, br].filter(Boolean).join(' · ') || 'Browser'
}

/** Lowercase without accents, for matching typed text. */
export const fold = (s: string) => s.normalize('NFD').replace(/\p{M}/gu, '').toLowerCase().trim()

export const REPO = 'https://github.com/FusselTV/DeckConnector'
export const docsPage = (lang: string, page: string) =>
  `https://deckconnector.com/${lang === 'de' ? 'de/' : ''}docs/${page}/`

/** The source of a `git describe --tags --always --dirty` version; the repo itself for local changes. */
export function sourceUrl(version?: string) {
  if (!version || version.endsWith('-dirty')) return REPO
  return `${REPO}/tree/${version.match(/-g([0-9a-f]{7,})$/)?.[1] ?? version}`
}

/** The other firmware image (standard or Matter) of the newest release, once the running one is not older. */
export function otherImage(u: UpdateCheck, matter: boolean) {
  const v = u.latest
  if (!v || !(u.url || v === u.current) || !u.notes?.includes('/releases/tag/')) return null
  // release.yml names them deckconnector-<tag>.dbfw and deckconnector-matter-<tag>.dbfw
  return {
    v,
    url: `${u.notes.replace('/releases/tag/', '/releases/download/')}/deckconnector${matter ? '' : '-matter'}-${v}.dbfw`,
  }
}
