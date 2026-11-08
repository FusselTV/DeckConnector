import { defineConfig, devices } from '@playwright/test'

// The built demo (npm run build:demo) under a sub path, as the website serves it. No device, no simulator.
const PORT = 5191
export default defineConfig({
  testDir: 'e2e-demo',
  fullyParallel: true,
  reporter: 'list',
  use: { baseURL: `http://127.0.0.1:${PORT}/demo/app/`, trace: 'retain-on-failure' },
  projects: [
    { name: 'desktop', use: { ...devices['Desktop Chrome'], viewport: { width: 1280, height: 900 } } },
    { name: 'phone', use: { ...devices['Desktop Chrome'], viewport: { width: 390, height: 844 }, hasTouch: true } },
  ],
  webServer: {
    command: `npx vite preview --mode demo --base /demo/app/ --host 127.0.0.1 --port ${PORT} --strictPort`,
    url: `http://127.0.0.1:${PORT}/demo/app/`,
    reuseExistingServer: false,
  },
})
