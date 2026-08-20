"""Per-faction aggregate short-circuit for a fully-cached convert.

When every plan row and every object is already cached, the parent still walks
both loops to rediscover that fact — ~28 s per faction, ~200 s across seven.
This cache lets the parent verify one aggregate identity instead and emit the
coverage document it produced last time.

**What keys it, and what does not.** The key is a function of every *input* to
the plan and convert stages: faction, game, catalog identity, effective-assets
fingerprint, the census graph digest, the full-corpus document closure digest,
the faction policy fingerprint, and each convert lane's compiler identity.

The plan aggregate and the coverage aggregate are deliberately **not** key
components. They are outputs of exactly the work being skipped, so making them
key material would require doing that work first. They are instead stored and
re-verified for integrity: the loaded coverage document must reproduce its own
recorded digest and its own recorded ``planAggregateSha256``.

**The trust trade.** The parent stops re-deriving per-row identities and trusts
a per-faction aggregate. That is acceptable only because every failure falls
through to the full per-row walk, which is the ordinary code path and the thing
that produced the stored document in the first place. Every refusal is reported
with the component that moved, so a short-circuit that silently stops firing is
visible rather than merely slow.

Artifacts are covered too: the envelope carries a digest manifest of every
per-object artifact file the run wrote, and a missing or edited artifact refuses
the entry. Skipping the loops must not leave a deleted artifact unrestored.
"""

from __future__ import annotations

from dataclasses import dataclass, field
import hashlib
import json
import os
from pathlib import Path
import threading
from typing import Any, Mapping

from .util import read_json, write_json_atomic


COVERAGE_CACHE_SCHEMA = "openbfme.faction-coverage-shortcircuit"
# Bump on ANY change to key material or stored envelope.
COVERAGE_CACHE_VERSION = 1

_LOCK = threading.Lock()


def _canonical_bytes(value: object) -> bytes:
    return json.dumps(
        value,
        sort_keys=True,
        separators=(",", ":"),
        ensure_ascii=False,
        allow_nan=False,
    ).encode("utf-8")


def coverage_cache_key(components: Mapping[str, Any]) -> str:
    payload = {
        "schema": COVERAGE_CACHE_SCHEMA,
        "version": COVERAGE_CACHE_VERSION,
        "components": dict(components),
    }
    return hashlib.sha256(_canonical_bytes(payload)).hexdigest()


def default_coverage_cache_root(state_root: Path) -> Path:
    shared = os.environ.get("OPENBFME_SHARED_CACHE", "").strip()
    if shared:
        return Path(shared).expanduser().resolve() / "faction-coverage"
    return Path(state_root).expanduser().resolve() / "cache" / "faction-coverage"


def coverage_cache_disabled() -> bool:
    for name in ("OPENBFME_NO_COVERAGE_SHORTCIRCUIT", "OPENBFME_NO_OBJECT_CACHE"):
        if os.environ.get(name, "").strip().casefold() in {"1", "true", "yes"}:
            return True
    return False


def artifact_manifest(artifact_root: Path) -> dict[str, str]:
    """sha256 of every per-object artifact file under ``artifact_root``."""

    manifest: dict[str, str] = {}
    if not artifact_root.is_dir():
        return manifest
    for path in sorted(artifact_root.rglob("*.json")):
        if path.is_file():
            key = path.relative_to(artifact_root).as_posix().casefold()
            manifest[key] = hashlib.sha256(path.read_bytes()).hexdigest()
    return manifest


@dataclass(slots=True)
class FactionCoverageCache:
    root: Path
    hits: int = 0
    misses: int = 0
    refusals: list[str] = field(default_factory=list)

    def __post_init__(self) -> None:
        self.root.mkdir(parents=True, exist_ok=True)

    def _entry_path(self, key: str) -> Path:
        return self.root / key[:2] / key / "coverage.json"

    def get(
        self, key: str, *, components: Mapping[str, Any], artifact_root: Path | None
    ) -> dict[str, Any] | None:
        """Stored coverage, or ``None`` with ``refusals`` naming what moved."""

        try:
            path = self._entry_path(key)
            if not path.is_file():
                self.misses += 1
                self.refusals.append("no entry for this input identity")
                return None
            value = read_json(path)
            if not isinstance(value, dict):
                self.refusals.append("envelope is not an object")
                return None
            if value.get("schema") != COVERAGE_CACHE_SCHEMA:
                self.refusals.append("schema mismatch")
                return None
            if value.get("version") != COVERAGE_CACHE_VERSION:
                self.refusals.append("cache format version drift")
                return None
            if value.get("key") != key:
                self.refusals.append("key mismatch")
                return None
            stored_components = value.get("components")
            if not isinstance(stored_components, dict):
                self.refusals.append("components missing")
                return None
            # Name the component that moved. The key already covers this, but a
            # short-circuit that stops firing must say why, not just be slow.
            for name, expected in components.items():
                if stored_components.get(name) != expected:
                    self.refusals.append(f"input moved: {name}")
                    return None
            coverage = value.get("coverage")
            if not isinstance(coverage, dict):
                self.refusals.append("coverage document missing")
                return None
            if value.get("coverageSha256") != hashlib.sha256(
                _canonical_bytes(coverage)
            ).hexdigest():
                self.refusals.append("stored coverage failed its own digest")
                return None
            if coverage.get("planAggregateSha256") != value.get(
                "planAggregateSha256"
            ):
                self.refusals.append("stored plan aggregate disagrees")
                return None
            if coverage.get("aggregateSha256") != value.get("aggregateSha256"):
                self.refusals.append("stored coverage aggregate disagrees")
                return None
            stored_artifacts = value.get("artifacts")
            if artifact_root is not None:
                if not isinstance(stored_artifacts, dict):
                    self.refusals.append("artifact manifest missing")
                    return None
                if artifact_manifest(artifact_root) != stored_artifacts:
                    # Skipping the loops must never leave a deleted or edited
                    # artifact unrestored.
                    self.refusals.append("artifact tree does not match")
                    return None
        except (OSError, ValueError, TypeError, KeyError, json.JSONDecodeError):
            self.refusals.append("entry unreadable")
            return None
        self.hits += 1
        return coverage

    def put(
        self,
        key: str,
        *,
        components: Mapping[str, Any],
        coverage: Mapping[str, Any],
        artifact_root: Path | None,
    ) -> None:
        document = dict(coverage)
        payload = {
            "schema": COVERAGE_CACHE_SCHEMA,
            "version": COVERAGE_CACHE_VERSION,
            "key": key,
            "components": dict(components),
            "planAggregateSha256": document.get("planAggregateSha256"),
            "aggregateSha256": document.get("aggregateSha256"),
            "coverageSha256": hashlib.sha256(
                _canonical_bytes(document)
            ).hexdigest(),
            "artifacts": (
                artifact_manifest(artifact_root) if artifact_root is not None else {}
            ),
            "coverage": document,
        }
        try:
            entry = self._entry_path(key).parent
            entry.mkdir(parents=True, exist_ok=True)
            with _LOCK:
                write_json_atomic(entry / "coverage.json", payload)
        except OSError:
            return


__all__ = [
    "COVERAGE_CACHE_SCHEMA",
    "COVERAGE_CACHE_VERSION",
    "FactionCoverageCache",
    "artifact_manifest",
    "coverage_cache_disabled",
    "coverage_cache_key",
    "default_coverage_cache_root",
]
