"""Compose coverage-approved faction artifacts into an import profile."""
from __future__ import annotations
from copy import deepcopy
import hashlib, json
from pathlib import Path
from typing import Mapping, Sequence
from .faction_import import coverage_digest_payload
from .playable_structure_pack_compiler import validate_structure_visual_recipe
from .playable_unit_import import FACTIONS as _FACTION_ROWS, extend_profile_with_unit
from .retail_fords_completion_profile import (
    MEN_SELECTION_PACK_KEY,
    MEN_SELECTION_RESOURCES,
    MEN_SELECTION_RUNTIME_PATH,
)

# Authoritative BFME2 faction slugs (men, elves, dwarves, isengard, mordor,
# wild) come from the playable-unit FACTIONS registry.  RotWK 2.01 adds its own
# expansion factions; these are kept out of the BFME2 FACTIONS tuple (which
# carries BFME2-only policy invariants such as curated census roots) and are
# registered here so a RotWK faction is composable without inventing BFME2
# curations.  The per-faction vertical-slice pack convention is
# <game>-<faction>-vslice (e.g. bfme2-men-vslice, rotwk-angmar-vslice).
_ROTWK_FACTIONS = frozenset({"angmar"})
_KNOWN_FACTIONS = frozenset(row[0] for row in _FACTION_ROWS) | _ROTWK_FACTIONS

# Map an import game identity to its pack-id prefix.  BFME2 is the default so
# every existing bfme2 composition keeps its byte-identical id.
_GAME_PACK_PREFIX = {"bfme2": "bfme2", "rotwk": "rotwk"}
from .spellbook_pack_compiler import validate_spellbook_pack_recipe

def _bytes(value: object) -> bytes:
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False, allow_nan=False).encode()

def _unsigned_digest(document: Mapping[str, object], field: str) -> str:
    value = deepcopy(dict(document)); value.pop(field, None)
    return hashlib.sha256(_bytes(value)).hexdigest()

def _coverage_unsigned_digest(document: Mapping[str, object]) -> str:
    """Match build_faction_conversion aggregate (strips cacheHit / workers)."""
    return hashlib.sha256(_bytes(coverage_digest_payload(document))).hexdigest()

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

def _add_spellbook(profile: Mapping[str, object], recipe: Mapping[str, object], runtime: Mapping[str, object]) -> tuple[dict[str, object], dict[str, object]]:
    validate_spellbook_pack_recipe(recipe)
    if runtime.get("schema") != "openbfme.spellbook-runtime" or runtime.get("schemaVersion") != 0: raise ValueError("spellbook runtime schema is invalid")
    object_id = str(recipe.get("spellBookObjectId", ""))
    if object_id.casefold() != str(runtime.get("spellBookObjectId", "")).casefold(): raise ValueError("spellbook recipe/runtime Object identities differ")
    if recipe.get("recipeSha256") != runtime.get("recipeSha256"): raise ValueError("spellbook recipe/runtime digests differ")
    if runtime.get("runtimeSha256") != _unsigned_digest(runtime, "runtimeSha256"): raise ValueError("spellbook runtime digest is invalid")
    target = deepcopy(dict(profile)); resources = target.get("resources"); data = target.get("runtime_data"); pack = target.get("pack")
    if not isinstance(resources, list) or not isinstance(data, dict) or not isinstance(pack, dict): raise ValueError("base profile is not extensible")
    files = pack.get("files")
    if not isinstance(files, dict): raise ValueError("base profile pack has no file registry")
    existing = {str(row["id"]).casefold(): row for row in resources if isinstance(row, Mapping) and isinstance(row.get("id"), str)}
    added: list[str] = []
    raw_resources = recipe.get("resources")
    if not isinstance(raw_resources, list): raise ValueError("spellbook recipe resources are invalid")
    for raw in raw_resources:
        if not isinstance(raw, Mapping) or not isinstance(raw.get("id"), str): raise ValueError("spellbook recipe resource is invalid")
        row = deepcopy(dict(raw)); key = str(row["id"]).casefold()
        if key in existing:
            if dict(existing[key]) != row: raise ValueError(f"spellbook resource id collision: {row['id']}")
            continue
        resources.append(row); existing[key] = row; added.append(str(row["id"]))
    slug = _slug(str(runtime.get("slug", object_id))); runtime_path = f"data/spellbooks/{slug}.json"; file_key = f"spellbook.{slug}"
    old = data.get(runtime_path)
    if old is not None and old != runtime: raise ValueError(f"spellbook runtime path collision: {runtime_path}")
    for key, value in files.items():
        if str(key).casefold() == file_key.casefold() and str(key) != file_key: raise ValueError(f"spellbook file key collides by case: {file_key}")
        if str(value).casefold() == runtime_path.casefold() and str(key) != file_key: raise ValueError(f"spellbook runtime path has multiple owners: {runtime_path}")
    if file_key in files and files[file_key] != runtime_path: raise ValueError(f"spellbook file key collision: {file_key}")
    data[runtime_path] = deepcopy(dict(runtime)); files[file_key] = runtime_path
    return target, {"objectId": object_id, "runtimePath": runtime_path, "packFileKey": file_key, "resourceIds": sorted(added, key=str.casefold)}

