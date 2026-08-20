"""Content-addressed cache for per-object faction conversion artifacts.

Caches descriptors, recipes, closures, and runtime docs so repeated
import-faction convert runs and cross-faction shared objects skip recompute.
"""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import threading
from typing import Any, Mapping, Sequence

from .util import read_json, write_json_atomic


CACHE_SCHEMA = "openbfme.faction-object-cache"
# Bump when key material or stored artifact envelope changes.
CACHE_VERSION = 5
# Artifacts that are large / re-derivable and must not bloat durable DDC.
_CACHE_EXCLUDED_ARTIFACTS = frozenset({"visual-closure"})
_LOCK = threading.Lock()
# Compiler modules whose bytes are mixed into cook-salt identity.
_COMPILER_SALT_MODULES = (
    # Unit/structure descriptors call the shared armor and weapon-upgrade
    # compiler. Its source bytes must invalidate cached converted rows; without
    # this, a fixed upgrade contract can keep returning a stale converter gap.
    "armor_compiler.py",
    "playable_unit_compiler.py",
    "playable_unit_pack_compiler.py",
    "playable_unit_import.py",
    "playable_structure_compiler.py",
    # The fortress CastleUpgrade harvester feeds
    # `registration.gameplay.castleUpgrades` straight into structure
    # descriptors, so its source bytes must invalidate cached converted rows
    # for exactly the reason `armor_compiler.py` above must: otherwise a fix to
    # the trigger->upgrade harvest keeps returning stale descriptors that were
    # compiled before it. Added 2026-08-04 with the surface itself.
    "castle_behavior.py",
    "playable_structure_pack_compiler.py",
    "playable_structure_lifecycle_evidence.py",
    "spellbook_compiler.py",
    "spellbook_pack_compiler.py",
    "spellbook_import.py",
    # The ability/spellbook FX ingress lane feeds resources and runtime
    # bindings straight into unit and spellbook pack recipes, so its source
    # must invalidate cached objects exactly like the compilers it feeds.
    "retail_ability_fx_ingress.py",
    # Same reasoning for the effect-geometry lane: it emits the model resources
    # and visual bindings that ride the spellbook recipe/runtime.
    "spellbook_visual_ingress.py",
    "faction_census.py",
    "sage_string.py",
    "retail_visual_closure.py",
    "retail_building_lifecycle.py",
    "typed_visual_graph.py",
    "visual_leaf.py",
    # Pack-recipe catalog identity gate: changes must invalidate durable object
    # DDC so pre-fix recipes with archive-orphan patterns cannot cache-hit.
    "pack_recipe_catalog_identity.py",
    "sage_cst.py",
    "sage_ini.py",
    "w3d_index.py",
    "w3d_texture_closure.py",
    "w3d_glb_validation.py",
    "faction_import.py",
    "faction_policy.py",
    "faction_object_cache.py",
    "incremental_rebuild.py",
)


def _canonical_bytes(value: object) -> bytes:
    return json.dumps(
        value,
        sort_keys=True,
        separators=(",", ":"),
        ensure_ascii=False,
        allow_nan=False,
    ).encode("utf-8")


def documents_fingerprint(documents: Mapping[str, bytes]) -> str:
    digest = hashlib.sha256()
    for path, payload in sorted(
        documents.items(), key=lambda item: (item[0].casefold(), item[0])
    ):
        digest.update(path.replace("\\", "/").casefold().encode("utf-8"))
        digest.update(b"\0")
        digest.update(hashlib.sha256(payload).digest())
        digest.update(b"\n")
    return digest.hexdigest()


def policy_roots_fingerprint(
    *,
    spawned: Sequence[str] = (),
    spawned_roles: Mapping[str, str] | None = None,
    wall_templates: Sequence[str] = (),
    source_null_sets: Sequence[str] = (),
) -> str:
    """Hash structure policy roots that affect structure descriptors."""

    payload = {
        "spawned": sorted({s.casefold() for s in spawned}),
        "spawned_roles": {
            str(key).casefold(): str(value)
            for key, value in sorted(
                (spawned_roles or {}).items(), key=lambda item: str(item[0]).casefold()
            )
        },
        "wall_templates": sorted({s.casefold() for s in wall_templates}),
        "source_null_sets": sorted({s.casefold() for s in source_null_sets}),
    }
    return hashlib.sha256(_canonical_bytes(payload)).hexdigest()


