// writes .br and .gz next to every text file in dist/ (nginx serves them with gzip_static)
import { readdirSync, readFileSync, statSync, writeFileSync } from 'node:fs';
import { join, extname } from 'node:path';
import { brotliCompressSync, gzipSync, constants } from 'node:zlib';

const dist = new URL('../dist/', import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, '$1');
const TEXT = new Set(['.html', '.css', '.js', '.mjs', '.svg', '.xml', '.txt', '.json', '.webmanifest', '.map']);
const MIN_BYTES = 1024;

let files = 0;
let before = 0;
let afterBr = 0;

function walk(dir) {
  for (const name of readdirSync(dir)) {
    const p = join(dir, name);
    const st = statSync(p);
    if (st.isDirectory()) {
      walk(p);
      continue;
    }
    if (!TEXT.has(extname(name)) || st.size < MIN_BYTES) continue;
    const buf = readFileSync(p);
    const br = brotliCompressSync(buf, {
      params: {
        [constants.BROTLI_PARAM_QUALITY]: constants.BROTLI_MAX_QUALITY,
        [constants.BROTLI_PARAM_SIZE_HINT]: buf.length,
      },
    });
    const gz = gzipSync(buf, { level: 9 });
    if (br.length < buf.length) writeFileSync(`${p}.br`, br);
    if (gz.length < buf.length) writeFileSync(`${p}.gz`, gz);
    files++;
    before += buf.length;
    afterBr += Math.min(br.length, buf.length);
  }
}

walk(dist);
const kb = (n) => `${(n / 1024).toFixed(0)} KB`;
console.log(`compressed ${files} files: ${kb(before)} -> ${kb(afterBr)} with Brotli`);
