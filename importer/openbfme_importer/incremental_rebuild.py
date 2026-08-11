"""Fail-closed identities and operator reports for incremental rebuilds.

The cache deliberately uses explicit compiler dependency manifests.  A family
only pays for modules in its compiler lane; an unrecognised family or an
incomplete manifest falls back to every importer module so uncertainty can
only cause extra work, never stale output.
"""

from __future__ import annotations

import ast
import fnmatch
import hashlib
import json
from pathlib import Path, PurePosixPath
import threading
from typing import Any, Mapping, Sequence


_COMMON_COMPILER_MODULES = frozenset(
    {
        "armor_compiler.py",
        "faction_object_cache.py",
        "incremental_rebuild.py",
        "faction_policy.py",
        "pack_recipe_catalog_identity.py",
        "sage_cst.py",
        "sage_ini.py",
        "sage_string.py",
        "visual_leaf.py",
    }
)

_COMPILER_DEPENDENCY_MANIFESTS: dict[str, frozenset[str]] = {
    "unit": _COMMON_COMPILER_MODULES
    | {
        "playable_unit_compiler.py",
        "playable_unit_import.py",
        "playable_unit_pack_compiler.py",
        "retail_ability_fx_ingress.py",
        "retail_visual_closure.py",
        "typed_visual_graph.py",
        "w3d_glb_validation.py",
        "w3d_index.py",
        "w3d_texture_closure.py",
    },
    "structure": _COMMON_COMPILER_MODULES
    | {
        "castle_behavior.py",
        "playable_structure_compiler.py",
        "playable_structure_lifecycle_evidence.py",
        "playable_structure_pack_compiler.py",
        "retail_building_lifecycle.py",
        "retail_visual_closure.py",
        "typed_visual_graph.py",
        "w3d_glb_validation.py",
        "w3d_index.py",
        "w3d_texture_closure.py",
    },
    "spellbook": _COMMON_COMPILER_MODULES
    | {
        "retail_ability_fx_ingress.py",
        "spellbook_compiler.py",
        "spellbook_import.py",
        "spellbook_pack_compiler.py",
        "spellbook_visual_ingress.py",
        "typed_visual_graph.py",
        "w3d_glb_validation.py",
        "w3d_index.py",
        "w3d_texture_closure.py",
    },
}

_UNIT_FAMILIES = frozenset(
    {
        "unit",
        "playable-unit",
        "hero",
        "horde",
        "banner-carrier",
        "unit-extension",
        "hero-extension",
        "horde-extension",
    }
)
_IDENTITY_LEAF_MODULES = frozenset(
    {"faction_object_cache.py", "incremental_rebuild.py"}
)
_LIVE_COMPILER_IDENTITY_MEMO: dict[str, dict[str, Any]] = {}
_LIVE_COMPILER_IDENTITY_LOCK = threading.Lock()


def _canonical_bytes(value: object) -> bytes:
    return json.dumps(
        value,
        sort_keys=True,
        separators=(",", ":"),
        ensure_ascii=False,
        allow_nan=False,
    ).encode("utf-8")


def _family_lane(family: str) -> str | None:
    folded = family.casefold().strip()
    if folded in _UNIT_FAMILIES:
        return "unit"
    if folded in {"structure", "spellbook"}:
        return folded
    return None