def _compiler_source_salt() -> str:
    """Content salt over converter source modules (algorithm edits invalidate)."""

    package_dir = Path(__file__).resolve().parent
    digest = hashlib.sha256()
    for name in _COMPILER_SALT_MODULES:
        path = package_dir / name
        digest.update(name.encode("utf-8"))
        digest.update(b"\0")
        try:
            digest.update(hashlib.sha256(path.read_bytes()).digest())
        except OSError:
            digest.update(b"missing")
        digest.update(b"\n")
    # Blender adapter scripts affect W3D GLB bytes used by pack recipes/closures.
    blender_dir = package_dir.parent / "blender"
    if blender_dir.is_dir():
        for path in sorted(blender_dir.glob("*.py"), key=lambda p: p.name.casefold()):
            digest.update(b"blender/")
            digest.update(path.name.encode("utf-8"))
            digest.update(b"\0")
            try:
                digest.update(hashlib.sha256(path.read_bytes()).digest())
            except OSError:
                digest.update(b"missing")
            digest.update(b"\n")
    return digest.hexdigest()


_COMPILER_TOKEN_MEMO: dict[str, str] = {}
_COMPILER_TOKEN_LOCK = threading.Lock()


def clear_compiler_identity_token_memo() -> None:
    """Drop both layers of process-local compiler identity memoization."""

    global _COMPILER_TOKEN_MEMO
    with _COMPILER_TOKEN_LOCK:
        _COMPILER_TOKEN_MEMO = {}
    # Local import avoids the module-load cycle.  Both memo layers must clear
    # together or source-mutation tests can observe a stale family identity.
    from . import incremental_rebuild

    with incremental_rebuild._LIVE_COMPILER_IDENTITY_LOCK:
        incremental_rebuild._LIVE_COMPILER_IDENTITY_MEMO.clear()
        # The live package-source snapshot is the third memo layer; leaving it
        # behind would keep serving pre-mutation bytes to a fresh identity.
        incremental_rebuild._LIVE_SOURCE_MEMO = None


def compiler_identity_token(family: str | None = None) -> str:
    """Pin converter schemas + source bytes so code/schema bumps invalidate DDC.

    Memoized per process: hashing every compiler module on each faction convert
    was pure overhead once the salt is fixed for the process lifetime.
    """

    memo_key = (family or "<all>").casefold().strip()
    with _COMPILER_TOKEN_LOCK:
        if memo_key in _COMPILER_TOKEN_MEMO:
            return _COMPILER_TOKEN_MEMO[memo_key]

    if family is not None:
        from .incremental_rebuild import compiler_dependency_identity

        token = str(compiler_dependency_identity(family)["sha256"])
        with _COMPILER_TOKEN_LOCK:
            _COMPILER_TOKEN_MEMO[memo_key] = token
        return token

    # Local imports avoid circular import at module load.
    from .playable_structure_compiler import (
        SCHEMA as structure_schema,
        SCHEMA_VERSION as structure_version,
    )
    from .playable_structure_pack_compiler import (
        RUNTIME_SCHEMA as structure_runtime_schema,
        RUNTIME_SCHEMA_VERSION as structure_runtime_version,
        SCHEMA as structure_pack_schema,
        SCHEMA_VERSION as structure_pack_version,
    )
    from .playable_unit_compiler import (
        SCHEMA as unit_schema,
        SCHEMA_VERSION as unit_version,
    )
    from .playable_unit_pack_compiler import (
        SCHEMA as unit_pack_schema,
        SCHEMA_VERSION as unit_pack_version,
    )
    from .spellbook_compiler import (
        SCHEMA as spellbook_schema,
        SCHEMA_VERSION as spellbook_version,
    )
    from .spellbook_pack_compiler import (
        RUNTIME_SCHEMA as spellbook_runtime_schema,
        RUNTIME_SCHEMA_VERSION as spellbook_runtime_version,
        SCHEMA as spellbook_pack_schema,
        SCHEMA_VERSION as spellbook_pack_version,
    )

    payload = {
        "cache_version": CACHE_VERSION,
        "source_salt": _compiler_source_salt(),
        "compilers": {
            "structure": f"{structure_schema}:{structure_version}",
            "structure_pack": f"{structure_pack_schema}:{structure_pack_version}",
            "structure_runtime": (
                f"{structure_runtime_schema}:{structure_runtime_version}"
            ),
            "unit": f"{unit_schema}:{unit_version}",
            "unit_pack": f"{unit_pack_schema}:{unit_pack_version}",
            "spellbook": f"{spellbook_schema}:{spellbook_version}",
            "spellbook_pack": f"{spellbook_pack_schema}:{spellbook_pack_version}",
            "spellbook_runtime": (
                f"{spellbook_runtime_schema}:{spellbook_runtime_version}"
            ),
        },
    }
    token = hashlib.sha256(_canonical_bytes(payload)).hexdigest()
    with _COMPILER_TOKEN_LOCK:
        _COMPILER_TOKEN_MEMO[memo_key] = token
    return token


