"""Payload-private M3 Men profile composition.

The tracked v1 profile is a declarative recipe. Retail values are emitted only
into a private generated profile by these bounded helpers.
"""

from __future__ import annotations

from copy import deepcopy
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import tempfile
from typing import Any, Iterable, Mapping, Sequence

from .paths import safe_relative_parts
from .profile import MAX_PATTERNS_PER_RESOURCE, MAX_RESOURCES
from .sage_cst import SageAssignment, SageBlock, SageObject, parse_sage_document
from .sage_ini import IniBlock, parse_flat_named_blocks
from .retail_unit_rules import (
    RANGER_UNIT_SPEC,
    extract_retail_unit_rules,
    retail_unit_rule_source_paths,
)
from .util import write_json_atomic
from .w3d_metadata import scan_w3d_metadata


RECIPE_SCHEMA = "openbfme.m3-men-pack-recipe"
BUILDINGS = (
    "MenFortress", "GondorFarm", "GondorBarracks", "GondorArcherRange",
    "GondorStable", "GondorWorkshop", "GondorBattleTower", "GondorWell",
    "GondorStatue", "GondorForge", "GondorMarketPlace", "GondorCastleWallHub",
)
BUILDING_COMMAND_ALIASES = {
    "MenFortress": (
        "MenFortressCitadel",
        "MenFortressExpansionPadCorner",
        "MenFortressExpansionPadSide",
    ),
}
UNITS = ("GondorTrebuchet", "GondorCavalry", "GondorTowerShieldGuard", "GondorRanger")
NEW_BUILDINGS = (
    "GondorWorkshop", "GondorBattleTower", "GondorWell", "GondorStatue",
    "GondorForge", "GondorMarketPlace", "GondorCastleWallHub",
)
NEW_UNITS = ("GondorTrebuchet", "GondorRanger")
CONVERSION_SOURCE_GAPS = {
    "m3-gondorbattletower-gbbtltwrs-bib": (
        "Retail GBBTLTWRS_BIB exists, but the existing hierarchical W3D converter "
        "fails its source-backed skin-validation proof."
    ),
    "m3-gondorstatue-gphealstue": (
        "Retail GPHEALSTUE and GPHEALSTUE_D3 exist, but the existing animated W3D "
        "converter fails its source-backed rig-resolution proof."
    ),
    "m3-gondormarketplace-gbmarket-d1": (
        "Retail GBMARKET_D1 exists, but the existing hierarchical W3D converter "
        "fails its source-backed skin-validation proof."
    ),
}
UPGRADES = (
    "Upgrade_GondorHeavyArmor", "Upgrade_GondorFireArrows",
    "Upgrade_GondorForgedBlades", "Upgrade_GondorBasicTraining",
)
RUNTIME_PATHS = {
    "buildingStats": "data/building-stats.json",
    "icons": "data/m3/icon-census.json",
    "upgrades": "data/m3/upgrades.json",
    "spellbook": "data/spellbook.json",
    "gameData": "data/game-data.json",
    "houseColor": "data/house-color.json",
    "selectionTransitions": "data/m3/selection-transitions.json",
    "models": "data/m3/model-census.json",
    "rangerRuntime": "data/m3/ranger-runtime.json",
    "trebuchetRuntime": "data/m3/trebuchet-runtime.json",
}
RANGER_RUNTIME_PATH = RUNTIME_PATHS["rangerRuntime"]
RANGER_RUNTIME_SCHEMA = "openbfme.ranger-runtime-contract"
TREBUCHET_RUNTIME_PATH = RUNTIME_PATHS["trebuchetRuntime"]
TREBUCHET_RUNTIME_SCHEMA = "openbfme.trebuchet-runtime-contract"
BUILDING_RUNTIME_PATH = "data/m3/building-runtime.json"
BUILDING_RUNTIME_SCHEMA = "openbfme.building-runtime-capabilities"
BUILDING_RUNTIME_SCOPE = "bfme2-106-men-ordinary-buildings-v0"
BUILDING_RUNTIME_REQUESTED_IDS = (
    "GondorWorkshop",
    "GondorBattleTower",
    "GondorWell",
    "GondorStatue",
    "GondorForge",
    "GondorMarketPlace",
)
BUILDING_COMMAND_OWNERS = {"MenFortress": "MenFortressCitadel"}
PORTER_CONSTRUCT_TARGETS = {
    "GondorCastleWallHub": ("MenWallHubSmallOuter", "MenWallHubSmall"),
}
PORTER_CONSTRUCT_COMMAND_TARGETS = {
    "GondorCastleWallHub": (
        ("Command_PorterConstructMenWallHubOuter", "MenWallHubSmallOuter"),
        ("Command_PorterConstructMenWallHub", "MenWallHubSmall"),
    ),
}
COMMAND_BUTTON_PATH = "data/ini/commandbutton.ini"
COMMAND_SET_PATH = "data/ini/commandset.ini"
GAMEDATA_PATH = "data/ini/gamedata.ini"
UPGRADE_PATH = "data/ini/upgrade.ini"
ARCHERY_RANGE_PATH = "data/ini/object/goodfaction/structures/men/archerrange.ini"
WEAPON_PATH = "data/ini/weapon.ini"
WORKSHOP_PATH = "data/ini/object/goodfaction/structures/men/workshop.ini"
TREBUCHET_PATH = "data/ini/object/goodfaction/units/men/trebuchet.ini"
GOOD_SUBOBJECTS_PATH = "data/ini/object/goodfaction/goodfactionsubobjects.ini"
TREBUCHET_PROJECTILE_W3D = "art/w3d/gu/gusiegtrerk.w3d"
TREBUCHET_PROJECTILE_OUTPUT = "assets/models/m3/projectiles/gondor-trebuchet-rock.glb"
SELECTION_TRANSITIONS = {
    "GondorFighter": ("GUManMocap_ATNA", "GUManMocap_ATNB", "GUManMocap_ATND"),
    "GondorArcher": ("GUArcher_ATNA", "GUArcher_ATNB", "GUArcher_ATNC"),
}
UNIT_MODEL_RECIPES = {
    "GondorTrebuchet": {
        "model": "art/w3d/gu/gusiegtreb_skn.w3d",
        "animations": (
            "art/w3d/gu/gusiegtreb_idla.w3d",
            "art/w3d/gu/gusiegtreb_wlka.w3d",
            "art/w3d/gu/gusiegtreb_atak.w3d",
        ),
        # Retail changes the DYING drawable itself. DIEA owns a separate
        # hierarchy and embeds its animation; it is not a clip for SKL.
        "embedded_models": {
            "death": "art/w3d/gu/gusiegtreb_diea.w3d",
        },
    },
    "GondorRanger": {
        "model": "art/w3d/gu/guranger_skn.w3d",
        "animations": (
            "art/w3d/gu/guranger_idla.w3d",
            "art/w3d/gu/guranger_runa.w3d",
            "art/w3d/gu/guranger_atkd1.w3d",
            "art/w3d/gu/guranger_diea.w3d",
        ),
    },
}
BASE_BUILDING_OUTPUTS = {
    "MenFortress": "assets/models/structures/men-fortress/intact.glb",
    "GondorFarm": "assets/models/structures/men-farm/intact.glb",
    "GondorBarracks": "assets/models/structures/men-barracks/intact.glb",
    "GondorArcherRange": "assets/models/structures/men-archery-range/intact.glb",
    "GondorStable": "assets/models/structures/men-stable/intact.glb",
}
BASE_UNIT_OUTPUTS = {
    "GondorFighter": "assets/models/units/gondor-fighter.glb",
    "GondorArcher": "assets/models/units/gondor-archer.glb",
    "GondorCavalry": "assets/models/units/gondor-knight.glb",
    "GondorTowerShieldGuard": "assets/models/units/gondor-tower-guard.glb",
    "MenPorter": "assets/models/units/men-porter.glb",
}
CP_FIELDS = (
    "GoodCommandPointLimit", "GoodCommandPoints", "GoodCommandPointsBonus",
    "GoodCommandPointsAI", "GoodCommandPointsMP2", "GoodCommandPointsMP3",
    "GoodCommandPointsMP4", "GoodCommandPointsMP5", "GoodCommandPointsMP6",
    "GoodCommandPointsMP7", "GoodCommandPointsMP8",
)
_SHA = re.compile(r"^[0-9a-f]{64}$")
_ID = re.compile(r"^[A-Za-z_][A-Za-z0-9_.:+\-']{0,255}$")
_ASSIGN = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.*?)\s*$")


def _obj(value: Any, label: str) -> Mapping[str, Any]:
    if not isinstance(value, Mapping):
        raise ValueError(f"{label} must be an object")
    return value


def _list(value: Any, label: str) -> list[Any]:
    if not isinstance(value, list):
        raise ValueError(f"{label} must be an array")
    return value


def _text(value: Any, label: str, limit: int = 512) -> str:
    if not isinstance(value, str) or not value or value != value.strip() or len(value) > limit or "\0" in value:
        raise ValueError(f"{label} must be a bounded nonempty string")
    return value


def _id(value: Any, label: str) -> str:
    result = _text(value, label, 256)
    if _ID.fullmatch(result) is None:
        raise ValueError(f"{label} must be an identifier")
    return result


def _unique(values: Any, label: str) -> tuple[str, ...]:
    result: dict[str, str] = {}
    for index, value in enumerate(_list(values, label)):
        item = _text(value, f"{label}[{index}]")
        if item.casefold() in result:
            raise ValueError(f"{label} contains duplicates")
        result[item.casefold()] = item
    return tuple(sorted(result.values(), key=lambda x: (x.casefold(), x)))


def _defs(report: Mapping[str, Any], name: str) -> dict[str, Mapping[str, Any]]:
    rows = _list(_obj(report.get("definitions"), "definitions").get(name), f"definitions.{name}")
    result: dict[str, Mapping[str, Any]] = {}
    for index, value in enumerate(rows):
        row = _obj(value, f"definitions.{name}[{index}]")
        identifier = _id(row.get("id"), f"definitions.{name}[{index}].id")
        if identifier.casefold() in result:
            raise ValueError(f"duplicate {name} definition: {identifier}")
        result[identifier.casefold()] = row
    return result


def _fields(row: Mapping[str, Any], key: str) -> tuple[str, ...]:
    fields = _obj(row.get("fields"), f"{row.get('id')} fields")
    values: dict[str, str] = {}
    for index, raw in enumerate(_list(fields.get(key, []), f"{row.get('id')} {key}")):
        value = _text(raw, f"{row.get('id')} {key}[{index}]")
        previous = values.get(value.casefold())
        if previous is not None and previous != value:
            raise ValueError(f"{row.get('id')} {key} is case-ambiguous")
        values[value.casefold()] = value
    return tuple(sorted(values.values(), key=lambda item: (item.casefold(), item)))


def _dedupe(values: Any, label: str) -> tuple[str, ...]:
    result: dict[str, str] = {}
    for index, raw in enumerate(_list(values, label)):
        value = _text(raw, f"{label}[{index}]")
        previous = result.get(value.casefold())
        if previous is not None and previous != value:
            raise ValueError(f"{label} is case-ambiguous")
        result[value.casefold()] = value
    return tuple(sorted(result.values(), key=lambda item: (item.casefold(), item)))


def build_icon_census(report: Mapping[str, Any]) -> dict[str, Any]:
    objects, sets, buttons = _defs(report, "objects"), _defs(report, "commandSets"), _defs(report, "commandButtons")
    images = {
        _id(_obj(row, "mapped image").get("id"), "mapped image id").casefold()
        for row in _list(_obj(report.get("resolvedLeaves"), "resolvedLeaves").get("mappedImages"), "mappedImages")
    }
    buildings, gaps = [], []
    for identifier in BUILDINGS:
        row = objects.get(identifier.casefold())
        if row is None:
            raise ValueError(f"missing building definition: {identifier}")
        set_ids, portraits = set(), set()
        command_owners = [row]
        for alias in BUILDING_COMMAND_ALIASES.get(identifier, ()):
            alias_row = objects.get(alias.casefold())
            if alias_row is not None:
                command_owners.append(alias_row)
        for owner in command_owners:
            owner_id = _id(owner.get("id"), "building command owner id")
            for raw_edge in _list(owner.get("edges"), f"{owner_id} edges"):
                edge = _obj(raw_edge, f"{owner_id} edge")
                field, target = _text(edge.get("field"), "edge field"), _text(edge.get("targetId"), "edge target")
                if field.casefold() == "commandset": set_ids.add(target)
                if field.casefold() == "selectportrait": portraits.add(target)
        commands: dict[str, dict[str, Any]] = {}
        for set_id in sorted(set_ids, key=str.casefold):
            set_row = sets.get(set_id.casefold())
            if set_row is None:
                raise ValueError(f"missing command set: {set_id}")
            for button_id in _unique(set_row.get("buttons"), f"{set_id} buttons"):
                button = buttons.get(button_id.casefold())
                if button is None:
                    raise ValueError(f"missing command button: {button_id}")
                icon_ids = _fields(button, "ButtonImage")
                command_kinds = {item.casefold() for item in _fields(button, "Command")}
                dynamic_revive_icon = not icon_ids and command_kinds == {"revive"}
                present = bool(icon_ids) and all(item.casefold() in images for item in icon_ids)
                text_ids = sorted(set(_fields(button, "TextLabel") + _fields(button, "DescriptLabel")), key=str.casefold)
                commands.setdefault(button_id.casefold(), {"id": button_id, "images": list(icon_ids), "iconSource": "unresolved-runtime-hero-slot" if dynamic_revive_icon else "mapped-image", "textIds": text_ids, "iconPresent": present})
                if not present:
                    gaps.append({"id": f"{identifier}.{button_id}.ButtonImage", "building": identifier, "command": button_id, "reason": "unresolved-dynamic-hero-slot" if dynamic_revive_icon else "no-authored-or-resolved-image"})
        construction_commands: list[dict[str, Any]] = []
        for button in buttons.values():
            if identifier.casefold() not in {item.casefold() for item in _fields(button, "Object")}:
                continue
            command_kinds = {item.casefold() for item in _fields(button, "Command")}
            if not command_kinds.intersection({"dozer_construct", "object_upgrade"}):
                continue
            button_id = _id(button.get("id"), "construction command button id")
            icon_ids = _fields(button, "ButtonImage")
            present = bool(icon_ids) and all(item.casefold() in images for item in icon_ids)
            text_ids = sorted(set(_fields(button, "TextLabel") + _fields(button, "DescriptLabel")), key=str.casefold)
            construction_commands.append(
                {"id": button_id, "images": list(icon_ids), "textIds": text_ids, "iconPresent": present}
            )
            if not present:
                gaps.append(
                    {
                        "id": f"{identifier}.{button_id}.ButtonImage",
                        "building": identifier,
                        "command": button_id,
                        "reason": "building-construction-icon-unresolved",
                    }
                )
        construction_commands.sort(key=lambda item: str(item["id"]).casefold())
        if not construction_commands:
            gaps.append(
                {
                    "id": f"{identifier}.ConstructionCommand",
                    "building": identifier,
                    "command": "building-construction",
                    "reason": "no-authored-construction-command",
                }
            )
        portrait_rows = [{"id": item, "iconPresent": item.casefold() in images} for item in sorted(portraits, key=str.casefold)]
        for item in portrait_rows:
            if not item["iconPresent"]:
                gaps.append({"id": f"{identifier}.SelectPortrait", "building": identifier, "command": "building-portrait", "reason": "unresolved-image"})
        buildings.append({"id": identifier, "portraits": portrait_rows, "constructionCommands": construction_commands, "commandSets": sorted(set_ids, key=str.casefold), "commands": [commands[key] for key in sorted(commands)]})
    return {
        "schema": "openbfme.m3-icon-census", "schemaVersion": 0, "complete": not gaps,
        "buildings": buildings, "missing": gaps,
        "summary": {"buildingCount": len(buildings), "buildingButtonCount": sum(len(x["constructionCommands"]) for x in buildings), "commandCount": sum(len(x["commands"]) for x in buildings), "dynamicIconCount": sum(1 for x in buildings for command in x["commands"] if command["iconSource"] == "unresolved-runtime-hero-slot"), "missingCount": len(gaps)},
    }


def build_upgrade_manifest(report: Mapping[str, Any]) -> dict[str, Any]:
    definitions = _defs(report, "upgrades")
    resolved_images = {
        _id(_obj(row, "mapped image").get("id"), "mapped image id").casefold()
        for row in _list(_obj(report.get("resolvedLeaves"), "resolvedLeaves").get("mappedImages"), "mappedImages")
    }
    rows = []
    for identifier in UPGRADES:
        definition = definitions.get(identifier.casefold())
        if definition is None:
            raise ValueError(f"missing required upgrade definition: {identifier}")
        references = _obj(definition.get("references"), f"upgrade {identifier} references")
        icons = _unique(references.get("mappedImages", []), f"upgrade {identifier} icons")
        if not icons or any(icon.casefold() not in resolved_images for icon in icons):
            raise ValueError(f"required upgrade has no resolved icon: {identifier}")
        digest = _text(definition.get("definitionSha256"), f"upgrade {identifier} digest", 64).casefold()
        if _SHA.fullmatch(digest) is None:
            raise ValueError(f"upgrade {identifier} has invalid definition digest")
        rows.append({
            "id": identifier,
            "definitionSha256": digest,
            "icons": list(icons),
            "textIds": list(_unique(references.get("localizedStrings", []), f"upgrade {identifier} text ids")),
        })
    return {"schema": "openbfme.m3-upgrades", "schemaVersion": 0, "upgrades": rows, "count": len(rows)}


