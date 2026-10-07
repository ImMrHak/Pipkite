// subsets Noto Sans Arabic to the characters used in ar.ts (~160 KB -> ~16 KB). runs before every build.
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import subsetFont from 'subset-font';

const require = createRequire(import.meta.url);
const root = new URL('..', import.meta.url);
const src = require.resolve('@fontsource-variable/noto-sans-arabic/files/noto-sans-arabic-arabic-wght-normal.woff2');

const ARABIC = [[0x0600, 0x06ff], [0x0750, 0x077f], [0xfb50, 0xfdff], [0xfe70, 0xfeff]];
const text = readFileSync(new URL('src/i18n/ar.ts', root), 'utf8');
const used = new Set([...text].filter((ch) => ARABIC.some(([lo, hi]) => ch.codePointAt(0) >= lo && ch.codePointAt(0) <= hi)));
// punctuation, tatweel and arabic-indic digits (the parser demo uses them)
for (let c = 0x0660; c <= 0x066c; c++) used.add(String.fromCharCode(c));
for (const c of [0x060c, 0x061b, 0x061f, 0x0640]) used.add(String.fromCharCode(c));
const chars = [...used].join('');

const full = readFileSync(src);
const subset = await subsetFont(full, chars, { targetFormat: 'woff2', variationAxes: { wght: { min: 400, max: 700 } } });

const outDir = new URL('src/assets/fonts/', root);
mkdirSync(outDir, { recursive: true });
writeFileSync(new URL('noto-sans-arabic-subset.woff2', outDir), subset);
console.log(`arabic font: ${used.size} characters, ${(full.length / 1024).toFixed(0)} KB -> ${(subset.length / 1024).toFixed(0)} KB`);
