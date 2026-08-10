"""Compose coverage-approved faction artifacts into an import profile."""
from __future__ import annotations
from copy import deepcopy
import hashlib, json, re
from pathlib import Path
from typing import Mapping, Sequence
from .cah_model_pack import CAH_MODEL_PACK_ROOT
from .cah_system_compiler import CahSystemCompilerError, validate_cah_system_runtime
from .faction_import import coverage_digest_payload
from .interface_art import PACK_INDEX_SCHEMA as INTERFACE_ART_SCHEMA
from .livingworld import LIVING_WORLD_PACK_PATH
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

# --- pack strings lane -----------------------------------------------------
# Every CONTROLBAR: id a shipped runtime document references is HUD text the
# control bar will try to draw.  The gate
# (game/tests/hud_string_completeness_runner.gd) scrapes the published pack's
# JSON text with exactly this regex, so "reference" here MUST mean the same
# thing: any quoted CONTROLBAR: string value anywhere in a runtime document.
STRINGS_RUNTIME_PATH = "data/strings.json"
STRINGS_PACK_KEY = "strings"
_CONTROLBAR_REFERENCE = re.compile(r'"(CONTROLBAR:[^"]+)"')

# The Create-a-Hero class table.  Compiled by cah_system_compiler from the
# effective-assets oracle; published only by the RotWK Men host pack (the
# established owner of global strategic documents -- see the livingWorld
# handling in compose_faction_profile).
CAH_SYSTEM_RUNTIME_PATH = "data/cah/system.json"
CAH_SYSTEM_PACK_KEY = "cah.system"

# The retail interface-art index (every MappedImage the corpus references,
# cut verbatim from its compiled atlas).  Published by the host Men pack for
# the same single-owner reason as livingWorld and cah.system: two mounted
# owners of one document resolve by mount order, which is not a contract.
# Scope is pack-wide, never CaH-only -- the index is what makes retail icons
# resolve at all, and a per-faction slice of it would leave the other
# factions' buttons blank.
INTERFACE_ART_RUNTIME_PATH = "data/interface-art/index.json"
INTERFACE_ART_PACK_KEY = "interfaceArt"


def _referenced_controlbar_ids(runtime_data: Mapping[str, object]) -> set[str]:
    """Every CONTROLBAR: id the composed runtime documents reference.

    Scans the canonical JSON serialization of each document, matching the HUD
    completeness gate's own scrape (`"(CONTROLBAR:[^"]+)"` over file text).
    The strings document itself is excluded: its keys are answers, not
    questions, and they enter the composed document through the merge lane.
    """

    references: set[str] = set()
    for path, document in runtime_data.items():
        if str(path) == STRINGS_RUNTIME_PATH:
            continue
        text = json.dumps(
            document, sort_keys=True, ensure_ascii=False, allow_nan=False
        )
        references.update(_CONTROLBAR_REFERENCE.findall(text))
    return references


def _build_strings_document(
    references: set[str],
    string_catalog,
    existing: Mapping[str, object] | None,
) -> tuple[dict[str, object], dict[str, object]]:
    """Resolve referenced ids against the retail table into a pack document.

    Returns ``(document, stats)``.  Every referenced id lands in exactly one
    bucket: a resolved row in ``strings`` (retail has the text -- shipped
    verbatim, including retail's own empty rows) or ``sourceNullStringIds``
    (the LAYERED retail table has NO row: the compiler's retail-absent
    evidence, which the gate derives its unfixable set from).  Rows an
    existing base document carries for ids this scan did not reference are
    kept, never dropped -- the BFME2 census-selected rows stay authoritative
    for their ids.
    """

    resolved: dict[str, str] = {}
    source_null: set[str] = set()
    for identifier in sorted(references, key=lambda item: (item.casefold(), item)):
        record = string_catalog.record(identifier)
        if record is None:
            source_null.add(identifier)
        else:
            # Key under the retail table's own spelling so two referenced
            # casings of one id collapse to the single retail row.
            resolved[record.identifier] = record.value
    resolved_count = len(resolved)
    carried_count = 0
    if existing is not None:
        rows = existing.get("strings")
        if isinstance(rows, Mapping):
            folded = {key.casefold() for key in resolved}
            for key in sorted(rows, key=lambda item: (str(item).casefold(), str(item))):
                value = rows[key]
                if (
                    isinstance(key, str)
                    and isinstance(value, str)
                    and key.casefold() not in folded
                ):
                    resolved[key] = value
                    folded.add(key.casefold())
                    carried_count += 1
    document: dict[str, object] = {
        "schema": "openbfme.localized-strings",
        "schemaVersion": 0,
        "locale": "en",
        # Matches the BFME2 leaf-profile precedent: this is a referenced-id
        # selection, never the whole retail table.
        "complete": False,
        "strings": dict(
            sorted(resolved.items(), key=lambda item: (item[0].casefold(), item[0]))
        ),
    }
    if source_null:
        document["sourceNullStringIds"] = sorted(
            source_null, key=lambda item: (item.casefold(), item)
        )
    stats = {
        "runtimePath": STRINGS_RUNTIME_PATH,
        "packFileKey": STRINGS_PACK_KEY,
        "referencedCount": len(references),
        "resolvedCount": resolved_count,
        "carriedCount": carried_count,
        "sourceNullCount": len(source_null),
    }
    return document, stats


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

