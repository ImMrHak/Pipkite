// copies ../dist release files into public/downloads (local testing; in prod use PUBLIC_DOWNLOAD_BASE)
import { copyFileSync, existsSync, mkdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const src = join(root, '..', 'dist');
const dest = join(root, 'public', 'downloads');
const files = ['Pipkite-1.0.0-setup.exe', 'Pipkite-1.0.0-win-x64.zip', 'Pipkite-1.0.0-win-x64.zip.sha256'];

mkdirSync(dest, { recursive: true });
for (const f of files) {
  const from = join(src, f);
  if (!existsSync(from)) {
    console.warn(`missing: ${from} (run installer\\package.ps1 first)`);
    continue;
  }
  copyFileSync(from, join(dest, f));
  console.log(`copied ${f}`);
}