def _block_index(source: bytes, kind: str) -> dict[str, IniBlock]:
    result: dict[str, IniBlock] = {}
    for block in parse_flat_named_blocks(source, kind):
        if block.name.casefold() in result:
            raise ValueError(f"ambiguous {kind}: {block.name}")
        result[block.name.casefold()] = block
    return result


def _block_groups(source: bytes, kind: str) -> dict[str, tuple[IniBlock, ...]]:
    grouped: dict[str, list[IniBlock]] = {}
    for block in parse_flat_named_blocks(source, kind):
        grouped.setdefault(block.name.casefold(), []).append(block)
    return {key: tuple(values) for key, values in grouped.items()}


def _unambiguous_block(
    groups: Mapping[str, tuple[IniBlock, ...]], identifier: str, kind: str
) -> IniBlock:
    candidates = groups.get(identifier.casefold(), ())
    if not candidates:
        raise ValueError(f"missing {kind}: {identifier}")
    shapes = {candidate.assignments for candidate in candidates}
    if len(shapes) != 1:
        raise ValueError(f"ambiguous {kind}: {identifier}")
    return candidates[0]


def _one(block: IniBlock, field: str, required: bool = False) -> str | None:
    values = block.values(field)
    if len(values) > 1 or (required and not values):
        raise ValueError(f"{block.kind} {block.name} has invalid {field}")
    return values[0] if values else None


def _integer(block: IniBlock, field: str, required: bool = False) -> int | None:
    value = _one(block, field, required)
    if value is None: return None
    if re.fullmatch(r"[0-9]+", value) is None:
        raise ValueError(f"{block.kind} {block.name} has non-integer {field}")
    return int(value)


def _defines(source: bytes | None) -> dict[str, int]:
    result: dict[str, int] = {}
    if source is None:
        return result
    for raw in _decode(source, "gamedata.ini").splitlines():
        line = _clean(raw)
        match = re.fullmatch(r"#define\s+([A-Za-z_][A-Za-z0-9_]*)\s+([0-9]+)", line)
        if match:
            result[match.group(1)] = int(match.group(2))
    return result


def _profile_attested_paths(profile: Mapping[str, Any]) -> frozenset[str]:
    paths: dict[str, str] = {}
    for index, raw in enumerate(_list(profile.get("resources"), "profile resources")):
        resource = _obj(raw, f"profile resources[{index}]")
        for pattern_index, raw_pattern in enumerate(
            _list(resource.get("patterns"), f"profile resources[{index}].patterns")
        ):
            pattern = _text(
                raw_pattern,
                f"profile resources[{index}].patterns[{pattern_index}]",
                1_024,
            )
            if any(marker in pattern for marker in ("*", "?", "[", "]")):
                continue
            canonical = PurePosixPath(pattern.replace("\\", "/")).as_posix()
            previous = paths.get(canonical.casefold())
            if previous is not None and previous != canonical:
                raise ValueError("profile has case-ambiguous exact resource paths")
            paths[canonical.casefold()] = canonical
    return frozenset(paths.values())


def _effective_manifest_index(manifest: Mapping[str, Any]) -> dict[str, Mapping[str, Any]]:
    result: dict[str, Mapping[str, Any]] = {}
    for index, raw in enumerate(_list(manifest.get("files"), "effective manifest files")):
        row = _obj(raw, f"effective manifest files[{index}]")
        path = PurePosixPath(
            _text(row.get("path"), f"effective manifest files[{index}].path", 1_024).replace(
                "\\", "/"
            )
        ).as_posix()
        previous = result.get(path.casefold())
        if previous is not None:
            raise ValueError(f"effective manifest has an ambiguous path: {path}")
        result[path.casefold()] = row
    return result


def _read_attested_ini(
    assets_root: Path,
    path: str,
    manifest: Mapping[str, Mapping[str, Any]],
    attested_paths: frozenset[str],
) -> bytes:
    canonical = PurePosixPath(path.replace("\\", "/")).as_posix()
    attested = {value.casefold(): value for value in attested_paths}
    if canonical.casefold() not in attested:
        raise ValueError(f"INI source is not hash-attested by the profile: {canonical}")
    row = manifest.get(canonical.casefold())
    if row is None:
        raise ValueError(f"INI source is absent from the effective-assets manifest: {canonical}")
    parts = safe_relative_parts(canonical)
    source_path = assets_root.joinpath(*parts)
    if source_path.is_symlink() or not source_path.is_file():
        raise ValueError(f"INI source is not a regular effective-assets file: {canonical}")
    before = source_path.stat()
    if before.st_size > 16 * 1024 * 1024:
        raise ValueError(f"INI source exceeds the bounded read limit: {canonical}")
    source = source_path.read_bytes()
    after = source_path.stat()
    expected_size = row.get("size")
    expected_sha256 = row.get("sha256")
    actual_sha256 = hashlib.sha256(source).hexdigest()
    if (
        before.st_size != after.st_size
        or before.st_mtime_ns != after.st_mtime_ns
        or len(source) != before.st_size
        or expected_size != len(source)
        or expected_sha256 != actual_sha256
    ):
        raise ValueError(f"INI source does not match effective-assets evidence: {canonical}")
    return source


def _definition_source_paths(row: Mapping[str, Any], label: str) -> tuple[str, ...]:
    paths: dict[str, str] = {}
    sources = [_obj(row.get("source"), f"{label} source")]
    sources.extend(
        _obj(raw, f"{label} inheritance source")
        for raw in _list(row.get("inheritanceSources", []), f"{label} inheritanceSources")
    )
    for source in sources:
        path = PurePosixPath(
            _text(source.get("virtualPath"), f"{label} source path", 1_024).replace(
                "\\", "/"
            )
        ).as_posix()
        digest = _text(source.get("sha256"), f"{label} source SHA-256", 64).casefold()
        if _SHA.fullmatch(digest) is None:
            raise ValueError(f"{label} source SHA-256 is invalid")
        previous = paths.get(path.casefold())
        if previous is not None and previous != path:
            raise ValueError(f"{label} source paths are case-ambiguous")
        paths[path.casefold()] = path
    return tuple(sorted(paths.values(), key=lambda item: (item.casefold(), item)))


def _definition_primary_source(row: Mapping[str, Any], label: str) -> str:
    source = _obj(row.get("source"), f"{label} source")
    return PurePosixPath(
        _text(source.get("virtualPath"), f"{label} source path", 1_024).replace(
            "\\", "/"
        )
    ).as_posix()


def _object_documents(
    report: Mapping[str, Any],
    object_ids: Iterable[str],
    read_source: Any,
) -> dict[str, SageObject]:
    definitions = _defs(report, "objects")
    paths: dict[str, str] = {}
    for identifier in object_ids:
        definition = definitions.get(identifier.casefold())
        if definition is None:
            continue
        for path in _definition_source_paths(definition, f"Object {identifier}"):
            paths[path.casefold()] = path
    index: dict[str, SageObject] = {}
    for path in sorted(paths.values(), key=lambda item: (item.casefold(), item)):
        document = parse_sage_document(read_source(path), path)
        for item in document.objects:
            previous = index.get(item.name.casefold())
            if previous is not None:
                raise ValueError(f"ambiguous Object definition: {item.name}")
            index[item.name.casefold()] = item
    return index


def _known_ancestry(index: Mapping[str, SageObject], target: SageObject) -> tuple[SageObject, ...]:
    result = [target]
    seen = {target.name.casefold()}
    current = target
    while current.parent is not None:
        parent = index.get(current.parent.casefold())
        if parent is None:
            break
        key = parent.name.casefold()
        if key in seen or len(result) >= 32:
            raise ValueError(f"invalid Object ancestry: {target.name}")
        seen.add(key)
        result.append(parent)
        current = parent
    return tuple(reversed(result))


def _effective_assignment(
    ancestry: Sequence[SageObject], key: str, label: str
) -> SageAssignment:
    selected: SageAssignment | None = None
    for item in ancestry:
        values = tuple(
            assignment
            for assignment in item.assignments
            if assignment.key.casefold() == key.casefold()
        )
        if len({value.value.strip().casefold() for value in values}) > 1:
            raise ValueError(f"{label} has ambiguous {key}")
        if values:
            selected = values[-1]
    if selected is None:
        raise ValueError(f"{label} has no authored {key}")
    return selected


def _effective_max_health(ancestry: Sequence[SageObject], label: str) -> SageAssignment:
    bodies: dict[str, SageBlock] = {}
    for item in ancestry:
        for block in item.blocks:
            if (block.header_key or "").casefold() != "body":
                continue
            tag = (block.instance_tag or block.raw_header).casefold()
            bodies[tag] = block
    values = [
        assignment
        for block in bodies.values()
        for assignment in block.assignments
        if assignment.key.casefold() == "maxhealth"
    ]
    if len(values) != 1:
        raise ValueError(f"{label} must have exactly one effective Body MaxHealth")
    return values[0]


def _numeric_defines(source: bytes) -> dict[str, int | float]:
    result: dict[str, int | float] = {}
    pattern = re.compile(
        r"#define\s+([A-Za-z_][A-Za-z0-9_]*)\s+(-?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+))"
    )
    for raw in _decode(source, GAMEDATA_PATH).splitlines():
        match = pattern.fullmatch(_clean(raw))
        if match is None:
            continue
        key = match.group(1).casefold()
        if key in result:
            raise ValueError(f"ambiguous GameData constant: {match.group(1)}")
        token = match.group(2)
        result[key] = float(token) if "." in token else int(token)
    return result


def _resolved_number(
    assignment: SageAssignment,
    constants: Mapping[str, int | float],
    label: str,
) -> tuple[int | float, dict[str, Any]]:
    expression = assignment.value.strip()
    if re.fullmatch(r"-?[0-9]+", expression):
        value: int | float = int(expression)
        constant_source = None
    elif re.fullmatch(r"-?(?:[0-9]+\.[0-9]*|\.[0-9]+)", expression):
        value = float(expression)
        constant_source = None
    else:
        resolved = constants.get(expression.casefold())
        if resolved is None:
            raise ValueError(f"{label} has unresolved expression: {expression}")
        value = resolved
        constant_source = GAMEDATA_PATH
    provenance: dict[str, Any] = {
        "expression": expression,
        "sourceIni": assignment.source_virtual_path,
    }
    if constant_source is not None:
        provenance["constantSourceIni"] = constant_source
    return value, provenance


def _command_slots(block: IniBlock) -> list[dict[str, Any]]:
    result: list[dict[str, Any]] = []
    seen: set[int] = set()
    for key, value in block.assignments:
        if re.fullmatch(r"[0-9]+", key) is None:
            continue
        slot = int(key)
        if slot in seen or slot < 1:
            raise ValueError(f"CommandSet {block.name} has an ambiguous slot {slot}")
        command = _id(value, f"CommandSet {block.name} slot {slot}")
        seen.add(slot)
        result.append({"slot": slot, "id": command})
    if not result:
        raise ValueError(f"CommandSet {block.name} has no commands")
    return sorted(result, key=lambda row: int(row["slot"]))


def _porter_command(
    building_id: str, buttons: Mapping[str, tuple[IniBlock, ...]]
) -> dict[str, Any]:
    ordered_targets = PORTER_CONSTRUCT_TARGETS.get(building_id, (building_id,))
    target_rank = {
        target.casefold(): rank for rank, target in enumerate(ordered_targets)
    }
    command_target_rank = {
        (command.casefold(), target.casefold()): rank
        for rank, (command, target) in enumerate(
            PORTER_CONSTRUCT_COMMAND_TARGETS.get(building_id, ())
        )
    }
    matches: list[tuple[IniBlock, str]] = []
    for candidates in buttons.values():
        if not candidates[0].name.casefold().startswith("command_porterconstruct"):
            continue
        matching_candidates = []
        for block in candidates:
            commands = {value.casefold() for value in block.values("Command")}
            objects = block.values("Object")
            command_target = (
                block.name.casefold(),
                objects[0].casefold() if len(objects) == 1 else "",
            )
            if (
                commands == {"dozer_construct"}
                and len(objects) == 1
                and (
                    command_target in command_target_rank
                    if command_target_rank
                    else objects[0].casefold() in target_rank
                )
            ):
                matching_candidates.append((block, objects[0]))
        if not matching_candidates:
            continue
        block = _unambiguous_block(buttons, candidates[0].name, "CommandButton")
        commands = {value.casefold() for value in block.values("Command")}
        objects = block.values("Object")
        command_target = (
            block.name.casefold(),
            objects[0].casefold() if len(objects) == 1 else "",
        )
        accepted_target = (
            command_target in command_target_rank
            if command_target_rank
            else len(objects) == 1 and objects[0].casefold() in target_rank
        )
        if commands == {"dozer_construct"} and len(objects) == 1 and accepted_target:
            matches.append((block, objects[0]))
    if matches:
        rank_by_match = (
            lambda block, target: command_target_rank[
                (block.name.casefold(), target.casefold())
            ]
            if command_target_rank
            else target_rank[target.casefold()]
        )
        best_rank = min(rank_by_match(block, target) for block, target in matches)
        matches = [
            match
            for match in matches
            if rank_by_match(match[0], match[1]) == best_rank
        ]
    if len(matches) != 1:
        raise ValueError(
            f"Object {building_id} must have exactly one authored porter construct command"
        )
    block, target = matches[0]
    return {"id": block.name, "targetId": target, "sourceIni": COMMAND_BUTTON_PATH}


def _record_gap(record_type: str, identifier: str, error: Exception) -> dict[str, Any]:
    return {
        "recordType": record_type,
        "id": identifier,
        "reason": str(error),
    }


