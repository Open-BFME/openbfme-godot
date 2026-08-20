"""Content-addressed cache for per-object faction *plan* rows.

The convert loop has had a durable cache for a long time; the plan stage never
did, so a fully warm faction convert still paid a complete second descriptor
compile for every object (measured: 167-220 s of a Men faction).  This module
caches the plan row list that ``build_faction_import_plan`` produces per object.

Trust model — a cached row is admitted only when *every* byte-identity of the
inputs that produced it still matches:

* the cache **key** binds the full-corpus document fingerprint, the catalog and
  effective-assets identities, the faction graph identity, the structure policy
  roots, the process-wide compiler identity token, the game and the object id;
* the stored **entry** additionally records the per-family
  ``compiler_dependency_identity`` and the per-object
  ``document_closure_identity`` of the row it produced, and both are recomputed
  and compared on load;
* the entry carries a digest of its own rows, so a tampered payload is refused
  rather than served.

Anything that does not match — including a corrupt file, a truncated write, or
a format-version drift — makes :meth:`FactionPlanRowCache.get` return ``None``.
A cache problem must only ever cost time, never correctness, so every failure
path falls through to a fresh compute.
"""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import threading
from typing import Any, Callable, Mapping, Sequence

from .util import read_json, write_json_atomic


PLAN_CACHE_SCHEMA = "openbfme.faction-plan-row-cache"
# Bump on ANY change to the key material or the stored envelope. Entries written
# by another version are ignored (clean miss), never partially interpreted.
PLAN_CACHE_VERSION = 1

_LOCK = threading.Lock()


def _canonical_bytes(value: object) -> bytes:
    return json.dumps(
        value,
        sort_keys=True,
        separators=(",", ":"),
        ensure_ascii=False,
        allow_nan=False,
    ).encode("utf-8")


def rows_digest(rows: Sequence[Mapping[str, object]]) -> str:
    """Self-integrity digest over the stored plan rows."""

    return hashlib.sha256(_canonical_bytes(list(rows))).hexdigest()


def plan_row_cache_key(
    *,
    object_id: str,
    documents_fp: str,
    catalog_identity_sha256: str,
    effective_root_fp: str,
    graph_identity_sha256: str,
    policy_fp: str,
    compiler_token: str,
    game: str,
) -> str:
    """Identity of one planned object.

    ``documents_fp`` is the *full corpus* closure identity, not a per-object
    projection: the plan stage decides an object's family before any source
    closure is known, so the conservative whole-corpus identity is the only
    honest pre-compute key. The per-object closure is verified separately on
    load, once the family and declared sources are known from the entry.
    """

    payload = {
        "schema": PLAN_CACHE_SCHEMA,
        "version": PLAN_CACHE_VERSION,
        "object_id": object_id.casefold(),
        "documents_fp": documents_fp,
        "catalog_identity_sha256": catalog_identity_sha256.casefold(),
        "effective_root_fp": effective_root_fp,
        "graph_identity_sha256": graph_identity_sha256.casefold(),
        "policy_fp": policy_fp.casefold(),
        "compiler_token": compiler_token.casefold(),
        "game": game.casefold().strip(),
    }
    return hashlib.sha256(_canonical_bytes(payload)).hexdigest()


def default_plan_cache_root(state_root: Path) -> Path:
    """Sibling of the conversion cache, never inside it."""

    shared = os.environ.get("OPENBFME_SHARED_CACHE", "").strip()
    if shared:
        return Path(shared).expanduser().resolve() / "faction-plan-rows"
    return Path(state_root).expanduser().resolve() / "cache" / "faction-plan-rows"


def plan_cache_disabled() -> bool:
    """``OPENBFME_NO_PLAN_CACHE`` (or the object-cache kill switch) turns it off."""

    for name in ("OPENBFME_NO_PLAN_CACHE", "OPENBFME_NO_OBJECT_CACHE"):
        if os.environ.get(name, "").strip().casefold() in {"1", "true", "yes"}:
            return True
    return False


@dataclass(slots=True)
class FactionPlanRowCache:
    root: Path
    hits: int = 0
    misses: int = 0
    refusals: int = 0

    def __post_init__(self) -> None:
        self.root.mkdir(parents=True, exist_ok=True)

    def _entry_path(self, key: str) -> Path:
        return self.root / key[:2] / key / "plan-rows.json"

    def get(
        self,
        key: str,
        *,
        verify_compiler_identity: Callable[[str], str],
        verify_document_closure: Callable[[list[str] | None], str],
    ) -> list[dict[str, Any]] | None:
        """Return cached rows, or ``None`` for any doubt whatsoever.

        ``verify_compiler_identity(family)`` and
        ``verify_document_closure(source_paths)`` are supplied by the caller so
        this module never guesses which identity a row should have; both are
        recomputed from the *current* inputs and must match what was stored.
        """

        try:
            path = self._entry_path(key)
            if not path.is_file():
                self.misses += 1
                return None
            value = read_json(path)
            if not isinstance(value, dict):
                self.refusals += 1
                return None
            if (
                value.get("schema") != PLAN_CACHE_SCHEMA
                or value.get("version") != PLAN_CACHE_VERSION
                or value.get("key") != key
            ):
                self.refusals += 1
                return None
            rows = value.get("rows")
            if not isinstance(rows, list) or not all(
                isinstance(row, dict) for row in rows
            ):
                self.refusals += 1
                return None
            # Self-integrity: a tampered or truncated payload is refused.
            if value.get("rowsSha256") != rows_digest(rows):
                self.refusals += 1
                return None
            family = value.get("family")
            if not isinstance(family, str):
                self.refusals += 1
                return None
            source_paths = value.get("sourceDocumentPaths")
            if source_paths is not None and not (
                isinstance(source_paths, list)
                and all(isinstance(item, str) for item in source_paths)
            ):
                self.refusals += 1
                return None
            # Input identity: the compiler that produced this row and the exact
            # source documents it declared must both still hash the same.
            if value.get("compilerIdentity") != verify_compiler_identity(family):
                self.refusals += 1
                return None
            if value.get("documentClosureSha256") != verify_document_closure(
                source_paths
            ):
                self.refusals += 1
                return None
        except (OSError, ValueError, TypeError, KeyError, json.JSONDecodeError):
            # Fail open: a broken cache costs time, never correctness.
            self.refusals += 1
            return None
        self.hits += 1
        return [dict(row) for row in rows]

    def put(
        self,
        key: str,
        *,
        rows: Sequence[Mapping[str, object]],
        family: str,
        compiler_identity: str,
        document_closure_sha256: str,
        source_paths: list[str] | None,
    ) -> None:
        payload = {
            "schema": PLAN_CACHE_SCHEMA,
            "version": PLAN_CACHE_VERSION,
            "key": key,
            "family": family,
            "compilerIdentity": compiler_identity,
            "documentClosureSha256": document_closure_sha256,
            "sourceDocumentPaths": source_paths,
            "rowsSha256": rows_digest(rows),
            "rows": [dict(row) for row in rows],
        }
        try:
            entry = self._entry_path(key).parent
            entry.mkdir(parents=True, exist_ok=True)
            with _LOCK:
                write_json_atomic(entry / "plan-rows.json", payload)
        except OSError:
            # A cache we cannot write is a cache we do without.
            return


__all__ = [
    "PLAN_CACHE_SCHEMA",
    "PLAN_CACHE_VERSION",
    "FactionPlanRowCache",
    "default_plan_cache_root",
    "plan_cache_disabled",
    "plan_row_cache_key",
    "rows_digest",
]
