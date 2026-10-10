// og.png + icons. npm run images
import sharp from 'sharp';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const pub = (f) => join(root, 'public', f);

const logo = (size, radius) => `
<svg xmlns="http://www.w3.org/2000/svg" width="${size}" height="${size}" viewBox="0 0 32 32">
  <rect width="32" height="32" rx="${radius}" fill="#2f6fde"/>
  <path d="m9 10 6 6-6 6M17 10l6 6-6 6" fill="none" stroke="#fff" stroke-width="2.6" stroke-linecap="round" stroke-linejoin="round"/>
</svg>`;

await sharp(Buffer.from(logo(180, 0))).png().toFile(pub('apple-touch-icon.png'));
await sharp(Buffer.from(logo(192, 7))).png().toFile(pub('icon-192.png'));
await sharp(Buffer.from(logo(512, 7))).png().toFile(pub('icon-512.png'));
await sharp(Buffer.from(logo(512, 0))).png().toFile(pub('icon-maskable-512.png'));

const W = 1200;
const H = 630;
const bg = `
<svg xmlns="http://www.w3.org/2000/svg" width="${W}" height="${H}">
  <defs>
    <radialGradient id="g1" cx="0.2" cy="0.1" r="0.8"><stop offset="0" stop-color="#2f6fde" stop-opacity="0.45"/><stop offset="1" stop-color="#0b0d11" stop-opacity="0"/></radialGradient>
    <radialGradient id="g2" cx="0.9" cy="0.9" r="0.7"><stop offset="0" stop-color="#8b5cf6" stop-opacity="0.30"/><stop offset="1" stop-color="#0b0d11" stop-opacity="0"/></radialGradient>
    <linearGradient id="t" x1="0" x2="1"><stop offset="0" stop-color="#4f8ff7"/><stop offset="1" stop-color="#a78bfa"/></linearGradient>
  </defs>
  <rect width="${W}" height="${H}" fill="#0b0d11"/>
  <rect width="${W}" height="${H}" fill="url(#g1)"/>
  <rect width="${W}" height="${H}" fill="url(#g2)"/>
  <g font-family="Segoe UI, Inter, Arial, sans-serif">
    <g transform="translate(64 64)">${logo(56, 8).replace('<svg', '<svg x="0" y="0"')}</g>
    <text x="136" y="104" font-size="34" font-weight="600" fill="#eef1f6">Pipkite</text>
    <text x="64" y="232" font-size="64" font-weight="700" fill="#eef1f6" letter-spacing="-1.5">Telegram signals</text>
    <text x="64" y="308" font-size="64" font-weight="700" fill="#eef1f6" letter-spacing="-1.5">to MetaTrader 5</text>
    <text x="64" y="384" font-size="64" font-weight="700" fill="url(#t)" letter-spacing="-1.5">in 4.3 ms</text>
    <text x="64" y="452" font-size="26" fill="#a3abba">Free for Windows · AI review included</text>
    <rect x="64" y="500" width="214" height="56" rx="14" fill="#2f6fde"/>
    <text x="171" y="536" font-size="24" font-weight="600" fill="#fff" text-anchor="middle">100% free</text>
  </g>
</svg>`;

const shot = await sharp(join(root, 'src/assets/screenshots/dark-dashboard.png'))
  .resize({ width: 640 })
  .toBuffer();
const shotMeta = await sharp(shot).metadata();
const frame = Buffer.from(
  `<svg xmlns="http://www.w3.org/2000/svg" width="${shotMeta.width + 4}" height="${shotMeta.height + 4}"><rect x="1" y="1" width="${shotMeta.width + 2}" height="${shotMeta.height + 2}" rx="12" fill="#262b35"/></svg>`,
);

await sharp(Buffer.from(bg))
  .composite([
    { input: frame, left: 618, top: 168 },
    { input: shot, left: 620, top: 170 },
  ])
  .png({ compressionLevel: 9, palette: false })
  .toFile(pub('og.png'));

console.log('wrote og.png, apple-touch-icon.png, icon-192.png, icon-512.png, icon-maskable-512.png');