def extract_building_stats(
    report: Mapping[str, Any],
    assets_root: Path,
    effective_manifest: Mapping[str, Any],
    profile: Mapping[str, Any],
) -> dict[str, Any]:
    """Extract source-attested building and trainable stats without INI fallback."""

    attested_paths = _profile_attested_paths(profile)
    manifest = _effective_manifest_index(effective_manifest)

    def read_source(path: str) -> bytes:
        return _read_attested_ini(assets_root, path, manifest, attested_paths)

    command_sets = _block_groups(read_source(COMMAND_SET_PATH), "CommandSet")
    command_buttons = _block_groups(read_source(COMMAND_BUTTON_PATH), "CommandButton")
    constants = _numeric_defines(read_source(GAMEDATA_PATH))
    definitions = _defs(report, "objects")
    requested_objects = set(BUILDINGS)
    requested_objects.update(BUILDING_COMMAND_OWNERS.values())
    requested_objects.update(PORTER_CONSTRUCT_TARGETS.get("GondorCastleWallHub", ()))
    object_index = _object_documents(report, requested_objects, read_source)

    buildings: list[dict[str, Any]] = []
    missing: list[dict[str, Any]] = []
    trainable_references: dict[str, list[dict[str, str]]] = {}
    for identifier in BUILDINGS:
        try:
            building_trainable_references: dict[str, list[dict[str, str]]] = {}
            definition = definitions.get(identifier.casefold())
            if definition is None:
                raise ValueError(f"Object {identifier} is absent from the Men census")
            target = object_index.get(identifier.casefold())
            if target is None:
                raise ValueError(f"Object {identifier} source definition cannot be loaded")
            ancestry = _known_ancestry(object_index, target)
            build_cost_assignment = _effective_assignment(
                ancestry, "BuildCost", f"Object {identifier}"
            )
            build_time_assignment = _effective_assignment(
                ancestry, "BuildTime", f"Object {identifier}"
            )
            max_health_assignment = _effective_max_health(ancestry, f"Object {identifier}")
            command_owner_id = BUILDING_COMMAND_OWNERS.get(identifier, identifier)
            command_owner = object_index.get(command_owner_id.casefold())
            if command_owner is None:
                raise ValueError(f"Object {command_owner_id} command owner cannot be loaded")
            command_assignment = _effective_assignment(
                _known_ancestry(object_index, command_owner),
                "CommandSet",
                f"Object {command_owner_id}",
            )
            command_set_id = _id(command_assignment.value, f"Object {identifier} CommandSet")
            command_set = _unambiguous_block(
                command_sets, command_set_id, "CommandSet"
            )
            commands = _command_slots(command_set)
            for command in commands:
                button = _unambiguous_block(
                    command_buttons, str(command["id"]), "CommandButton"
                )
                if {value.casefold() for value in button.values("Command")} != {"unit_build"}:
                    continue
                objects = button.values("Object")
                if len(objects) != 1:
                    raise ValueError(f"UNIT_BUILD command {button.name} must name one Object")
                building_trainable_references.setdefault(objects[0].casefold(), []).append(
                    {
                        "buildingId": identifier,
                        "commandSetId": command_set_id,
                        "commandId": button.name,
                        "objectId": objects[0],
                    }
                )
            build_cost, build_cost_source = _resolved_number(
                build_cost_assignment, constants, f"Object {identifier} BuildCost"
            )
            build_time, build_time_source = _resolved_number(
                build_time_assignment, constants, f"Object {identifier} BuildTime"
            )
            max_health, max_health_source = _resolved_number(
                max_health_assignment, constants, f"Object {identifier} MaxHealth"
            )
            source_ini = _definition_primary_source(definition, f"Object {identifier}")
            buildings.append(
                {
                    "id": identifier,
                    "sourceIni": source_ini,
                    "buildCost": build_cost,
                    "buildTime": build_time,
                    "maxHealth": max_health,
                    "commandSet": {
                        "id": command_set_id,
                        "ownerId": command_owner_id,
                        "sourceIni": COMMAND_SET_PATH,
                        "commands": commands,
                    },
                    "porterConstructCommand": _porter_command(identifier, command_buttons),
                    "provenance": {
                        "buildCost": build_cost_source,
                        "buildTime": build_time_source,
                        "maxHealth": max_health_source,
                        "commandSet": {
                            "sourceIni": command_assignment.source_virtual_path,
                            "commandListSourceIni": COMMAND_SET_PATH,
                        },
                    },
                }
            )
            for key, references in building_trainable_references.items():
                trainable_references.setdefault(key, []).extend(references)
        except (FileNotFoundError, OSError, ValueError) as error:
            missing.append(_record_gap("building", identifier, error))

    trainable_ids = [
        references[0]["objectId"]
        for _, references in sorted(trainable_references.items(), key=lambda item: item[0])
    ]
    object_index.update(_object_documents(report, trainable_ids, read_source))
    trainables: list[dict[str, Any]] = []
    for folded_id, references in sorted(trainable_references.items(), key=lambda item: item[0]):
        identifier = references[0]["objectId"]
        try:
            definition = definitions.get(folded_id)
            if definition is None:
                raise ValueError(f"Object {identifier} is absent from the Men census")
            target = object_index.get(folded_id)
            if target is None:
                raise ValueError(f"Object {identifier} source definition cannot be loaded")
            ancestry = _known_ancestry(object_index, target)
            assignments = {
                key: _effective_assignment(ancestry, key, f"Object {identifier}")
                for key in ("BuildCost", "BuildTime", "CommandPoints")
            }
            resolved = {
                key: _resolved_number(
                    assignment, constants, f"Object {identifier} {key}"
                )
                for key, assignment in assignments.items()
            }
            trainables.append(
                {
                    "id": identifier,
                    "sourceIni": _definition_primary_source(
                        definition, f"Object {identifier}"
                    ),
                    "buildCost": resolved["BuildCost"][0],
                    "buildTime": resolved["BuildTime"][0],
                    "commandPoints": resolved["CommandPoints"][0],
                    "referencedBy": sorted(
                        (
                            {
                                "buildingId": row["buildingId"],
                                "commandSetId": row["commandSetId"],
                                "commandId": row["commandId"],
                            }
                            for row in references
                        ),
                        key=lambda row: (
                            row["buildingId"].casefold(),
                            row["commandId"].casefold(),
                        ),
                    ),
                    "provenance": {
                        "buildCost": resolved["BuildCost"][1],
                        "buildTime": resolved["BuildTime"][1],
                        "commandPoints": resolved["CommandPoints"][1],
                    },
                }
            )
        except (FileNotFoundError, OSError, ValueError) as error:
            missing.append(_record_gap("trainable", identifier, error))

    return {
        "schema": "openbfme.building-stats",
        "schemaVersion": 0,
        "sourcePolicy": "effective-assets-profile-attested",
        "complete": len(buildings) == len(BUILDINGS) and not missing,
        "buildings": buildings,
        "trainables": trainables,
        "missing": missing,
        "summary": {
            "requestedBuildingCount": len(BUILDINGS),
            "buildingCount": len(buildings),
            "trainableCount": len(trainables),
            "missingCount": len(missing),
        },
    }


def build_ranger_runtime_contract(
    assets_root: Path,
    effective_manifest: Mapping[str, Any],
    profile: Mapping[str, Any],
    building_stats: Mapping[str, Any],
) -> dict[str, Any]:
    """Build the bounded Ranger and Archery Range prerequisite contract."""

    attested_paths = _profile_attested_paths(profile)
    manifest = _effective_manifest_index(effective_manifest)
    source_payloads: dict[str, bytes] = {}

    def read_source(path: str) -> bytes:
        payload = _read_attested_ini(assets_root, path, manifest, attested_paths)
        source_payloads[path] = payload
        return payload

    rule_paths = retail_unit_rule_source_paths((RANGER_UNIT_SPEC,))
    rule_sources: dict[str, Path] = {}
    for path in sorted(rule_paths):
        read_source(path)
        rule_sources[path] = assets_root.joinpath(*safe_relative_parts(path))
    rule_document = extract_retail_unit_rules(
        rule_sources,
        unit_specs=(RANGER_UNIT_SPEC,),
    )
    if len(rule_document["units"]) != 1:
        raise ValueError("Ranger unit-rule extraction did not return one unit")
    ranger_rule = deepcopy(rule_document["units"][0])

    trainables = [
        _obj(row, "Ranger trainable")
        for row in _list(building_stats.get("trainables"), "building trainables")
        if str(_obj(row, "trainable").get("id", "")).casefold()
        == "gondorrangerhorde".casefold()
    ]
    if len(trainables) != 1:
        raise ValueError("building stats must contain one GondorRangerHorde")
    production = deepcopy(dict(trainables[0]))
    if (
        production.get("buildCost"),
        production.get("buildTime"),
        production.get("commandPoints"),
    ) != (600, 30, 70):
        raise ValueError("effective Ranger production values changed")

    upgrade_source = read_source(UPGRADE_PATH)
    gamedata_source = read_source(GAMEDATA_PATH)
    constants = _numeric_defines(gamedata_source)
    upgrade = _unambiguous_block(
        _block_groups(upgrade_source, "Upgrade"),
        "Upgrade_GondorArcheryRangeLevel2",
        "Upgrade",
    )
    upgrade_type = _one(upgrade, "Type", True)
    cost_expression = _one(upgrade, "BuildCost", True)
    time_expression = _one(upgrade, "BuildTime", True)
    assert upgrade_type is not None and cost_expression is not None and time_expression is not None
    cost = constants.get(cost_expression.casefold())
    build_time = constants.get(time_expression.casefold())
    if upgrade_type.casefold() != "object" or (cost, build_time) != (500, 30):
        raise ValueError("Archery Range level-2 Upgrade contract changed")

    command_buttons = _block_groups(read_source(COMMAND_BUTTON_PATH), "CommandButton")
    upgrade_button = _unambiguous_block(
        command_buttons,
        "Command_PurchaseUpgradeGondorArcheryRangeLevel2",
        "CommandButton",
    )
    ranger_button = _unambiguous_block(
        command_buttons,
        "Command_ConstructGondorRangerHorde",
        "CommandButton",
    )
    if (
        _one(upgrade_button, "Command", True),
        _one(upgrade_button, "Upgrade", True),
        _one(upgrade_button, "Options", True),
    ) != (
        "OBJECT_UPGRADE",
        "Upgrade_GondorArcheryRangeLevel2",
        "CANCELABLE",
    ):
        raise ValueError("Archery Range level-2 command button changed")
    if (
        _one(ranger_button, "Command", True),
        _one(ranger_button, "Object", True),
        set((_one(ranger_button, "Options", True) or "").split()),
        _one(ranger_button, "NeededUpgrade", True),
        _one(ranger_button, "NeededUpgradeAny", True),
    ) != (
        "UNIT_BUILD",
        "GondorRangerHorde",
        {"NEED_UPGRADE", "CANCELABLE"},
        "Upgrade_GondorArcheryRangeLevel2",
        "Yes",
    ):
        raise ValueError("Ranger production prerequisite changed")

    command_sets = _block_groups(read_source(COMMAND_SET_PATH), "CommandSet")
    command_set_rows: list[dict[str, Any]] = []
    for command_set_id in ("GondorArcheryCommandSet", "GondorArcheryCommandSetLevel2"):
        row = _unambiguous_block(command_sets, command_set_id, "CommandSet")
        slots = _command_slots(row)
        slot_map = {item["slot"]: item["id"] for item in slots}
        if slot_map.get(2) != "Command_ConstructGondorRangerHorde":
            raise ValueError(f"{command_set_id} no longer exposes Ranger in slot 2")
        if command_set_id == "GondorArcheryCommandSet" and slot_map.get(4) != (
            "Command_PurchaseUpgradeGondorArcheryRangeLevel2"
        ):
            raise ValueError("base Archery Range no longer buys level 2 from slot 4")
        command_set_rows.append({"id": command_set_id, "commands": slots})

    archery_document = parse_sage_document(read_source(ARCHERY_RANGE_PATH), ARCHERY_RANGE_PATH)
    archery_objects = [
        item for item in archery_document.objects if item.name.casefold() == "gondorarcherrange"
    ]
    if len(archery_objects) != 1:
        raise ValueError("Archery Range source must contain one GondorArcherRange")
    transition_blocks = [
        block
        for block in archery_objects[0].blocks
        if block.kind.casefold() == "commandsetupgrade"
        and "upgrade_gondorarcheryrangelevel2".casefold()
        in {value.casefold() for value in block.values("TriggeredBy")}
    ]
    if len(transition_blocks) != 1:
        raise ValueError("Archery Range level-2 command-set transition is ambiguous")
    transition = transition_blocks[0]
    if (
        transition.values("CommandSet"),
        transition.values("ConflictsWith"),
    ) != (
        ("GondorArcheryCommandSetLevel2",),
        ("Upgrade_GondorArcheryRangeLevel3",),
    ):
        raise ValueError("Archery Range level-2 command-set transition changed")

    level_up_blocks = [
        block
        for block in archery_objects[0].blocks
        if block.kind.casefold() == "levelupupgrade"
        and "upgrade_gondorarcheryrangelevel2".casefold()
        in {value.casefold() for value in block.values("TriggeredBy")}
    ]
    if len(level_up_blocks) != 1:
        raise ValueError("Archery Range level-2 building-level transition is ambiguous")
    level_up = level_up_blocks[0]
    if (level_up.values("LevelsToGain"), level_up.values("LevelCap")) != (("1",), ("3",)):
        raise ValueError("Archery Range level-2 building-level transition changed")

    ranger_member_source = source_payloads[RANGER_UNIT_SPEC[4]]
    ranger_member_document = parse_sage_document(ranger_member_source, RANGER_UNIT_SPEC[4])
    ranger_members = [
        item
        for item in ranger_member_document.objects
        if item.kind.casefold() == "object" and item.name.casefold() == "gondorranger"
    ]
    horde_path = "data/ini/object/goodfaction/hordes/men/menhordes.ini"
    ranger_horde_document = parse_sage_document(source_payloads[horde_path], horde_path)
    ranger_hordes = [
        item
        for item in ranger_horde_document.objects
        if item.kind.casefold() == "object" and item.name.casefold() == "gondorrangerhorde"
    ]
    if len(ranger_members) != 1 or len(ranger_hordes) != 1:
        raise ValueError("effective Ranger member/horde source is ambiguous")

    ranger_bow = _unambiguous_block(
        _block_groups(source_payloads[WEAPON_PATH], "Weapon"),
        "GondorRangerBow",
        "Weapon",
    )
    clip_size = int(_one(ranger_bow, "ClipSize", True) or "0")
    clip_reload_ms = int(_one(ranger_bow, "ClipReloadTime", True) or "0")
    continuous_fire_one = int(_one(ranger_bow, "ContinuousFireOne", True) or "0")
    coast_expression = _one(ranger_bow, "ContinuousFireCoast", True) or ""
    continuous_fire_coast_ms = constants.get(coast_expression.casefold())
    weapon_bonus = (_one(ranger_bow, "WeaponBonus", True) or "").split()
    if (
        clip_size != 1
        or clip_reload_ms != 1366
        or (_one(ranger_bow, "AutoReloadsClip", True) or "").casefold() != "yes"
        or continuous_fire_one != 2
        or continuous_fire_coast_ms != 2000
        or weapon_bonus != ["CONTINUOUS_FIRE_MEAN", "RATE_OF_FIRE", "225%"]
    ):
        raise ValueError("effective GondorRangerBow cadence contract changed")

    default_states: list[SageBlock] = []

    def collect_default_states(block: SageBlock) -> None:
        if block.kind.casefold() == "defaultmodelconditionstate":
            default_states.append(block)
        for child in block.blocks:
            collect_default_states(child)

    for block in ranger_members[0].blocks:
        collect_default_states(block)
    if len(default_states) != 1:
        raise ValueError("effective Ranger default model state is ambiguous")
    launch_bones = [
        assignment
        for assignment in default_states[0].assignments
        if assignment.key.casefold() == "weaponlaunchbone"
        and assignment.value.split()[:1] == ["PRIMARY"]
    ]
    if len(launch_bones) != 1 or launch_bones[0].value.split() != ["PRIMARY", "ARROW"]:
        raise ValueError("effective Ranger primary launch bone changed")
    launch_bone_binding = {
        "slot": "PRIMARY",
        "bone": "ARROW",
        "source": {
            "ini": launch_bones[0].source_virtual_path,
            "kind": "DefaultModelConditionState",
            "id": "GondorRanger",
            "field": launch_bones[0].key,
            "line": launch_bones[0].line,
        },
    }
    clip_contract = {
        "size": clip_size,
        "reloadTimeMs": clip_reload_ms,
        "autoReloads": True,
        "continuousFireOne": continuous_fire_one,
        "continuousFireCoastMs": continuous_fire_coast_ms,
        "continuousFireRatePercent": 225,
        "source": {"ini": WEAPON_PATH, "kind": "Weapon", "id": "GondorRangerBow"},
    }
    ranger_rule["member"]["weapon"]["clip"] = deepcopy(clip_contract)
    default_weapon_sets = [
        row
        for row in ranger_rule["member"]["weaponSets"]
        if [str(value).casefold() for value in row.get("conditions", [])] == ["none"]
    ]
    if len(default_weapon_sets) != 1:
        raise ValueError("effective Ranger default weapon set is ambiguous")
    default_weapon_sets[0]["slots"]["primary"]["clip"] = deepcopy(clip_contract)

    def source_binding(obj: SageObject, field: str) -> dict[str, Any]:
        assignments = [
            assignment
            for assignment in obj.assignments
            if assignment.key.casefold() == field.casefold()
        ]
        if len(assignments) != 1:
            raise ValueError(f"{obj.name} has invalid {field} binding")
        assignment = assignments[0]
        return {
            "id": assignment.value,
            "source": {
                "ini": assignment.source_virtual_path,
                "kind": obj.kind,
                "id": obj.name,
                "field": assignment.key,
                "line": assignment.line,
            },
        }

    member_bindings = {
        "created": source_binding(ranger_members[0], "VoiceCreated"),
        "select": source_binding(ranger_members[0], "VoiceSelect"),
        "move": source_binding(ranger_members[0], "VoiceMove"),
        "attack": source_binding(ranger_members[0], "VoiceAttack"),
    }
    portrait_binding = source_binding(ranger_hordes[0], "SelectPortrait")
    purchase_sound = _one(ranger_button, "UnitSpecificSound", True)
    train_image = _one(ranger_button, "ButtonImage", True)
    assert purchase_sound is not None and train_image is not None
    purchase_binding = {
        "id": purchase_sound,
        "source": {
            "ini": COMMAND_BUTTON_PATH,
            "kind": "CommandButton",
            "id": ranger_button.name,
            "field": "UnitSpecificSound",
        },
    }
    train_image_binding = {
        "id": train_image,
        "source": {
            "ini": COMMAND_BUTTON_PATH,
            "kind": "CommandButton",
            "id": ranger_button.name,
            "field": "ButtonImage",
        },
    }

    runtime = _obj(profile.get("runtime_data"), "profile runtime_data")
    audio_document = _obj(runtime.get("data/audio_events.json"), "audio events")
    audio = _obj(audio_document.get("events"), "audio event definitions")
    multisounds = _obj(audio_document.get("multisounds"), "audio multisounds")
    audio_bindings = {"purchase": purchase_binding, **member_bindings}
    audio_route_kinds: dict[str, str] = {}
    for route, binding in audio_bindings.items():
        event_id = str(binding["id"])
        kind = "multisound" if event_id in multisounds else "event"
        definitions = audio if kind == "event" else multisounds
        if event_id not in definitions:
            raise ValueError(f"Ranger audio {route} {kind} is absent: {event_id}")
        audio_route_kinds[route] = kind
    ui = _obj(runtime.get("data/ui_manifest.json"), "UI manifest")
    images = {
        str(_obj(row, "UI image").get("id", "")).casefold()
        for row in _list(ui.get("images"), "UI images")
    }
    for image_id in (train_image_binding["id"], portrait_binding["id"]):
        if image_id.casefold() not in images:
            raise ValueError(f"Ranger UI image is absent: {image_id}")

    return {
        "schema": RANGER_RUNTIME_SCHEMA,
        "schemaVersion": 0,
        "capabilityStatus": "rules-and-prerequisite-ready",
        "unitRule": ranger_rule,
        "production": production,
        "prerequisite": {
            "upgradeId": "Upgrade_GondorArcheryRangeLevel2",
            "type": "OBJECT",
            "cost": cost,
            "buildTimeSeconds": build_time,
            "commandId": "Command_PurchaseUpgradeGondorArcheryRangeLevel2",
            "options": ["CANCELABLE"],
            "fromCommandSet": "GondorArcheryCommandSet",
            "toCommandSet": "GondorArcheryCommandSetLevel2",
            "conflictsWith": ["Upgrade_GondorArcheryRangeLevel3"],
            "levelsToGain": 1,
            "levelCap": 3,
            "purchaseCommandSlot": 4,
            "trainCommandId": "Command_ConstructGondorRangerHorde",
            "trainCommandOptions": ["NEED_UPGRADE", "CANCELABLE"],
        },
        "commandSets": command_set_rows,
        "presentation": {
            "trainButtonImage": train_image_binding,
            "portraitImage": portrait_binding,
            "primaryLaunchBone": launch_bone_binding,
        },
        "audioRoutes": audio_bindings,
        "audioRouteKinds": audio_route_kinds,
        "deferredCapabilities": [
            "model-and-animation-conversion",
            "close-range-sword-and-transition-presentation",
            "camouflage",
            "fire-arrows",
            "long-shot",
            "bombard",
            "veterancy-and-banner",
            "full-animation-and-audiovisual-oracle",
        ],
        "sources": [
            {
                "ini": path,
                "sha256": hashlib.sha256(payload).hexdigest(),
                "byteCount": len(payload),
            }
            for path, payload in sorted(source_payloads.items())
        ],
    }


