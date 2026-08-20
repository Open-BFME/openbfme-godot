"""Durable cache for the per-faction census graph.

Census is a pure function of the installed catalog and the faction policy, and
it costs ~11 s per faction on an idle host. Every convert worker process and
the parent all recompute it, so a seven-faction pooled run paid it dozens of
times.

Storage is plain JSON. The census graph was measured to be JSON-stable — a
``json.loads(json.dumps(graph))`` round trip compares equal and re-serialises
to identical bytes, and a recursive scan finds no tuples, sets or non-string
keys. That matters because the graph's canonical digest keys both the plan-row
and object caches: had it contained tuples, a round trip would have turned them
into lists and silently moved those identities. ``verify_graph_identity`` on
load re-derives the digest from the loaded object and refuses any entry whose
digest does not match what was stored, so a round-trip that ever stopped being
stable becomes a clean miss instead of a corrupted key.

Trust model matches ``faction_plan_cache``: key on the full fail-closed
identity chain, verify on load, fail open to a recompute on any doubt.
"""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import threading
from typing import Any, Callable

from .util import read_json, write_json_atomic


CENSUS_CACHE_SCHEMA = "openbfme.faction-census-cache"
# Bump on ANY change to key material or stored envelope.
CENSUS_CACHE_VERSION = 1

_LOCK = threading.Lock()


def _canonical_bytes(value: object) -> bytes:
    return json.dumps(
        value,
        sort_keys=True,
        separators=(",", ":"),
        ensure_ascii=False,
        allow_nan=False,
    ).encode("utf-8")


def graph_digest(graph: object) -> str:
    """Canonical digest of a census graph.

    This is the same serialisation ``build_faction_conversion`` uses for
    ``graph_identity_sha256``, so a cached graph that reproduces this digest
    reproduces every downstream cache key too.
    """

    return hashlib.sha256(_canonical_bytes(graph)).hexdigest()


def census_cache_key(
    *,
    faction: str,
    game: str,
    catalog_identity_sha256: str,
    effective_root_fp: str,
    policy_fp: str,
    census_identity: str,
) -> str:
    payload = {
        "schema": CENSUS_CACHE_SCHEMA,
        "version": CENSUS_CACHE_VERSION,
        "faction": faction.casefold().strip(),
        "game": game.casefold().strip(),
        "catalog_identity_sha256": catalog_identity_sha256.casefold(),
        "effective_root_fp": effective_root_fp,
        "policy_fp": policy_fp,
        "census_identity": census_identity.casefold(),
    }
    return hashlib.sha256(_canonical_bytes(payload)).hexdigest()


def default_census_cache_root(state_root: Path) -> Path:
    shared = os.environ.get("OPENBFME_SHARED_CACHE", "").strip()
    if shared:
        return Path(shared).expanduser().resolve() / "faction-census"
    return Path(state_root).expanduser().resolve() / "cache" / "faction-census"


def census_cache_disabled() -> bool:
    for name in ("OPENBFME_NO_CENSUS_CACHE", "OPENBFME_NO_OBJECT_CACHE"):
        if os.environ.get(name, "").strip().casefold() in {"1", "true", "yes"}:
            return True
    return False


@dataclass(slots=True)
class FactionCensusCache:
    root: Path
    hits: int = 0
    misses: int = 0
    refusals: int = 0

    def __post_init__(self) -> None:
        self.root.mkdir(parents=True, exist_ok=True)

    def _entry_path(self, key: str) -> Path:
        return self.root / key[:2] / key / "census.json"

    def get(self, key: str) -> dict[str, Any] | None:
        """Return the cached graph, or ``None`` for any doubt whatsoever."""

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
                value.get("schema") != CENSUS_CACHE_SCHEMA
                or value.get("version") != CENSUS_CACHE_VERSION
                or value.get("key") != key
            ):
                self.refusals += 1
                return None
            graph = value.get("graph")
            if not isinstance(graph, dict):
                self.refusals += 1
                return None
            # The digest is the load-bearing check: it is exactly the value
            # that keys the plan and object caches downstream.
            if value.get("graphSha256") != graph_digest(graph):
                self.refusals += 1
                return None
        except (OSError, ValueError, TypeError, KeyError, json.JSONDecodeError):
            self.refusals += 1
            return None
        self.hits += 1
        return graph

    def put(self, key: str, *, graph: dict[str, Any]) -> None:
        payload = {
            "schema": CENSUS_CACHE_SCHEMA,
            "version": CENSUS_CACHE_VERSION,
            "key": key,
            "graphSha256": graph_digest(graph),
            "graph": graph,
        }
        try:
            entry = self._entry_path(key).parent
            entry.mkdir(parents=True, exist_ok=True)
            with _LOCK:
                write_json_atomic(entry / "census.json", payload)
        except OSError:
            # A cache we cannot write is a cache we do without.
            return


def load_or_build_census(
    cache: FactionCensusCache | None,
    key_material: Callable[[], dict[str, str]],
    build: Callable[[], dict[str, Any]],
) -> dict[str, Any]:
    """Census with a durable cache, falling back to ``build`` on any doubt."""

    if cache is None:
        return build()
    key = census_cache_key(**key_material())
    hit = cache.get(key)
    if hit is not None:
        return hit
    graph = build()
    cache.put(key, graph=graph)
    return graph


__all__ = [
    "CENSUS_CACHE_SCHEMA",
    "CENSUS_CACHE_VERSION",
    "FactionCensusCache",
    "census_cache_disabled",
    "census_cache_key",
    "default_census_cache_root",
    "graph_digest",
    "load_or_build_census",
]
