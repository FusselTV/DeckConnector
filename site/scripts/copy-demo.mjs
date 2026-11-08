import { cpSync, existsSync, rmSync } from 'node:fs'

const from = new URL('../../web/dist-demo/', import.meta.url)
const to = new URL('../public/demo/app/', import.meta.url)

rmSync(to, { recursive: true, force: true })
if (existsSync(from)) cpSync(from, to, { recursive: true })
else console.warn('copy-demo: web/dist-demo is missing, /demo shows a notice. Run npm run build:demo in web/ first.')
