// inlined in <head> to avoid a theme flash. astro.config.mjs hashes this string for the CSP,
// so keep it here and not in the layout.
export const THEME_SCRIPT = `(() => {
  const root = document.documentElement;
  root.classList.remove('no-js');
  let theme;
  try { theme = localStorage.getItem('theme'); } catch {}
  if (theme !== 'light' && theme !== 'dark') {
    theme = matchMedia('(prefers-color-scheme: light)').matches ? 'light' : 'dark';
  }
  root.dataset.theme = theme;
  window.syncShots = () => {
    const light = root.dataset.theme === 'light';
    document.querySelectorAll('picture[data-themed] source').forEach((s) => {
      s.media = light ? 'all' : 'not all';
    });
  };
  document.addEventListener('DOMContentLoaded', window.syncShots);
})();`;
