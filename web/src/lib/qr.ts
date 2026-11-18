/** An SVG path of the dark modules of "size:hex" (row by row, lowest bit first). */
export function qrPath(bits: string): { n: number; d: string } {
  const [size, hex = ''] = bits.split(':')
  const n = Number(size) || 0
  let d = ''
  for (let i = 0; i < n * n; i++)
    if ((parseInt(hex.slice((i >> 3) * 2, (i >> 3) * 2 + 2), 16) >> (i & 7)) & 1)
      d += `M${i % n} ${Math.floor(i / n)}h1v1h-1z`
  return { n, d }
}
