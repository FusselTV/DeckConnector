/// <reference types="vitest/config" />
import { readFileSync } from 'node:fs'
import react from '@vitejs/plugin-react'
import { defineConfig, loadEnv } from 'vite'
import { viteSingleFile } from 'vite-plugin-singlefile'

// DC_TARGET picks the device behind /api in `npm run dev`. Anything but the local simulator (loopback on
// DC_SIM_PORT) is read-only: the proxy refuses every request and the web socket that could change it, and the
// page refuses to send presses. A port matched by "loopback" alone would also cover an ssh tunnel to a device.
export default defineConfig(({ mode }) => {
  const env = { ...loadEnv(mode, process.cwd(), 'DC_'), ...process.env }
  const target = env.DC_TARGET || 'http://127.0.0.1:18200'
  const readonly = target.replace(/\/$/, '') !== `http://127.0.0.1:${env.DC_SIM_PORT || '18200'}`
  const origin = (req: { setHeader(k: string, v: string): void }) => req.setHeader('origin', target)
  // `--mode demo`: the page with the in-browser mock of the device from src/demo, built into dist-demo.
  const demo = mode === 'demo'
  const demoEntry = {
    name: 'demo-entry',
    transformIndexHtml: {
      order: 'pre' as const,
      handler: (html: string) =>
        html
          .replace('/src/main.tsx', '/src/demo/main.ts')
          .replace(
            '<title>DeckConnector</title>',
            '<title>DeckConnector demo</title><meta name="robots" content="noindex">',
          ),
    },
  }
  // The deck's WebAssembly starts loading with the page, not after the script that asks for it.
  const preloadWasm = {
    name: 'demo-preload-wasm',
    transformIndexHtml: {
      order: 'post' as const,
      handler: (_html: string, ctx: { bundle?: Record<string, unknown> }) =>
        Object.keys(ctx.bundle ?? {})
          .filter((f) => f.endsWith('.wasm'))
          .map((f) => ({ tag: 'link', attrs: { rel: 'preload', href: `./${f}`, as: 'fetch', crossorigin: '' } })),
    },
  }

  // The device page ships these packages, so THIRD_PARTY_LICENSES.txt must name each one with its version.
  const licenses = {
    name: 'third-party-licenses',
    generateBundle(_: unknown, bundle: Record<string, { modules?: Record<string, unknown> }>) {
      const listed = readFileSync(new URL('../THIRD_PARTY_LICENSES.txt', import.meta.url), 'utf8')
      const ids = Object.values(bundle).flatMap((c) => Object.keys(c.modules ?? {}))
      const pkgs = new Set(ids.map((id) => id.match(/.*node_modules\/((?:@[^/]+\/)?[^/]+)/)?.[1]).filter(Boolean))
      if (!pkgs.has('react')) throw new Error('third-party-licenses: found no bundled packages, the module ids changed')
      const missing = [...pkgs].filter((p) => {
        const pkg = readFileSync(new URL(`node_modules/${p}/package.json`, import.meta.url), 'utf8')
        const { version } = JSON.parse(pkg) as { version: string }
        return !new RegExp(`(^|[\\s(,])${p} ${version.replaceAll('.', '\\.')}(?![\\w.])`).test(listed)
      })
      if (missing.length) throw new Error(`THIRD_PARTY_LICENSES.txt lacks ${missing.join(', ')} (name and version)`)
    },
  }

  return {
    // The device needs one file; the demo loads faster as files the browser fetches side by side and caches.
    plugins: [
      react(),
      !demo && viteSingleFile({ removeViteModuleLoader: true }),
      !demo && licenses,
      demo && demoEntry,
      demo && preloadWasm,
    ],
    define: { __DC_READONLY__: JSON.stringify(!demo && mode !== 'production' && readonly) },
    base: demo ? './' : undefined,
    build: {
      target: 'es2022',
      reportCompressedSize: false,
      outDir: demo ? 'dist-demo' : 'dist',
      chunkSizeWarningLimit: 1000,
    },
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
            proxy.on('proxyReqWs', (req, _r, socket) => (readonly ? socket.destroy() : origin(req)))
          },
        },
      },
    },
    test: { include: ['src/**/*.test.ts'] },
  }
})