def compiler_dependency_identity(
    family: str,
    *,
    module_sources: Mapping[str, bytes] | None = None,
) -> dict[str, Any]:
    """Return a family-scoped compiler identity, broadening on uncertainty."""

    live_sources = module_sources is None
    memo_key = family.casefold().strip()
    if live_sources:
        with _LIVE_COMPILER_IDENTITY_LOCK:
            cached = _LIVE_COMPILER_IDENTITY_MEMO.get(memo_key)
        if cached is not None:
            return cached
    if live_sources:
        package = Path(__file__).resolve().parent
        sources: dict[str, bytes] = {}
        for path in sorted(package.glob("*.py"), key=lambda item: item.name.casefold()):
            try:
                sources[path.name] = path.read_bytes()
            except OSError:
                # A read race is uncertainty: preserve the filename and make
                # the identity differ rather than silently omitting it.
                sources[path.name] = b"<unreadable>"
    else:
        sources = {str(name): bytes(payload) for name, payload in module_sources.items()}

    lane = _family_lane(family)
    declared = _COMPILER_DEPENDENCY_MANIFESTS.get(lane or "")
    missing = sorted((declared or frozenset()) - set(sources))
    discovery_uncertain = False
    expanded = set(declared or ())
    if declared is not None and not (live_sources and missing):
        pending = list(expanded & set(sources))
        parsed: set[str] = set()
        while pending and not discovery_uncertain:
            name = pending.pop()
            if name in parsed:
                continue
            parsed.add(name)
            if name in _IDENTITY_LEAF_MODULES:
                continue
            try:
                tree = ast.parse(sources[name], filename=name)
            except (SyntaxError, ValueError):
                discovery_uncertain = True
                break
            for node in ast.walk(tree):
                if isinstance(node, ast.Call):
                    function = node.func
                    dynamic = (
                        isinstance(function, ast.Name) and function.id == "__import__"
                    ) or (
                        isinstance(function, ast.Attribute)
                        and function.attr == "import_module"
                    )
                    if dynamic:
                        discovery_uncertain = True
                        break
                candidates: list[str] = []
                if isinstance(node, ast.ImportFrom) and node.level:
                    if node.module:
                        candidates.append(node.module.split(".")[0] + ".py")
                    else:
                        candidates.extend(alias.name.split(".")[0] + ".py" for alias in node.names)
                elif isinstance(node, ast.Import):
                    candidates.extend(
                        alias.name.removeprefix("openbfme_importer.").split(".")[0] + ".py"
                        for alias in node.names
                        if alias.name.startswith("openbfme_importer.")
                    )
                for candidate in candidates:
                    if candidate not in sources:
                        # Standard-library or external relative package imports
                        # do not live in this source corpus. A genuinely missing
                        # local module from an explicit relative import is doubt.
                        if isinstance(node, ast.ImportFrom) and node.level:
                            discovery_uncertain = True
                            break
                        continue
                    if candidate not in expanded:
                        expanded.add(candidate)
                        pending.append(candidate)
                if discovery_uncertain:
                    break
    if declared is None or (live_sources and missing) or discovery_uncertain:
        selected = sorted(sources, key=lambda value: (value.casefold(), value))
        mode = "full-compiler-fallback"
    else:
        # ``module_sources`` is a synthetic test seam: the supplied mapping is
        # the complete corpus for that test, so absent production modules are
        # not evidence of a broken checkout.  Live identities remain strict.
        selected = sorted(
            expanded & set(sources), key=lambda value: (value.casefold(), value)
        )
        mode = "explicit-family-manifest" if selected else "full-compiler-fallback"
        if not selected:
            selected = sorted(sources, key=lambda value: (value.casefold(), value))
    payload = {
        "family": lane or family.casefold().strip(),
        "mode": mode,
        "modules": [
            {"path": name, "sha256": hashlib.sha256(sources[name]).hexdigest()}
            for name in selected
        ],
        "missingDeclaredModules": missing,
        "dependencyDiscoveryUncertain": discovery_uncertain,
    }
    result = {
        **payload,
        "sha256": hashlib.sha256(_canonical_bytes(payload)).hexdigest(),
    }
    if live_sources:
        with _LIVE_COMPILER_IDENTITY_LOCK:
            _LIVE_COMPILER_IDENTITY_MEMO[memo_key] = result
    return result


def document_closure_identity(
    documents: Mapping[str, bytes],
    source_paths: Sequence[str] | None,
    *,
    document_hashes: Mapping[str, str] | None = None,
) -> dict[str, Any]:
    """Hash only declared source INIs, or the entire corpus on any doubt."""

    by_key = {
        str(path).replace("\\", "/").casefold(): (str(path), bytes(payload))
        for path, payload in documents.items()
    }
    hashes_by_key = {
        str(path).replace("\\", "/").casefold(): str(digest).casefold()
        for path, digest in (document_hashes or {}).items()
    }
    requested = (
        sorted(
            {str(path).replace("\\", "/").casefold() for path in source_paths},
        )
        if source_paths
        else []
    )
    missing = [path for path in requested if path not in by_key]
    if not requested or missing:
        selected = sorted(by_key, key=lambda value: value)
        mode = "full-corpus-fallback"
    else:
        selected = requested
        mode = "explicit-source-closure"
    rows = [
        {
            "path": by_key[key][0].replace("\\", "/"),
            "sha256": hashes_by_key.get(key)
            or hashlib.sha256(by_key[key][1]).hexdigest(),
        }
        for key in selected
    ]
    payload = {"mode": mode, "documents": rows, "missing": missing}
    return {
        "mode": mode,
        "sourceDocuments": rows,
        "missing": missing,
        "sha256": hashlib.sha256(_canonical_bytes(payload)).hexdigest(),
    }


