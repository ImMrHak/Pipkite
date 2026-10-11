// @ts-check
import { defineConfig } from 'astro/config';
import { loadEnv } from 'vite';

import tailwindcss from '@tailwindcss/vite';
import sitemap from '@astrojs/sitemap';
import { createHash } from 'node:crypto';
import { THEME_SCRIPT } from './src/theme-script.mjs';

const env = loadEnv(process.env.NODE_ENV ?? 'production', process.cwd(), '');

// must be the real domain in prod (canonicals, sitemap, og)
const site = (env.SITE_URL || 'https://pipkite.com').replace(/\/$/, '');

// only needed when the API lives on another origin
const apiOrigin = /^https?:\/\//.test(env.PUBLIC_API_BASE ?? '') ? new URL(env.PUBLIC_API_BASE).origin : null;

// astro hashes its own scripts, this one is ours
const themeScriptHash = `sha256-${createHash('sha256').update(THEME_SCRIPT).digest('base64')}`;

const LOCALES = { en: 'en', fr: 'fr', es: 'es', ar: 'ar' };

export default defineConfig({
  site,
  trailingSlash: 'never',
  build: {
    format: 'file',
  },
  compressHTML: true,

  // the desktop app links to /get-license
  redirects: {
    '/get-license': '/license',
  },

  security: {
    // CSP as a <meta> tag. frame-ancestors/HSTS can't go in a meta, see deploy/
    csp: {
      algorithm: 'SHA-256',
      scriptDirective: {
        hashes: [/** @type {`sha256-${string}`} */ (themeScriptHash)],
      },
      directives: [
        "default-src 'self'",
        "img-src 'self' data:",
        "font-src 'self'",
        `connect-src 'self'${apiOrigin ? ` ${apiOrigin}` : ''}`,
        "manifest-src 'self'",
        "media-src 'self'",
        "object-src 'none'",
        "base-uri 'self'",
        `form-action 'self'${apiOrigin ? ` ${apiOrigin}` : ''}`,
        "frame-src 'none'",
        'upgrade-insecure-requests',
      ],
    },
  },

  vite: {
    plugins: [tailwindcss()],
    build: {
      assetsInlineLimit: 0,
    },
    server: {
      // dev: proxy to the local backend
      proxy: {
        '/api': 'http://localhost:8080',
      },
    },
  },

  integrations: [
    sitemap({
      filter: (page) => !page.includes('/get-license') && !/\/404$/.test(page),
      lastmod: new Date(),
      i18n: { defaultLocale: 'en', locales: LOCALES },
      serialize(item) {
        const path = new URL(item.url).pathname.replace(/^\/(fr|es|ar)(?=\/|$)/, '') || '/';
        item.priority = path === '/' ? 1.0 : ['/download', '/telegram-to-mt5-copier'].includes(path) ? 0.9 : 0.7;
        return item;
      },
    }),
  ],
});
