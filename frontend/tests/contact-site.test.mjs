import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import test from 'node:test';

const read = (file) => readFile(new URL(`../dist/${file}`, import.meta.url), 'utf8');
const alternateLinks = (markup) => [...markup.matchAll(/hreflang="([^"]+)" href="([^"]+)"/g)]
  .map(([, language, href]) => [language, href])
  .sort();

const locales = [
  ['en', '', 'index.html', 'Contact Us'],
  ['fr', '/fr', 'fr.html', 'Contactez-nous'],
  ['es', '/es', 'es.html', 'Contáctanos'],
  ['ar', '/ar', 'ar.html', 'تواصل معنا'],
];

for (const [locale, prefix, home, label] of locales) {
  test(`publishes a standalone contact page and navigation in ${locale}`, async () => {
    const path = `${prefix}/contact`;
    const homepage = await read(home);
    const link = new RegExp(`<a\\b[^>]*href="${path}"[^>]*>\\s*${label}\\s*</a>`, 'g');
    assert.equal([...homepage.matchAll(link)].length, 3); // desktop, mobile, footer
    assert.doesNotMatch(homepage, /data-support-widget|data-contact-form/);

    const page = await read(`${path.slice(1)}.html`);
    const title = page.match(/<title>([^<]+)<\/title>/)?.[1];
    assert.equal(title?.match(/Pipkite/g)?.length, 1);
    assert.match(page, /<meta name="description" content="[^"]+"/);
    assert.match(page, /<meta name="robots" content="index, follow/);
    const canonicalUrl = page.match(/<link rel="canonical" href="([^"]+)"/)?.[1];
    assert.ok(canonicalUrl, `Canonical URL for ${path}`);
    const canonical = new URL(canonicalUrl);
    assert.equal(canonical.pathname, path);
    const alternates = locales.map(([language, base]) => [language, new URL(`${base}/contact`, canonical).href]);
    alternates.push(['x-default', new URL('/contact', canonical).href]);
    assert.deepEqual(alternateLinks(page), alternates.sort());

    const sitemap = await read('sitemap-0.xml');
    const entry = sitemap.split('</url>').find((item) => item.includes(`<loc>${canonical.href}</loc>`));
    assert.ok(entry, `Sitemap entry for ${canonical.href}`);
    assert.deepEqual(alternateLinks(entry), alternates);

    assert.match(page, /"@type":"ContactPage"/);
    assert.match(page, /"@type":"BreadcrumbList"/);
    assert.ok(page.includes(`<h1`) && page.includes(`>${label}</h1>`));
    assert.match(page, /<form\b[^>]*method="post"[^>]*data-contact-form/);
    for (const name of ['name', 'email', 'category', 'message']) assert.ok(page.includes(`name="${name}"`));
    assert.doesNotMatch(page, /data-support-widget|includeConversation/);

    const formStart = page.indexOf('data-contact-form');
    const formEnd = page.indexOf('</form>', formStart);
    assert.ok(formEnd >= 0);
    assert.ok(!page.slice(formStart, formEnd).includes(`href="${prefix}/privacy#website-support"`));
    const supportLinks = page.slice(formEnd);
    assert.match(supportLinks, /href="mailto:support@pipkite\.com"/);
    assert.ok(supportLinks.includes(`href="${prefix}/telegram-to-mt5-copier#troubleshooting"`));
    assert.doesNotMatch(page, /contact-message-hint|type="file"/);
  });
}
