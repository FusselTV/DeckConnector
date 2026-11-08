import { useSyncExternalStore } from 'react'

/** A value outside React with selective subscriptions: readers re-render only when their slice changes. */
export function atom<V>(initial: V) {
  let v = initial
  const subs = new Set<() => void>()
  const subscribe = (f: () => void) => {
    subs.add(f)
    return () => void subs.delete(f)
  }
  return {
    get: () => v,
    subscribe,
    set(next: V | ((p: V) => V)) {
      const n = typeof next === 'function' ? (next as (p: V) => V)(v) : next
      if (Object.is(n, v)) return
      v = n
      subs.forEach((f) => f())
    },
    use<R = V>(sel: (s: V) => R = (s) => s as unknown as R): R {
      return useSyncExternalStore(subscribe, () => sel(v))
    },
  }
}
