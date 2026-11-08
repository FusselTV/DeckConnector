# DeckConnector website

Static Next.js export (landing, docs, flasher, demo) in `out/`, for Cloudflare Pages.

```bash
npm ci
npm run dev     # http://localhost:3000
npm run check   # typecheck, lint, static build into out/
```

## Demo mount

- `npm run build:demo` in `web/` writes `web/dist-demo/`.
- `scripts/copy-demo.mjs` (runs before `dev` and `build`) copies it to `public/demo/app/`, which is not in Git.
- The export serves it at `/demo/app/`; `/demo/` and `/de/demo/` show it in an iframe and pass on the language, the theme and their own query (`?setup`, `?login`).
- Without `web/dist-demo/` the build still passes and `/demo/` shows a notice.
