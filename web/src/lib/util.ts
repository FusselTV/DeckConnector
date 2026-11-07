import { useLayoutEffect, useRef } from 'react'

export const cx = (...a: (string | false | null | undefined)[]) => a.filter(Boolean).join(' ')

export const ls = {
  get<V>(k: string, d: V): V {
    try {
      const v = localStorage.getItem(k)
      return v == null ? d : (JSON.parse(v) as V)
    } catch {
      return d
    }
  },
  set(k: string, v: unknown) {
    try {
      localStorage.setItem(k, JSON.stringify(v))
    } catch {
      /* private mode */
    }
  },
}

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

/** The target byte of a .dbfw header, as fw.py writes it. */
export async function dbfwTarget(file: Blob): Promise<'p4' | 'c6' | 'page' | 'bootloader' | 'ptable' | null> {
  const b = new Uint8Array(await file.slice(0, 16).arrayBuffer())
  const map = { 1: 'p4', 2: 'c6', 3: 'page', 4: 'bootloader', 5: 'ptable' } as const
  return b[5] !== undefined && b[5] in map ? map[b[5] as keyof typeof map] : null
}

/** Moves focus between `[data-roam]` children with the arrow keys. */
export function roam(e: React.KeyboardEvent<HTMLElement>) {
  if (!['ArrowDown', 'ArrowUp', 'ArrowLeft', 'ArrowRight'].includes(e.key)) return
  const b = [...e.currentTarget.querySelectorAll<HTMLElement>('[data-roam]')]
  const i = b.indexOf(document.activeElement as HTMLElement)
  if (i < 0) return
  e.preventDefault()
  b[(i + (e.key === 'ArrowDown' || e.key === 'ArrowRight' ? 1 : b.length - 1)) % b.length]?.focus()
}

/** A ref that always holds the latest value, for listeners and timers set up once. */
export function useLatest<V>(v: V) {
  const ref = useRef(v)
  useLayoutEffect(() => {
    ref.current = v
  })
  return ref
}
