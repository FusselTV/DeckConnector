import { renameSync, rmSync } from 'node:fs'

const out = new URL('../out/de/', import.meta.url)
renameSync(new URL('404/index.html', out), new URL('404.html', out))
rmSync(new URL('404/', out), { recursive: true })