def attach_ranger_playable_bindings(
    profile: dict[str, Any],
    ranger_runtime: dict[str, Any],
    model_census: Mapping[str, Any],
) -> None:
    """Register the converted Ranger as a bounded member/horde presentation.

    The source contract owns gameplay values.  This bridge only binds the
    already-converted model, its four proven clips, and the two source UI
    images into the base pack documents consumed by Godot.
    """

    runtime = _obj(profile.get("runtime_data"), "profile runtime_data")
    objects_document = _obj(runtime.get("data/objects.json"), "objects document")
    capabilities_document = _obj(
        runtime.get("data/animation_capabilities.json"),
        "animation capabilities document",
    )
    ui_document = _obj(runtime.get("data/ui_manifest.json"), "UI manifest")
    objects = _list(objects_document.get("objects"), "objects")
    capabilities = _list(capabilities_document.get("capabilities"), "capabilities")
    existing_object_ids = {
        _id(_obj(row, "object row").get("id"), "object id").casefold()
        for row in objects
    }
    existing_capability_ids = {
        _id(_obj(row, "capability row").get("id"), "capability id").casefold()
        for row in capabilities
    }
    required_object_ids = {
        "bfme2.object.gondor-ranger",
        "bfme2.object.gondor-ranger-horde",
    }
    if existing_object_ids.intersection(required_object_ids):
        raise ValueError("Ranger object binding already exists")
    if "bfme2.animation.gondor-ranger" in existing_capability_ids:
        raise ValueError("Ranger animation binding already exists")

    ranger_models = [
        _obj(row, "Ranger model row")
        for row in _list(model_census.get("units"), "model units")
        if str(_obj(row, "model row").get("id", "")).casefold()
        == "gondorranger".casefold()
    ]
    if len(ranger_models) != 1:
        raise ValueError("model census must contain one GondorRanger")
    model = ranger_models[0]
    if model.get("coverage") != "m3-converted":
        raise ValueError("GondorRanger model is not converted")
    model_output = _text(model.get("output"), "Ranger model output")
    if model_output != "assets/models/m3/units/gondorranger.glb":
        raise ValueError("GondorRanger model output changed")

    image_paths: dict[str, str] = {}
    for row in _list(ui_document.get("images"), "UI images"):
        image = _obj(row, "UI image")
        image_id = str(image.get("id", ""))
        if image_id in {"BGArcheryRange_Rangers", "UPGondor_Ranger"}:
            image_paths[image_id] = _text(image.get("path"), f"{image_id} path")
    if set(image_paths) != {"BGArcheryRange_Rangers", "UPGondor_Ranger"}:
        raise ValueError("Ranger UI bindings are incomplete")

    production = _obj(ranger_runtime.get("production"), "Ranger production")
    member_rule = _obj(
        _obj(ranger_runtime.get("unitRule"), "Ranger unit rule").get("member"),
        "Ranger member rule",
    )
    member_health = int(_obj(member_rule.get("health"), "Ranger health").get("value", 0))
    if (
        int(production.get("commandPoints", 0)) != 70
        or int(production.get("buildCost", 0)) != 600
        or member_health != 300
    ):
        raise ValueError("Ranger playable binding values changed")
    presentation = _obj(ranger_runtime.get("presentation"), "Ranger presentation")
    launch_bone = _obj(presentation.get("primaryLaunchBone"), "Ranger primary launch bone")
    if (launch_bone.get("slot"), launch_bone.get("bone")) != ("PRIMARY", "ARROW"):
        raise ValueError("Ranger primary launch bone contract changed")

    objects.extend(
        [
            {
                "id": "bfme2.object.gondor-ranger",
                "kind": "member",
                "displayName": "Ithilien Ranger",
                "animationCapabilityId": "bfme2.animation.gondor-ranger",
                "simulation": {"health": member_health},
                "presentation": {
                    "model": model_output,
                    "icon": image_paths["UPGondor_Ranger"],
                    "commandIcon": image_paths["BGArcheryRange_Rangers"],
                    "weaponLaunchBone": "ARROW",
                },
            },
            {
                "id": "bfme2.object.gondor-ranger-horde",
                "kind": "battalion",
                "displayName": "Ithilien Rangers",
                "memberObjectId": "bfme2.object.gondor-ranger",
                "memberCount": 10,
                "commandPoints": 70,
                "formations": ["source-authored"],
            },
        ]
    )
    capabilities.append(
        {
            "id": "bfme2.animation.gondor-ranger",
            "presentationMode": "skeletal-glb",
            "conversionProof": "provenance/conversion/m3-gondorranger-rig-and-core-clips.json",
            "states": {
                "idle": {"clips": ["guranger_idla"], "mode": "loop", "required": True},
                "move": {"clips": ["guranger_runa"], "mode": "loop", "required": True},
                "attack": {
                    "clips": ["guranger_atkd1"],
                    "mode": "once",
                    "required": True,
                    "useWeaponTiming": True,
                },
                "death": {"clips": ["guranger_diea"], "mode": "once", "required": True},
            },
        }
    )
    presentation.update(
        {
            "memberObjectId": "bfme2.object.gondor-ranger",
            "hordeObjectId": "bfme2.object.gondor-ranger-horde",
            "animationCapabilityId": "bfme2.animation.gondor-ranger",
            "model": model_output,
            "coreClipStatus": "source-converted",
        }
    )
    deferred = _list(ranger_runtime.get("deferredCapabilities"), "Ranger deferred capabilities")
    if "model-and-animation-conversion" not in deferred:
        raise ValueError("Ranger conversion deferral was already removed")
    deferred.remove("model-and-animation-conversion")


def build_trebuchet_runtime_contract(
    assets_root: Path,
    effective_manifest: Mapping[str, Any],
    profile: Mapping[str, Any],
    building_stats: Mapping[str, Any],
    model_census: Mapping[str, Any],
) -> dict[str, Any]:
    """Extract the bounded Workshop/Trebuchet direct-attack contract."""

    paths = (
        GAMEDATA_PATH,
        COMMAND_BUTTON_PATH,
        COMMAND_SET_PATH,
        WEAPON_PATH,
        WORKSHOP_PATH,
        TREBUCHET_PATH,
        GOOD_SUBOBJECTS_PATH,
    )
    attested = _profile_attested_paths(profile)
    manifest = _effective_manifest_index(effective_manifest)
    sources = {
        path: _read_attested_ini(assets_root, path, manifest, attested) for path in paths
    }
    constants = _numeric_defines(sources[GAMEDATA_PATH])

    def number(raw: str, label: str) -> int | float:
        value = raw.strip()
        if re.fullmatch(r"-?[0-9]+", value):
            return int(value)
        if re.fullmatch(r"-?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)", value):
            return float(value)
        resolved = constants.get(value.casefold())
        if resolved is None:
            raise ValueError(f"{label} has unresolved value: {value}")
        return resolved

    def one_object(path: str, identifier: str) -> SageObject:
        rows = [
            row
            for row in parse_sage_document(sources[path], path).objects
            if row.name.casefold() == identifier.casefold()
        ]
        if len(rows) != 1:
            raise ValueError(f"{path} must contain one Object {identifier}")
        return rows[0]

    def assignment(obj: SageObject, field: str) -> SageAssignment:
        rows = [row for row in obj.assignments if row.key.casefold() == field.casefold()]
        if len(rows) != 1:
            raise ValueError(f"Object {obj.name} has invalid {field}")
        return rows[0]

    def block(obj: SageObject, kind: str) -> SageBlock:
        rows = [row for row in obj.blocks if row.kind.casefold() == kind.casefold()]
        if len(rows) != 1:
            raise ValueError(f"Object {obj.name} has invalid {kind}")
        return rows[0]

    def block_value(source: IniBlock, field: str) -> str:
        value = _one(source, field, True)
        assert value is not None
        return value

    buildings = [
        _obj(row, "Workshop building stats")
        for row in _list(building_stats.get("buildings"), "building stats")
        if str(_obj(row, "building stats row").get("id", "")).casefold()
        == "gondorworkshop"
    ]
    trainables = [
        _obj(row, "Trebuchet trainable stats")
        for row in _list(building_stats.get("trainables"), "trainable stats")
        if str(_obj(row, "trainable stats row").get("id", "")).casefold()
        == "gondortrebuchet"
    ]
    if len(buildings) != 1 or len(trainables) != 1:
        raise ValueError("Workshop/Trebuchet production stats are incomplete")
    workshop_stats, production = deepcopy(dict(buildings[0])), deepcopy(dict(trainables[0]))
    if (
        workshop_stats.get("buildCost"),
        workshop_stats.get("buildTime"),
        workshop_stats.get("maxHealth"),
    ) != (500, 45, 3000):
        raise ValueError("effective Workshop values changed")
    if (
        production.get("buildCost"),
        production.get("buildTime"),
        production.get("commandPoints"),
    ) != (700, 30, 25):
        raise ValueError("effective Trebuchet production values changed")

    buttons = _block_groups(sources[COMMAND_BUTTON_PATH], "CommandButton")
    train_button = _unambiguous_block(
        buttons, "Command_ConstructGondorTrebuchet", "CommandButton"
    )
    build_button = _unambiguous_block(
        buttons, "Command_PorterConstructMenWorkshop", "CommandButton"
    )
    if (
        block_value(train_button, "Command"),
        block_value(train_button, "Object"),
        block_value(train_button, "Options"),
    ) != ("UNIT_BUILD", "GondorTrebuchet", "CANCELABLE"):
        raise ValueError("Trebuchet production command changed")
    command_set = _unambiguous_block(
        _block_groups(sources[COMMAND_SET_PATH], "CommandSet"),
        "GondorWorkshopCommandSet",
        "CommandSet",
    )
    slots = _command_slots(command_set)
    if {row["slot"]: row["id"] for row in slots}.get(1) != train_button.name:
        raise ValueError("Workshop no longer trains Trebuchet from slot 1")

    trebuchet = one_object(TREBUCHET_PATH, "GondorTrebuchet")
    workshop_object = one_object(WORKSHOP_PATH, "GondorWorkshop")
    locomotor = block(trebuchet, "LocomotorSet")
    locomotor_fields = {row.key.casefold(): row for row in locomotor.assignments}
    if set(("locomotor", "speed")) - set(locomotor_fields):
        raise ValueError("Trebuchet locomotor binding is incomplete")
    body = block(trebuchet, "ActiveBody")
    health_rows = [row for row in body.assignments if row.key.casefold() == "maxhealth"]
    if len(health_rows) != 1:
        raise ValueError("Trebuchet health binding is ambiguous")
    weapon_sets = [row for row in trebuchet.blocks if row.kind.casefold() == "weaponset"]
    if not weapon_sets:
        raise ValueError("Trebuchet has no WeaponSet")
    primary_rows = [
        row
        for row in weapon_sets[0].assignments
        if row.key.casefold() == "weapon" and row.value.split()[:1] == ["PRIMARY"]
    ]
    if len(primary_rows) != 1 or primary_rows[0].value.split()[1:] != ["GondorTrebuchetRock"]:
        raise ValueError("Trebuchet primary weapon changed")
    launch_bones = [
        row
        for draw in trebuchet.blocks
        if draw.kind.casefold().endswith("draw")
        for state in draw.blocks
        if state.kind.casefold() in {"defaultmodelconditionstate", "modelconditionstate"}
        for row in state.assignments
        if row.key.casefold() == "weaponlaunchbone"
        and row.value.split()[:1] == ["PRIMARY"]
    ]
    if not launch_bones or any(row.value.split() != ["PRIMARY", "Projectile"] for row in launch_bones):
        raise ValueError("Trebuchet primary launch-bone binding changed")

    weapon = _unambiguous_block(
        _block_groups(sources[WEAPON_PATH], "Weapon"),
        "GondorTrebuchetRock",
        "Weapon",
    )
    warhead_name = block_value(weapon, "WarheadTemplateName")
    warhead = _unambiguous_block(
        _block_groups(sources[WEAPON_PATH], "Weapon"), warhead_name, "Weapon"
    )
    combat = {
        "weaponId": weapon.name,
        "attackRange": number(block_value(weapon, "AttackRange"), "attack range"),
        "minimumAttackRange": number(
            block_value(weapon, "MinimumAttackRange"), "minimum attack range"
        ),
        "projectileSpeed": number(block_value(weapon, "WeaponSpeed"), "projectile speed"),
        "delayBetweenShotsMs": number(
            block_value(weapon, "DelayBetweenShots"), "delay between shots"
        ),
        "preAttackDelayMs": number(
            block_value(weapon, "PreAttackDelay"), "preattack delay"
        ),
        "firingDurationMs": number(
            block_value(weapon, "FiringDuration"), "firing duration"
        ),
        "clipSize": int(block_value(weapon, "ClipSize")),
        "projectileObjectId": block_value(weapon, "ProjectileTemplateName"),
        "damage": number(block_value(warhead, "Damage"), "warhead damage"),
        "damageType": block_value(warhead, "DamageType"),
        "scope": "direct-structure-first-slice",
        "source": {"ini": WEAPON_PATH, "weapon": weapon.name, "warhead": warhead.name},
    }
    if tuple(combat[key] for key in (
        "attackRange", "minimumAttackRange", "projectileSpeed",
        "delayBetweenShotsMs", "preAttackDelayMs", "firingDurationMs",
        "clipSize", "damage", "damageType",
    )) != (500, 300, 321, 8000, 1200, 5400, 8, 390, "SIEGE"):
        raise ValueError("effective Trebuchet direct-combat values changed")

    projectile = one_object(GOOD_SUBOBJECTS_PATH, "GondorTrebuchetRockProjectile")
    projectile_draws = [
        row for row in projectile.blocks if row.kind.casefold() == "w3dscriptedmodeldraw"
    ]
    if len(projectile_draws) != 1:
        raise ValueError("Trebuchet projectile Draw binding is ambiguous")
    projectile_models = [
        row.value
        for state in projectile_draws[0].blocks
        if state.kind.casefold() == "defaultmodelconditionstate"
        for row in state.assignments
        if row.key.casefold() == "model"
    ]
    projectile_animations = [
        row.value
        for state in projectile_draws[0].blocks
        if state.kind.casefold() == "idleanimationstate"
        for animation in state.blocks
        if animation.kind.casefold() == "animation"
        for row in animation.assignments
        if row.key.casefold() == "animationname"
    ]
    if projectile_models != ["GUSiegTreRk"] or projectile_animations != [
        "GUSiegTreRk.GUSiegTreRk"
    ]:
        raise ValueError("Trebuchet projectile model/animation binding changed")
    bezier = block(projectile, "BezierProjectileBehavior")
    bezier_values = {row.key: row.value for row in bezier.assignments}
    projectile_contract = {
        "objectId": projectile.name,
        "model": TREBUCHET_PROJECTILE_OUTPUT,
        "sourceW3d": TREBUCHET_PROJECTILE_W3D,
        "sourceModelId": projectile_models[0],
        "sourceAnimationId": projectile_animations[0],
        "embeddedAnimation": True,
        "flightAudio": assignment(projectile, "SoundAmbient").value,
        "firstHeight": number(bezier_values["FirstHeight"], "first height"),
        "secondHeight": number(bezier_values["SecondHeight"], "second height"),
        "firstPercentIndent": bezier_values["FirstPercentIndent"],
        "secondPercentIndent": bezier_values["SecondPercentIndent"],
        "preLandingStateTimeMs": number(
            bezier_values["PreLandingStateTime"], "prelanding time"
        ),
        "preLandingEmotionRadius": number(
            bezier_values["PreLandingEmotionRadius"], "prelanding radius"
        ),
        "source": {"ini": GOOD_SUBOBJECTS_PATH, "object": projectile.name},
    }

    models = _list(model_census.get("units"), "model units")
    unit_models = [
        _obj(row, "Trebuchet model")
        for row in models
        if str(_obj(row, "model row").get("id", "")).casefold() == "gondortrebuchet"
    ]
    workshop_models = [
        _obj(row, "Workshop model")
        for row in _list(model_census.get("buildings"), "model buildings")
        if str(_obj(row, "model row").get("id", "")).casefold() == "gondorworkshop"
    ]
    if len(unit_models) != 1 or len(workshop_models) != 1:
        raise ValueError("Workshop/Trebuchet model census is incomplete")

    runtime = _obj(profile.get("runtime_data"), "profile runtime_data")
    ui = _obj(runtime.get("data/ui_manifest.json"), "UI manifest")
    image_ids = {
        str(_obj(row, "UI image").get("id", "")).casefold()
        for row in _list(ui.get("images"), "UI images")
    }
    required_images = {
        block_value(build_button, "ButtonImage"),
        block_value(train_button, "ButtonImage"),
        assignment(trebuchet, "SelectPortrait").value,
        assignment(workshop_object, "SelectPortrait").value,
    }
    if any(value.casefold() not in image_ids for value in required_images):
        raise ValueError("Workshop/Trebuchet UI image closure is incomplete")
    audio_document = _obj(runtime.get("data/audio_events.json"), "audio events")
    audio_ids = set(_obj(audio_document.get("events"), "audio definitions")) | set(
        _obj(audio_document.get("multisounds"), "audio multisounds")
    )
    death_sounds = {
        row.value.split()[1]
        for behavior in trebuchet.blocks
        if behavior.kind.casefold() == "slowdeathbehavior"
        for row in behavior.assignments
        if row.key.casefold() == "sound"
        and len(row.value.split()) == 2
        and row.value.split()[0] == "INITIAL"
    }
    if death_sounds != {"TrebuchetDie"}:
        raise ValueError("Trebuchet death audio binding changed")
    audio_routes = {
        "workshopSelect": assignment(workshop_object, "VoiceSelect").value,
        "select": assignment(trebuchet, "VoiceSelect").value,
        "move": assignment(trebuchet, "VoiceMove").value,
        "attack": assignment(trebuchet, "VoiceAttack").value,
        "death": next(iter(death_sounds)),
        "impact": assignment(trebuchet, "SoundImpact").value,
    }
    if any(value not in audio_ids for value in audio_routes.values()):
        raise ValueError("Trebuchet audio closure is incomplete")

    strings = _obj(
        _obj(runtime.get("data/strings.json"), "strings document").get("strings"),
        "strings",
    )
    text_bindings = {
        "workshopLabel": block_value(build_button, "TextLabel"),
        "workshopTooltip": block_value(build_button, "DescriptLabel"),
        "trainLabel": block_value(train_button, "TextLabel"),
        "trainTooltip": block_value(train_button, "DescriptLabel"),
    }
    if any(value not in strings for value in text_bindings.values()):
        raise ValueError("Workshop/Trebuchet string closure is incomplete")

    return {
        "schema": TREBUCHET_RUNTIME_SCHEMA,
        "schemaVersion": 0,
        "capabilityStatus": "bounded-direct-structure-ready",
        "workshop": {
            "stats": workshop_stats,
            "constructCommandId": build_button.name,
            "commandSetId": command_set.name,
            "trainCommandId": train_button.name,
            "trainCommandSlot": 1,
            "models": deepcopy(workshop_models[0]),
        },
        "production": production,
        "unit": {
            "objectId": trebuchet.name,
            "maximumHealth": number(health_rows[0].value, "Trebuchet health"),
            "visionRange": number(assignment(trebuchet, "VisionRange").value, "vision"),
            "movement": {
                "mode": "existing-generic-unit-path",
                "speed": number(locomotor_fields["speed"].value, "Trebuchet speed"),
                "sourceTemplate": locomotor_fields["locomotor"].value.split()[0],
            },
            "models": deepcopy(unit_models[0]),
        },
        "combat": combat,
        "projectile": projectile_contract,
        "presentation": {
            "workshopButtonImage": block_value(build_button, "ButtonImage"),
            "workshopPortraitImage": assignment(workshop_object, "SelectPortrait").value,
            "trainButtonImage": block_value(train_button, "ButtonImage"),
            "portraitImage": assignment(trebuchet, "SelectPortrait").value,
            "primaryLaunchBone": {"weaponSlot": "PRIMARY", "bone": "Projectile"},
            "text": text_bindings,
        },
        "audioRoutes": audio_routes,
        "unboundSourceAudio": {
            "flight": projectile_contract["flightAudio"],
            "status": "definition-and-sample-closure-deferred",
        },
        "deferredCapabilities": [
            "projectile-flight-audio-definition-sample-closure",
            "siege-particle-fx-closure",
            "catapult-locomotor-vehicle-handling",
            "area-scatter-shroud-combat",
            "workshop-footprint-create-rally-geometry",
        ],
        "sources": [
            {"ini": path, "sha256": hashlib.sha256(payload).hexdigest(), "byteCount": len(payload)}
            for path, payload in sorted(sources.items())
        ],
    }


