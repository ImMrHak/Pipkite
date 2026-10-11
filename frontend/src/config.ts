// update on every release

export const SITE_NAME = 'Pipkite';
export const VERSION = '1.0.0';
export const CONTENT_UPDATED = '2026-10-11';

// empty = hidden
export const CONTACT_EMAIL = '';
// official profiles for schema.org sameAs
export const SAME_AS: string[] = [];
export const RELEASE_DATE = '2026-09-30';

const downloadBase = (import.meta.env.PUBLIC_DOWNLOAD_BASE ?? '/downloads').replace(/\/$/, '');
export const API_BASE = (import.meta.env.PUBLIC_API_BASE ?? '/api/v1').replace(/\/$/, '');

// hide the public download counter until it reaches this (0 = always show)
export const DOWNLOADS_PUBLIC_MIN = Number(import.meta.env.PUBLIC_DOWNLOADS_MIN ?? 500);

export const DOWNLOADS = {
  installer: {
    name: `Pipkite-${VERSION}-setup.exe`,
    url: `${downloadBase}/Pipkite-${VERSION}-setup.exe`,
    size: '30.9 MB',
    sha256: 'b9211f78786d8fb7fa1a7564eb1ddd3559f846a672ffe3d11c8d6919cde05fdd',
  },
  portable: {
    name: `Pipkite-${VERSION}-win-x64.zip`,
    url: `${downloadBase}/Pipkite-${VERSION}-win-x64.zip`,
    size: '35.1 MB',
    sha256: 'dd654642d07c76b2c30b0837b7713d8d9de9824059e9edf7981d2d505c4b8631',
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
