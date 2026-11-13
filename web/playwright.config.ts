import { defineConfig, devices } from '@playwright/test'
import { SIM_PORT as SIM, WEB_PORT as WEB } from './e2e/ports.ts'

// The simulator from firmware/sim on its own port, and the dev server pointed at it. Never the bench bridge.
export default defineConfig({
  testDir: 'e2e',
  fullyParallel: false,
  workers: 1,
  reporter: 'list',
  use: { baseURL: `http://127.0.0.1:${WEB}`, trace: 'retain-on-failure' },
  projects: [
    { name: 'desktop', use: { ...devices['Desktop Chrome'], viewport: { width: 1280, height: 900 } } },
    { name: 'phone', use: { ...devices['Desktop Chrome'], viewport: { width: 390, height: 844 }, hasTouch: true } },
  ],
  webServer: [
    {
      command: `node scripts/sim.mjs`,
      env: { DC_SIM_PORT: String(SIM), DB_SIM_LOGIN_ACCEPT: '1' },
      url: `http://127.0.0.1:${SIM}/`,
      reuseExistingServer: false,
    },
    {
      command: `npx vite --host 127.0.0.1 --port ${WEB} --strictPort`,
      env: { DC_TARGET: `http://127.0.0.1:${SIM}`, DC_SIM_PORT: String(SIM) },
      url: `http://127.0.0.1:${WEB}/`,
      reuseExistingServer: false,
    },
  ],
})
