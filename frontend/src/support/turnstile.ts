type Turnstile = {
  render: (container: HTMLElement, options: {
    sitekey: string;
    action: string;
    language: string;
    appearance: 'interaction-only';
    size: 'compact' | 'flexible';
    callback: (token: string) => void;
    'error-callback': () => void;
    'expired-callback': () => void;
    'timeout-callback': () => void;
  }) => string;
  remove: (id: string) => void;
};

const browser = window as Window & { turnstile?: Turnstile };
let pending: Promise<Turnstile> | undefined;

function loadTurnstile(): Promise<Turnstile> {
  if (browser.turnstile) return Promise.resolve(browser.turnstile);
  pending ??= new Promise<Turnstile>((resolve, reject) => {
    const script = document.createElement('script');
    script.src = 'https://challenges.cloudflare.com/turnstile/v0/api.js?render=explicit';
    script.async = true;
    const fail = () => {
      script.remove();
      reject(new Error('verification'));
    };
    const timer = window.setTimeout(fail, 10_000);
    script.onload = () => {
      window.clearTimeout(timer);
      if (browser.turnstile) resolve(browser.turnstile);
      else fail();
    };
    script.onerror = () => {
      window.clearTimeout(timer);
      fail();
    };
    document.head.append(script);
  }).catch((error: unknown) => {
    pending = undefined;
    throw error;
  });
  return pending;
}

export async function verifyVisitor(container: HTMLElement, sitekey: string, language: string) {
  const turnstile = await loadTurnstile();
  let id: string | undefined;
  let timer: number | undefined;
  try {
    return await new Promise<string>((resolve, reject) => {
      const fail = () => reject(new Error('verification'));
      timer = window.setTimeout(fail, 90_000);
      const size = (container.parentElement?.clientWidth ?? 0) < 340 ? 'compact' : 'flexible';
      id = turnstile.render(container, {
        sitekey, language, action: 'support_ticket', appearance: 'interaction-only', size,
        callback: resolve, 'error-callback': fail, 'expired-callback': fail, 'timeout-callback': fail,
      });
      container.scrollIntoView({ block: 'nearest' });
    });
  } finally {
    window.clearTimeout(timer);
    if (id) turnstile.remove(id);
    container.replaceChildren();
  }
}