def reconvert_requested(asset_id: str, patterns: Sequence[str]) -> bool:
    """Case-insensitive shell-pattern match for explicit W3D cache bypass."""

    folded = asset_id.casefold()
    return any(fnmatch.fnmatchcase(folded, pattern.casefold()) for pattern in patterns)


def w3d_adapter_cache_identity(
    adapter_sha256: str, asset_id: str, patterns: Sequence[str]
) -> str:
    """Return exact identity for selected assets, stable scope for siblings."""

    if not patterns or reconvert_requested(asset_id, patterns):
        return adapter_sha256
    return "operator-scoped-reconversion-v1"


def rebuild_execution_provenance(
    *, reconvert_only: Sequence[str], single_build: bool
) -> dict[str, Any]:
    """Describe partial conversion and reproducibility without upgrading claims."""

    patterns = [str(pattern) for pattern in reconvert_only]
    reconversion = {
        "scope": "partial" if patterns else "cache-policy",
        "patterns": patterns,
        "fullCorpusReconverted": False if patterns else None,
    }
    if single_build:
        reproducibility = {
            "mode": "single-build",
            "attested": False,
            "reason": "cold A/B byte comparison was explicitly skipped",
        }
    else:
        reproducibility = {
            "mode": "cold-a-b-required",
            "attested": False,
            "scope": "partial-reconversion" if patterns else "full-build",
            "reason": "attestation is established only by the external cold A/B gate",
        }
    return {"reconversion": reconversion, "reproducibility": reproducibility}


def rebuild_status_for_coverage(
    coverage: Mapping[str, Any], assets_root: Path | str, *, cache_root: Path | str | None = None
) -> dict[str, Any]:
    """Compare recorded object identities to current inputs without converting."""

    root = Path(assets_root).expanduser().resolve()
    output: list[dict[str, Any]] = []
    objects = coverage.get("objects")
    if not isinstance(objects, list):
        raise ValueError("coverage objects must be a list")
    for candidate in objects:
        if not isinstance(candidate, Mapping):
            continue
        object_id = str(candidate.get("id", ""))
        family = str(candidate.get("family", ""))
        incremental = candidate.get("incremental")
        reasons: list[str] = []
        if not isinstance(incremental, Mapping):
            reasons.append("missing-incremental-metadata")
        else:
            recorded_sources = incremental.get("sourceDocuments")
            if not isinstance(recorded_sources, list) or not recorded_sources:
                reasons.append("unknown-source-dependencies")
            else:
                for source in recorded_sources:
                    if not isinstance(source, Mapping):
                        reasons.append("malformed-source-dependency")
                        continue
                    relative = str(source.get("path", "")).replace("\\", "/")
                    relative_path = PurePosixPath(relative)
                    if (
                        not relative
                        or relative_path.is_absolute()
                        or ".." in relative_path.parts
                    ):
                        reasons.append("malformed-source-dependency")
                        continue
                    path = root.joinpath(*relative.split("/"))
                    if not path.is_file():
                        reasons.append(f"source-missing:{relative}")
                    elif hashlib.sha256(path.read_bytes()).hexdigest() != str(
                        source.get("sha256", "")
                    ):
                        reasons.append(f"source-changed:{relative}")
            current_compiler = compiler_dependency_identity(family)["sha256"]
            if current_compiler != str(incremental.get("compilerIdentity", "")):
                reasons.append(f"compiler-identity-changed:{current_compiler}")
            if cache_root is not None:
                cache_key = str(incremental.get("cacheKey", ""))
                entry = (
                    Path(cache_root).expanduser().resolve()
                    / cache_key[:2]
                    / cache_key
                    / "artifacts.json"
                )
                if len(cache_key) != 64 or not entry.is_file():
                    reasons.append("cache-entry-missing")
        output.append(
            {
                "id": object_id,
                "family": family,
                "wouldRebuild": bool(reasons),
                "reasons": reasons,
            }
        )
    rebuild = sum(1 for row in output if row["wouldRebuild"])
    target = coverage.get("target")
    inputs = coverage.get("inputs")
    return {
        "faction": coverage.get("faction")
        or (target.get("faction") if isinstance(target, Mapping) else None)
        or (inputs.get("faction") if isinstance(inputs, Mapping) else None),
        "objects": output,
        "summary": {"objects": len(output), "rebuild": rebuild, "reuse": len(output) - rebuild},
    }


__all__ = [
    "compiler_dependency_identity",
    "document_closure_identity",
    "rebuild_execution_provenance",
    "rebuild_status_for_coverage",
    "reconvert_requested",
    "w3d_adapter_cache_identity",
]
