// Starts the host simulator from firmware/sim on DC_SIM_PORT (default 18200). It runs a copy of the ELF, so a
// rebuild of the simulator does not pull the binary away under a running dev session.
import { spawn } from 'node:child_process'
import { copyFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join } from 'node:path'

const port = process.env.DC_SIM_PORT ?? '18200'
const password = process.env.DC_SIM_PASSWORD ?? 'simulator-pass'
const elf = new URL('../../firmware/sim/build/deckbridge_sim.elf', import.meta.url).pathname
const copy = join(tmpdir(), `deckconnector-sim-${port}.elf`)
copyFileSync(elf, copy)
console.log(`simulator on http://127.0.0.1:${port}, password "${password}"`)
// The simulator dies of SIGPIPE when a browser drops a socket mid-write; an ignored signal survives the exec.
const sim = spawn('sh', ['-c', 'trap "" PIPE; exec "$0"', copy], {
  env: { ...process.env, DB_SIM_PORT: port, DB_SIM_PASSWORD: password },
  stdio: ['ignore', 'inherit', 'inherit'],
})
sim.on('exit', (code, signal) => {
  console.error(`simulator exited: code ${code} signal ${signal}`)
  process.exit(code ?? 1)
})
for (const s of ['SIGINT', 'SIGTERM']) process.on(s, () => sim.kill())
