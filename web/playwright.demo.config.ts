import { defineConfig, devices } from '@playwright/test'

// The built demo (npm run build:demo) under a sub path, as the website serves it. No device. The "contract" project
// compares the mock with the firmware simulator on its own port.
const PORT = 5191
const SIM = 18230
export default defineConfig({
  testDir: 'e2e-demo',
  fullyParallel: true,
  reporter: 'list',
  use: { baseURL: `http://127.0.0.1:${PORT}/demo/app/`, trace: 'retain-on-failure' },
  projects: [
    {
      name: 'desktop',
      testIgnore: 'contract.spec.ts',
      use: { ...devices['Desktop Chrome'], viewport: { width: 1280, height: 900 } },
    },
    {
      name: 'phone',
      testIgnore: 'contract.spec.ts',
      use: { ...devices['Desktop Chrome'], viewport: { width: 390, height: 844 }, hasTouch: true },
    },
    { name: 'contract', testMatch: 'contract.spec.ts' },
  ],
  webServer: [
    {
      command: `npx vite preview --mode demo --base /demo/app/ --host 127.0.0.1 --port ${PORT} --strictPort`,
      url: `http://127.0.0.1:${PORT}/demo/app/`,
      reuseExistingServer: false,
    },
    // CI has no simulator build and runs only desktop and phone
    ...(process.env.DC_NO_SIM
      ? []
      : [
          {
            command: 'node scripts/sim.mjs',
            env: { DC_SIM_PORT: String(SIM) },
            url: `http://127.0.0.1:${SIM}/`,
            reuseExistingServer: false,
          },
        ]),
  ],
})
