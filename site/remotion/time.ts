import { Easing, interpolate } from 'remotion'

const clamp = { extrapolateLeft: 'clamp', extrapolateRight: 'clamp' } as const
export const ease = Easing.bezier(0.45, 0, 0.2, 1)
export const ramp = (f: number, a: number, b: number, from = 0, to = 1) =>
  interpolate(f, [a, b], [from, to], { ...clamp, easing: ease })
export const lin = (f: number, a: number, b: number, from = 0, to = 1) => interpolate(f, [a, b], [from, to], clamp)
/** 0 → 1 → 0 over [a, a+up, b-down, b]. */
export const pulse = (f: number, a: number, b: number, up = 4, down = 8) =>
  b - a > up + down
    ? interpolate(f, [a, a + up, b - down, b], [0, 1, 1, 0], clamp)
    : interpolate(f, [a, a + ((b - a) * up) / (up + down), b], [0, 1, 0], clamp)