def attach_trebuchet_playable_bindings(
    profile: dict[str, Any], contract: Mapping[str, Any]
) -> None:
    """Attach the converted first-slice objects without adding gameplay policy."""

    runtime = _obj(profile.get("runtime_data"), "profile runtime_data")
    objects = _list(
        _obj(runtime.get("data/objects.json"), "objects document").get("objects"),
        "objects",
    )
    existing = {
        str(_obj(row, "object row").get("id", "")).casefold() for row in objects
    }
    ids = {"bfme2.object.gondor-workshop", "bfme2.object.gondor-trebuchet"}
    if existing.intersection(ids):
        raise ValueError("Workshop/Trebuchet object binding already exists")
    workshop = _obj(contract.get("workshop"), "Workshop contract")
    workshop_stats = _obj(workshop.get("stats"), "Workshop stats")
    workshop_models = _obj(workshop.get("models"), "Workshop models")
    unit = _obj(contract.get("unit"), "Trebuchet unit")
    unit_models = _obj(unit.get("models"), "Trebuchet models")
    death = _obj(
        _obj(unit_models.get("embeddedDrawables"), "Trebuchet embedded models").get("death"),
        "Trebuchet death model",
    )
    objects.extend(
        [
            {
                "id": "bfme2.object.gondor-workshop",
                "kind": "structure",
                "displayName": "Gondor Workshop",
                "simulation": {"health": workshop_stats["maxHealth"]},
                "presentation": {
                    "modelStateEvidence": deepcopy(workshop_models["states"]),
                    "lifecycleStatus": "deferred-composite-role-and-runtime-binding",
                },
            },
            {
                "id": "bfme2.object.gondor-trebuchet",
                "kind": "member",
                "displayName": "Gondor Trebuchet",
                "simulation": {"health": unit["maximumHealth"]},
                "presentation": {
                    "model": unit_models["output"],
                    "deathModel": death["output"],
                    "projectileModel": _obj(contract.get("projectile"), "projectile")["model"],
                    "iconId": _obj(contract.get("presentation"), "presentation")["portraitImage"],
                    "weaponLaunchBone": _obj(
                        _obj(contract.get("presentation"), "presentation").get("primaryLaunchBone"),
                        "primary launch bone",
                    )["bone"],
                },
            },
        ]
    )


def extract_spellbook(report: Mapping[str, Any], science_source: bytes, power_source: bytes, gamedata_source: bytes | None = None) -> dict[str, Any]:
    dependencies = _obj(report.get("dependencies"), "dependencies")
    science_ids = _unique(dependencies.get("spellbookSciences"), "spellbookSciences")
    power_ids = _unique(dependencies.get("spellbookSpecialPowers"), "spellbookSpecialPowers")
    if len(science_ids) != 12 or len(power_ids) != 12:
        raise ValueError("Men spellbook must have 12 sciences and 12 powers")
    sciences, powers, buttons = _block_index(science_source, "Science"), _block_index(power_source, "SpecialPower"), _defs(report, "commandButtons")
    purchase, cast = {}, {}
    for button in buttons.values():
        for identifier in _fields(button, "Science"):
            if identifier.casefold() in {x.casefold() for x in science_ids}: purchase[identifier.casefold()] = button
        for identifier in _fields(button, "SpecialPower"):
            if identifier.casefold() in {x.casefold() for x in power_ids}: cast[identifier.casefold()] = button
    science_rows, power_rows, constants = [], [], _defines(gamedata_source)
    for identifier in science_ids:
        block, button = sciences.get(identifier.casefold()), purchase.get(identifier.casefold())
        if block is None or button is None: raise ValueError(f"incomplete spell science: {identifier}")
        prerequisites = sorted({token for value in block.values("PrerequisiteSciences") for token in value.split() if token.startswith("SCIENCE_")}, key=str.casefold)
        science_rows.append({"id": identifier, "pointCost": _integer(block, "SciencePurchasePointCostMP", True), "campaignPointCostExpression": _one(block, "SciencePurchasePointCost", True), "prerequisites": prerequisites, "command": button["id"], "icons": list(_fields(button, "ButtonImage")), "textIds": sorted(set(_fields(button, "TextLabel") + _fields(button, "DescriptLabel")), key=str.casefold)})
    for identifier in power_ids:
        block, button = powers.get(identifier.casefold()), cast.get(identifier.casefold())
        if block is None or button is None: raise ValueError(f"incomplete spell power: {identifier}")
        expression = _one(block, "ReloadTime", True)
        reload_time = int(expression) if re.fullmatch(r"[0-9]+", expression) else constants.get(expression)
        if reload_time is None:
            raise ValueError(f"SpecialPower {identifier} has unresolved ReloadTime expression: {expression}")
        power_rows.append({"id": identifier, "reloadTimeMs": reload_time, "reloadTimeExpression": expression, "command": button["id"], "icons": list(_fields(button, "ButtonImage")), "textIds": sorted(set(_fields(button, "TextLabel") + _fields(button, "DescriptLabel")), key=str.casefold)})
    return {"schema": "openbfme.spellbook", "schemaVersion": 0, "faction": "Men", "sciences": science_rows, "powers": power_rows, "scienceCount": 12, "powerCount": 12}


def _decode(source: bytes, label: str) -> str:
    if len(source) > 16 * 1024 * 1024 or b"\0" in source: raise ValueError(f"{label} is unbounded")
    return source.decode("cp1252")


def _clean(raw: str) -> str:
    return raw.split(";", 1)[0].split("//", 1)[0].strip()


def extract_command_points(source: bytes) -> dict[str, Any]:
    wanted = {x.casefold(): x for x in CP_FIELDS}
    values: dict[str, list[int]] = {}
    for raw in _decode(source, "gamedata.ini").splitlines():
        match = _ASSIGN.fullmatch(_clean(raw))
        if match is None or match.group(1).casefold() not in wanted: continue
        key = wanted[match.group(1).casefold()]
        if key in values: raise ValueError(f"duplicate {key}")
        tokens = match.group(2).split()
        if not tokens or any(re.fullmatch(r"[0-9]+", x) is None for x in tokens): raise ValueError(f"invalid {key}")
        values[key] = [int(x) for x in tokens]
    missing = [x for x in CP_FIELDS if x not in values]
    if missing: raise ValueError("missing command-point fields: " + ", ".join(missing))
    return {"schema": "openbfme.game-data", "schemaVersion": 0, "factionSide": "Good", "commandPoints": values}


def parse_house_colors(source: bytes, virtual_paths: Iterable[str]) -> dict[str, dict[str, str]]:
    by_name: dict[str, list[str]] = {}
    by_stem: dict[str, list[str]] = {}
    for raw in virtual_paths:
        path = "/".join(safe_relative_parts(_text(raw, "virtual path")))
        by_name.setdefault(PurePosixPath(path).name.casefold(), []).append(path)
        by_stem.setdefault(PurePosixPath(path).stem.casefold(), []).append(path)
    blocks, current = [], None
    for raw in _decode(source, "housecolor.ini").splitlines():
        line = _clean(raw)
        if not line: continue
        if line.casefold() == "housecolor":
            if current is not None: raise ValueError("nested HouseColor block")
            current = {}
        elif line.casefold() == "end" and current is not None:
            if set(current) != {"basetexture", "housetexture"}: raise ValueError("incomplete HouseColor block")
            blocks.append(current); current = None
        elif current is not None:
            match = _ASSIGN.fullmatch(line)
            if match and match.group(1).casefold() in {"basetexture", "housetexture"}:
                key = match.group(1).casefold()
                if key in current: raise ValueError(f"duplicate HouseColor {key}")
                current[key] = _text(match.group(2), f"HouseColor {key}")
    if current is not None: raise ValueError("unterminated HouseColor block")
    def choose(name: str, *, mask: bool = False) -> list[str]:
        exact = by_name.get(PurePosixPath(name).name.casefold(), [])
        candidates = exact or by_stem.get(PurePosixPath(name).stem.casefold(), [])
        compiled = [path for path in candidates if path.casefold().startswith("art/compiledtextures/")]
        if mask and len(compiled) == 2 and {PurePosixPath(path).suffix.casefold() for path in compiled} == {".jpg", ".png"}:
            return sorted(compiled, key=str.casefold)
        if len(compiled) == 1:
            return compiled
        if len(exact) == 1:
            return exact
        return candidates
    result = {}
    for block in blocks:
        base_name, mask_name = block["basetexture"], block["housetexture"]
        base, mask = choose(base_name), choose(mask_name, mask=True)
        if len(base) != 1 or len(mask) not in {1, 2}: continue  # unrelated table entries may be absent from the bounded closure
        key = PurePosixPath(base_name).name.casefold()
        pair = {"baseTexture": base[0], "maskTextures": mask}
        if key in result and result[key] != pair: raise ValueError(f"ambiguous HouseColor base: {base_name}")
        result[key] = pair
    return result


def build_house_color_manifest(model_metadata: Mapping[str, Iterable[Mapping[str, Any]]], table: Mapping[str, Mapping[str, str]]) -> dict[str, Any]:
    models, masks = [], set()
    for model_id in sorted(model_metadata, key=str.casefold):
        meshes, textures, sources = set(), set(), set()
        for raw in model_metadata[model_id]:
            row = _obj(raw, f"metadata {model_id}")
            sources.add(_text(row.get("virtualPath"), "W3D path")); meshes.update(_dedupe(row.get("meshNames", []), "meshNames")); textures.update(_dedupe(row.get("textureNames", []), "textureNames"))
        bindings = []
        for texture in sorted(textures, key=str.casefold):
            pair = table.get(PurePosixPath(texture).name.casefold())
            if pair:
                bindings.append({"sourceTexture": texture, **dict(pair)}); masks.update(pair["maskTextures"])
        marked = sorted((x for x in meshes if x.casefold().startswith("housecolor")), key=str.casefold)
        models.append({"id": model_id, "sourceW3d": sorted(sources, key=str.casefold), "houseColorMeshes": marked, "textureBindings": bindings, "present": bool(marked or bindings)})
    return {"schema": "openbfme.house-color-manifest", "schemaVersion": 0, "models": models, "maskTextures": sorted(masks, key=str.casefold), "summary": {"modelCount": len(models), "presentCount": sum(x["present"] for x in models), "maskTextureCount": len(masks)}}


