import { ar } from './ar';
import { type Lang, localize } from './config';
import { en, type Dict } from './en';
import { es } from './es';
import { fr } from './fr';

export * from './config';
export type { Dict };

const DICTS: Record<Lang, Dict> = { en, fr, es, ar };

export const t = (lang: Lang): Dict => DICTS[lang];

export const localizeHtml = (lang: Lang, html: string) =>
  html.replace(/href="(\/[^"]*)"/g, (_, path: string) => `href="${localize(lang, path)}"`);
