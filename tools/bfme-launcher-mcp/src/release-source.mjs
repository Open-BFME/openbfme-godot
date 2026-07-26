// Resolve the publish/update target at runtime.
//
// Mirrors tools/release_source.py. The target repository has moved once and is
// expected to move again, so no owner/name literal may appear in code, CI, or
// the launcher. Everything reads config/release-source.json, overridable by
// environment variables.
//
// There is deliberately NO built-in default repository: an unset target throws
// so an update attempt fails loudly instead of pointing somewhere stale.

import { readFileSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const HERE = path.dirname(fileURLToPath(import.meta.url));
// src/ -> bfme-launcher-mcp/ -> tools/ -> repo root
export const REPO_ROOT = path.resolve(HERE, "..", "..", "..");
export const CONFIG_RELATIVE = path.join("config", "release-source.json");

// owner/name only: no scheme, no host, no path traversal.
const REPOSITORY_PATTERN = /^[A-Za-z0-9][A-Za-z0-9._-]*\/[A-Za-z0-9][A-Za-z0-9._-]*$/;

// Hosts we are willing to talk to. Extend deliberately, never from config alone.
export const ALLOWED_HOSTS = Object.freeze(["github.com"]);

export class ReleaseSourceError extends Error {}
export class ReleaseSourceUnset extends ReleaseSourceError {}

export function loadConfig(root = REPO_ROOT) {
  const file = path.join(root, CONFIG_RELATIVE);
  let raw;
  try {
    raw = readFileSync(file, "utf8");
  } catch {
    return {};
  }
  let parsed;
  try {
    parsed = JSON.parse(raw);
  } catch (error) {
    throw new ReleaseSourceError(`${file} is not valid JSON: ${error.message}`);
  }
  if (!parsed || typeof parsed !== "object" || Array.isArray(parsed)) {
    throw new ReleaseSourceError(`${file} must contain a JSON object.`);
  }
  return parsed;
}

/**
 * @returns {{host: string, repository: string, channel: string,
 *            cloneUrl: string, releasesApi: string,
 *            latestAssetUrl: (asset: string) => string}}
 */
export function resolveReleaseSource(environment = process.env, root = REPO_ROOT) {
  const config = loadConfig(root);

  const host = (environment.OPENBFME_RELEASE_HOST || config.host || "github.com").trim();
  const channel = (environment.OPENBFME_RELEASE_CHANNEL || config.channel || "stable").trim();
  const repository = (environment.OPENBFME_RELEASE_REPOSITORY || config.repository || "").trim();

  if (!repository) {
    throw new ReleaseSourceUnset(
      "The publish/update target is not configured. Set " +
        "OPENBFME_RELEASE_REPOSITORY=<owner>/<name>, or fill in the " +
        `"repository" field of ${CONFIG_RELATIVE.split(path.sep).join("/")}. ` +
        "There is no default on purpose: updating from the wrong repository is not recoverable.",
    );
  }
  if (!REPOSITORY_PATTERN.test(repository)) {
    throw new ReleaseSourceError(
      `Invalid repository "${repository}"; expected "<owner>/<name>" with no scheme, host, or path segments.`,
    );
  }
  if (!ALLOWED_HOSTS.includes(host)) {
    throw new ReleaseSourceError(
      `Host "${host}" is not in the allow-list ${JSON.stringify(ALLOWED_HOSTS)}.`,
    );
  }

  return Object.freeze({
    host,
    repository,
    channel,
    cloneUrl: `https://${host}/${repository}.git`,
    releasesApi: `https://api.${host}/repos/${repository}/releases`,
    latestAssetUrl: (asset) => `https://${host}/${repository}/releases/latest/download/${asset}`,
  });
}

export function isReleaseSourceConfigured(environment = process.env, root = REPO_ROOT) {
  try {
    resolveReleaseSource(environment, root);
    return true;
  } catch {
    return false;
  }
}
