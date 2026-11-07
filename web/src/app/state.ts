import type { ReactNode } from 'react'
import type { ConfigPatch, Coredump, WifiAp } from '../api/types'
import { atom } from '../lib/atom'
import { ls } from '../lib/util'
import type { Lang } from '../i18n'
import { guessLang } from '../i18n'

export const TABS = ['overview', 'sources', 'deck', 'network', 'access', 'firmware', 'diagnose'] as const
export type Tab = (typeof TABS)[number]
export const fromHash = (): Tab => {
  const h = location.hash.slice(1) as Tab
  return TABS.includes(h) ? h : 'overview'
}

export const langAtom = atom<Lang>(ls.get<Lang>('dc.lang', guessLang()))
export const themeAtom = atom<'light' | 'dark' | null>(ls.get<'light' | 'dark' | null>('dc.theme', null))
export const tabAtom = atom<Tab>(fromHash())

export interface Toast {
  id: number
  msg: string
  bad?: boolean
  undo?: () => void
}
export const toastsAtom = atom<Toast[]>([])
let toastSeq = 0
export function toast(msg: string, o: Omit<Toast, 'id' | 'msg'> = {}) {
  const id = ++toastSeq
  toastsAtom.set((x) => [...x.slice(-1), { id, msg, ...o }])
}
export const dismissToast = (id: number) => toastsAtom.set((x) => x.filter((y) => y.id !== id))

/** Remote control: the live deck takes presses only while armed. Off after 60 s idle or when the page hides. */
export const ARM_MS = 60_000
export const armAtom = atom(0)
export const nudgeAtom = atom(0)
/** Why the login page shows, for its one line. */
export const loginReasonAtom = atom<'logout' | 'pw' | 'expired' | null>(null)
/** Presses go out with force past input_lock, confirmed for this arm only. */
export const forceAtom = atom(false)
export const deckSelAtom = atom(0)

/** A tab's unsaved settings. The save bar merges every dirty draft into one PUT with at most one confirm. */
export interface Draft {
  dirty: boolean
  restart: boolean
  /** Returns false when a value is invalid, after opening the tab and focusing the field. */
  check: () => boolean
  /** Only the fields the person changed. */
  patch: ConfigPatch
  /** What the restart confirm says for this draft. */
  confirm?: { title: string; body: ReactNode }
  /** Runs before the PUT (a Wi-Fi join test); false stops the save. */
  prepare?: () => Promise<boolean>
  /** Where the page guesses the device is after a restart, for firmware without `reach`. */
  where?: string[]
  saved: () => void
  reset: () => void
}
export const draftsAtom = atom<Partial<Record<Tab, { dirty: boolean; restart: boolean; ref: { current: Draft } }>>>({})

/** The last crash report, null when none or when the firmware has no coredump route. */
/** The last Wi-Fi scan, so search can offer the networks without scanning again. */
export const wifiScanAtom = atom<WifiAp[]>([])

export const crashAtom = atom<Coredump | null>(null)
export const bootAckAtom = atom<number>(ls.get('dc.bootAck', 0))
export const crashAckAtom = atom<string>(ls.get('dc.crashAck', ''))
export const crashKey = (c: Coredump) => `${c.elf_sha256}:${c.pc}:${c.size}`