def build_house_color_from_assets(
    closure: Mapping[str, Any], assets_root: Path, manifest_paths: Iterable[str]
) -> dict[str, Any]:
    targets = (*BUILDINGS, *UNITS, *SELECTION_TRANSITIONS)
    metadata: dict[str, list[dict[str, Any]]] = {target: [] for target in targets}
    pending: dict[str, set[str]] = {target: set() for target in targets}
    model_id_paths: dict[str, set[str]] = {}
    for raw in _list(closure.get("scannedW3d"), "scannedW3d"):
        row = _obj(raw, "scanned W3D")
        virtual_path = _text(row.get("virtualPath"), "scanned W3D path")
        headers = _obj(row.get("headerIds"), f"{virtual_path} headerIds")
        for identifier in _list(headers.get("modelIds", []), f"{virtual_path} modelIds"):
            model_id_paths.setdefault(str(identifier).casefold(), set()).add(virtual_path)
    seen: set[tuple[str, str]] = set()
    for index, raw in enumerate(_list(closure.get("exactLeaves"), "exactLeaves")):
        row = _obj(raw, f"exactLeaves[{index}]")
        target = str(row.get("targetObject", ""))
        if target not in metadata or str(row.get("kind", "")).casefold() != "model":
            continue
        for virtual_path in _paths(row, f"{target} model paths"):
            pending[target].add(virtual_path)
    for target in targets:
        while pending[target]:
            virtual_path = min(pending[target], key=str.casefold)
            pending[target].remove(virtual_path)
            key = (target.casefold(), virtual_path.casefold())
            if key in seen:
                continue
            seen.add(key)
            source_path = assets_root.joinpath(*safe_relative_parts(virtual_path))
            source = source_path.read_bytes()
            scanned = scan_w3d_metadata(source, virtual_path)
            for reference in scanned.model_references:
                pending[target].update(
                    model_id_paths.get(reference.identifier.casefold(), set())
                )
            meshes = {
                value
                for header in scanned.mesh_headers
                for value in (header.mesh_name, header.identifier)
                if value
            }
            meshes.update(
                reference.identifier
                for reference in scanned.model_references
                if reference.identifier
            )
            textures = {
                reference.identifier
                for reference in scanned.texture_references
                if reference.identifier
            }
            textures.update(
                str(prop.value)
                for prop in scanned.shader_material_properties
                if prop.name
                and "texture" in prop.name.casefold()
                and isinstance(prop.value, str)
                and prop.value
            )
            metadata[target].append(
                {
                    "virtualPath": virtual_path,
                    "meshNames": sorted(meshes, key=str.casefold),
                    "textureNames": sorted(textures, key=str.casefold),
                    "metadataWarnings": [warning.neutral() for warning in scanned.warnings],
                }
            )
    missing_metadata = [target for target, rows in metadata.items() if not rows]
    if missing_metadata:
        raise ValueError("house-color metadata is missing models: " + ", ".join(missing_metadata))
    source = assets_root.joinpath("data", "ini", "housecolor.ini").read_bytes()
    result = build_house_color_manifest(metadata, parse_house_colors(source, manifest_paths))
    missing = [row["id"] for row in result["models"] if not row["present"]]
    result["missing"] = missing
    result["sourceNull"] = [
        {
            "id": f"{identifier}.HouseColorMask",
            "reason": "No HouseColor table binding or HOUSECOLOR mesh marker exists in the resolved retail W3D model closure.",
        }
        for identifier in missing
    ]
    result["complete"] = (
        len(result["models"]) == len(targets)
        and result["summary"]["presentCount"] == result["summary"]["modelCount"]
    )
    return result


def _resolved_animation_paths(
    closure: Mapping[str, Any], target: str, identifiers: Iterable[str]
) -> dict[str, str]:
    wanted = {value.casefold(): value for value in identifiers}
    result: dict[str, str] = {}
    for raw in _list(closure.get("exactLeaves"), "exactLeaves"):
        row = _obj(raw, "exact leaf")
        if (
            str(row.get("targetObject", "")).casefold() != target.casefold()
            or str(row.get("kind", "")).casefold() != "animation"
        ):
            continue
        identifier = str(row.get("identifier", ""))
        key = identifier.casefold()
        if key not in wanted:
            continue
        paths = _paths(row, f"{target} animation {identifier}")
        if len(paths) != 1:
            raise ValueError(f"selection transition is not a single exact leaf: {identifier}")
        result[key] = paths[0]
    missing = [value for key, value in wanted.items() if key not in result]
    if missing:
        raise ValueError("missing selection transition W3D: " + ", ".join(missing))
    return result


def extend_selection_transitions(
    profile: dict[str, Any], closure: Mapping[str, Any]
) -> dict[str, Any]:
    output_by_target = {
        "GondorFighter": "assets/models/units/gondor-fighter.glb",
        "GondorArcher": "assets/models/units/gondor-archer.glb",
    }
    manifest_rows: list[dict[str, Any]] = []
    resources = _list(profile.get("resources"), "profile resources")
    for target, identifiers in SELECTION_TRANSITIONS.items():
        candidates = [
            _obj(row, "profile resource")
            for row in resources
            if _obj(row, "profile resource").get("output") == output_by_target[target]
        ]
        if len(candidates) != 1:
            raise ValueError(f"selection transition model resource is ambiguous: {target}")
        resource = candidates[0]
        resolved = _resolved_animation_paths(closure, target, identifiers)
        patterns = list(_dedupe(resource.get("patterns", []), f"{target} patterns"))
        options = _obj(resource.get("options"), f"{target} options")
        animations = list(_dedupe(options.get("animations", []), f"{target} animations"))
        rows: list[dict[str, str]] = []
        for identifier in identifiers:
            path = resolved[identifier.casefold()]
            if path.casefold() not in {value.casefold() for value in patterns}:
                patterns.append(path)
            filename = PurePosixPath(path).name
            if filename.casefold() not in {value.casefold() for value in animations}:
                animations.append(filename)
            rows.append({"id": identifier, "sourceW3d": path})
        resource["patterns"] = sorted(patterns, key=str.casefold)
        options["animations"] = animations
        resource["limit"] = len(resource["patterns"])
        resource["expected_count"] = len(resource["patterns"])
        manifest_rows.append(
            {
                "id": target,
                "model": output_by_target[target],
                "clips": rows,
                "explicitTargetStatus": "resolved",
                "resolvedClipCount": len(rows),
            }
        )
    return {
        "schema": "openbfme.m3-selection-transitions",
        "schemaVersion": 0,
        "units": manifest_rows,
        "explicitTargetStatus": "resolved",
        "clipCount": sum(len(row["clips"]) for row in manifest_rows),
    }


def declarative_visual_resources(closure: Mapping[str, Any]) -> list[dict[str, Any]]:
    scanned = _list(closure.get("scannedW3d"), "visual closure scannedW3d")
    paths = sorted({_text(_obj(x, "scanned W3D").get("virtualPath"), "W3D path") for x in scanned}, key=str.casefold)
    resources = []
    for offset in range(0, len(paths), MAX_PATTERNS_PER_RESOURCE):
        batch = paths[offset:offset + MAX_PATTERNS_PER_RESOURCE]
        resources.append({"id": f"m3-men-visual-sources-{offset // MAX_PATTERNS_PER_RESOURCE:03d}", "kind": "model", "converter": "hash-only", "patterns": batch, "required": True, "limit": len(batch), "expected_count": len(batch)})
    return resources


def _slug(value: str) -> str:
    result = re.sub(r"[^a-z0-9]+", "-", value.casefold()).strip("-")
    if not result:
        raise ValueError("M3 resource slug is empty")
    return result


def _paths(row: Mapping[str, Any], label: str) -> tuple[str, ...]:
    return _dedupe(row.get("physicalVirtualPaths", []), label)


def _scanned_w3d_index(closure: Mapping[str, Any]) -> dict[str, Mapping[str, Any]]:
    result: dict[str, Mapping[str, Any]] = {}
    for index, raw in enumerate(_list(closure.get("scannedW3d"), "scannedW3d")):
        row = _obj(raw, f"scannedW3d[{index}]")
        path = _text(row.get("virtualPath"), f"scannedW3d[{index}].virtualPath")
        if path.casefold() in result:
            raise ValueError(f"duplicate scanned W3D: {path}")
        result[path.casefold()] = row
    return result


def _hierarchy_dependencies(
    animation_paths: Iterable[str], scanned: Mapping[str, Mapping[str, Any]]
) -> tuple[str, ...]:
    hierarchy_ids: set[str] = set()
    hierarchy_parents: dict[str, set[str]] = {}
    for path in animation_paths:
        row = scanned.get(path.casefold())
        if row is None:
            raise ValueError(f"animation is absent from scannedW3d: {path}")
        headers = _obj(row.get("headerIds"), f"{path} headerIds")
        for raw in _list(headers.get("animationIds", []), f"{path} animationIds"):
            identifier = _text(raw, f"{path} animation id")
            if "." in identifier:
                hierarchy = identifier.split(".", 1)[0].casefold()
                if re.fullmatch(r"[a-z0-9_]+", hierarchy) is None:
                    raise ValueError(f"animation has unsafe hierarchy id: {identifier}")
                hierarchy_ids.add(hierarchy)
                hierarchy_parents.setdefault(hierarchy, set()).add(
                    str(PurePosixPath(path).parent)
                )
    result: list[str] = []
    resolved: set[str] = set()
    for path_key, row in scanned.items():
        headers = _obj(row.get("headerIds"), f"{path_key} headerIds")
        authored = {
            _text(value, f"{path_key} hierarchy id").casefold()
            for value in _list(headers.get("hierarchyIds", []), f"{path_key} hierarchyIds")
        }
        if authored & hierarchy_ids:
            result.append(_text(row.get("virtualPath"), f"{path_key} virtualPath"))
            resolved.update(authored & hierarchy_ids)
    for hierarchy in sorted(hierarchy_ids - resolved):
        parents = hierarchy_parents.get(hierarchy, set())
        if len(parents) != 1:
            raise ValueError(f"animation hierarchy path is ambiguous: {hierarchy}")
        result.append(f"{next(iter(parents))}/{hierarchy}.w3d")
    return tuple(sorted(result, key=str.casefold))


def _texture_paths_for_w3d(
    closure: Mapping[str, Any], source_paths: Iterable[str]
) -> tuple[str, ...]:
    selected = {value.casefold() for value in source_paths}
    dependency = _obj(closure.get("w3dDependencyClosure"), "w3dDependencyClosure")
    textures: set[str] = set()
    for index, raw in enumerate(
        _list(dependency.get("embeddedTextures"), "w3dDependencyClosure.embeddedTextures")
    ):
        row = _obj(raw, f"embeddedTextures[{index}]")
        source = _text(row.get("sourceW3dVirtualPath"), "embedded texture source")
        if source.casefold() not in selected:
            continue
        if row.get("status") != "resolved":
            raise ValueError(f"unresolved embedded texture for selected M3 W3D: {source}")
        textures.update(_paths(row, f"embedded texture {source}"))
    return tuple(sorted(textures, key=str.casefold))


def _texture_resources(target: str, paths: tuple[str, ...]) -> tuple[list[dict[str, Any]], list[str]]:
    resources: list[dict[str, Any]] = []
    identifiers: list[str] = []
    target_slug = _slug(target)
    for offset in range(0, len(paths), MAX_PATTERNS_PER_RESOURCE):
        batch = paths[offset : offset + MAX_PATTERNS_PER_RESOURCE]
        identifier = f"m3-{target_slug}-material-textures-{offset // MAX_PATTERNS_PER_RESOURCE:03d}"
        identifiers.append(identifier)
        resources.append(
            {
                "id": identifier,
                "kind": "texture",
                "converter": "hash-only",
                "patterns": list(batch),
                "required": True,
                "limit": len(batch),
                "expected_count": len(batch),
            }
        )
    return resources, identifiers


def build_m3_visual_resources(
    closure: Mapping[str, Any], base_profile: Mapping[str, Any]
) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    """Build the currently selected Men W3D candidate resources."""

    if closure.get("schema") != "openbfme.retail-visual-closure":
        raise ValueError("unexpected M3 visual closure schema")
    targets = {
        _text(_obj(row, "visual target").get("name"), "visual target name").casefold()
        for row in _list(closure.get("targets"), "visual targets")
        if _obj(row, "visual target").get("status") == "resolved"
    }
    required_targets = {value.casefold() for value in (*BUILDINGS, *UNITS, *SELECTION_TRANSITIONS)}
    if not required_targets.issubset(targets):
        missing = sorted(required_targets - targets)
        raise ValueError("M3 visual closure is missing targets: " + ", ".join(missing))

    scanned = _scanned_w3d_index(closure)
    exact = [_obj(row, "exact leaf") for row in _list(closure.get("exactLeaves"), "exactLeaves")]
    resources: list[dict[str, Any]] = []
    building_rows: list[dict[str, Any]] = [
        {
            "id": identifier,
            "coverage": "base-profile-lifecycle",
            "states": [{"phases": ["construction", "intact", "damaged", "really-damaged", "rubble"], "output": output}],
        }
        for identifier, output in BASE_BUILDING_OUTPUTS.items()
    ]
    unit_rows: list[dict[str, Any]] = [
        {"id": identifier, "coverage": "base-profile", "output": output}
        for identifier, output in BASE_UNIT_OUTPUTS.items()
    ]

    for target in NEW_BUILDINGS:
        model_phases: dict[str, set[str]] = {}
        animation_phases: dict[str, set[str]] = {}
        for row in exact:
            if str(row.get("targetObject", "")).casefold() != target.casefold():
                continue
            kind = str(row.get("kind", "")).casefold()
            if kind not in {"model", "animation"}:
                continue
            phases = {
                _text(value, f"{target} lifecycle phase")
                for value in _list(row.get("lifecyclePhases", []), f"{target} lifecycle phases")
            }
            destination = model_phases if kind == "model" else animation_phases
            for path in _paths(row, f"{target} {kind} paths"):
                destination.setdefault(path, set()).update(phases)
        if not model_phases:
            raise ValueError(f"M3 building has no resolved model: {target}")

        records: list[tuple[str, tuple[str, ...], tuple[str, ...], str]] = []
        selected_w3d: set[str] = set()
        for model_path in sorted(model_phases, key=str.casefold):
            phases = tuple(sorted(model_phases[model_path], key=str.casefold))
            stem = PurePosixPath(model_path).stem.casefold()
            suppress_animation = (
                stem.startswith(("gbhc", "gphc"))
                or "_bib" in stem
                or stem.endswith("_d1")
            )
            animations = tuple(
                sorted(
                    (
                        path
                        for path, authored_phases in animation_phases.items()
                        if not suppress_animation and authored_phases.intersection(phases)
                    ),
                    key=str.casefold,
                )
            )
            hierarchies = _hierarchy_dependencies(animations, scanned)
            selected_w3d.update((model_path, *animations, *hierarchies))
            phase_slug = "-".join(_slug(value) for value in phases) or "intact"
            output = f"assets/models/m3/structures/{_slug(target)}/{phase_slug}-{_slug(stem)}.glb"
            records.append((model_path, animations, hierarchies, output))

        texture_paths = _texture_paths_for_w3d(closure, selected_w3d)
        texture_rules, texture_ids = _texture_resources(target, texture_paths)
        resources.extend(texture_rules)
        states: list[dict[str, Any]] = []
        for model_path, animations, hierarchies, output in records:
            patterns = sorted({model_path, *animations, *hierarchies}, key=str.casefold)
            converter = "w3d-bundle" if animations else "w3d-hierarchical"
            options: dict[str, Any] = {
                "model": PurePosixPath(model_path).name,
                "inputResourceIds": texture_ids,
            }
            if animations:
                options["animations"] = [PurePosixPath(path).name for path in animations]
            resource_id = f"m3-{_slug(target)}-{_slug(PurePosixPath(model_path).stem)}"
            state = {
                "phases": sorted(model_phases[model_path], key=str.casefold),
                "sourceW3d": model_path,
                "animations": list(animations),
            }
            if resource_id in CONVERSION_SOURCE_GAPS:
                state.update(
                    {
                        "coverage": "source-gap",
                        "sourceGapId": resource_id,
                    }
                )
            else:
                resources.append(
                    {
                        "id": resource_id,
                        "kind": "model",
                        "converter": converter,
                        "patterns": patterns,
                        "output": output,
                        "options": options,
                        "required": True,
                        "limit": len(patterns),
                        "expected_count": len(patterns),
                    }
                )
                state.update({"coverage": "converted", "output": output})
            states.append(state)
        building_rows.append(
            {
                "id": target,
                "coverage": (
                    "source-gaps-present"
                    if any(state.get("coverage") == "source-gap" for state in states)
                    else "m3-converted"
                ),
                "states": states,
            }
        )

    for target in NEW_UNITS:
        recipe = _obj(UNIT_MODEL_RECIPES[target], f"unit recipe {target}")
        model = _text(recipe.get("model"), f"unit recipe {target} model")
        animations = tuple(recipe.get("animations", ()))
        embedded_models = {
            _text(role, f"unit recipe {target} embedded role"): _text(
                path, f"unit recipe {target} embedded {role}"
            )
            for role, path in _obj(
                recipe.get("embedded_models", {}),
                f"unit recipe {target} embedded models",
            ).items()
        }
        authored = {model, *animations, *embedded_models.values()}
        selected = {
            model,
            *animations,
            *_hierarchy_dependencies(animations, scanned),
        }
        for path in authored:
            if path.casefold() not in scanned:
                raise ValueError(f"M3 unit recipe selects absent W3D: {path}")
        texture_paths = _texture_paths_for_w3d(
            closure, {*selected, *embedded_models.values()}
        )
        patterns = sorted(selected, key=str.casefold)
        output = f"assets/models/m3/units/{_slug(target)}.glb"
        resource_id = f"m3-{_slug(target)}-rig-and-core-clips"
        if resource_id not in CONVERSION_SOURCE_GAPS:
            texture_rules, texture_ids = _texture_resources(target, texture_paths)
            resources.extend(texture_rules)
            resources.append(
                {
                    "id": resource_id,
                    "kind": "model",
                    "converter": "w3d-bundle",
                    "patterns": patterns,
                    "output": output,
                    "options": {
                        "model": PurePosixPath(model).name,
                        "animations": [PurePosixPath(path).name for path in animations],
                        "inputResourceIds": texture_ids,
                    },
                    "required": True,
                    "limit": len(patterns),
                    "expected_count": len(patterns),
                }
            )
            for role, embedded_model in sorted(
                embedded_models.items(), key=lambda item: item[0].casefold()
            ):
                embedded_output = (
                    f"assets/models/m3/units/{_slug(target)}-{_slug(role)}.glb"
                )
                resources.append(
                    {
                        "id": f"m3-{_slug(target)}-{_slug(role)}-embedded",
                        "kind": "model",
                        "converter": "w3d-bundle",
                        "patterns": [embedded_model],
                        "output": embedded_output,
                        "options": {
                            "model": PurePosixPath(embedded_model).name,
                            "animations": [PurePosixPath(embedded_model).name],
                            "inputResourceIds": texture_ids,
                        },
                        "required": True,
                        "limit": 1,
                        "expected_count": 1,
                    }
                )
            if target == "GondorTrebuchet":
                # The semantic projectile Object is outside the unit-drawable
                # target closure. Its exact W3D/texture pair is independently
                # proven by the strict effective catalog and the production
                # converter; do not misclassify it as a Trebuchet body clip.
                projectile_textures = ("art/compiledtextures/gb/gbbricks.dds",)
                projectile_texture_rules, projectile_texture_ids = _texture_resources(
                    "GondorTrebuchetRockProjectile", projectile_textures
                )
                resources.extend(projectile_texture_rules)
                resources.append(
                    {
                        "id": "m3-gondortrebuchet-rock-projectile",
                        "kind": "model",
                        "converter": "w3d-bundle",
                        "patterns": [TREBUCHET_PROJECTILE_W3D],
                        "output": TREBUCHET_PROJECTILE_OUTPUT,
                        "options": {
                            "model": PurePosixPath(TREBUCHET_PROJECTILE_W3D).name,
                            "animations": [PurePosixPath(TREBUCHET_PROJECTILE_W3D).name],
                            "inputResourceIds": projectile_texture_ids,
                        },
                        "required": True,
                        "limit": 1,
                        "expected_count": 1,
                    }
                )
        unit_row = {
            "id": target,
            "coverage": (
                "source-gap" if resource_id in CONVERSION_SOURCE_GAPS else "m3-converted"
            ),
            "sourceW3d": model,
            "animations": list(animations),
        }
        if resource_id in CONVERSION_SOURCE_GAPS:
            unit_row["sourceGapId"] = resource_id
        else:
            unit_row["output"] = output
            if target == "GondorTrebuchet":
                unit_row["projectile"] = {
                    "sourceW3d": TREBUCHET_PROJECTILE_W3D,
                    "output": TREBUCHET_PROJECTILE_OUTPUT,
                    "embeddedAnimation": True,
                }
            if embedded_models:
                unit_row["embeddedDrawables"] = {
                    role: {
                        "sourceW3d": embedded_models[role],
                        "output": (
                            f"assets/models/m3/units/{_slug(target)}-"
                            f"{_slug(role)}.glb"
                        ),
                    }
                    for role in sorted(embedded_models, key=str.casefold)
                }
        unit_rows.append(unit_row)

    resource_ids = [str(row["id"]) for row in resources]
    if len({value.casefold() for value in resource_ids}) != len(resource_ids):
        raise ValueError("M3 visual resource id collision")
    base_ids = {
        str(_obj(row, "base resource").get("id", "")).casefold()
        for row in _list(base_profile.get("resources"), "base resources")
    }
    overlap = sorted(value for value in resource_ids if value.casefold() in base_ids)
    if overlap:
        raise ValueError("M3 visual resources overlap base ids: " + ", ".join(overlap))
    census = {
        "schema": "openbfme.m3-model-census",
        "schemaVersion": 0,
        "buildings": building_rows,
        "units": unit_rows,
        "sourceNull": [
            {
                "id": "MenBatteringRam",
                "reason": "BFME2 1.06 authors no Men battering-ram Object or Men production command.",
            },
            *[
                {"id": resource_id, "reason": reason}
                for resource_id, reason in CONVERSION_SOURCE_GAPS.items()
            ],
        ],
        "summary": {
            "buildingCount": len(building_rows),
            "unitCount": len(unit_rows),
            "newModelResourceCount": sum(row.get("converter", "") != "hash-only" for row in resources),
        },
    }
    return resources, census


