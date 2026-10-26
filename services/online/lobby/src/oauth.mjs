// OAuth provider transport for SOCIAL-2b.  This module deliberately owns no
// identity state: exchange returns a raw subject in memory; callers hash it before persistence.
// ONLINEMENU-1 (owner 2026-10-20): Discord sign-in only (scope identify); the Google client was dropped.

const encoder = new TextEncoder();
const decoder = new TextDecoder();
const REDIRECT_URI = 'http://127.0.0.1:43827/oauth/callback';
const MAX_PROVIDER_BYTES = 64 * 1024;
const PROVIDER_TIMEOUT_MS = 5000;
const ASCII = /^[\x20-\x7e]+$/;
const ACCESS_TOKEN = /^[\x20-\x7e]{1,4096}$/;
const DISCORD_SUBJECT = /^\d{1,20}$/;

export const OAUTH_PROVIDERS = Object.freeze(['discord']);

export class OAuthProviderFailure extends Error {
  constructor() {
    super('the OAuth provider is unavailable');
    this.name = 'OAuthProviderFailure';
  }
}

function secret(name, value, maximum) {
  if (typeof value !== 'string' || value.length < 1 || value.length > maximum || !ASCII.test(value)) {
    throw new Error(`${name} must be printable ASCII`);
  }
  return value;
}

function pair(env, provider, idName, secretName) {
  const id = env[idName] ?? null;
  const clientSecret = env[secretName] ?? null;
  if ((id === null) !== (clientSecret === null)) throw new Error(`${provider} OAuth client id and secret must be configured together`);
  if (id === null) return null;
  return {
    id: secret(idName, String(id), 255),
    secret: secret(secretName, String(clientSecret), 512),
  };
}

export function oauthConfigFromEnv(env) {
  return {
    discord: pair(env, 'Discord', 'OAUTH_DISCORD_CLIENT_ID', 'OAUTH_DISCORD_CLIENT_SECRET'),
  };
}

export function providerConfig(config, provider) {
  if (!OAUTH_PROVIDERS.includes(provider)) return null;
  return config?.[provider] ?? null;
}

export function authorizationUrl(provider, clientId, state, challenge) {
  if (!OAUTH_PROVIDERS.includes(provider)) throw new Error('unknown OAuth provider');
  const endpoint = 'https://discord.com/oauth2/authorize';
  const params = new URLSearchParams({
    client_id: clientId,
    response_type: 'code',
    redirect_uri: REDIRECT_URI,
    scope: 'identify',
    prompt: 'consent',
    state,
    code_challenge: challenge,
    code_challenge_method: 'S256',
  });
  return `${endpoint}?${params}`;
}

export function callbackRedirectUri() {
  return REDIRECT_URI;
}

function abortSignal(signal) {
  const controller = new AbortController();
  let cancel = () => {};
  const abort = () => controller.abort();
  if (signal?.aborted) controller.abort();
  else signal?.addEventListener('abort', abort, { once: true });
  let timeoutResolve;
  const timeout = new Promise((_, reject) => { timeoutResolve = () => reject(new OAuthProviderFailure()); });
  const timer = setTimeout(() => {
    abort();
    void cancel();
    timeoutResolve();
  }, PROVIDER_TIMEOUT_MS);
  return {
    signal: controller.signal,
    timeout,
    setCancel(fn) { cancel = fn; if (controller.signal.aborted) void cancel(); },
    abort() { abort(); void cancel(); },
    close() {
      clearTimeout(timer);
      signal?.removeEventListener('abort', abort);
    },
  };
}

