# Pipkite website

The website for [Pipkite](../README.md), the free Telegram to MetaTrader 5 signal copier.

It has the landing page, the download page, the free license key page, a step-by-step setup guide, the
changelog and the privacy page, in **English, French, Spanish and Arabic** (Arabic is right-to-left).

Built with [Astro](https://astro.build) and [Tailwind CSS](https://tailwindcss.com). The result is plain static
HTML: fast, no tracking, no cookies, nothing loaded from other sites.

## Quick start

You need [Node.js](https://nodejs.org) 22.12 or newer.

```powershell
cd frontend
npm install
npm run dev
```

Open http://localhost:4321. Changes show up as soon as you save.

## Commands

| Command | What it does |
|---|---|
| `npm run dev` | Local dev server with live reload |
| `npm run build` | Checks the code and builds the site into `dist/` |
| `npm run preview` | Serves the built `dist/` folder locally |
| `npm run sync:downloads` | Copies the app's release files from `..\dist` into `public\downloads` |
| `npm run images` | Regenerates the social preview image and app icons |

## Settings

Copy `.env.example` to `.env` and fill it in:

| Setting | What it's for |
|---|---|
| `SITE_URL` | Your domain, e.g. `https://pipkite.com`. Used for links, the sitemap and social previews. |
| `PUBLIC_API_BASE` | Where the API is. Leave empty to use `/api/v1` on the same domain (recommended). |
| `PUBLIC_DOWNLOAD_BASE` | Where the installer and zip are hosted. Leave empty to serve them from `/downloads`. |
| `PUBLIC_DOWNLOADS_MIN` | The download counter only appears from this number on (default 500, `0` = always). |

## Project layout

```
frontend/
├── src/
│   ├── i18n/         all the text, one file per language (en.ts is the reference)
│   ├── views/        the pages: Home, Download, License, Guide, Changelog, Privacy, 404
│   ├── pages/        the routes (English at /, other languages at /fr, /es, /ar)
│   ├── components/   header, footer and home page sections
│   ├── layouts/      the shared page layout
│   ├── seo/          structured data for search engines
│   ├── styles/       colors, light/dark themes, shared styles
│   └── config.ts     version, download sizes and hashes, contact info
├── public/           icons, social image, downloads
├── scripts/          build helpers (Arabic font, compression, images)
└── deploy/           Docker, Caddy and nginx configs
```

## Changing text or adding a translation

All the text is in `src/i18n/`. Edit `en.ts` first, then the same key in `fr.ts`, `es.ts` and `ar.ts`. The build
fails if a language is missing a key, so nothing gets left untranslated by accident.

The app's button names stay in English in every language, because the app itself is in English.

## Deploying

Run `npm run build` and put the `dist/` folder on any static host. For your own server, `deploy/` has ready-made
configs with HTTPS and security headers:

- **Docker** (easiest on a VPS): see the top of `deploy/Dockerfile` for the build and run commands.
- **Caddy**: `deploy/Caddyfile`.
- **nginx**: `deploy/nginx.conf`.
- **Netlify / Cloudflare Pages**: publish `dist/`. The headers come from `public/_headers`.

The license key page and the download counter call the Pipkite API, so on your server `/api/` has to be
forwarded to it (the configs in `deploy/` already do this).

After going live, submit `https://your-domain/sitemap-index.xml` to Google Search Console.

## Good to know

- The site has a strict security policy: don't add inline `style="..."` attributes or hand-written `<script>`
  tags, they will be blocked. Use classes and Astro components instead.
- Use logical CSS classes (`ms-*`, `me-*`, `text-start`) instead of left/right ones, so Arabic stays correct.
- Before changing anything on the privacy page, keep it in sync with what the site actually does.
