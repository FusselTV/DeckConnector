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

## Files for AI assistants

- `/llms.txt`: what DeckConnector is, key facts, the comparison and links to every docs page. Built from `lib/llm.ts`, `components/compare/data.ts` and the docs sidebar.
- `/llms-full.txt`: the same intro plus every English docs page as Markdown.
- `/md/docs/<page>.md` and `/md/de/docs/<page>.md`: each docs page as Markdown.
- `node --test scripts/llms.test.mjs` after a build checks these files for leftover MDX, broken fences and dead links.