def _append_pack_resources(
    target: dict[str, object],
    rows: Sequence[Mapping[str, object]],
    label: str,
) -> list[str]:
    """Append lane-owned resources to the composed profile, fail-closed.

    Runs AFTER the lean-expansion filter for the same reason the cah.system
    and strings documents do: these are emissions of THIS compose, not base
    profile rows that have to survive a filter.  An id or output that already
    exists refuses rather than silently overwriting another owner's bytes.
    """

    resources = target.get("resources")
    if not isinstance(resources, list):
        raise ValueError(f"{label} resources cannot be appended: profile has none")
    known_ids = {
        str(row.get("id", "")).casefold()
        for row in resources
        if isinstance(row, Mapping)
    }
    known_outputs = {
        str(row.get("output", "")).casefold()
        for row in resources
        if isinstance(row, Mapping) and row.get("output")
    }
    added: list[str] = []
    for raw in rows:
        if not isinstance(raw, Mapping) or not isinstance(raw.get("id"), str):
            raise ValueError(f"{label} resource row is invalid")
        row = deepcopy(dict(raw))
        key = str(row["id"]).casefold()
        if key in known_ids:
            raise ValueError(f"{label} resource id collision: {row['id']}")
        output = str(row.get("output", "")).casefold()
        if output and output in known_outputs:
            raise ValueError(f"{label} resource output collision: {row['output']}")
        resources.append(row)
        known_ids.add(key)
        if output:
            known_outputs.add(output)
        added.append(str(row["id"]))
    return added


