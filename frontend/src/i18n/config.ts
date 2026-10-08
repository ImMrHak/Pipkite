// en at the root, the rest under /fr, /es, /ar
export const LOCALES = ['en', 'fr', 'es', 'ar'] as const;
export type Lang = (typeof LOCALES)[number];
export const DEFAULT_LANG: Lang = 'en';

export const LOCALE_META: Record<Lang, { name: string; dir: 'ltr' | 'rtl'; og: string; intl: string }> = {
  en: { name: 'English', dir: 'ltr', og: 'en_US', intl: 'en-US' },
  fr: { name: 'Français', dir: 'ltr', og: 'fr_FR', intl: 'fr-FR' },
  es: { name: 'Español', dir: 'ltr', og: 'es_ES', intl: 'es-ES' },
  ar: { name: 'العربية', dir: 'rtl', og: 'ar_AR', intl: 'ar' },
};

export const isLang = (v: unknown): v is Lang => typeof v === 'string' && (LOCALES as readonly string[]).includes(v);

export function localize(lang: Lang, path: string): string {
  if (lang === DEFAULT_LANG) return path;
  if (path === '/') return `/${lang}`;
  if (path.startsWith('/#')) return `/${lang}${path.slice(1)}`;
  return `/${lang}${path}`;
}

export function unlocalize(pathname: string): string {
  const m = pathname.match(/^\/(fr|es|ar)(\/.*)?$/);
  if (!m) return pathname || '/';
  return m[2] || '/';
}

export const prefixedLocalePaths = () =>
  LOCALES.filter((l) => l !== DEFAULT_LANG).map((lang) => ({ params: { lang } }));
