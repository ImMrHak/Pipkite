# SignalRelay website

The SignalRelay website: landing page, downloads, free license key and a setup guide, in English, French,
Spanish and Arabic. Built with [Astro](https://astro.build) and [Tailwind CSS](https://tailwindcss.com); the
output is plain static HTML.

## Commands (in `frontend/`)

```powershell
npm install
npm run dev              # http://localhost:4321
npm run build            # builds into dist/
npm run preview          # serve dist/ locally
npm run sync:downloads   # copy the release files from ..\dist into public\downloads
```

## Settings (`.env`, see `.env.example`)

- `SITE_URL`: your domain (used for links, the sitemap and social previews)
- `PUBLIC_API_BASE`: the API address, leave empty to use `/api/v1` on the same domain
- `PUBLIC_DOWNLOAD_BASE`: where the release files are hosted, empty means `/downloads`
- `PUBLIC_DOWNLOADS_MIN`: the download counter only shows from this number on (default 500)

## Translations

All text is in `src/i18n/` (`en.ts` is the reference, the build fails if another language misses a key).
Pages live in `src/views/`. Arabic is right-to-left.

## Deploy

Any static host works. `deploy/` has ready configs for Docker (Caddy, automatic HTTPS), Caddy and nginx, with
security headers. Netlify / Cloudflare Pages: publish `dist/`.

Don't add inline `style="..."` or `<script>` by hand: the site has a strict Content-Security-Policy.