def compose_faction_profile(base: Mapping[str, object], report_root: Path, factions: Sequence[str], *, game: str = "bfme2") -> tuple[dict[str, object], dict[str, object]]:
    """Add only artifacts bound to converted rows in digested coverage reports."""
    game_key = str(game).strip().casefold()
    game_prefix = _GAME_PACK_PREFIX.get(game_key)
    if game_prefix is None: raise ValueError(f"unsupported compose game: {game!r}")
    target = deepcopy(dict(base)); receipts: list[dict[str, object]] = []; deltas: list[dict[str, object]] = []; seen: set[str] = set(); ordered: list[str] = []
    for raw_faction in factions:
        faction = raw_faction.strip().lower()
        if not faction or faction in seen: raise ValueError(f"duplicate or empty faction: {raw_faction!r}")
        if faction not in _KNOWN_FACTIONS: raise ValueError(f"unknown faction: {raw_faction!r}")
        seen.add(faction); ordered.append(faction); coverage = _load(report_root / f"{faction}-coverage.json", "faction coverage")
        if coverage.get("schema") != "openbfme.faction-import-coverage" or coverage.get("schemaVersion") != 0: raise ValueError(f"unsupported faction coverage schema: {faction}")
        aggregate = coverage.get("aggregateSha256")
        if aggregate != _coverage_unsigned_digest(coverage):
            raise ValueError(f"faction coverage digest is invalid: {faction}")
        rows = coverage.get("objects"); summary = coverage.get("summary")
        if not isinstance(rows, list) or not isinstance(summary, Mapping): raise ValueError(f"faction coverage body is invalid: {faction}")
        converted = [r for r in rows if isinstance(r, Mapping) and r.get("status") == "converted"]
        if len(converted) != summary.get("convertedCount"): raise ValueError(f"faction converted count is inconsistent: {faction}")
        converted_index = {(str(r.get("family", "")), str(r.get("id", "")).casefold()) for r in converted}
        for row in sorted(converted, key=lambda r: (str(r.get("id", "")).casefold(), str(r.get("id", "")))):
            object_id = str(row.get("id", "")); root = report_root / faction / "objects" / object_id.casefold(); recipe = _load(root / "pack-recipe.json", "pack recipe")
            family = str(row.get("family", ""))
            recipe_object_id = recipe.get("spellBookObjectId") if family == "spellbook" else recipe.get("objectId")
            if recipe.get("recipeSha256") != row.get("recipeSha256"): raise ValueError(f"coverage/recipe identity mismatch: {faction}/{object_id}")
            alias_of = ""
            if recipe_object_id != object_id:
                # A horde-member row carries its parent horde's recipe. It is
                # valid only as an alias: the horde must stand as its own
                # converted playable-unit row, which then composes the recipe
                # under its own strict identity below.
                if family != "playable-unit" or not isinstance(recipe_object_id, str): raise ValueError(f"coverage/recipe identity mismatch: {faction}/{object_id}")
                if (family, recipe_object_id.casefold()) not in converted_index: raise ValueError(f"coverage/recipe identity mismatch: {faction}/{object_id}")
                alias_of = recipe_object_id
                deltas.append({"faction": faction, "family": family, "objectId": object_id, "aliasOf": alias_of})
                continue
            if family == "playable-unit": target, delta = extend_profile_with_unit(target, recipe)
            elif family == "structure":
                runtime = _load(root / "runtime.json", "structure runtime")
                if runtime.get("runtimeSha256") != row.get("runtimeSha256"): raise ValueError(f"coverage/runtime identity mismatch: {faction}/{object_id}")
                target, delta = _add_structure(target, recipe, runtime)
            elif family == "spellbook":
                runtime = _load(root / "runtime.json", "spellbook runtime")
                if runtime.get("runtimeSha256") != row.get("runtimeSha256"): raise ValueError(f"coverage/runtime identity mismatch: {faction}/{object_id}")
                target, delta = _add_spellbook(target, recipe, runtime)
            else: raise ValueError(f"converted row has unsupported family: {faction}/{object_id}")
            deltas.append({"faction": faction, "family": family, "objectId": object_id, **delta})
        receipts.append({"faction": faction, "coverageAggregateSha256": aggregate, "convertedCount": len(converted), "converterGapCount": int(summary.get("converterGapCount", 0)), "conversionComplete": bool(summary.get("conversionComplete", False))})
    pack = target.get("pack")
    if not isinstance(pack, dict): raise ValueError("target profile pack is invalid")
    if game_key != "bfme2":
        # Expansion factions publish LEAN supplemental packs: the BFME2 men
        # host payload (HUD APT bundle, Fords environment, host units) is
        # pinned to BFME2 1.06 bytes and can never resolve against an
        # expansion catalog, and the running slice already loads it from the
        # active host pack. Keep exactly the resources, runtime documents,
        # and pack file registrations this compose added; the base profile
        # remains only the validated structural skeleton.
        added_resource_ids = {
            str(rid).casefold()
            for delta in deltas
            for rid in delta.get("resourceIds", [])
        }
        added_paths = {str(d["runtimePath"]) for d in deltas if "runtimePath" in d}
        added_file_keys = {str(d["packFileKey"]) for d in deltas if "packFileKey" in d}
        resources = target.get("resources")
        runtime_data = target.get("runtime_data")
        files = pack.get("files")
        if not isinstance(resources, list) or not isinstance(runtime_data, dict) or not isinstance(files, dict):
            raise ValueError("target profile is not filterable")
        # Exception to the lean filter: the universal SHADOW_MERGE_DECAL
        # selection contract stays. Every faction pack ships the identical
        # contract so a solo-mounted expansion pack binds retail selection
        # decals without depending on a host pack being mounted; its two
        # source textures resolve through the expansion's layered install
        # (the BFME2 layer supplies the bytes). Fail closed if the base
        # profile does not carry the contract to keep the universality
        # invariant honest.
        selection_resource_ids = {
            str(row["id"]).casefold() for row in MEN_SELECTION_RESOURCES
        }
        base_resource_ids = {
            str(row.get("id", "")).casefold() for row in resources if isinstance(row, Mapping)
        }
        if (
            not selection_resource_ids <= base_resource_ids
            or MEN_SELECTION_RUNTIME_PATH not in runtime_data
            or files.get(MEN_SELECTION_PACK_KEY) != MEN_SELECTION_RUNTIME_PATH
        ):
            raise ValueError(
                "expansion base profile is missing the universal selection-decal contract"
            )
        added_resource_ids |= selection_resource_ids
        added_paths.add(MEN_SELECTION_RUNTIME_PATH)
        added_file_keys.add(MEN_SELECTION_PACK_KEY)
        missing_owned = added_resource_ids - {
            str(row.get("id", "")).casefold() for row in resources if isinstance(row, Mapping)
        }
        if missing_owned:
            raise ValueError("lean expansion pack lost owned resources: " + ", ".join(sorted(missing_owned)))
        target["resources"] = [
            row for row in resources
            if isinstance(row, Mapping) and str(row.get("id", "")).casefold() in added_resource_ids
        ]
        target["runtime_data"] = {k: v for k, v in runtime_data.items() if str(k) in added_paths}
        pack["files"] = {k: v for k, v in files.items() if str(k) in added_file_keys}
        if len(target["runtime_data"]) != len(added_paths) or len(pack["files"]) != len(added_file_keys):
            raise ValueError("lean expansion pack lost owned runtime documents")
    # Bind the composed pack to its faction's vertical-slice id rather than
    # inheriting the base profile's (Men) id, so a non-Men publish lands under
    # bfme2-<faction>-vslice/ instead of stray-bundling under bfme2-men-vslice/.
    # A single-faction publish is the only shape the CLI emits; when exactly one
    # faction is composed we own the pack id deterministically.
    if len(ordered) == 1: pack["id"] = f"{game_prefix}-{ordered[0]}-vslice"
    pack.update({"vertical_slice_complete": False, "full_faction_complete": False, "asset_conversion_complete": False, "factionImportCoverage": receipts})
    target["id"] = "faction-slice-" + hashlib.sha256(_bytes(receipts)).hexdigest()[:16]
    return target, {"factions": receipts, "objects": deltas}

__all__ = ["compose_faction_profile"]