def durable_effective_assets_fingerprint(effective_root: Path | str) -> str:
    """Content identity for durable object DDC (not process-local memo).

    Prefers the extract manifest's aggregate_sha256 (full inventory digest).
    Never embeds absolute host paths so shared DDC is path-portable.
    """

    root = Path(effective_root).expanduser()
    manifest = root / ".openbfme" / "manifest.json"
    if not manifest.is_file():
        return _tree_inventory_fingerprint(root, "missing-manifest")
    try:
        value = read_json(manifest)
    except (OSError, ValueError, TypeError, json.JSONDecodeError):
        return _tree_inventory_fingerprint(root, "unreadable-manifest")
    if isinstance(value, dict):
        aggregate = value.get("aggregate_sha256")
        if isinstance(aggregate, str) and len(aggregate) == 64:
            return f"manifest-agg:{aggregate.casefold()}"
        # Fall back to full manifest file content (not 4k peek / mtime).
    try:
        digest = hashlib.sha256(manifest.read_bytes()).hexdigest()
    except OSError:
        return _tree_inventory_fingerprint(root, "unreadable-manifest")
    return f"manifest-file:{digest}"


def durable_non_ini_assets_fingerprint(effective_root: Path | str) -> str:
    """Hash manifest rows outside ``data/ini/**``.

    The converted object envelope also contains visual recipes, so dropping
    effective-assets identity entirely would under-invalidate on a model or
    texture edit.  A valid effective-assets manifest lets us separate those
    bytes from source INIs.  Any malformed or missing inventory falls back to
    the existing whole-tree fingerprint.
    """

    root = Path(effective_root).expanduser()
    manifest_path = root / ".openbfme" / "manifest.json"
    try:
        manifest = read_json(manifest_path)
        files = manifest["files"]
        if not isinstance(files, list):
            raise TypeError("manifest files is not a list")
        rows: list[dict[str, object]] = []
        for item in files:
            if not isinstance(item, Mapping):
                raise TypeError("manifest file row is not an object")
            path = str(item["path"]).replace("\\", "/")
            size = int(item["size"])
            sha256 = str(item["sha256"]).casefold()
            if len(sha256) != 64:
                raise ValueError("manifest file hash is malformed")
            if not path.casefold().startswith("data/ini/"):
                rows.append({"path": path.casefold(), "size": size, "sha256": sha256})
        return "non-ini-manifest:" + hashlib.sha256(_canonical_bytes(rows)).hexdigest()
    except (OSError, KeyError, TypeError, ValueError, json.JSONDecodeError):
        return "full-fallback:" + durable_effective_assets_fingerprint(root)