def house_color_mask_resources(manifest: Mapping[str, Any]) -> list[dict[str, Any]]:
    paths = _unique(manifest.get("maskTextures"), "house-color mask textures")
    resources = []
    for path in paths:
        digest = hashlib.sha256(b"openbfme.m3-house-color\0" + path.casefold().encode("utf-8")).hexdigest()
        resources.append({
            "id": f"m3-house-color-{digest[:32]}",
            "kind": "texture",
            "converter": "texture",
            "patterns": [path],
            "output": f"assets/textures/house-color/mask-{digest}.png",
            "required": True,
            "limit": 1,
            "expected_count": 1,
        })
    return resources


def validate_tooltip_closure(runtime_documents: Mapping[str, Any], base_profile: Mapping[str, Any]) -> None:
    strings_doc = _obj(_obj(base_profile.get("runtime_data"), "base runtime_data").get("data/strings.json"), "base strings")
    strings = _obj(strings_doc.get("strings"), "base strings.strings")
    available = {str(key).casefold() for key in strings}
    required: set[str] = set()
    for building in _list(_obj(runtime_documents.get(RUNTIME_PATHS["icons"]), "icon census").get("buildings"), "icon buildings"):
        building_row = _obj(building, "icon building")
        for command in _list(building_row.get("commands"), "icon commands"):
            required.update(_unique(_obj(command, "icon command").get("textIds", []), "icon text ids"))
        for command in _list(building_row.get("constructionCommands"), "construction commands"):
            required.update(_unique(_obj(command, "construction command").get("textIds", []), "construction text ids"))
    for row in _list(_obj(runtime_documents.get(RUNTIME_PATHS["upgrades"]), "upgrade manifest").get("upgrades"), "upgrades"):
        required.update(_unique(_obj(row, "upgrade row").get("textIds", []), "upgrade text ids"))
    spellbook = _obj(runtime_documents.get(RUNTIME_PATHS["spellbook"]), "spellbook")
    for family in ("sciences", "powers"):
        for row in _list(spellbook.get(family), f"spellbook {family}"):
            required.update(_unique(_obj(row, "spell row").get("textIds", []), "spell text ids"))
    missing = sorted((item for item in required if item.casefold() not in available), key=str.casefold)
    if missing:
        raise ValueError("M3 tooltip closure is missing localized strings: " + ", ".join(missing))


def validate_recipe(recipe: Mapping[str, Any]) -> Mapping[str, Any]:
    if recipe.get("runtime_data"): raise ValueError("tracked v1 must not embed retail runtime data")
    metadata = _obj(_obj(recipe.get("pack"), "pack").get("m3Recipe"), "pack.m3Recipe")
    if metadata.get("schema") != RECIPE_SCHEMA or metadata.get("schemaVersion") != 0: raise ValueError("unsupported M3 recipe")
    base = _obj(metadata.get("baseProfile"), "baseProfile")
    if _SHA.fullmatch(_text(base.get("sha256"), "base sha", 64).casefold()) is None or _SHA.fullmatch(_text(metadata.get("censusInputSha256"), "census sha", 64).casefold()) is None: raise ValueError("invalid M3 identity")
    targets = _obj(metadata.get("targets"), "targets")
    if {x.casefold() for x in _unique(targets.get("buildings"), "buildings")} != {x.casefold() for x in BUILDINGS}: raise ValueError("building targets changed")
    if {x.casefold() for x in _unique(targets.get("units"), "units")} != {x.casefold() for x in UNITS}: raise ValueError("unit targets changed")
    if {x.casefold() for x in _unique(targets.get("upgrades"), "upgrades")} != {x.casefold() for x in UPGRADES}: raise ValueError("upgrade targets changed")
    return metadata


def _canonical_bytes(value: Mapping[str, Any]) -> bytes:
    return (
        json.dumps(value, indent=2, sort_keys=True, ensure_ascii=False, allow_nan=False)
        + "\n"
    ).encode("utf-8")


def build_building_runtime_gap_contract(
    recipe: Mapping[str, Any],
    base_profile_input_sha256: str,
    recipe_sha256: str,
    building_stats: Mapping[str, Any],
    model_census: Mapping[str, Any],
) -> tuple[dict[str, Any], dict[str, Any]]:
    """Build the initial fail-closed M3 building contract and manifest descriptor."""

    pack = _obj(recipe.get("pack"), "recipe pack")
    pack_id = _id(pack.get("id"), "recipe pack id")
    pack_version = _text(pack.get("version"), "recipe pack version")
    base_profile_input_sha256 = _text(
        base_profile_input_sha256, "base profile input sha", 64
    ).casefold()
    recipe_sha256 = _text(recipe_sha256, "recipe sha", 64).casefold()
    if _SHA.fullmatch(base_profile_input_sha256) is None or _SHA.fullmatch(recipe_sha256) is None:
        raise ValueError("building runtime input provenance is not SHA-256")
    reason = (
        "No approved lifecycle, route, and behavior evidence was supplied for "
        "capability promotion."
    )
    document = {
        "schema": BUILDING_RUNTIME_SCHEMA,
        "schemaVersion": 0,
        "pack": {"id": pack_id, "version": pack_version},
        "scope": {
            "id": BUILDING_RUNTIME_SCOPE,
            "requestedIds": list(BUILDING_RUNTIME_REQUESTED_IDS),
        },
        "provenance": {
            "baseProfileInputSha256": base_profile_input_sha256,
            "recipeSha256": recipe_sha256,
            "buildingStatsSha256": hashlib.sha256(
                _canonical_bytes(building_stats)
            ).hexdigest(),
            "modelCensusSha256": hashlib.sha256(
                _canonical_bytes(model_census)
            ).hexdigest(),
        },
        "capabilities": [],
        "gaps": [
            {
                "sourceObjectId": source_id,
                "evidenceIds": [],
                "reasons": [reason],
            }
            for source_id in BUILDING_RUNTIME_REQUESTED_IDS
        ],
    }
    descriptor = {
        "path": BUILDING_RUNTIME_PATH,
        "schema": BUILDING_RUNTIME_SCHEMA,
        "schemaVersion": 0,
        "sha256": hashlib.sha256(_canonical_bytes(document)).hexdigest(),
    }
    return document, descriptor


def attach_building_runtime_gap_contract(
    profile: dict[str, Any],
    recipe: Mapping[str, Any],
    base_profile_input_sha256: str,
    recipe_sha256: str,
) -> tuple[dict[str, Any], dict[str, Any]]:
    """Attach the gap-only document through the same seam used by composition."""

    runtime = _obj(profile.setdefault("runtime_data", {}), "composed runtime_data")
    pack = _obj(profile.get("pack"), "composed pack")
    files = _obj(pack.setdefault("files", {}), "composed pack files")
    if BUILDING_RUNTIME_PATH in runtime or "buildingRuntime" in files:
        raise ValueError("building runtime contract is already attached")
    document, descriptor = build_building_runtime_gap_contract(
        recipe,
        base_profile_input_sha256,
        recipe_sha256,
        _obj(runtime.get(RUNTIME_PATHS["buildingStats"]), "composed building stats"),
        _obj(runtime.get(RUNTIME_PATHS["models"]), "composed model census"),
    )
    runtime[BUILDING_RUNTIME_PATH] = document
    files["buildingRuntime"] = descriptor
    return document, descriptor


def candidate_pack_state() -> dict[str, Any]:
    """Return the public maturity contract for the incomplete M3 census output."""

    return {
        "vertical_slice_complete": False,
        "m3SourceClosureComplete": False,
        "full_faction_complete": False,
        "asset_conversion_complete": False,
        "oracle_parity_complete": False,
        "capability_maturity": "m3-bounded-men-census-candidate",
        "m3KnownGapsReported": True,
        "m3ScopeKind": "bounded-explicit-targets",
        "m3CoverageDenominatorComplete": False,
    }


def validate_candidate_pack_state(pack: Mapping[str, Any]) -> None:
    for key, expected in candidate_pack_state().items():
        if pack.get(key) != expected:
            raise ValueError(f"M3 candidate pack state is invalid: {key}")


def _effective_manifest_paths(manifest: Mapping[str, Any]) -> tuple[str, ...]:
    paths = []
    for index, raw in enumerate(_list(manifest.get("files"), "effective manifest files")):
        row = _obj(raw, f"effective manifest files[{index}]")
        paths.append(_text(row.get("path"), f"effective manifest files[{index}].path"))
    return _dedupe(paths, "effective manifest paths")


def _require_effective_catalog_identity(
    manifest: Mapping[str, Any], expected_identity: str
) -> str:
    if (
        not isinstance(expected_identity, str)
        or not re.fullmatch(r"[0-9a-f]{64}", expected_identity)
    ):
        raise ValueError("expected catalog identity is invalid")
    catalog = _obj(manifest.get("catalog"), "effective manifest catalog")
    actual_identity = catalog.get("identity_sha256")
    if (
        not isinstance(actual_identity, str)
        or not re.fullmatch(r"[0-9a-f]{64}", actual_identity)
    ):
        raise ValueError("effective manifest catalog identity is invalid")
    if actual_identity != expected_identity:
        raise ValueError(
            "effective manifest catalog identity does not match the current catalog"
        )
    return actual_identity


