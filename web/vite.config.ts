/// <reference types="vitest/config" />
import tailwindcss from '@tailwindcss/vite'
import react from '@vitejs/plugin-react'
import { defineConfig, loadEnv } from 'vite'
import { viteSingleFile } from 'vite-plugin-singlefile'

// DC_TARGET picks the device behind /api in `npm run dev`. Anything but the local simulator is read-only:
// the proxy refuses every request that could change it, and the page refuses to send presses.
export default defineConfig(({ mode }) => {
  const env = { ...loadEnv(mode, process.cwd(), 'DC_'), ...process.env }
  const target = env.DC_TARGET || 'http://127.0.0.1:18200'
  const readonly = !/^https?:\/\/(127\.0\.0\.1|localhost)(:\d+)?$/.test(target)
  const origin = (req: { setHeader(k: string, v: string): void }) => req.setHeader('origin', target)
  return {
    plugins: [react(), tailwindcss(), viteSingleFile({ removeViteModuleLoader: true })],
    define: { __DC_READONLY__: JSON.stringify(mode !== 'production' && readonly) },
    build: { target: 'es2022', reportCompressedSize: false },
    server: {
      proxy: {
        '/api': {
          target,
          changeOrigin: true,
          secure: false,
          ws: true,
          bypass: (req) =>
            readonly && req.method !== 'GET' && !/^\/api\/log(in|out)$/.test(req.url ?? '') ? false : undefined,
          configure: (proxy) => {
            proxy.on('proxyReq', origin)
            proxy.on('proxyReqWs', origin)
          },
        },
      },
    },
    test: { include: ['src/**/*.test.ts'] },
  }
})