def _tree_inventory_fingerprint(root: Path, reason: str) -> str:
    """Content identity for roots without a usable extract manifest.

    Two different trees must never share one identity bucket under a shared
    DDC root, so the constant sentinel is mixed with the tree's actual file
    inventory (relative names + sizes, capped for cost).
    """

    digest = hashlib.sha256(reason.encode("utf-8"))
    try:
        entries = sorted(
            (
                str(path.relative_to(root)).replace("\\", "/").casefold(),
                path.stat().st_size,
            )
            for path in root.rglob("*")
            if path.is_file()
        )
    except OSError:
        entries = []
    for name, size in entries[:20000]:
        digest.update(name.encode("utf-8"))
        digest.update(b"\0")
        digest.update(str(size).encode("ascii"))
        digest.update(b"\n")
    digest.update(f"count:{len(entries)}".encode("ascii"))
    return f"tree-inventory:{digest.hexdigest()}"


def object_cache_key(
    *,
    family: str,
    object_id: str,
    documents_fp: str,
    catalog_identity_sha256: str,
    effective_root_fp: str,
    graph_identity_sha256: str = "",
    plan_aggregate_sha256: str = "",
    policy_fp: str = "",
    compiler_token: str = "",
    numeric_defines_sha256: str = "",
    plan_descriptor_sha256: str = "",
    extra: Mapping[str, object] | None = None,
) -> str:
    payload = {
        "schema": CACHE_SCHEMA,
        "version": CACHE_VERSION,
        "family": family.casefold(),
        "object_id": object_id.casefold(),
        "documents_fp": documents_fp,
        "catalog_identity_sha256": catalog_identity_sha256.casefold(),
        "effective_root_fp": effective_root_fp,
        "graph_identity_sha256": graph_identity_sha256.casefold(),
        "plan_aggregate_sha256": plan_aggregate_sha256.casefold(),
        "policy_fp": policy_fp.casefold(),
        "compiler_token": compiler_token.casefold(),
        "numeric_defines_sha256": numeric_defines_sha256.casefold(),
        "plan_descriptor_sha256": plan_descriptor_sha256.casefold(),
        "extra": dict(extra or {}),
    }
    return hashlib.sha256(_canonical_bytes(payload)).hexdigest()


def default_cache_root(state_root: Path) -> Path:
    shared = os.environ.get("OPENBFME_SHARED_CACHE", "").strip()
    if shared:
        return Path(shared).expanduser().resolve() / "faction-objects"
    return Path(state_root).expanduser().resolve() / "cache" / "faction-objects"


@dataclass(slots=True)
class FactionObjectCache:
    root: Path

    def __post_init__(self) -> None:
        self.root.mkdir(parents=True, exist_ok=True)

    def _entry_dir(self, key: str) -> Path:
        return self.root / key[:2] / key

    def get(self, key: str) -> dict[str, Any] | None:
        path = self._entry_dir(key) / "artifacts.json"
        if not path.is_file():
            return None
        try:
            value = read_json(path)
        except (OSError, ValueError, TypeError, json.JSONDecodeError):
            return None
        if not isinstance(value, dict):
            return None
        if value.get("schema") != CACHE_SCHEMA or value.get("version") != CACHE_VERSION:
            return None
        if value.get("key") != key:
            return None
        artifacts = value.get("artifacts")
        row = value.get("row")
        if not isinstance(artifacts, dict) or not isinstance(row, dict):
            return None
        return {"artifacts": artifacts, "row": row}

    def put(
        self,
        key: str,
        *,
        row: Mapping[str, object],
        artifacts: Mapping[str, Mapping[str, object]],
    ) -> None:
        entry = self._entry_dir(key)
        entry.mkdir(parents=True, exist_ok=True)
        # Drop large re-derivable artifacts (visual-closure) from durable DDC.
        stored = {
            name: dict(doc)
            for name, doc in artifacts.items()
            if name not in _CACHE_EXCLUDED_ARTIFACTS
        }
        payload = {
            "schema": CACHE_SCHEMA,
            "version": CACHE_VERSION,
            "key": key,
            "row": dict(row),
            "artifacts": stored,
        }
        try:
            with _LOCK:
                write_json_atomic(entry / "artifacts.json", payload)
        except OSError:
            # A cache we cannot write is a cache we do without. On Windows
            # ``os.replace`` raises PermissionError (WinError 5) when a
            # concurrent reader holds the destination open; with parallel
            # convert workers sharing one cache root that is routine. Failing
            # to store an entry only costs a recompute next run, so it must
            # never surface as a converter gap.
            return
