// update on every release

export const SITE_NAME = 'Pipkite';
export const VERSION = '1.0.1';
export const CONTENT_UPDATED = '2026-10-11';

// empty = hidden
export const CONTACT_EMAIL = 'support@pipkite.com';
export const GITHUB_URL = 'https://github.com/ImMrHak/Pipkite';
// ideas and improvement requests arrive as GitHub issues
export const IDEAS_URL = `${GITHUB_URL}/issues/new`;
// official profiles for schema.org sameAs
export const SAME_AS: string[] = [GITHUB_URL];
export const RELEASE_DATE = '2026-11-01';
// every release, newest first: the changelog page lists them with the notes in i18n changelog.releases
export const RELEASES = [
  { version: '1.0.1', date: '2026-11-01' },
  { version: '1.0.0', date: '2026-09-30' },
] as const;

const downloadBase = (import.meta.env.PUBLIC_DOWNLOAD_BASE ?? '/downloads').replace(/\/$/, '');
export const API_BASE = (import.meta.env.PUBLIC_API_BASE || '/api/v1').replace(/\/$/, '');

// hide the public download counter until it reaches this (0 = always show)
export const DOWNLOADS_PUBLIC_MIN = Number(import.meta.env.PUBLIC_DOWNLOADS_MIN ?? 0);

export const DOWNLOADS = {
  installer: {
    name: `Pipkite-${VERSION}-setup.exe`,
    url: `${downloadBase}/Pipkite-${VERSION}-setup.exe`,
    size: '29.2 MB',
    sha256: 'aa144b106b5199fda34ee9cd3ee30c7a634f4c3bf919aef91ac016a5ffdea087',
  },
  portable: {
    name: `Pipkite-${VERSION}-win-x64.zip`,
    url: `${downloadBase}/Pipkite-${VERSION}-win-x64.zip`,
    size: '33.1 MB',
    sha256: 'a7ede86eb6722e76ad414f9bdd02c091a31fe78d710f4ca72cdf81d6209bcecf',
  },
} as const;

// Ryzen 5 7600X, Win 11, Release x64, journal on
export const BENCH = {
  ownOverheadP50Ms: 4.3,
  ownOverheadP95Ms: 5.2,
  ownOverheadBeforeP50Ms: 15.5,
  parseP50Us: 1.6,
  parseP95Us: 5.5,
  parseThroughput: 459_406,
  decisionP95Ms: 1.04,
  pipeRoundTripUs: 15.1,
  flushesPerOrder: 1,
  flushesBefore: 4,
  machine: 'AMD Ryzen 5 7600X · Windows 11 · NVMe SSD',
};

// our own measurements of other copiers (telegram msg -> order sent), same machine/broker.
// empty = section hidden. these end up public, so only real numbers.
export const COMPARISON: { label: string; p50Ms: number }[] = [
  // { label: 'Copier A', p50Ms: 0 },
  // { label: 'Copier B', p50Ms: 0 },
];
export const COMPARISON_METHOD = '';
