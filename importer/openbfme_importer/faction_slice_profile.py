"""Compose coverage-approved faction artifacts into an import profile."""
from __future__ import annotations
from copy import deepcopy
import hashlib, json
from pathlib import Path
from typing import Mapping, Sequence
from .playable_structure_pack_compiler import validate_structure_visual_recipe
from .playable_unit_import import extend_profile_with_unit

def _bytes(value: object) -> bytes:
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False, allow_nan=False).encode()

def _unsigned_digest(document: Mapping[str, object], field: str) -> str:
    value = deepcopy(dict(document)); value.pop(field, None)
    return hashlib.sha256(_bytes(value)).hexdigest()

def _load(path: Path, label: str) -> dict[str, object]:
    try: value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc: raise ValueError(f"{label} is unreadable: {path}") from exc
    if not isinstance(value, dict): raise ValueError(f"{label} root is not an object: {path}")
    return value

def _slug(value: str) -> str:
    result = "".join(c.lower() for c in value if c.isalnum())
    if not result: raise ValueError("Object id has no safe slug")
    return result

def _add_structure(profile: Mapping[str, object], recipe: Mapping[str, object], runtime: Mapping[str, object]) -> tuple[dict[str, object], dict[str, object]]:
    validate_structure_visual_recipe(recipe)
    if runtime.get("schema") != "openbfme.playable-structure-runtime" or runtime.get("schemaVersion") != 0: raise ValueError("playable-structure runtime schema is invalid")
    object_id = str(recipe.get("objectId", ""))
    if object_id.casefold() != str(runtime.get("objectId", "")).casefold(): raise ValueError("structure recipe/runtime Object identities differ")
    if recipe.get("recipeSha256") != runtime.get("recipeSha256"): raise ValueError("structure recipe/runtime digests differ")
    if runtime.get("runtimeSha256") != _unsigned_digest(runtime, "runtimeSha256"): raise ValueError("structure runtime digest is invalid")
    target = deepcopy(dict(profile)); resources = target.get("resources"); data = target.get("runtime_data"); pack = target.get("pack")
    if not isinstance(resources, list) or not isinstance(data, dict) or not isinstance(pack, dict): raise ValueError("base profile is not extensible")
    files = pack.get("files")
    if not isinstance(files, dict): raise ValueError("base profile pack has no file registry")
    existing = {str(row["id"]).casefold(): row for row in resources if isinstance(row, Mapping) and isinstance(row.get("id"), str)}
    added: list[str] = []
    raw_resources = recipe.get("resources")
    if not isinstance(raw_resources, list): raise ValueError("structure recipe resources are invalid")
    for raw in raw_resources:
        if not isinstance(raw, Mapping) or not isinstance(raw.get("id"), str): raise ValueError("structure recipe resource is invalid")
        row = deepcopy(dict(raw)); key = str(row["id"]).casefold()
        if key in existing:
            if dict(existing[key]) != row: raise ValueError(f"structure resource id collision: {row['id']}")
            continue
        resources.append(row); existing[key] = row; added.append(str(row["id"]))
    slug = _slug(str(runtime.get("slug", object_id))); runtime_path = f"data/playable-structures/{slug}.json"; file_key = f"playableStructure.{slug}"
    old = data.get(runtime_path)
    if old is not None and old != runtime: raise ValueError(f"structure runtime path collision: {runtime_path}")
    for key, value in files.items():
        if str(key).casefold() == file_key.casefold() and str(key) != file_key: raise ValueError(f"structure file key collides by case: {file_key}")
        if str(value).casefold() == runtime_path.casefold() and str(key) != file_key: raise ValueError(f"structure runtime path has multiple owners: {runtime_path}")
    if file_key in files and files[file_key] != runtime_path: raise ValueError(f"structure file key collision: {file_key}")
    data[runtime_path] = deepcopy(dict(runtime)); files[file_key] = runtime_path
    return target, {"objectId": object_id, "runtimePath": runtime_path, "packFileKey": file_key, "resourceIds": sorted(added, key=str.casefold)}

def compose_faction_profile(base: Mapping[str, object], report_root: Path, factions: Sequence[str]) -> tuple[dict[str, object], dict[str, object]]:
    """Add only artifacts bound to converted rows in digested coverage reports."""
    target = deepcopy(dict(base)); receipts: list[dict[str, object]] = []; deltas: list[dict[str, object]] = []; seen: set[str] = set()
    for raw_faction in factions:
        faction = raw_faction.strip().lower()
        if not faction or faction in seen: raise ValueError(f"duplicate or empty faction: {raw_faction!r}")
        seen.add(faction); coverage = _load(report_root / f"{faction}-coverage.json", "faction coverage")
        if coverage.get("schema") != "openbfme.faction-import-coverage" or coverage.get("schemaVersion") != 0: raise ValueError(f"unsupported faction coverage schema: {faction}")
        aggregate = coverage.get("aggregateSha256")
        if aggregate != _unsigned_digest(coverage, "aggregateSha256"): raise ValueError(f"faction coverage digest is invalid: {faction}")
        rows = coverage.get("objects"); summary = coverage.get("summary")
        if not isinstance(rows, list) or not isinstance(summary, Mapping): raise ValueError(f"faction coverage body is invalid: {faction}")
        converted = [r for r in rows if isinstance(r, Mapping) and r.get("status") == "converted"]
        if len(converted) != summary.get("convertedCount"): raise ValueError(f"faction converted count is inconsistent: {faction}")
        for row in sorted(converted, key=lambda r: (str(r.get("id", "")).casefold(), str(r.get("id", "")))):
            object_id = str(row.get("id", "")); root = report_root / faction / "objects" / object_id.casefold(); recipe = _load(root / "pack-recipe.json", "pack recipe")
            if recipe.get("objectId") != object_id or recipe.get("recipeSha256") != row.get("recipeSha256"): raise ValueError(f"coverage/recipe identity mismatch: {faction}/{object_id}")
            family = str(row.get("family", ""))
            if family == "playable-unit": target, delta = extend_profile_with_unit(target, recipe)
            elif family == "structure":
                runtime = _load(root / "runtime.json", "structure runtime")
                if runtime.get("runtimeSha256") != row.get("runtimeSha256"): raise ValueError(f"coverage/runtime identity mismatch: {faction}/{object_id}")
                target, delta = _add_structure(target, recipe, runtime)
            else: raise ValueError(f"converted row has unsupported family: {faction}/{object_id}")
            deltas.append({"faction": faction, "family": family, "objectId": object_id, **delta})
        receipts.append({"faction": faction, "coverageAggregateSha256": aggregate, "convertedCount": len(converted), "converterGapCount": int(summary.get("converterGapCount", 0)), "conversionComplete": bool(summary.get("conversionComplete", False))})
    pack = target.get("pack")
    if not isinstance(pack, dict): raise ValueError("target profile pack is invalid")
    pack.update({"vertical_slice_complete": False, "full_faction_complete": False, "asset_conversion_complete": False, "factionImportCoverage": receipts})
    target["id"] = "faction-slice-" + hashlib.sha256(_bytes(receipts)).hexdigest()[:16]
    return target, {"factions": receipts, "objects": deltas}

__all__ = ["compose_faction_profile"]