def compose_faction_profile(
    base: Mapping[str, object],
    report_root: Path,
    factions: Sequence[str],
    *,
    game: str = "bfme2",
    string_catalog=None,
    cah_runtime: Mapping[str, object] | None = None,
    cah_model_resources: Sequence[Mapping[str, object]] | None = None,
    interface_art: tuple[Sequence[Mapping[str, object]], Mapping[str, object]]
    | None = None,
) -> tuple[dict[str, object], dict[str, object]]:
    """Add only artifacts bound to converted rows in digested coverage reports.

    ``string_catalog`` is the parsed retail string table this cook resolves
    against (``faction_import.load_retail_string_catalog``).  When supplied,
    the composed pack ships a ``data/strings.json`` covering every CONTROLBAR:
    id its own runtime documents reference, with retail-absent ids recorded as
    ``sourceNullStringIds`` evidence.  When ``None`` the lane is skipped -- a
    legacy shape kept for direct callers; the publish CLI always supplies it.

    ``cah_runtime`` is a validated ``openbfme.cah-system-runtime`` document.
    Only the RotWK Men host compose may carry it (the same single-owner rule
    as livingWorld); an invalid or misaddressed document refuses the compose.

    ``cah_model_resources`` are the profile resources that convert the meshes
    that table selects (``cah_model_pack``); they ride with it and share its
    single-owner rule.  ``interface_art`` is ``(resources, index document)``
    from ``interface_art_pack``: the pack-wide retail icon index, owned by the
    Men host pack for the same reason.
    """
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
            if family in {"playable-unit", "banner-carrier"}: target, delta = extend_profile_with_unit(target, recipe)
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
        # The selected RotWK Men pack is the host pack for global strategic
        # data.  Preserve a generated Living World document only there; every
        # supplemental faction remains lean and never duplicates ownership.
        living_world_key = "livingWorld"
        has_living_world_data = LIVING_WORLD_PACK_PATH in runtime_data
        has_living_world_file = files.get(living_world_key) == LIVING_WORLD_PACK_PATH
        if has_living_world_data != has_living_world_file:
            raise ValueError(
                "RotWK Men Living World document and pack registration must coexist"
            )
        if ordered == ["men"] and has_living_world_data:
            added_paths.add(LIVING_WORLD_PACK_PATH)
            added_file_keys.add(living_world_key)
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
    runtime_data = target.get("runtime_data")
    files = pack.get("files")
    if not isinstance(runtime_data, dict) or not isinstance(files, dict):
        raise ValueError("target profile is not extensible for pack documents")
    strings_receipt: dict[str, object] | None = None
    cah_receipt: dict[str, object] | None = None
    # --- Create-a-Hero system table ----------------------------------------
    # Registered BEFORE the strings scan so any CONTROLBAR: id the table
    # carries is covered by the strings document like every other reference.
    # Both lanes run after the lean expansion filter: their documents are pack
    # emissions of THIS compose and must survive it by construction, not by
    # being smuggled into the added-path allowlists.
    if cah_runtime is not None:
        # Single-owner rule, same shape as livingWorld above: the RotWK Men
        # host pack owns global strategic documents. Publishing the table from
        # a supplemental faction would create two mounted owners whose
        # last-wins resolution depends on mount order.
        if game_key != "rotwk" or ordered != ["men"]:
            raise ValueError(
                "cah.system is owned by the RotWK Men host pack; refusing to "
                f"publish it from game={game_key!r} factions={ordered!r}"
            )
        try:
            validate_cah_system_runtime(cah_runtime)
        except CahSystemCompilerError as exc:
            raise ValueError(f"cah system runtime document is invalid: {exc}") from exc
        existing_cah = runtime_data.get(CAH_SYSTEM_RUNTIME_PATH)
        if existing_cah is not None and existing_cah != dict(cah_runtime):
            raise ValueError(f"cah runtime path collision: {CAH_SYSTEM_RUNTIME_PATH}")
        cah_owner = files.get(CAH_SYSTEM_PACK_KEY)
        if cah_owner is not None and str(cah_owner) != CAH_SYSTEM_RUNTIME_PATH:
            raise ValueError(f"cah.system pack registration has a foreign owner: {cah_owner}")
        runtime_data[CAH_SYSTEM_RUNTIME_PATH] = deepcopy(dict(cah_runtime))
        files[CAH_SYSTEM_PACK_KEY] = CAH_SYSTEM_RUNTIME_PATH
        cah_receipt = {
            "runtimePath": CAH_SYSTEM_RUNTIME_PATH,
            "packFileKey": CAH_SYSTEM_PACK_KEY,
            "runtimeSha256": cah_runtime["runtimeSha256"],
        }
    # --- Create-a-Hero meshes ----------------------------------------------
    # The table above names 34 meshes by id; without these resources the pack
    # ships a hero roster whose every entry resolves to nothing.  They ride
    # with the table under the same single-owner rule.
    cah_model_receipt: dict[str, object] | None = None
    if cah_model_resources:
        if cah_runtime is None:
            raise ValueError(
                "cah model resources require the cah.system table they are "
                "derived from; refusing to ship meshes with no roster"
            )
        added = _append_pack_resources(target, cah_model_resources, "cah model")
        cah_model_receipt = {
            "packRoot": CAH_MODEL_PACK_ROOT,
            "resourceIds": added,
            "modelOutputs": sorted(
                (
                    str(row["output"])
                    for row in cah_model_resources
                    if isinstance(row, Mapping) and row.get("output")
                ),
                key=str.casefold,
            ),
        }
    # --- retail interface art ----------------------------------------------
    interface_art_receipt: dict[str, object] | None = None
    if interface_art is not None:
        art_resources, art_document = interface_art
        if ordered != ["men"]:
            raise ValueError(
                "the interface-art index is owned by the Men host pack; "
                f"refusing to publish it from factions={ordered!r}"
            )
        if (
            not isinstance(art_document, Mapping)
            or art_document.get("schema") != INTERFACE_ART_SCHEMA
            or not isinstance(art_document.get("images"), Mapping)
        ):
            raise ValueError("interface-art index document is invalid")
        existing_art = runtime_data.get(INTERFACE_ART_RUNTIME_PATH)
        if existing_art is not None and existing_art != dict(art_document):
            raise ValueError(
                f"interface-art runtime path collision: {INTERFACE_ART_RUNTIME_PATH}"
            )
        art_owner = files.get(INTERFACE_ART_PACK_KEY)
        if art_owner is not None and str(art_owner) != INTERFACE_ART_RUNTIME_PATH:
            raise ValueError(
                f"interfaceArt pack registration has a foreign owner: {art_owner}"
            )
        added_art = _append_pack_resources(target, art_resources, "interface art")
        runtime_data[INTERFACE_ART_RUNTIME_PATH] = deepcopy(dict(art_document))
        files[INTERFACE_ART_PACK_KEY] = INTERFACE_ART_RUNTIME_PATH
        interface_art_receipt = {
            "runtimePath": INTERFACE_ART_RUNTIME_PATH,
            "packFileKey": INTERFACE_ART_PACK_KEY,
            "resourceCount": len(added_art),
            "imageCount": len(art_document["images"]),
            "gapCount": len(art_document.get("gaps") or []),
        }
    # --- pack strings lane -------------------------------------------------
    if string_catalog is not None:
        existing_strings = runtime_data.get(STRINGS_RUNTIME_PATH)
        if existing_strings is not None and not isinstance(existing_strings, Mapping):
            raise ValueError("existing pack strings document is invalid")
        strings_owner = files.get(STRINGS_PACK_KEY)
        if strings_owner is not None and str(strings_owner) != STRINGS_RUNTIME_PATH:
            raise ValueError(f"strings pack registration has a foreign owner: {strings_owner}")
        strings_document, strings_receipt = _build_strings_document(
            _referenced_controlbar_ids(runtime_data),
            string_catalog,
            existing_strings,
        )
        runtime_data[STRINGS_RUNTIME_PATH] = strings_document
        files[STRINGS_PACK_KEY] = STRINGS_RUNTIME_PATH
    # Bind the composed pack to its faction's vertical-slice id rather than
    # inheriting the base profile's (Men) id, so a non-Men publish lands under
    # bfme2-<faction>-vslice/ instead of stray-bundling under bfme2-men-vslice/.
    # A single-faction publish is the only shape the CLI emits; when exactly one
    # faction is composed we own the pack id deterministically.
    if len(ordered) == 1: pack["id"] = f"{game_prefix}-{ordered[0]}-vslice"
    pack.update({"vertical_slice_complete": False, "full_faction_complete": False, "asset_conversion_complete": False, "factionImportCoverage": receipts})
    target["id"] = "faction-slice-" + hashlib.sha256(_bytes(receipts)).hexdigest()[:16]
    receipt: dict[str, object] = {"factions": receipts, "objects": deltas}
    if strings_receipt is not None:
        receipt["strings"] = strings_receipt
    if cah_receipt is not None:
        receipt["cahSystem"] = cah_receipt
    if cah_model_receipt is not None:
        receipt["cahModels"] = cah_model_receipt
    if interface_art_receipt is not None:
        receipt["interfaceArt"] = interface_art_receipt
    return target, receipt

__all__ = [
    "CAH_SYSTEM_PACK_KEY",
    "CAH_SYSTEM_RUNTIME_PATH",
    "INTERFACE_ART_PACK_KEY",
    "INTERFACE_ART_RUNTIME_PATH",
    "STRINGS_PACK_KEY",
    "STRINGS_RUNTIME_PATH",
    "compose_faction_profile",
]