def compose_private_profile(
    recipe: Mapping[str, Any],
    base_profile: Mapping[str, Any],
    census_report: Mapping[str, Any],
    visual_closure: Mapping[str, Any],
    assets_root: Path,
    effective_manifest: Mapping[str, Any],
    input_provenance: Mapping[str, str],
) -> dict[str, Any]:
    metadata = validate_recipe(recipe)
    source_catalog_identity = _require_effective_catalog_identity(
        effective_manifest,
        _text(
            input_provenance.get("expectedCatalogIdentitySha256"),
            "expected catalog identity",
            64,
        ),
    )
    encoded_base = _canonical_bytes(base_profile)
    expected = _obj(metadata.get("baseProfile"), "baseProfile")
    if base_profile.get("id") != expected.get("id") or hashlib.sha256(encoded_base).hexdigest() != expected.get("sha256"): raise ValueError("private base profile identity changed")
    census_sha256 = hashlib.sha256(_canonical_bytes(census_report)).hexdigest()
    if census_sha256 != metadata.get("censusInputSha256"):
        raise ValueError("private Men census identity changed")
    if visual_closure.get("schema") != "openbfme.retail-visual-closure":
        raise ValueError("unexpected M3 visual closure")
    unresolved = _obj(visual_closure.get("unresolved"), "visual closure unresolved")
    unresolved_rows = _list(unresolved.get("references"), "visual unresolved references")
    allowed_unresolved = {
        "gbwallrampart.gbwallrampart",
        "gucavalry_atra",
    }
    actual_unresolved = {
        _text(_obj(row, "unresolved reference").get("identifier"), "unresolved identifier").casefold()
        for row in unresolved_rows
    }
    if actual_unresolved != allowed_unresolved:
        raise ValueError("M3 visual unresolved set changed")

    result = deepcopy(dict(base_profile))
    result["id"] = recipe["id"]
    result["title"] = recipe["title"]
    base_resources = list(_list(result.get("resources"), "base resources"))
    recipe_resources = deepcopy(_list(recipe.get("resources"), "recipe resources"))
    base_resource_ids = {
        _id(_obj(row, "base resource").get("id"), "base resource id").casefold()
        for row in base_resources
    }
    base_patterns = {
        str(pattern).replace("\\", "/").casefold()
        for row in base_resources
        for pattern in _list(_obj(row, "base resource").get("patterns"), "base resource patterns")
    }
    for row in recipe_resources:
        resource = _obj(row, "recipe resource")
        identifier = _id(resource.get("id"), "recipe resource id")
        if identifier.casefold() in base_resource_ids:
            raise ValueError(f"recipe resource collides with base profile: {identifier}")
        duplicate_patterns = sorted(
            str(pattern).replace("\\", "/")
            for pattern in _list(resource.get("patterns"), "recipe resource patterns")
            if str(pattern).replace("\\", "/").casefold() in base_patterns
        )
        if duplicate_patterns:
            raise ValueError(
                f"recipe resource duplicates base ownership: {identifier}: "
                + ", ".join(duplicate_patterns)
            )
    result["resources"] = [*base_resources, *recipe_resources]
    selection_transitions = extend_selection_transitions(result, visual_closure)
    visual_resources, model_census = build_m3_visual_resources(visual_closure, result)
    house_color = build_house_color_from_assets(
        visual_closure, assets_root, _effective_manifest_paths(effective_manifest)
    )
    spellbook = extract_spellbook(
        census_report,
        assets_root.joinpath("data", "ini", "science.ini").read_bytes(),
        assets_root.joinpath("data", "ini", "specialpower.ini").read_bytes(),
        assets_root.joinpath("data", "ini", "gamedata.ini").read_bytes(),
    )
    resolved_images = {
        _id(_obj(row, "mapped image").get("id"), "mapped image id").casefold()
        for row in _list(
            _obj(census_report.get("resolvedLeaves"), "resolvedLeaves").get("mappedImages"),
            "mappedImages",
        )
    }
    for family in ("sciences", "powers"):
        for row in spellbook[family]:
            if not row["icons"] or any(icon.casefold() not in resolved_images for icon in row["icons"]):
                raise ValueError(f"spellbook row has unresolved icon: {row['id']}")
    building_stats = extract_building_stats(
        census_report, assets_root, effective_manifest, result
    )
    ranger_runtime = build_ranger_runtime_contract(
        assets_root, effective_manifest, result, building_stats
    )
    attach_ranger_playable_bindings(result, ranger_runtime, model_census)
    trebuchet_runtime = build_trebuchet_runtime_contract(
        assets_root, effective_manifest, result, building_stats, model_census
    )
    attach_trebuchet_playable_bindings(result, trebuchet_runtime)
    runtime_documents = {
        RUNTIME_PATHS["buildingStats"]: building_stats,
        RUNTIME_PATHS["icons"]: build_icon_census(census_report),
        RUNTIME_PATHS["upgrades"]: build_upgrade_manifest(census_report),
        RUNTIME_PATHS["spellbook"]: spellbook,
        RUNTIME_PATHS["gameData"]: extract_command_points(
            assets_root.joinpath("data", "ini", "gamedata.ini").read_bytes()
        ),
        RUNTIME_PATHS["houseColor"]: house_color,
        RUNTIME_PATHS["selectionTransitions"]: selection_transitions,
        RUNTIME_PATHS["models"]: model_census,
        RUNTIME_PATHS["rangerRuntime"]: ranger_runtime,
        RUNTIME_PATHS["trebuchetRuntime"]: trebuchet_runtime,
    }
    if set(runtime_documents) != set(RUNTIME_PATHS.values()):
        raise ValueError("M3 runtime document set is incomplete")
    validate_tooltip_closure(runtime_documents, result)

    pack = _obj(result.get("pack"), "base pack")
    pack.update(
        {
            "version": recipe["pack"]["version"],
            "sourceCatalogIdentitySha256": source_catalog_identity,
            **candidate_pack_state(),
            "m3Recipe": deepcopy(metadata),
            "m3VisualClosureSha256": hashlib.sha256(_canonical_bytes(visual_closure)).hexdigest(),
            "m3MissingSources": [
                *deepcopy(metadata.get("sourceNull", [])),
                *deepcopy(house_color.get("sourceNull", [])),
                *deepcopy(runtime_documents[RUNTIME_PATHS["icons"]].get("missing", [])),
                *deepcopy(
                    runtime_documents[RUNTIME_PATHS["buildingStats"]].get("missing", [])
                ),
                *[
                    {"id": resource_id, "reason": reason}
                    for resource_id, reason in CONVERSION_SOURCE_GAPS.items()
                ],
            ],
        }
    )
    validate_candidate_pack_state(pack)
    files = pack.setdefault("files", {})
    files.update(RUNTIME_PATHS)
    resources = list(_list(result.get("resources"), "base resources"))
    added_resources = [*visual_resources, *house_color_mask_resources(house_color)]
    resources.extend(added_resources)
    resource_ids = [str(_obj(row, "resource").get("id", "")) for row in resources]
    if len(resource_ids) != len({value.casefold() for value in resource_ids}):
        raise ValueError("composed M3 profile has resource id collisions")
    base_outputs = {
        str(_obj(row, "base resource").get("output")).casefold()
        for row in _list(result.get("resources"), "base resources")
        if _obj(row, "base resource").get("output")
    }
    outputs = [
        str(_obj(row, "resource").get("output"))
        for row in added_resources
        if _obj(row, "resource").get("output")
    ]
    if len(outputs) != len({value.casefold() for value in outputs}) or any(
        value.casefold() in base_outputs for value in outputs
    ):
        raise ValueError("composed M3 profile has output collisions")
    if len(resources) > MAX_RESOURCES: raise ValueError("composed M3 profile exceeds resource limit")
    result["resources"] = resources
    runtime = result.setdefault("runtime_data", {})
    runtime.update(deepcopy(runtime_documents))
    attach_building_runtime_gap_contract(
        result,
        recipe,
        _text(input_provenance.get("baseProfileInputSha256"), "base profile input sha", 64),
        _text(input_provenance.get("recipeSha256"), "recipe sha", 64),
    )
    return result


def _load_json_with_sha256(
    path: Path, label: str, maximum: int = 64 * 1024 * 1024
) -> tuple[Mapping[str, Any], str]:
    if not path.is_file():
        raise FileNotFoundError(f"{label} not found: {path}")
    if path.stat().st_size > maximum:
        raise ValueError(f"{label} exceeds {maximum} byte limit")
    with path.open("rb") as stream:
        payload = stream.read(maximum + 1)
    if len(payload) > maximum:
        raise ValueError(f"{label} exceeds {maximum} byte limit")
    value = json.loads(payload.decode("utf-8"))
    return _obj(value, label), hashlib.sha256(payload).hexdigest()


def _load_json(path: Path, label: str, maximum: int = 64 * 1024 * 1024) -> Mapping[str, Any]:
    return _load_json_with_sha256(path, label, maximum)[0]


def validated_private_output_path(output_path: Path, private_root: Path) -> Path:
    """Resolve an output below an existing canonical `.private` root."""

    root = private_root.resolve(strict=True)
    if not root.is_dir() or root.name.casefold() != ".private":
        raise ValueError("private output root must be an existing .private directory")
    parent = output_path.parent.resolve(strict=True)
    resolved = parent / output_path.name
    try:
        resolved.relative_to(root)
    except ValueError as error:
        raise ValueError("generated M3 output must remain below the private root") from error
    if output_path.is_symlink():
        raise ValueError("generated M3 output must not be a symbolic link")
    return resolved


def compose_profile_from_paths(
    recipe_path: Path,
    base_profile_path: Path,
    census_path: Path,
    visual_closure_path: Path,
    assets_root: Path,
    effective_manifest_path: Path,
    expected_catalog_identity_sha256: str,
    output_path: Path,
    private_root: Path,
) -> dict[str, Any]:
    output_path = validated_private_output_path(output_path, private_root)
    recipe, recipe_sha256 = _load_json_with_sha256(recipe_path, "M3 recipe")
    base_profile, base_profile_sha256 = _load_json_with_sha256(
        base_profile_path, "M3 base profile"
    )
    effective_manifest = _load_json(
        effective_manifest_path, "effective-assets manifest"
    )
    _require_effective_catalog_identity(
        effective_manifest, expected_catalog_identity_sha256
    )
    profile = compose_private_profile(
        recipe,
        base_profile,
        _load_json(census_path, "M3 Men census"),
        _load_json(visual_closure_path, "M3 visual closure"),
        assets_root.resolve(),
        effective_manifest,
        {
            "baseProfileInputSha256": base_profile_sha256,
            "recipeSha256": recipe_sha256,
            "expectedCatalogIdentitySha256": expected_catalog_identity_sha256,
        },
    )
    write_json_atomic(output_path, profile)
    return profile


def _write_text_atomic(path: Path, payload: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(
        prefix=path.name + ".", suffix=".tmp", dir=path.parent
    )
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as stream:
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        Path(temporary).unlink(missing_ok=True)


def write_m3_expansion_report(
    profile_path: Path,
    bundle_a: str,
    bundle_b: str,
    selection_path: Path,
    output_path: Path,
    private_root: Path,
) -> None:
    output_path = validated_private_output_path(output_path, private_root)
    bundle_a = bundle_a.casefold()
    bundle_b = bundle_b.casefold()
    if _SHA.fullmatch(bundle_a) is None or _SHA.fullmatch(bundle_b) is None:
        raise ValueError("M3 bundle digests must be SHA-256 values")
    if bundle_a != bundle_b:
        raise ValueError("M3 Build A and Build B bundle digests differ")
    profile = _load_json(profile_path, "generated M3 profile")
    selection = _load_json(selection_path, "pack selection", 64 * 1024)
    expected_selection = f"bfme2-men-vslice/{bundle_b}"
    if selection.get("activePack") != expected_selection:
        raise ValueError("selection.json does not point at the M3 bundle")
    runtime = _obj(profile.get("runtime_data"), "M3 runtime_data")
    icons = _obj(runtime.get(RUNTIME_PATHS["icons"]), "M3 icon census")
    building_stats = _obj(
        runtime.get(RUNTIME_PATHS["buildingStats"]), "M3 building stats"
    )
    models = _obj(runtime.get(RUNTIME_PATHS["models"]), "M3 model census")
    upgrades = _obj(runtime.get(RUNTIME_PATHS["upgrades"]), "M3 upgrades")
    spellbook = _obj(runtime.get(RUNTIME_PATHS["spellbook"]), "M3 spellbook")
    house = _obj(runtime.get(RUNTIME_PATHS["houseColor"]), "M3 house color")
    strings = _obj(
        _obj(runtime.get("data/strings.json"), "M3 strings document").get("strings"),
        "M3 strings",
    )
    audio = _obj(runtime.get("data/audio_events.json"), "M3 audio events")
    ui = _obj(runtime.get("data/ui_manifest.json"), "M3 UI manifest")
    pack = _obj(profile.get("pack"), "M3 pack")
    missing = _list(pack.get("m3MissingSources", []), "M3 missing sources")

    lines = [
        "# M3 Men source-census candidate report",
        "",
        "## Identity and deterministic build",
        "",
        f"- Generated profile: `{profile.get('id')}`",
        f"- Profile SHA-256: `{hashlib.sha256(profile_path.read_bytes()).hexdigest()}`",
        f"- Build A bundle SHA-256: `{bundle_a}`",
        f"- Build B bundle SHA-256: `{bundle_b}`",
        f"- Selected pack: `{expected_selection}`",
        "- A/B result: byte-identical bundle digests.",
        "",
        "## Content census",
        "",
        f"- Buildings: {len(_list(models.get('buildings'), 'model buildings'))}",
        f"- Typed building gameplay-stat records: {len(_list(building_stats.get('buildings'), 'building stats'))}",
        f"- Typed trainable gameplay-stat records: {len(_list(building_stats.get('trainables'), 'trainable stats'))}",
        f"- Units with converted/base model coverage: {len(_list(models.get('units'), 'model units'))}",
        f"- New model conversion resources: {_obj(models.get('summary'), 'model summary').get('newModelResourceCount')}",
        f"- Building commands with icons: {_obj(icons.get('summary'), 'icon summary').get('commandCount')}",
        f"- Retail dynamic hero-revive icon slots: {_obj(icons.get('summary'), 'icon summary').get('dynamicIconCount')}",
        f"- Building construction buttons with icons: {_obj(icons.get('summary'), 'icon summary').get('buildingButtonCount')}",
        f"- UI mapped images: {len(_list(ui.get('images'), 'UI mapped images'))}",
        f"- Required upgrades: {upgrades.get('count')}",
        f"- SpellBook sciences/powers: {spellbook.get('scienceCount')}/{spellbook.get('powerCount')}",
        f"- Tooltip strings: {len(strings)}",
        f"- Audio definitions: {len(audio)} top-level records (full event/sample counts remain in `data/audio_events.json`).",
        f"- House-color model entries/masks: {_obj(house.get('summary'), 'house summary').get('modelCount')}/{_obj(house.get('summary'), 'house summary').get('maskTextureCount')}",
        "- BFME2 logo: no clean logo leaf found; classified in the missing-source list.",
        "",
        "## Building command icon coverage",
        "",
        "| Building | Building button(s) | Expected commands | Icon present |",
        "|---|---|---|---|",
    ]
    for raw_building in _list(icons.get("buildings"), "icon buildings"):
        building = _obj(raw_building, "icon building")
        commands = _list(building.get("commands"), "building commands")
        construction = _list(building.get("constructionCommands"), "building construction commands")
        command_text = ", ".join(str(_obj(row, "command").get("id")) for row in commands)
        construction_text = ", ".join(
            str(_obj(row, "construction command").get("id")) for row in construction
        )
        present = bool(construction) and all(
            bool(_obj(row, "construction command").get("iconPresent"))
            for row in construction
        ) and all(bool(_obj(row, "command").get("iconPresent")) for row in commands)
        lines.append(f"| {building.get('id')} | {construction_text or '(none authored)'} | {command_text or '(none authored)'} | {'yes' if present else 'no'} |")
    lines.extend(["", "## Missing retail sources", ""])
    for raw in missing:
        row = _obj(raw, "missing source")
        missing_id = _text(row.get("id"), "missing source id")
        reason = _text(row.get("reason"), f"missing source {missing_id} reason")
        lines.append(f"- `{missing_id}`: {reason}")
    lines.extend(
        [
            "",
            "## Scope and containment",
            "",
            "- Generated retail-derived data and converted payloads remain below `.private/`.",
            "- Tracked changes are limited to importer source, profile recipe, and tests.",
            "- No file under `game/` was modified and no Godot gate was run.",
            "",
        ]
    )
    _write_text_atomic(output_path, "\n".join(lines))


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="python -m openbfme_importer.m3_pack_expansion")
    sub = parser.add_subparsers(dest="command", required=True)
    compose = sub.add_parser("compose")
    compose.add_argument("--recipe", type=Path, required=True)
    compose.add_argument("--base-profile", type=Path, required=True)
    compose.add_argument("--census", type=Path, required=True)
    compose.add_argument("--visual-closure", type=Path, required=True)
    compose.add_argument("--assets-root", type=Path, required=True)
    compose.add_argument("--effective-manifest", type=Path, required=True)
    compose.add_argument("--expected-catalog-identity", required=True)
    compose.add_argument("--output", type=Path, required=True)
    compose.add_argument("--private-root", type=Path, required=True)
    report = sub.add_parser("report")
    report.add_argument("--profile", type=Path, required=True)
    report.add_argument("--bundle-a", required=True)
    report.add_argument("--bundle-b", required=True)
    report.add_argument("--selection", type=Path, required=True)
    report.add_argument("--output", type=Path, required=True)
    report.add_argument("--private-root", type=Path, required=True)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    if args.command == "compose":
        profile = compose_profile_from_paths(
            args.recipe,
            args.base_profile,
            args.census,
            args.visual_closure,
            args.assets_root,
            args.effective_manifest,
            args.expected_catalog_identity,
            args.output,
            args.private_root,
        )
        print(
            json.dumps(
                {
                    "profile": str(args.output.resolve()),
                    "profileSha256": hashlib.sha256(args.output.read_bytes()).hexdigest(),
                    "resourceCount": len(profile["resources"]),
                },
                indent=2,
                sort_keys=True,
            )
        )
        return 0
    write_m3_expansion_report(
        args.profile, args.bundle_a, args.bundle_b, args.selection, args.output, args.private_root
    )
    print(json.dumps({"report": str(args.output.resolve())}, indent=2, sort_keys=True))
    return 0


__all__ = ["BUILDINGS", "BUILDING_RUNTIME_PATH", "BUILDING_RUNTIME_REQUESTED_IDS", "CP_FIELDS", "RANGER_RUNTIME_PATH", "RANGER_RUNTIME_SCHEMA", "TREBUCHET_RUNTIME_PATH", "TREBUCHET_RUNTIME_SCHEMA", "RUNTIME_PATHS", "SELECTION_TRANSITIONS", "UNITS", "UPGRADES", "attach_building_runtime_gap_contract", "attach_ranger_playable_bindings", "attach_trebuchet_playable_bindings", "build_building_runtime_gap_contract", "build_house_color_from_assets", "build_house_color_manifest", "build_icon_census", "build_m3_visual_resources", "build_ranger_runtime_contract", "build_trebuchet_runtime_contract", "build_upgrade_manifest", "candidate_pack_state", "compose_private_profile", "compose_profile_from_paths", "declarative_visual_resources", "extend_selection_transitions", "extract_building_stats", "extract_command_points", "extract_spellbook", "house_color_mask_resources", "parse_house_colors", "validated_private_output_path", "validate_candidate_pack_state", "validate_recipe", "validate_tooltip_closure", "write_m3_expansion_report"]


if __name__ == "__main__":
    raise SystemExit(main())
