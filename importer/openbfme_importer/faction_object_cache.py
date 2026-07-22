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
CACHE_VERSION = 3
# Artifacts that are large / re-derivable and must not bloat durable DDC.
_CACHE_EXCLUDED_ARTIFACTS = frozenset({"visual-closure"})
_LOCK = threading.Lock()
# Compiler modules whose bytes are mixed into cook-salt identity.
_COMPILER_SALT_MODULES = (
    "playable_unit_compiler.py",
    "playable_unit_pack_compiler.py",
    "playable_unit_import.py",
    "playable_structure_compiler.py",
    "playable_structure_pack_compiler.py",
    "playable_structure_lifecycle_evidence.py",
    "spellbook_compiler.py",
    "spellbook_pack_compiler.py",
    "spellbook_import.py",
    "faction_census.py",
    "sage_string.py",
    "retail_visual_closure.py",
    "retail_building_lifecycle.py",
    "typed_visual_graph.py",
    "visual_leaf.py",
    "sage_cst.py",
    "sage_ini.py",
    "w3d_index.py",
    "w3d_texture_closure.py",
    "w3d_glb_validation.py",
    "faction_import.py",
    "faction_policy.py",
    "faction_object_cache.py",
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
    wall_templates: Sequence[str] = (),
    source_null_sets: Sequence[str] = (),
) -> str:
    """Hash structure policy roots that affect structure descriptors."""

    payload = {
        "spawned": sorted({s.casefold() for s in spawned}),
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


def compiler_identity_token() -> str:
    """Pin converter schemas + source bytes so code/schema bumps invalidate DDC."""

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
    return hashlib.sha256(_canonical_bytes(payload)).hexdigest()


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
    graph_input_set_sha256: str = "",
    plan_aggregate_sha256: str = "",
    policy_fp: str = "",
    compiler_token: str = "",
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
        "graph_input_set_sha256": graph_input_set_sha256.casefold(),
        "plan_aggregate_sha256": plan_aggregate_sha256.casefold(),
        "policy_fp": policy_fp.casefold(),
        "compiler_token": compiler_token.casefold(),
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
        with _LOCK:
            write_json_atomic(entry / "artifacts.json", payload)
