import { LOCALE_META, type Lang } from './config';

// western digits on the arabic pages too, that's how prices are written
const intl = (lang: Lang) => (lang === 'ar' ? 'ar-u-nu-latn' : LOCALE_META[lang].intl);

export const num = (lang: Lang, n: number, dec = 0) =>
  n.toLocaleString(intl(lang), { minimumFractionDigits: dec, maximumFractionDigits: dec });

export const date = (lang: Lang, iso: string) =>
  new Date(`${iso}T12:00:00Z`).toLocaleDateString(intl(lang), { year: 'numeric', month: 'long', day: 'numeric', timeZone: 'UTC' });
