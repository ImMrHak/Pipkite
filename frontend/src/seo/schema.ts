// schema.org JSON-LD. no ratings/reviews until we have real ones.
import { CONTACT_EMAIL, CONTENT_UPDATED, DOWNLOADS, RELEASE_DATE, SAME_AS, SITE_NAME, VERSION } from '../config';
import { LOCALES, localize, t, type Lang } from '../i18n';

type Thing = Record<string, unknown>;

export const abs = (site: URL, path: string) => new URL(path, site).href;
const id = (site: URL, frag: string) => `${site.origin}/#${frag}`;

export function organization(site: URL): Thing {
  return {
    '@type': 'Organization',
    '@id': id(site, 'organization'),
    name: SITE_NAME,
    url: abs(site, '/'),
    logo: { '@type': 'ImageObject', url: abs(site, '/icon-512.png'), width: 512, height: 512 },
    ...(SAME_AS.length ? { sameAs: SAME_AS } : {}),
    ...(CONTACT_EMAIL
      ? { contactPoint: { '@type': 'ContactPoint', contactType: 'customer support', email: CONTACT_EMAIL, availableLanguage: [...LOCALES] } }
      : {}),
  };
}

export function website(site: URL, lang: Lang): Thing {
  return {
    '@type': 'WebSite',
    '@id': id(site, 'website'),
    name: SITE_NAME,
    url: abs(site, localize(lang, '/')),
    inLanguage: [...LOCALES],
    publisher: { '@id': id(site, 'organization') },
  };
}

export function softwareApplication(site: URL, lang: Lang, screenshots: string[]): Thing {
  const d = t(lang);
  return {
    '@type': 'SoftwareApplication',
    '@id': id(site, 'software'),
    name: SITE_NAME,
    alternateName: 'Pipkite Telegram to MT5 copier',
    description: d.meta.appDescription,
    url: abs(site, localize(lang, '/')),
    applicationCategory: 'FinanceApplication',
    applicationSubCategory: 'Trade copier',
    operatingSystem: 'Windows 10, Windows 11 (64-bit)',
    softwareVersion: VERSION,
    datePublished: RELEASE_DATE,
    downloadUrl: abs(site, DOWNLOADS.installer.url),
    installUrl: abs(site, localize(lang, '/download')),
    fileSize: DOWNLOADS.installer.size,
    softwareRequirements: 'MetaTrader 5, a Telegram account, .NET Framework 4.8',
    memoryRequirements: '4 GB',
    releaseNotes: abs(site, localize(lang, '/changelog')),
    screenshot: screenshots,
    isAccessibleForFree: true,
    // the app itself is english only
    inLanguage: 'en',
    offers: {
      '@type': 'Offer',
      price: '0',
      priceCurrency: 'USD',
      availability: 'https://schema.org/InStock',
      url: abs(site, localize(lang, '/download')),
    },
    featureList: d.meta.featureList,
    publisher: { '@id': id(site, 'organization') },
  };
}

export function webPage(site: URL, lang: Lang, path: string, name: string, description: string, extra: Thing = {}): Thing {
  const url = abs(site, localize(lang, path));
  return {
    '@type': 'WebPage',
    '@id': `${url}#webpage`,
    url,
    name,
    description,
    inLanguage: lang,
    isPartOf: { '@id': id(site, 'website') },
    dateModified: CONTENT_UPDATED,
    ...extra,
  };
}

export function breadcrumbs(site: URL, lang: Lang, trail: { name: string; path: string }[]): Thing {
  return {
    '@type': 'BreadcrumbList',
    itemListElement: [{ name: t(lang).common.home, path: '/' }, ...trail].map((c, i) => ({
      '@type': 'ListItem',
      position: i + 1,
      name: c.name,
      item: abs(site, localize(lang, c.path)),
    })),
  };
}

export function faqPage(site: URL, lang: Lang, path: string, faqs: { q: string; a: string }[]): Thing {
  return {
    '@type': 'FAQPage',
    '@id': `${abs(site, localize(lang, path))}#faq`,
    inLanguage: lang,
    mainEntity: faqs.map((f) => ({
      '@type': 'Question',
      name: f.q,
      acceptedAnswer: { '@type': 'Answer', text: f.a },
    })),
  };
}

export function article(
  site: URL,
  lang: Lang,
  path: string,
  headline: string,
  description: string,
  image: string,
  datePublished: string,
): Thing {
  const url = abs(site, localize(lang, path));
  return {
    '@type': 'TechArticle',
    '@id': `${url}#article`,
    headline,
    description,
    image,
    datePublished,
    dateModified: CONTENT_UPDATED,
    inLanguage: lang,
    author: { '@id': id(site, 'organization') },
    publisher: { '@id': id(site, 'organization') },
    mainEntityOfPage: { '@id': `${url}#webpage` },
    about: { '@id': id(site, 'software') },
  };
}

export const graph = (...things: Thing[]) => ({ '@context': 'https://schema.org', '@graph': things });