async function bytes(response, request) {
  const contentLength = Number(response?.headers?.get?.('content-length') ?? 0);
  if (Number.isFinite(contentLength) && contentLength > MAX_PROVIDER_BYTES) {
    request.abort();
    throw new OAuthProviderFailure();
  }
  if (response?.body?.getReader) {
    const reader = response.body.getReader();
    request.setCancel(() => reader.cancel().catch(() => {}));
    const chunks = [];
    let total = 0;
    try {
      while (true) {
        const part = await reader.read();
        if (part.done) break;
        total += part.value.byteLength;
        if (total > MAX_PROVIDER_BYTES) {
          await reader.cancel().catch(() => {});
          throw new OAuthProviderFailure();
        }
        chunks.push(part.value);
      }
    } catch (error) {
      request.abort();
      if (error instanceof OAuthProviderFailure) throw error;
      throw new OAuthProviderFailure();
    } finally {
      request.setCancel(() => {});
    }
    const out = new Uint8Array(total);
    let at = 0;
    for (const chunk of chunks) {
      out.set(chunk, at);
      at += chunk.byteLength;
    }
    return out;
  }
  try {
    request.setCancel(() => response?.body?.cancel?.().catch(() => {}));
    const value = new Uint8Array(await response.arrayBuffer());
    if (value.length > MAX_PROVIDER_BYTES) throw new OAuthProviderFailure();
    return value;
  } catch (error) {
    request.abort();
    if (error instanceof OAuthProviderFailure) throw error;
    throw new OAuthProviderFailure();
  } finally {
    request.setCancel(() => {});
  }
}

async function fetchJson(env, url, options, signal) {
  const fetcher = env.oauthFetch ?? globalThis.fetch;
  if (typeof fetcher !== 'function') throw new OAuthProviderFailure();
  const request = abortSignal(signal);
  try {
    const operation = (async () => {
      const response = await fetcher(url, { ...options, redirect: 'error', signal: request.signal });
      if (!response || !Number.isInteger(response.status) || response.status < 200 || response.status >= 300) {
        request.abort();
        throw new OAuthProviderFailure();
      }
      const raw = await bytes(response, request);
      let value;
      try {
        value = JSON.parse(decoder.decode(raw));
      } catch {
        throw new OAuthProviderFailure();
      }
      if (!value || typeof value !== 'object' || Array.isArray(value)) throw new OAuthProviderFailure();
      return value;
    })();
    return await Promise.race([operation, request.timeout]);
  } catch (error) {
    request.abort();
    if (error instanceof OAuthProviderFailure) throw error;
    throw new OAuthProviderFailure();
  } finally {
    request.close();
  }
}

function accessToken(value) {
  if (typeof value !== 'string' || !ACCESS_TOKEN.test(value)) throw new OAuthProviderFailure();
  return value;
}

async function token(env, provider, cfg, code, verifier, signal) {
  const endpoint = 'https://discord.com/api/oauth2/token';
  const params = new URLSearchParams({
    client_id: cfg.id,
    client_secret: cfg.secret,
    grant_type: 'authorization_code',
    code,
    redirect_uri: REDIRECT_URI,
    code_verifier: verifier,
  });
  const result = await fetchJson(env, endpoint, {
    method: 'POST',
    headers: { accept: 'application/json', 'content-type': 'application/x-www-form-urlencoded' },
    body: params.toString(),
  }, signal);
  if (result.token_type !== 'Bearer') throw new OAuthProviderFailure();
  return accessToken(result.access_token);
}

async function subject(env, provider, bearer, signal) {
  const endpoint = 'https://discord.com/api/users/@me';
  const result = await fetchJson(env, endpoint, {
    method: 'GET',
    headers: { accept: 'application/json', authorization: `Bearer ${bearer}` },
  }, signal);
  const value = result.id;
  if (typeof value !== 'string' || !DISCORD_SUBJECT.test(value)) throw new OAuthProviderFailure();
  return value;
}

export async function exchangeProvider(env, config, provider, code, verifier, signal) {
  const cfg = providerConfig(config, provider);
  if (!cfg) throw new OAuthProviderFailure();
  const bearer = await token(env, provider, cfg, code, verifier, signal);
  return subject(env, provider, bearer, signal);
}

export async function pkceChallenge(env, verifier) {
  let digest;
  if (typeof env.sha256 === 'function') {
    digest = await env.sha256(encoder.encode(verifier));
  } else if (globalThis.crypto?.subtle) {
    digest = new Uint8Array(await globalThis.crypto.subtle.digest('SHA-256', encoder.encode(verifier)));
  } else {
    throw new OAuthProviderFailure();
  }
  if (!(digest instanceof Uint8Array) || digest.length !== 32) throw new OAuthProviderFailure();
  let binary = '';
  for (const byte of digest) binary += String.fromCharCode(byte);
  return btoa(binary).replaceAll('+', '-').replaceAll('/', '_').replace(/=+$/, '');
}
