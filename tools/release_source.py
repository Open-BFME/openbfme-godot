"""Resolve the publish/update target at runtime.

The target repository has moved once and is expected to move again, so no
owner/name literal may appear in code, CI, or the launcher. Everything reads
config/release-source.json, and every field can be overridden by an environment
variable.

Precedence (highest first):

1. ``OPENBFME_RELEASE_REPOSITORY`` / ``OPENBFME_RELEASE_HOST`` /
   ``OPENBFME_RELEASE_CHANNEL``
2. ``config/release-source.json``

There is deliberately **no built-in default repository**. An unset target raises
:class:`ReleaseSourceUnset` so a publish or update attempt fails loudly instead
of silently pointing at a stale or attacker-chosen host.
"""

from __future__ import annotations

import json
import os
import re
from dataclasses import dataclass
from pathlib import Path

CONFIG_RELATIVE = Path("config") / "release-source.json"

#: owner/name, as GitHub accepts them. Anchored: no scheme, no path traversal.
REPOSITORY_PATTERN = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]*/[A-Za-z0-9][A-Za-z0-9._-]*$")

#: Hosts we are willing to talk to. Extend deliberately, never from config alone.
ALLOWED_HOSTS = frozenset({"github.com"})


class ReleaseSourceError(RuntimeError):
    """Base class for release-target resolution failures."""


class ReleaseSourceUnset(ReleaseSourceError):
    """The publish/update target has not been configured yet."""


@dataclass(frozen=True)
class ReleaseSource:
    host: str
    repository: str
    channel: str

    @property
    def clone_url(self) -> str:
        return f"https://{self.host}/{self.repository}.git"

    @property
    def releases_api(self) -> str:
        # api.github.com only makes sense for github.com; other hosts would
        # need their own mapping, which is why ALLOWED_HOSTS is narrow.
        return f"https://api.{self.host}/repos/{self.repository}/releases"

    def latest_asset_url(self, asset: str) -> str:
        return f"https://{self.host}/{self.repository}/releases/latest/download/{asset}"


def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def load_config(root: Path | None = None) -> dict:
    path = (root or repo_root()) / CONFIG_RELATIVE
    if not path.is_file():
        return {}
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as error:
        raise ReleaseSourceError(f"{path} is not valid JSON: {error}") from error
    if not isinstance(data, dict):
        raise ReleaseSourceError(f"{path} must contain a JSON object.")
    return data


def resolve(root: Path | None = None, environ: dict | None = None) -> ReleaseSource:
    """Return the configured target, or raise a message a human can act on."""

    env = os.environ if environ is None else environ
    config = load_config(root)

    host = (env.get("OPENBFME_RELEASE_HOST") or config.get("host") or "github.com").strip()
    channel = (env.get("OPENBFME_RELEASE_CHANNEL") or config.get("channel") or "stable").strip()
    repository = (env.get("OPENBFME_RELEASE_REPOSITORY") or config.get("repository") or "").strip()

    if not repository:
        raise ReleaseSourceUnset(
            "The publish/update target is not configured.\n"
            "Set OPENBFME_RELEASE_REPOSITORY=<owner>/<name>, or fill in the\n"
            f'"repository" field of {CONFIG_RELATIVE.as_posix()}.\n'
            "There is no default on purpose: publishing to the wrong repository "
            "is not recoverable."
        )
    if not REPOSITORY_PATTERN.match(repository):
        raise ReleaseSourceError(
            f"Invalid repository {repository!r}; expected '<owner>/<name>' "
            "with no scheme, host, or path segments."
        )
    if host not in ALLOWED_HOSTS:
        raise ReleaseSourceError(
            f"Host {host!r} is not in the allow-list {sorted(ALLOWED_HOSTS)}. "
            "Add it deliberately in tools/release_source.py if that is intended."
        )
    return ReleaseSource(host=host, repository=repository, channel=channel)


def is_configured(root: Path | None = None, environ: dict | None = None) -> bool:
    try:
        resolve(root, environ)
    except ReleaseSourceError:
        return False
    return True


if __name__ == "__main__":  # pragma: no cover - operator convenience
    import sys

    try:
        source = resolve()
    except ReleaseSourceError as error:
        print(f"RELEASE_SOURCE UNRESOLVED\n{error}", file=sys.stderr)
        raise SystemExit(1)
    print(f"RELEASE_SOURCE OK repository={source.repository} host={source.host} channel={source.channel}")
    print(f"clone_url={source.clone_url}")
