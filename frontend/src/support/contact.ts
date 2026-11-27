import type { Dict } from '../i18n';
import { verifyVisitor } from './turnstile';

type ContactSession = {
  token: string;
  expires: number;
  pending?: { fingerprint: string; requestId: string };
};

const STORAGE = 'pipkite.support-request.v1';
const TTL = 30 * 60_000;

function hex(bytes: Uint8Array) {
  return Array.from(bytes, (byte) => byte.toString(16).padStart(2, '0')).join('');
}

function readSession(): ContactSession {
  const now = Date.now();
  try {
    const saved = JSON.parse(sessionStorage.getItem(STORAGE) ?? 'null') as ContactSession | null;
    if (saved && /^[0-9a-f]{64}$/.test(saved.token) && saved.expires > now && saved.expires <= now + TTL) return saved;
    sessionStorage.removeItem(STORAGE);
  } catch { /* Storage is optional. */ }
  return { token: hex(crypto.getRandomValues(new Uint8Array(32))), expires: now + TTL };
}

export function mountContactForm(root: HTMLElement) {
  const form = root.querySelector<HTMLFormElement>('[data-contact-form]')!;
  const submit = form.querySelector<HTMLButtonElement>('button[type=submit]')!;
  const status = form.querySelector<HTMLElement>('[data-contact-status]')!;
  const verification = form.querySelector<HTMLElement>('[data-verification]')!;
  const receipt = root.querySelector<HTMLElement>('[data-contact-receipt]')!;
  const reference = receipt.querySelector<HTMLElement>('[data-contact-reference]')!;
  const copy = JSON.parse(form.dataset.statusText!) as Dict['contact']['status'];

  function notice(message: string, isError = false) {
    status.textContent = message;
    status.toggleAttribute('data-error', isError);
    if (isError) status.focus();
  }

  if (!globalThis.crypto?.subtle || !crypto.randomUUID) {
    notice(copy.failed, true);
    return;
  }

  let session = readSession();
  let sending = false;

  function remember() {
    session.expires = Date.now() + TTL;
    try { sessionStorage.setItem(STORAGE, JSON.stringify(session)); } catch { /* Storage is optional. */ }
  }

  async function post(body: object) {
    const response = await fetch(form.dataset.endpoint!, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json', 'X-Pipkite-Support-Session': session.token },
      credentials: 'omit',
      referrerPolicy: 'no-referrer',
      body: JSON.stringify(body),
      signal: AbortSignal.timeout(30_000),
    });
    const data = await response.json().catch(() => null);
    if (response.status !== 202) {
      const code = response.status === 429 ? 'limited' : typeof data?.code === 'string' ? data.code : 'unavailable';
      throw new Error(code);
    }
    if (typeof data?.reference !== 'string' || !/^PIP-[0-9A-F]{16}$/.test(data.reference)) throw new Error('unavailable');
    return data.reference as string;
  }

  async function sendTicket(body: object) {
    try {
      return await post(body);
    } catch (error) {
      const sitekey = form.dataset.sitekey;
      if (!(error instanceof Error) || error.message !== 'verification_required' || !sitekey) throw error;
      const token = await verifyVisitor(verification, sitekey, form.dataset.lang!);
      return await post({ ...body, verification: token });
    }
  }

  form.addEventListener('submit', async (event) => {
    event.preventDefault();
    if (sending) return;
    if (session.expires <= Date.now()) session = readSession();

    const fields = new FormData(form);
    const body = {
      name: String(fields.get('name') ?? '').trim(),
      email: String(fields.get('email') ?? '').trim(),
      category: fields.get('category'),
      message: String(fields.get('message') ?? '').trim(),
      website: fields.get('website'),
    };
    if (body.message.length < 10) {
      notice(copy.invalid, true);
      return;
    }

    sending = true;
    submit.disabled = true;
    notice(copy.sending);
    try {
      // Keep the same id on retry without keeping contact details in browser storage.
      const digest = await crypto.subtle.digest('SHA-256', new TextEncoder().encode(session.token + JSON.stringify(body)));
      const fingerprint = hex(new Uint8Array(digest));
      const pending = session.pending;
      const requestId = pending?.fingerprint === fingerprint ? pending.requestId : crypto.randomUUID();
      session.pending = { fingerprint, requestId };
      remember();

      reference.textContent = await sendTicket({ ...body, requestId });
      delete session.pending;
      remember();
      form.reset();
      notice('');
      form.hidden = true;
      receipt.hidden = false;
      receipt.focus();
    } catch (error) {
      const code = error instanceof Error ? error.message : '';
      let message = copy.failed;
      if (code.startsWith('verification')) message = copy.verification;
      else if (code === 'limited') message = copy.limited;
      else if (code.startsWith('invalid')) message = copy.invalid;
      notice(message, true);
    } finally {
      sending = false;
      submit.disabled = false;
    }
  });

  receipt.querySelector('[data-contact-restart]')!.addEventListener('click', () => {
    reference.textContent = '';
    receipt.hidden = true;
    form.hidden = false;
    form.querySelector<HTMLInputElement>('[name=name]')!.focus();
  });
  submit.disabled = false;
}
