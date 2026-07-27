"""Compose the exact private Men/Fords retail-slice ImportProfile.

The composer is deliberately a strict, fail-closed join.  It does not search
for neighbouring assets or infer replacements.  Every source profile is first
accepted by :class:`ImportProfile`, every intentional overlap is named below,
and the finished profile is resolved against a caller-supplied BFME2 catalog.

This module only writes profile/report metadata.  It does not build, publish,
or select a content pack.
"""

from __future__ import annotations

from copy import deepcopy
from dataclasses import dataclass
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import tempfile
from typing import Any, Iterable, Mapping, Sequence

from .catalog import InstallCatalog
from .paths import safe_relative_parts
from .profile import (
    ImportProfile,
    MAX_PROFILE_BYTES,
    MAX_RESOURCES,
    W3D_SOURCE_VARIANT_OF_OPTION,
    resolve_profile,
)
from .util import write_json_atomic


BASE_PROFILE_ID = "men-fords-v0"
FULL_PROFILE_ID = "men-fords-v0-full-generated"
PACK_ID = "bfme2-men-vslice"
ROAD_PROFILE_ID = "men-fords-v0-roads-generated"
FACTION_PROFILE_ID = "bfme2-men-106-leaf-closure"

STATIC_PLAN_SCHEMA = "openbfme.retail-static-prop-plan"
STATIC_PLAN_SCHEMA_VERSION = 0
HIERARCHICAL_PLAN_SCHEMA = "openbfme.retail-hierarchical-prop-plan"
HIERARCHICAL_PLAN_SCHEMA_VERSION = 0
ANIMATED_PLAN_SCHEMA = "openbfme.retail-animated-prop-plan"
ANIMATED_PLAN_SCHEMA_VERSION = 0
COMPOSITION_REPORT_SCHEMA = "openbfme.retail-slice-profile-composition"
COMPOSITION_REPORT_SCHEMA_VERSION = 0

ROAD_RUNTIME_PATH = "maps/fords-of-isen-ii/road-materials.json"
ROAD_MATERIALS_RELATIVE_PATH = "road-materials.json"
MAP_RESOURCE_ID = "fords-map-binary"
PARTIAL_RESOURCE_ID = "gondor-fighter-definitions"

# 83 asset/data rules plus the living-world strategic document rule.
EXPECTED_BASE_RESOURCE_COUNT = 84
EXPECTED_ROAD_RESOURCE_COUNT = 89
EXPECTED_FACTION_RESOURCE_COUNT = 85
EXPECTED_STATIC_RESOURCE_COUNT = 53
EXPECTED_STATIC_BINDING_COUNT = 38
EXPECTED_HIERARCHICAL_RESOURCE_COUNT = 16
EXPECTED_HIERARCHICAL_BINDING_COUNT = 6
EXPECTED_HIERARCHICAL_REUSE_COUNT = 3
EXPECTED_HIERARCHICAL_ADDED_RESOURCE_COUNT = 13
EXPECTED_ANIMATED_RESOURCE_COUNT = 22
EXPECTED_ANIMATED_BINDING_COUNT = 10
EXPECTED_ANIMATED_REUSE_COUNT = 1
EXPECTED_ANIMATED_ADDED_RESOURCE_COUNT = 21
EXPECTED_PRE_ANIMATED_BINDING_COUNT = 44
EXPECTED_FINAL_BINDING_COUNT = 54
EXPECTED_FINAL_RESOURCE_COUNT = 241
EXPECTED_SOURCE_VARIANTS = {
    "men-fortress-damaged-model": "men-fortress-intact-model",
}

FACTION_RUNTIME_PATHS = (
    "data/audio_events.json",
    "data/strings.json",
    "data/ui_manifest.json",
)

EXPECTED_ROADS = (
    "Footprints",
    "FtPrintDrkGr02",
    "FtPrintGrass02",
    "FtprintsDrk",
    "FtprintsDrk02",
)

EXPECTED_ROAD_RESOURCES: dict[str, tuple[str, str]] = {
    "fords-road-texture-trdirtroad": (
        "art/compiledtextures/tr/trdirtroad.dds",
        "maps/fords-of-isen-ii/road-materials/textures/trdirtroad.png",
    ),
    "fords-road-texture-trfootprintdark02": (
        "art/compiledtextures/tr/trfootprintdark02.dds",
        "maps/fords-of-isen-ii/road-materials/textures/trfootprintdark02.png",
    ),
    "fords-road-texture-trfootprintdarksing": (
        "art/compiledtextures/tr/trfootprintdarksing.dds",
        "maps/fords-of-isen-ii/road-materials/textures/trfootprintdarksing.png",
    ),
    "fords-road-texture-trftprntdrksing": (
        "art/compiledtextures/tr/trftprntdrksing.dds",
        "maps/fords-of-isen-ii/road-materials/textures/trftprntdrksing.png",
    ),
    "fords-road-texture-trftprntgrsssing": (
        "art/compiledtextures/tr/trftprntgrsssing.dds",
        "maps/fords-of-isen-ii/road-materials/textures/trftprntgrsssing.png",
    ),
}

EXPECTED_HIERARCHICAL_REUSE_OWNERS = {
    "art/compiledtextures/gb/gbbarracks_n.dds": "men-structure-shared-material-textures",
    "art/compiledtextures/gb/gbfarm.dds": "men-farm-material-textures",
    "art/compiledtextures/pr/prgrey.dds": ("static-prop-texture-prgrey-791db9ad131c"),
}

EXPECTED_ANIMATED_REUSE_OWNERS = {
    "art/compiledtextures/sh/shadowi.tga": ("static-prop-texture-shadowi-6536093a930f"),
}
EXPECTED_ANIMATED_SHARED_W3D_RESOURCE_ID = (
    "animated-prop-shared-w3d-nuhorse-skl-4678722e3886"
)
EXPECTED_ANIMATED_SHARED_W3D_PATTERNS = (
    "art/w3d/nu/nuhorse_diea.w3d",
    "art/w3d/nu/nuhorse_dwna.w3d",
    "art/w3d/nu/nuhorse_grza.w3d",
    "art/w3d/nu/nuhorse_grzb.w3d",
    "art/w3d/nu/nuhorse_runa.w3d",
    "art/w3d/nu/nuhorse_skl.w3d",
    "art/w3d/nu/nuhorse_upa.w3d",
    "art/w3d/nu/nuhorse_wlka.w3d",
)
EXPECTED_ANIMATED_SHARED_W3D_CONSUMERS = frozenset(
    {
        "cuelk_skn.w3d",
        "cuelkf_skn.w3d",
    }
)
EXPECTED_ANIMATED_BINDING_TYPES = (
    "Bear",
    "CaptureFlag",
    "Duck",
    "Egret",
    "ElkFemale",
    "ElkMale",
    "Fish",
    "Rabbit",
    "Raccoon",
    "Wolf",
)

# Every deletion is an exact ID plus its complete expected source-pattern list.
# A renamed resource or changed source list is a contract change, not permission
# for this composer to make a fuzzy deletion.
FULLY_PRUNED_BASE_RESOURCES: dict[str, tuple[str, ...]] = {
    "fords-prop-ptgrass15-model": ("art/w3d/pt/ptgrass15.w3d",),
    "fords-prop-ptgrass15-texture": ("art/compiledtextures/pt/ptgrass05.dds",),
    "men-slice-building-definitions": (
        "data/ini/object/goodfaction/structures/men/fortress.ini",
        "data/ini/object/goodfaction/structures/men/farm.ini",
        "data/ini/object/goodfaction/structures/men/barracks.ini",
        "data/ini/object/goodfaction/structures/men/archerrange.ini",
        "data/ini/object/goodfaction/structures/men/stable.ini",
    ),
    "men-slice-ui-definitions": (
        "data/ini/mappedimages/aptimages/buildingradialbuttons.ini",
        "data/ini/mappedimages/aptimages/strategicimages.ini",
    ),
    "men-slice-additional-unit-definitions": (
        "data/ini/object/goodfaction/units/men/gondorarcher.ini",
        "data/ini/object/goodfaction/units/men/gondortowershieldguard.ini",
        "data/ini/object/goodfaction/units/men/gondorcavalry.ini",
        "data/ini/object/goodfaction/units/men/porter.ini",
    ),
    "fords-localization": ("data/lotr.str",),
    "gondor-fighter-select-voices": ("data/audio/sounds/gusoldg_voisel?.wav",),
    "gondor-fighter-battle-select-voices": ("data/audio/sounds/gusoldg_voiseb?.wav",),
    "gondor-fighter-attack-voices": ("data/audio/sounds/gusoldg_voiatt?.wav",),
    "gondor-fighter-charge-voices": ("data/audio/sounds/gusoldg_voiatc?.wav",),
    "gondor-fighter-building-attack-voices": ("data/audio/sounds/gusoldg_voiatb?.wav",),
    "gondor-fighter-additional-select-voice": (
        "data/audio/sounds/gugoswo_voise2a.wav",
    ),
    "gondor-archer-portrait": ("art/compiledtextures/up/upgondor_archer.dds",),
    "gondor-tower-guard-portrait": ("art/compiledtextures/up/upgondor_towerguard.dds",),
    "gondor-knight-portrait": ("art/compiledtextures/up/upgondor_knight.dds",),
    "gondor-fighter-portrait": ("art/compiledtextures/up/upgondor_soldier.dds",),
    "men-unit-command-icons": ("art/compiledtextures/st/strategicimages_001.dds",),
    "men-barracks-train-icons": (
        "art/compiledtextures/bu/buildingradialbuttons_168.dds",
    ),
    "gondor-archer-train-icon": (
        "art/compiledtextures/bu/buildingradialbuttons_167.dds",
    ),
    "gondor-knight-train-icon": (
        "art/compiledtextures/bu/buildingradialbuttons_187.dds",
    ),
}

SEMANTIC_PRUNE_IDS = frozenset(
    {
        "men-slice-building-definitions",
        "men-slice-ui-definitions",
        "men-slice-additional-unit-definitions",
        "fords-localization",
    }
)
AUDIO_PRUNE_IDS = frozenset(
    {
        "gondor-fighter-select-voices",
        "gondor-fighter-battle-select-voices",
        "gondor-fighter-attack-voices",
        "gondor-fighter-charge-voices",
        "gondor-fighter-building-attack-voices",
        "gondor-fighter-additional-select-voice",
    }
)
UI_PRUNE_IDS = frozenset(
    {
        "gondor-archer-portrait",
        "gondor-tower-guard-portrait",
        "gondor-knight-portrait",
        "gondor-fighter-portrait",
        "men-unit-command-icons",
        "men-barracks-train-icons",
        "gondor-archer-train-icon",
        "gondor-knight-train-icon",
    }
)
STATIC_REPLACEMENT_PRUNE_IDS = frozenset(
    {"fords-prop-ptgrass15-model", "fords-prop-ptgrass15-texture"}
)

PARTIAL_ORIGINAL_PATTERNS = (
    "data/ini/object/goodfaction/units/men/gondorfighter.ini",
    "data/ini/object/goodfaction/hordes/men/menhordes.ini",
    "data/ini/armor.ini",
    "data/ini/attributemodifier.ini",
    "data/ini/commandbutton.ini",
    "data/ini/commandset.ini",
    "data/ini/gamedata.ini",
    "data/ini/locomotor.ini",
    "data/ini/music.ini",
    "data/ini/voice.ini",
    "data/ini/weapon.ini",
)
PARTIAL_REMOVED_PATTERNS = (
    "data/ini/object/goodfaction/units/men/gondorfighter.ini",
    "data/ini/object/goodfaction/hordes/men/menhordes.ini",
    "data/ini/commandbutton.ini",
    "data/ini/commandset.ini",
    "data/ini/voice.ini",
)
PARTIAL_RETAINED_PATTERNS = (
    "data/ini/armor.ini",
    "data/ini/attributemodifier.ini",
    "data/ini/gamedata.ini",
    "data/ini/locomotor.ini",
    "data/ini/music.ini",
    "data/ini/weapon.ini",
)

OBJECT_UI_BINDINGS: dict[str, tuple[str, str, str, str]] = {
    "bfme2.object.gondor-fighter": (
        "UPGondor_Soldier",
        "WOR_GondorSoldier",
        "assets/ui/upgondor-soldier.png",
        "assets/ui/wor-gondor-soldier.png",
    ),
    "bfme2.object.gondor-archer": (
        "UPGondor_Archer",
        "WOR_GondorArcher",
        "assets/ui/upgondor-archer.png",
        "assets/ui/wor-gondor-archer.png",
    ),
    "bfme2.object.gondor-tower-guard": (
        "UPGondor_TowerGuard",
        "WOR_GondorTowerGuard",
        "assets/ui/upgondor-towerguard.png",
        "assets/ui/wor-gondor-tower-guard.png",
    ),
    "bfme2.object.gondor-knight": (
        "UPGondor_Knight",
        "WOR_GondorKnights",
        "assets/ui/upgondor-knight.png",
        "assets/ui/wor-gondor-knights.png",
    ),
}

EXPECTED_UI_SOURCE_PATHS: dict[str, str] = {
    "UPGondor_Soldier": "art/compiledtextures/up/upgondor_soldier.dds",
    "UPGondor_Archer": "art/compiledtextures/up/upgondor_archer.dds",
    "UPGondor_TowerGuard": "art/compiledtextures/up/upgondor_towerguard.dds",
    "UPGondor_Knight": "art/compiledtextures/up/upgondor_knight.dds",
    "WOR_GondorSoldier": "art/compiledtextures/st/strategicimages_001.dds",
    "WOR_GondorArcher": "art/compiledtextures/st/strategicimages_001.dds",
    "WOR_GondorTowerGuard": "art/compiledtextures/st/strategicimages_001.dds",
    "WOR_GondorKnights": "art/compiledtextures/st/strategicimages_001.dds",
}

_BUNDLE_CONVERTERS = {
    "retail-unit-rules",
    "living-world",
    "w3d-bundle",
    "w3d-hierarchical",
    "w3d-static",
    "sage-terrain-materials",
}


@dataclass(frozen=True, slots=True)
class ComposedRetailSliceProfile:
    """Validated profile payload and its deterministic composition report."""

    profile: dict[str, Any]
    report: dict[str, Any]


def _canonical_json_sha256(value: object) -> str:
    encoded = json.dumps(
        value,
        sort_keys=True,
        ensure_ascii=False,
        separators=(",", ":"),
        allow_nan=False,
    ).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def _pretty_json_bytes(value: object) -> bytes:
    return (
        json.dumps(value, indent=2, sort_keys=True, ensure_ascii=False) + "\n"
    ).encode("utf-8")


def _file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _mapping(value: object, label: str) -> Mapping[str, Any]:
    if not isinstance(value, Mapping):
        raise ValueError(f"{label} must be an object")
    return value


def _array(value: object, label: str) -> list[Any]:
    if not isinstance(value, list):
        raise ValueError(f"{label} must be an array")
    return value


def _text(value: object, label: str) -> str:
    if not isinstance(value, str) or not value:
        raise ValueError(f"{label} must be a non-empty string")
    return value


def _safe_path(value: object, label: str) -> str:
    path = _text(value, label)
    if "\\" in path:
        raise ValueError(f"{label} is not a canonical POSIX path: {path!r}")
    canonical = "/".join(safe_relative_parts(path))
    if canonical != path:
        raise ValueError(f"{label} is not canonical: {path!r}")
    return path


def _case_unique(values: Iterable[str], label: str) -> dict[str, str]:
    result: dict[str, str] = {}
    for value in values:
        key = value.casefold()
        previous = result.get(key)
        if previous is not None:
            raise ValueError(
                f"case-insensitive {label} collision: {previous!r}, {value!r}"
            )
        result[key] = value
    return result


def _resource_map(
    resources: Sequence[Mapping[str, Any]], label: str
) -> dict[str, Mapping[str, Any]]:
    result: dict[str, Mapping[str, Any]] = {}
    original: dict[str, str] = {}
    for position, resource in enumerate(resources):
        resource_id = _text(resource.get("id"), f"{label} resource {position} id")
        key = resource_id.casefold()
        if key in result:
            raise ValueError(
                f"case-insensitive {label} resource id collision: "
                f"{original[key]!r}, {resource_id!r}"
            )
        result[key] = resource
        original[key] = resource_id
    return result


def _load_profile(
    path: Path | str, label: str
) -> tuple[ImportProfile, dict[str, Any], Path]:
    source = Path(path).expanduser().resolve()
    loaded = ImportProfile.load(source)
    try:
        payload = json.loads(source.read_text(encoding="utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ValueError(f"invalid {label}: {exc}") from exc
    if not isinstance(payload, dict):
        raise ValueError(f"{label} root must be an object")
    return loaded, payload, source


def _load_static_plan(path: Path | str) -> tuple[dict[str, Any], Path]:
    source = Path(path).expanduser().resolve()
    if source.stat().st_size > MAX_PROFILE_BYTES:
        raise ValueError("static-prop plan exceeds the profile-size safety bound")
    try:
        payload = json.loads(source.read_text(encoding="utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ValueError(f"invalid static-prop plan: {exc}") from exc
    if not isinstance(payload, dict):
        raise ValueError("static-prop plan root must be an object")
    if payload.get("schema") != STATIC_PLAN_SCHEMA:
        raise ValueError("unsupported static-prop plan schema")
    if payload.get("schemaVersion") != STATIC_PLAN_SCHEMA_VERSION:
        raise ValueError("unsupported static-prop plan schema version")
    declared = _text(payload.get("aggregateSha256"), "static-prop aggregateSha256")
    digest_payload = dict(payload)
    digest_payload.pop("aggregateSha256", None)
    actual = _canonical_json_sha256(digest_payload)
    if declared != actual:
        raise ValueError(
            f"static-prop plan digest mismatch: declared {declared}, calculated {actual}"
        )
    return payload, source


def _load_hierarchical_plan(path: Path | str) -> tuple[dict[str, Any], Path]:
    source = Path(path).expanduser().resolve()
    if source.stat().st_size > MAX_PROFILE_BYTES:
        raise ValueError("hierarchical-prop plan exceeds the profile-size safety bound")
    try:
        payload = json.loads(source.read_text(encoding="utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ValueError(f"invalid hierarchical-prop plan: {exc}") from exc
    if not isinstance(payload, dict):
        raise ValueError("hierarchical-prop plan root must be an object")
    if payload.get("schema") != HIERARCHICAL_PLAN_SCHEMA:
        raise ValueError("unsupported hierarchical-prop plan schema")
    if payload.get("schemaVersion") != HIERARCHICAL_PLAN_SCHEMA_VERSION:
        raise ValueError("unsupported hierarchical-prop plan schema version")
    declared = _text(
        payload.get("aggregateSha256"), "hierarchical-prop aggregateSha256"
    )
    digest_payload = dict(payload)
    digest_payload.pop("aggregateSha256", None)
    actual = _canonical_json_sha256(digest_payload)
    if declared != actual:
        raise ValueError(
            "hierarchical-prop plan digest mismatch: "
            f"declared {declared}, calculated {actual}"
        )
    return payload, source


def _load_animated_plan(path: Path | str) -> tuple[dict[str, Any], Path]:
    source = Path(path).expanduser().resolve()
    if source.stat().st_size > MAX_PROFILE_BYTES:
        raise ValueError("animated-prop plan exceeds the profile-size safety bound")
    try:
        payload = json.loads(source.read_text(encoding="utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ValueError(f"invalid animated-prop plan: {exc}") from exc
    if not isinstance(payload, dict):
        raise ValueError("animated-prop plan root must be an object")
    if payload.get("schema") != ANIMATED_PLAN_SCHEMA:
        raise ValueError("unsupported animated-prop plan schema")
    if payload.get("schemaVersion") != ANIMATED_PLAN_SCHEMA_VERSION:
        raise ValueError("unsupported animated-prop plan schema version")
    declared = _text(payload.get("aggregateSha256"), "animated-prop aggregateSha256")
    digest_payload = dict(payload)
    digest_payload.pop("aggregateSha256", None)
    actual = _canonical_json_sha256(digest_payload)
    if declared != actual:
        raise ValueError(
            f"animated-prop plan digest mismatch: declared {declared}, calculated {actual}"
        )
    return payload, source


def _patterns(resource: Mapping[str, Any], label: str) -> tuple[str, ...]:
    raw = _array(resource.get("patterns"), f"{label} patterns")
    return tuple(_text(value, f"{label} pattern") for value in raw)


def _declared_source_variants(
    resources: Sequence[Mapping[str, Any]], label: str
) -> dict[str, str]:
    by_id = _resource_map(resources, label)
    variants: dict[str, str] = {}
    for position, resource in enumerate(resources):
        resource_id = _text(resource.get("id"), f"{label} resource {position} id")
        options = resource.get("options", {})
        if not isinstance(options, dict):
            raise ValueError(f"{label} resource {resource_id!r} options are malformed")
        owner_id = options.get(W3D_SOURCE_VARIANT_OF_OPTION)
        if owner_id is None:
            continue
        owner_id = _text(
            owner_id,
            f"{label} resource {resource_id!r} {W3D_SOURCE_VARIANT_OF_OPTION}",
        )
        owner = by_id.get(owner_id.casefold())
        if owner is None or owner.get("id") != owner_id:
            raise ValueError(
                f"{label} source variant {resource_id!r} has no exact owner {owner_id!r}"
            )
        owner_options = _mapping(
            owner.get("options", {}), f"{label} source owner {owner_id!r} options"
        )
        if (
            resource.get("converter") != owner.get("converter")
            or resource.get("kind") != owner.get("kind")
            or _patterns(resource, f"{label} variant {resource_id!r}")
            != _patterns(owner, f"{label} owner {owner_id!r}")
            or options.get("model") != owner_options.get("model")
            or "textureOverrides" not in options
        ):
            raise ValueError(
                f"{label} source variant {resource_id!r} does not preserve the "
                f"exact transformed-source contract of {owner_id!r}"
            )
        variants[resource_id.casefold()] = owner_id
    return variants


def _validate_named_prunes(
    base_resources: Sequence[Mapping[str, Any]],
) -> dict[str, Mapping[str, Any]]:
    by_id = _resource_map(base_resources, "base")
    for expected_id, expected_patterns in FULLY_PRUNED_BASE_RESOURCES.items():
        resource = by_id.get(expected_id.casefold())
        if resource is None or resource.get("id") != expected_id:
            raise ValueError(
                f"required exact pruned resource is missing: {expected_id!r}"
            )
        actual_patterns = _patterns(resource, f"base resource {expected_id!r}")
        if actual_patterns != expected_patterns:
            raise ValueError(
                f"base resource {expected_id!r} source contract changed; "
                f"expected {expected_patterns!r}, found {actual_patterns!r}"
            )
    partial = by_id.get(PARTIAL_RESOURCE_ID.casefold())
    if partial is None or partial.get("id") != PARTIAL_RESOURCE_ID:
        raise ValueError(
            f"required partial resource is missing: {PARTIAL_RESOURCE_ID!r}"
        )
    if (
        _patterns(partial, f"base resource {PARTIAL_RESOURCE_ID!r}")
        != PARTIAL_ORIGINAL_PATTERNS
    ):
        raise ValueError(
            f"base resource {PARTIAL_RESOURCE_ID!r} source contract changed"
        )
    return by_id


def _all_pattern_owners(
    resources: Sequence[Mapping[str, Any]], label: str
) -> dict[str, tuple[str, str]]:
    variants = _declared_source_variants(resources, label)
    owners: dict[str, tuple[str, str]] = {}
    for position, resource in enumerate(resources):
        resource_id = _text(resource.get("id"), f"{label} resource {position} id")
        for pattern in _patterns(resource, f"{label} resource {resource_id!r}"):
            key = pattern.casefold()
            previous = owners.get(key)
            if previous is not None:
                if (
                    variants.get(resource_id.casefold(), "").casefold()
                    == previous[0].casefold()
                ):
                    continue
                raise ValueError(
                    f"case-insensitive source pattern collision: "
                    f"{previous[0]!r}:{previous[1]!r}, {resource_id!r}:{pattern!r}"
                )
            owners[key] = (resource_id, pattern)
    return owners


def _validate_coverage(
    *,
    faction_resources: Sequence[Mapping[str, Any]],
    static_resources: Sequence[Mapping[str, Any]],
) -> None:
    faction_patterns = _all_pattern_owners(faction_resources, "faction")
    static_patterns = _all_pattern_owners(static_resources, "static")
    for resource_id in sorted(SEMANTIC_PRUNE_IDS | UI_PRUNE_IDS):
        for path in FULLY_PRUNED_BASE_RESOURCES[resource_id]:
            if path.casefold() not in faction_patterns:
                raise ValueError(
                    f"faction profile does not own pruned source {resource_id!r}:{path!r}"
                )
    for path in PARTIAL_REMOVED_PATTERNS:
        if path.casefold() not in faction_patterns:
            raise ValueError(
                f"faction profile does not own narrowed semantic source {path!r}"
            )
    for resource_id in sorted(STATIC_REPLACEMENT_PRUNE_IDS):
        for path in FULLY_PRUNED_BASE_RESOURCES[resource_id]:
            if path.casefold() not in static_patterns:
                raise ValueError(
                    f"static plan does not own replaced source {resource_id!r}:{path!r}"
                )


def _validate_road_profile(
    base: Mapping[str, Any], road: Mapping[str, Any]
) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    if road.get("id") != ROAD_PROFILE_ID:
        raise ValueError(f"unexpected Road profile id: {road.get('id')!r}")
    base_resources = [
        _mapping(value, f"base resource {index}")
        for index, value in enumerate(_array(base.get("resources"), "base resources"))
    ]
    road_resources = [
        _mapping(value, f"Road resource {index}")
        for index, value in enumerate(_array(road.get("resources"), "Road resources"))
    ]
    if len(base_resources) != EXPECTED_BASE_RESOURCE_COUNT:
        raise ValueError(
            f"base resource count changed: expected {EXPECTED_BASE_RESOURCE_COUNT}, "
            f"found {len(base_resources)}"
        )
    if len(road_resources) != EXPECTED_ROAD_RESOURCE_COUNT:
        raise ValueError(
            f"Road profile resource count changed: expected {EXPECTED_ROAD_RESOURCE_COUNT}, "
            f"found {len(road_resources)}"
        )
    base_by_id = _resource_map(base_resources, "base")
    road_by_id = _resource_map(road_resources, "Road")
    for key, base_resource in base_by_id.items():
        road_resource = road_by_id.get(key)
        if road_resource is None:
            raise ValueError(
                f"Road profile dropped base resource {base_resource['id']!r}"
            )
        if base_resource.get("id") != road_resource.get("id"):
            raise ValueError("Road profile case-changed a base resource id")
        if base_resource.get("id") == MAP_RESOURCE_ID:
            expected_map = deepcopy(dict(base_resource))
            metadata = _mapping(
                _mapping(
                    _mapping(expected_map.get("options"), "base map options").get(
                        "metadata"
                    ),
                    "base map metadata",
                ),
                "base map metadata",
            )
            if any(key.casefold() == "roadmaterials" for key in metadata):
                raise ValueError("base map already declares roadMaterials")
            # ``metadata`` is a dict in the deep copy; Mapping typing is only
            # used by the validators above.
            expected_map["options"]["metadata"]["roadMaterials"] = (
                ROAD_MATERIALS_RELATIVE_PATH
            )
            if road_resource != expected_map:
                raise ValueError(
                    "Road profile changed the Fords map resource beyond exact roadMaterials metadata"
                )
        elif road_resource != base_resource:
            raise ValueError(
                f"Road profile changed unrelated base resource {base_resource['id']!r}"
            )

    added = [
        deepcopy(dict(resource))
        for resource in road_resources
        if str(resource.get("id", "")).casefold() not in base_by_id
    ]
    if {str(resource["id"]) for resource in added} != set(EXPECTED_ROAD_RESOURCES):
        raise ValueError("Road profile does not contain the exact five Road resources")
    for resource in added:
        resource_id = str(resource["id"])
        expected_pattern, expected_output = EXPECTED_ROAD_RESOURCES[resource_id]
        if (
            resource.get("kind") != "texture"
            or resource.get("converter") != "texture"
            or _patterns(resource, f"Road resource {resource_id!r}")
            != (expected_pattern,)
            or resource.get("output") != expected_output
            or resource.get("limit") != 1
            or resource.get("expected_count") != 1
        ):
            raise ValueError(f"Road resource contract changed: {resource_id!r}")

    base_runtime = _mapping(base.get("runtime_data"), "base runtime_data")
    road_runtime = _mapping(road.get("runtime_data"), "Road runtime_data")
    base_runtime_keys = _case_unique(
        (_safe_path(key, "base runtime path") for key in base_runtime),
        "base runtime path",
    )
    road_runtime_keys = _case_unique(
        (_safe_path(key, "Road runtime path") for key in road_runtime),
        "Road runtime path",
    )
    if set(road_runtime_keys) != set(base_runtime_keys) | {
        ROAD_RUNTIME_PATH.casefold()
    }:
        raise ValueError(
            "Road profile runtime paths are not base plus exact road materials"
        )
    for key, value in base_runtime.items():
        if road_runtime.get(key) != value:
            raise ValueError(f"Road profile changed unrelated runtime document {key!r}")
    road_document = deepcopy(
        dict(
            _mapping(
                road_runtime.get(ROAD_RUNTIME_PATH), "Road materials runtime document"
            )
        )
    )
    if road_document.get("schema") != "openbfme.sage-road-materials":
        raise ValueError("Road materials runtime schema is unsupported")
    if road_document.get("roadCount") != len(EXPECTED_ROADS):
        raise ValueError("Road materials runtime count is not five")
    roads = _array(road_document.get("roads"), "Road materials roads")
    if (
        tuple(str(item.get("id")) for item in roads if isinstance(item, Mapping))
        != EXPECTED_ROADS
    ):
        raise ValueError("Road materials runtime ids/order changed")
    output_by_source = {
        source.casefold(): output for source, output in EXPECTED_ROAD_RESOURCES.values()
    }
    for position, raw in enumerate(roads):
        item = _mapping(raw, f"Road materials road {position}")
        source = _safe_path(item.get("sourceVirtualPath"), "Road sourceVirtualPath")
        texture_png = _safe_path(item.get("texturePng"), "Road texturePng")
        if output_by_source.get(source.casefold()) != texture_png:
            raise ValueError(
                f"Road runtime texture output is inconsistent for {item.get('id')!r}"
            )
    return added, road_document


def _validate_static_plan(
    plan: Mapping[str, Any],
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    summary = _mapping(plan.get("summary"), "static-prop summary")
    expected_summary = {
        "profileResourceCount": EXPECTED_STATIC_RESOURCE_COUNT,
        "objectBindingModelRowCount": EXPECTED_STATIC_BINDING_COUNT,
        "eligibleTargetTypeCount": EXPECTED_STATIC_BINDING_COUNT,
    }
    for key, expected in expected_summary.items():
        if summary.get(key) != expected:
            raise ValueError(
                f"static-prop summary {key} changed: expected {expected}, found {summary.get(key)!r}"
            )
    policy = _mapping(plan.get("policy"), "static-prop policy")
    if policy.get("substitutesAllowed") is not False:
        raise ValueError("static-prop plan permits substitutes")
    if policy.get("placementDataConsumed") is not False:
        raise ValueError("static-prop plan unexpectedly consumed placement data")
    fragment = _mapping(plan.get("profileFragment"), "static-prop profileFragment")
    if set(fragment) != {"resources", "objectBindings"}:
        raise ValueError("static-prop profileFragment contains unsupported fields")
    resources = [
        deepcopy(dict(_mapping(value, f"static resource {index}")))
        for index, value in enumerate(
            _array(fragment.get("resources"), "static-prop resources")
        )
    ]
    if len(resources) != EXPECTED_STATIC_RESOURCE_COUNT:
        raise ValueError(
            "static-prop profileFragment does not contain "
            f"{EXPECTED_STATIC_RESOURCE_COUNT} resources"
        )
    bindings_object = _mapping(
        fragment.get("objectBindings"), "static-prop objectBindings"
    )
    if set(bindings_object) != {"models"}:
        raise ValueError("static-prop objectBindings contains unsupported fields")
    bindings = [
        deepcopy(dict(_mapping(value, f"static binding {index}")))
        for index, value in enumerate(
            _array(bindings_object.get("models"), "static-prop model bindings")
        )
    ]
    if len(bindings) != EXPECTED_STATIC_BINDING_COUNT:
        raise ValueError("static-prop plan does not contain all 38 bindings")
    _case_unique(
        (_text(item.get("typeName"), "static binding typeName") for item in bindings),
        "binding typeName",
    )
    static_resource_by_id = _resource_map(resources, "static")
    static_models: dict[str, tuple[str, str]] = {}
    for resource in static_resource_by_id.values():
        if resource.get("converter") != "w3d-static":
            continue
        patterns = _patterns(resource, f"static model resource {resource.get('id')!r}")
        if len(patterns) != 1:
            raise ValueError("static model resource must have one exact W3D source")
        source = _safe_path(patterns[0], "static model source")
        output = _safe_path(resource.get("output"), "static model output")
        static_models[source.casefold()] = (source, output)
    for binding in bindings:
        if set(binding) != {"glb", "matchMethod", "sourceVirtualModel", "typeName"}:
            raise ValueError("static model binding contains unsupported fields")
        if binding.get("matchMethod") != "exact-type-name":
            raise ValueError("static model binding is not exact-type-name")
        source = _safe_path(binding.get("sourceVirtualModel"), "binding source model")
        output = _safe_path(binding.get("glb"), "binding GLB")
        expected = static_models.get(source.casefold())
        if expected is None or expected != (source, output):
            raise ValueError(
                f"static binding is not backed by its exact W3D resource: {binding.get('typeName')!r}"
            )
    if sum(1 for item in bindings if item.get("typeName") == "PTGrass15") != 1:
        raise ValueError("static plan does not replace the exact PTGrass15 binding")
    return resources, bindings


def _validate_hierarchical_plan(
    plan: Mapping[str, Any],
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    summary = _mapping(plan.get("summary"), "hierarchical-prop summary")
    expected_summary = {
        "profileResourceCount": EXPECTED_HIERARCHICAL_RESOURCE_COUNT,
        "objectBindingModelRowCount": EXPECTED_HIERARCHICAL_BINDING_COUNT,
        "eligibleTargetTypeCount": EXPECTED_HIERARCHICAL_BINDING_COUNT,
        "uniqueModelSourceCount": EXPECTED_HIERARCHICAL_BINDING_COUNT,
        "uniqueTextureSourceCount": 10,
        "cumulativePlannedTargetTypeCount": EXPECTED_PRE_ANIMATED_BINDING_COUNT,
    }
    for key, expected in expected_summary.items():
        if summary.get(key) != expected:
            raise ValueError(
                f"hierarchical-prop summary {key} changed: expected {expected}, "
                f"found {summary.get(key)!r}"
            )
    policy = _mapping(plan.get("policy"), "hierarchical-prop policy")
    if policy.get("substitutesAllowed") is not False:
        raise ValueError("hierarchical-prop plan permits substitutes")
    if policy.get("profileFragmentValidatedByImportProfile") is not True:
        raise ValueError("hierarchical-prop fragment lacks ImportProfile validation")
    fragment = _mapping(plan.get("profileFragment"), "hierarchical profileFragment")
    if set(fragment) != {"resources", "objectBindings"}:
        raise ValueError("hierarchical profileFragment contains unsupported fields")
    resources = [
        deepcopy(dict(_mapping(value, f"hierarchical resource {index}")))
        for index, value in enumerate(
            _array(fragment.get("resources"), "hierarchical resources")
        )
    ]
    if len(resources) != EXPECTED_HIERARCHICAL_RESOURCE_COUNT:
        raise ValueError(
            "hierarchical profileFragment does not contain "
            f"{EXPECTED_HIERARCHICAL_RESOURCE_COUNT} resources"
        )
    _resource_map(resources, "hierarchical")
    texture_count = 0
    model_sources: dict[str, tuple[str, str]] = {}
    for resource in resources:
        resource_id = _text(resource.get("id"), "hierarchical resource id")
        patterns = _patterns(resource, f"hierarchical resource {resource_id!r}")
        if len(patterns) != 1 or any(character in patterns[0] for character in "*?["):
            raise ValueError(
                f"hierarchical resource is not one exact source: {resource_id!r}"
            )
        source = _safe_path(patterns[0], "hierarchical source")
        output = _safe_path(resource.get("output"), "hierarchical output")
        if resource.get("converter") == "texture":
            texture_count += 1
        elif resource.get("converter") == "w3d-hierarchical":
            model_sources[source.casefold()] = (source, output)
        else:
            raise ValueError(
                f"hierarchical resource uses unsupported converter: {resource_id!r}"
            )
    if texture_count != 10 or len(model_sources) != EXPECTED_HIERARCHICAL_BINDING_COUNT:
        raise ValueError("hierarchical resource kind counts changed")

    bindings_object = _mapping(
        fragment.get("objectBindings"), "hierarchical objectBindings"
    )
    if set(bindings_object) != {"models"}:
        raise ValueError("hierarchical objectBindings contains unsupported fields")
    bindings = [
        deepcopy(dict(_mapping(value, f"hierarchical binding {index}")))
        for index, value in enumerate(
            _array(bindings_object.get("models"), "hierarchical model bindings")
        )
    ]
    if len(bindings) != EXPECTED_HIERARCHICAL_BINDING_COUNT:
        raise ValueError(
            "hierarchical plan does not contain all "
            f"{EXPECTED_HIERARCHICAL_BINDING_COUNT} bindings"
        )
    _case_unique(
        (
            _text(item.get("typeName"), "hierarchical binding typeName")
            for item in bindings
        ),
        "hierarchical binding typeName",
    )
    for binding in bindings:
        if set(binding) != {"glb", "matchMethod", "sourceVirtualModel", "typeName"}:
            raise ValueError("hierarchical model binding contains unsupported fields")
        if binding.get("matchMethod") != "exact-type-name":
            raise ValueError("hierarchical model binding is not exact-type-name")
        source = _safe_path(
            binding.get("sourceVirtualModel"), "hierarchical binding source model"
        )
        output = _safe_path(binding.get("glb"), "hierarchical binding GLB")
        if model_sources.get(source.casefold()) != (source, output):
            raise ValueError(
                "hierarchical binding is not backed by its exact W3D resource: "
                f"{binding.get('typeName')!r}"
            )
    return resources, bindings


def _validate_animated_plan(
    plan: Mapping[str, Any],
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    summary = _mapping(plan.get("summary"), "animated-prop summary")
    expected_summary = {
        "profileResourceCount": EXPECTED_ANIMATED_RESOURCE_COUNT,
        "objectBindingModelRowCount": EXPECTED_ANIMATED_BINDING_COUNT,
        "eligibleTargetTypeCount": EXPECTED_ANIMATED_BINDING_COUNT,
        "uniqueModelSourceCount": EXPECTED_ANIMATED_BINDING_COUNT,
        "uniqueTextureSourceCount": 11,
        "conversionGroupCount": EXPECTED_ANIMATED_BINDING_COUNT,
        "animatedBatchPlacementCount": 26,
    }
    for key, expected in expected_summary.items():
        if summary.get(key) != expected:
            raise ValueError(
                f"animated-prop summary {key} changed: expected {expected}, "
                f"found {summary.get(key)!r}"
            )
    policy = _mapping(plan.get("policy"), "animated-prop policy")
    if policy.get("substitutesAllowed") is not False:
        raise ValueError("animated-prop plan permits substitutes")
    if policy.get("profileFragmentValidatedByImportProfile") is not True:
        raise ValueError("animated-prop fragment lacks ImportProfile validation")
    if policy.get("converter") != "w3d-bundle":
        raise ValueError("animated-prop plan changed converter")
    if policy.get("lifecycleOrMultipleModelTargetsAllowed") is not False:
        raise ValueError("animated-prop plan permits unsupported lifecycle models")

    fragment = _mapping(plan.get("profileFragment"), "animated profileFragment")
    if set(fragment) != {"resources", "objectBindings"}:
        raise ValueError("animated profileFragment contains unsupported fields")
    resources = [
        deepcopy(dict(_mapping(value, f"animated resource {index}")))
        for index, value in enumerate(
            _array(fragment.get("resources"), "animated resources")
        )
    ]
    if len(resources) != EXPECTED_ANIMATED_RESOURCE_COUNT:
        raise ValueError(
            "animated profileFragment does not contain the exact resource closure"
        )
    _resource_map(resources, "animated")
    texture_count = 0
    shared_w3d_count = 0
    shared_w3d_consumers: set[str] = set()
    model_sources: dict[str, tuple[str, str]] = {}
    for resource in resources:
        resource_id = _text(resource.get("id"), "animated resource id")
        patterns = _patterns(resource, f"animated resource {resource_id!r}")
        if any(
            any(character in pattern for character in "*?[") for pattern in patterns
        ):
            raise ValueError(
                f"animated resource is not an exact closure: {resource_id!r}"
            )
        for pattern in patterns:
            _safe_path(pattern, f"animated resource {resource_id!r} source")
        if resource.get("expected_count") != len(patterns) or resource.get(
            "limit"
        ) != len(patterns):
            raise ValueError(
                f"animated resource count contract changed: {resource_id!r}"
            )
        if resource.get("converter") == "hash-only":
            if resource_id == EXPECTED_ANIMATED_SHARED_W3D_RESOURCE_ID:
                if (
                    resource.get("kind") != "data"
                    or tuple(patterns) != EXPECTED_ANIMATED_SHARED_W3D_PATTERNS
                ):
                    raise ValueError("animated shared W3D resource contract changed")
                shared_w3d_count += 1
            else:
                if len(patterns) != 1 or resource.get("kind") != "texture":
                    raise ValueError("animated texture resource contract changed")
                texture_count += 1
            continue
        if resource.get("converter") != "w3d-bundle":
            raise ValueError(
                f"animated resource uses unsupported converter: {resource_id!r}"
            )
        output = _safe_path(resource.get("output"), "animated model output")
        options = _mapping(resource.get("options"), "animated model options")
        model_name = _text(options.get("model"), "animated model identifier")
        model_matches = [
            pattern
            for pattern in patterns
            if PurePosixPath(pattern).name.casefold() == model_name.casefold()
        ]
        if len(model_matches) != 1:
            raise ValueError(
                f"animated model closure has no unique exact model source: {resource_id!r}"
            )
        animations = _array(options.get("animations"), "animated model animations")
        if not animations:
            raise ValueError(
                f"animated model has no authored animation: {resource_id!r}"
            )
        if options.get("required_equipment") != []:
            raise ValueError("animated prop unexpectedly requires character equipment")
        model_source = model_matches[0]
        dependencies = _array(
            options.get("inputResourceIds"), "animated model inputResourceIds"
        )
        if EXPECTED_ANIMATED_SHARED_W3D_RESOURCE_ID in dependencies:
            shared_w3d_consumers.add(PurePosixPath(model_source).name.casefold())
        model_sources[model_source.casefold()] = (model_source, output)
    if (
        texture_count != 11
        or shared_w3d_count != 1
        or shared_w3d_consumers != EXPECTED_ANIMATED_SHARED_W3D_CONSUMERS
        or len(model_sources) != EXPECTED_ANIMATED_BINDING_COUNT
    ):
        raise ValueError("animated resource kind counts changed")

    bindings_object = _mapping(
        fragment.get("objectBindings"), "animated objectBindings"
    )
    if set(bindings_object) != {"models"}:
        raise ValueError("animated objectBindings contains unsupported fields")
    bindings = [
        deepcopy(dict(_mapping(value, f"animated binding {index}")))
        for index, value in enumerate(
            _array(bindings_object.get("models"), "animated model bindings")
        )
    ]
    if len(bindings) != EXPECTED_ANIMATED_BINDING_COUNT:
        raise ValueError("animated plan does not contain every exact binding")
    binding_types = tuple(
        _text(item.get("typeName"), "animated binding typeName") for item in bindings
    )
    if binding_types != EXPECTED_ANIMATED_BINDING_TYPES:
        raise ValueError(
            f"animated binding types changed: expected {EXPECTED_ANIMATED_BINDING_TYPES!r}, "
            f"found {binding_types!r}"
        )
    _case_unique(binding_types, "animated binding typeName")
    for binding in bindings:
        if set(binding) != {"glb", "matchMethod", "sourceVirtualModel", "typeName"}:
            raise ValueError("animated model binding contains unsupported fields")
        if binding.get("matchMethod") != "exact-type-name":
            raise ValueError("animated model binding is not exact-type-name")
        source = _safe_path(
            binding.get("sourceVirtualModel"), "animated binding source model"
        )
        output = _safe_path(binding.get("glb"), "animated binding GLB")
        if model_sources.get(source.casefold()) != (source, output):
            raise ValueError(
                "animated binding is not backed by its exact W3D bundle: "
                f"{binding.get('typeName')!r}"
            )
    return resources, bindings


def _dedupe_hierarchical_resources(
    *,
    base_payload: Mapping[str, Any],
    existing_resources: Sequence[Mapping[str, Any]],
    hierarchical_resources: Sequence[Mapping[str, Any]],
    catalog: InstallCatalog,
) -> tuple[list[dict[str, Any]], list[dict[str, str]]]:
    probe = deepcopy(dict(base_payload))
    probe["id"] = FULL_PROFILE_ID
    probe["resources"] = deepcopy(list(existing_resources))
    existing_profile = _profile_from_payload(probe)
    resolved = resolve_profile(existing_profile, catalog)
    source_variants = _declared_source_variants(existing_resources, "pre-hierarchy")
    entry_owners: dict[tuple[str, str], str] = {}
    for resource in resolved.resources:
        if resource.missing_patterns or resource.count_error:
            raise ValueError(
                f"pre-hierarchy resource does not resolve exactly: {resource.rule.id!r}"
            )
        for entry in resource.entries:
            key = (entry.archive.casefold(), entry.name.casefold())
            previous = entry_owners.get(key)
            if previous is not None:
                if source_variants.get(resource.rule.id.casefold(), "").casefold() == (
                    previous.casefold()
                ):
                    continue
                raise ValueError(
                    f"pre-hierarchy CatalogEntry collision: {previous!r}, "
                    f"{resource.rule.id!r} own {entry.archive}:{entry.name}"
                )
            entry_owners[key] = resource.rule.id

    existing_by_id = _resource_map(existing_resources, "pre-hierarchy")
    aliases: dict[str, str] = {}
    reused: list[dict[str, str]] = []
    retained: list[dict[str, Any]] = []
    for raw in hierarchical_resources:
        resource = deepcopy(dict(raw))
        resource_id = str(resource["id"])
        source = _patterns(resource, f"hierarchical resource {resource_id!r}")[0]
        entry = catalog.resolve_exact(source)
        if entry is None:
            raise ValueError(f"hierarchical source is absent from catalog: {source!r}")
        owner = entry_owners.get((entry.archive.casefold(), entry.name.casefold()))
        if owner is None:
            retained.append(resource)
            continue
        if resource.get("converter") != "texture":
            raise ValueError(
                f"hierarchical non-texture source already has an owner: {source!r} -> {owner!r}"
            )
        existing_owner = existing_by_id.get(owner.casefold())
        if existing_owner is None or existing_owner.get("kind") != "texture":
            raise ValueError(
                f"hierarchical shared source owner is not a texture resource: {owner!r}"
            )
        aliases[resource_id.casefold()] = owner
        reused.append(
            {
                "sourceVirtualPath": source,
                "discardedResourceId": resource_id,
                "reusedResourceId": owner,
            }
        )

    actual_reuse = {
        item["sourceVirtualPath"]: item["reusedResourceId"] for item in reused
    }
    if actual_reuse != EXPECTED_HIERARCHICAL_REUSE_OWNERS:
        raise ValueError(
            "hierarchical shared-source closure changed: "
            f"expected {EXPECTED_HIERARCHICAL_REUSE_OWNERS!r}, found {actual_reuse!r}"
        )
    if len(reused) != EXPECTED_HIERARCHICAL_REUSE_COUNT:
        raise ValueError("hierarchical shared-source reuse count is not three")

    for resource in retained:
        if resource.get("converter") != "w3d-hierarchical":
            continue
        options = resource.get("options")
        if not isinstance(options, dict):
            raise ValueError("hierarchical model options must be an object")
        raw_dependencies = _array(
            options.get("inputResourceIds"), "hierarchical inputResourceIds"
        )
        rewritten: list[str] = []
        seen: set[str] = set()
        for raw_dependency in raw_dependencies:
            dependency = _text(raw_dependency, "hierarchical input resource id")
            replacement = aliases.get(dependency.casefold(), dependency)
            key = replacement.casefold()
            if key not in seen:
                rewritten.append(replacement)
                seen.add(key)
        options["inputResourceIds"] = rewritten

    if len(retained) != EXPECTED_HIERARCHICAL_ADDED_RESOURCE_COUNT:
        raise ValueError(
            "hierarchical deduplicated resource count changed: expected "
            f"{EXPECTED_HIERARCHICAL_ADDED_RESOURCE_COUNT}, found {len(retained)}"
        )
    return retained, reused


def _dedupe_animated_resources(
    *,
    base_payload: Mapping[str, Any],
    existing_resources: Sequence[Mapping[str, Any]],
    animated_resources: Sequence[Mapping[str, Any]],
    catalog: InstallCatalog,
) -> tuple[list[dict[str, Any]], list[dict[str, str]]]:
    probe = deepcopy(dict(base_payload))
    probe["id"] = FULL_PROFILE_ID
    probe["resources"] = deepcopy(list(existing_resources))
    existing_profile = _profile_from_payload(probe)
    resolved = resolve_profile(existing_profile, catalog)
    source_variants = _declared_source_variants(existing_resources, "pre-animated")
    entry_owners: dict[tuple[str, str], str] = {}
    for resource in resolved.resources:
        if resource.missing_patterns or resource.count_error:
            raise ValueError(
                f"pre-animated resource does not resolve exactly: {resource.rule.id!r}"
            )
        for entry in resource.entries:
            key = (entry.archive.casefold(), entry.name.casefold())
            previous = entry_owners.get(key)
            if previous is not None:
                if source_variants.get(resource.rule.id.casefold(), "").casefold() == (
                    previous.casefold()
                ):
                    continue
                raise ValueError(
                    f"pre-animated CatalogEntry collision: {previous!r}, "
                    f"{resource.rule.id!r} own {entry.archive}:{entry.name}"
                )
            entry_owners[key] = resource.rule.id

    existing_by_id = _resource_map(existing_resources, "pre-animated")
    aliases: dict[str, str] = {}
    reused: list[dict[str, str]] = []
    retained: list[dict[str, Any]] = []
    for raw in animated_resources:
        resource = deepcopy(dict(raw))
        resource_id = str(resource["id"])
        patterns = _patterns(resource, f"animated resource {resource_id!r}")
        owners: dict[str, str] = {}
        for source in patterns:
            entry = catalog.resolve_exact(source)
            if entry is None:
                raise ValueError(f"animated source is absent from catalog: {source!r}")
            owner = entry_owners.get((entry.archive.casefold(), entry.name.casefold()))
            if owner is not None:
                owners[source] = owner
        if resource.get("converter") != "hash-only":
            if owners:
                raise ValueError(
                    f"animated W3D closure already has source owners: {owners!r}"
                )
            retained.append(resource)
            continue
        if not owners:
            retained.append(resource)
            continue
        if len(patterns) != 1 or len(owners) != 1:
            raise ValueError("animated texture reuse is not one exact source")
        source = patterns[0]
        owner = owners[source]
        existing_owner = existing_by_id.get(owner.casefold())
        if existing_owner is None or existing_owner.get("kind") != "texture":
            raise ValueError(
                f"animated shared source owner is not a texture resource: {owner!r}"
            )
        aliases[resource_id.casefold()] = owner
        reused.append(
            {
                "sourceVirtualPath": source,
                "discardedResourceId": resource_id,
                "reusedResourceId": owner,
            }
        )

    actual_reuse = {
        item["sourceVirtualPath"]: item["reusedResourceId"] for item in reused
    }
    if actual_reuse != EXPECTED_ANIMATED_REUSE_OWNERS:
        raise ValueError(
            "animated shared-source closure changed: "
            f"expected {EXPECTED_ANIMATED_REUSE_OWNERS!r}, found {actual_reuse!r}"
        )
    if len(reused) != EXPECTED_ANIMATED_REUSE_COUNT:
        raise ValueError("animated shared-source reuse count is not one")

    for resource in retained:
        if resource.get("converter") != "w3d-bundle":
            continue
        options = resource.get("options")
        if not isinstance(options, dict):
            raise ValueError("animated model options must be an object")
        raw_dependencies = _array(
            options.get("inputResourceIds"), "animated inputResourceIds"
        )
        rewritten: list[str] = []
        seen: set[str] = set()
        for raw_dependency in raw_dependencies:
            dependency = _text(raw_dependency, "animated input resource id")
            replacement = aliases.get(dependency.casefold(), dependency)
            key = replacement.casefold()
            if key not in seen:
                rewritten.append(replacement)
                seen.add(key)
        options["inputResourceIds"] = rewritten

    if len(retained) != EXPECTED_ANIMATED_ADDED_RESOURCE_COUNT:
        raise ValueError(
            "animated deduplicated resource count changed: expected "
            f"{EXPECTED_ANIMATED_ADDED_RESOURCE_COUNT}, found {len(retained)}"
        )
    return retained, reused


def _validate_faction_profile(
    faction: Mapping[str, Any],
) -> tuple[list[dict[str, Any]], dict[str, dict[str, Any]], dict[str, dict[str, Any]]]:
    if faction.get("id") != FACTION_PROFILE_ID:
        raise ValueError(f"unexpected faction profile id: {faction.get('id')!r}")
    resources = [
        deepcopy(dict(_mapping(value, f"faction resource {index}")))
        for index, value in enumerate(
            _array(faction.get("resources"), "faction resources")
        )
    ]
    if len(resources) != EXPECTED_FACTION_RESOURCE_COUNT:
        raise ValueError(
            f"faction resource count changed: expected {EXPECTED_FACTION_RESOURCE_COUNT}, "
            f"found {len(resources)}"
        )
    runtime = _mapping(faction.get("runtime_data"), "faction runtime_data")
    if set(runtime) != set(FACTION_RUNTIME_PATHS):
        raise ValueError(
            "faction runtime_data does not contain the exact three documents"
        )
    documents = {
        path: deepcopy(dict(_mapping(runtime[path], f"faction runtime {path}")))
        for path in FACTION_RUNTIME_PATHS
    }
    expected_schemas = {
        "data/audio_events.json": "openbfme.audio-events",
        "data/strings.json": "openbfme.localized-strings",
        "data/ui_manifest.json": "openbfme.ui-manifest",
    }
    complete_flags: dict[str, dict[str, Any]] = {}
    for path, document in documents.items():
        if document.get("schema") != expected_schemas[path]:
            raise ValueError(f"faction runtime schema changed for {path!r}")
        # ``False`` is intentional here.  These leaf documents are exact and
        # usable for this scoped roster, but the producer conservatively does
        # not claim full-faction asset or oracle parity.
        if document.get("complete") is not False:
            raise ValueError(
                f"faction runtime completeness provenance changed for {path!r}; "
                "the composer must not relabel it"
            )
        complete_flags[path] = {"present": True, "value": False}

    pack = _mapping(faction.get("pack"), "faction pack")
    pack_files = _mapping(pack.get("files"), "faction pack.files")
    expected_pack_files = {
        "audioEvents": "data/audio_events.json",
        "strings": "data/strings.json",
        "uiManifest": "data/ui_manifest.json",
    }
    if pack_files != expected_pack_files:
        raise ValueError("faction pack.files contract changed")
    return resources, documents, complete_flags


def _ui_manifest_rows(
    faction_resources: Sequence[Mapping[str, Any]], manifest: Mapping[str, Any]
) -> dict[str, Mapping[str, Any]]:
    images = _array(manifest.get("images"), "UI manifest images")
    rows: dict[str, Mapping[str, Any]] = {}
    folded: dict[str, str] = {}
    for position, raw in enumerate(images):
        row = _mapping(raw, f"UI manifest image {position}")
        identifier = _text(row.get("id"), f"UI manifest image {position} id")
        key = identifier.casefold()
        if key in rows:
            raise ValueError(
                f"case-insensitive UI manifest id collision: {folded[key]!r}, {identifier!r}"
            )
        rows[key] = row
        folded[key] = identifier

    pattern_owners = _all_pattern_owners(faction_resources, "faction")
    selected: dict[str, Mapping[str, Any]] = {}
    for identifier, expected_source in EXPECTED_UI_SOURCE_PATHS.items():
        row = rows.get(identifier.casefold())
        if row is None or row.get("id") != identifier:
            raise ValueError(f"exact UI manifest row is missing: {identifier!r}")
        path = _safe_path(row.get("path"), f"UI manifest path for {identifier!r}")
        source_atlas = _mapping(
            row.get("sourceAtlas"), f"UI manifest sourceAtlas for {identifier!r}"
        )
        source = _safe_path(
            source_atlas.get("compiledVirtualPath"),
            f"UI manifest compiled source for {identifier!r}",
        )
        if source != expected_source:
            raise ValueError(f"UI manifest source changed for {identifier!r}")
        owner = pattern_owners.get(source.casefold())
        if owner is None:
            raise ValueError(f"UI manifest source has no faction resource: {source!r}")
        resource = next(
            item for item in faction_resources if item.get("id") == owner[0]
        )
        output_root = _safe_path(
            resource.get("output"), f"UI resource output for {identifier!r}"
        )
        if resource.get("converter") != "texture-atlas-crops" or not path.startswith(
            output_root + "/"
        ):
            raise ValueError(
                f"UI manifest path is not produced by its exact atlas: {identifier!r}"
            )
        crop_outputs = {
            _safe_path(
                f"{output_root}/{_text(crop.get('output'), 'UI crop output')}",
                "UI crop output path",
            )
            for crop in _array(
                _mapping(resource.get("options"), "UI resource options").get("crops"),
                "UI resource crops",
            )
            if isinstance(crop, Mapping)
        }
        if path not in crop_outputs:
            raise ValueError(
                f"UI manifest path is absent from exact crop outputs: {identifier!r}"
            )
        selected[identifier] = row
    return selected


def _replace_member_ui(
    objects_document: dict[str, Any], ui_rows: Mapping[str, Mapping[str, Any]]
) -> list[dict[str, str]]:
    objects = _array(objects_document.get("objects"), "objects runtime objects")
    by_id: dict[str, dict[str, Any]] = {}
    for position, raw in enumerate(objects):
        if not isinstance(raw, dict):
            raise ValueError(f"objects runtime object {position} must be an object")
        identifier = _text(raw.get("id"), f"objects runtime object {position} id")
        if identifier.casefold() in by_id:
            raise ValueError(f"case-insensitive object id collision: {identifier!r}")
        by_id[identifier.casefold()] = raw
    replacements: list[dict[str, str]] = []
    for object_id, (
        portrait_id,
        command_id,
        expected_old_icon,
        expected_old_command,
    ) in OBJECT_UI_BINDINGS.items():
        item = by_id.get(object_id.casefold())
        if item is None or item.get("id") != object_id:
            raise ValueError(f"exact member object is missing: {object_id!r}")
        presentation = item.get("presentation")
        if not isinstance(presentation, dict):
            raise ValueError(f"member object {object_id!r} has no presentation object")
        if presentation.get("icon") != expected_old_icon:
            raise ValueError(f"member object {object_id!r} old icon contract changed")
        if presentation.get("commandIcon") != expected_old_command:
            raise ValueError(
                f"member object {object_id!r} old command icon contract changed"
            )
        icon = _safe_path(ui_rows[portrait_id].get("path"), f"{portrait_id} path")
        command_icon = _safe_path(ui_rows[command_id].get("path"), f"{command_id} path")
        presentation["icon"] = icon
        presentation["commandIcon"] = command_icon
        replacements.append(
            {
                "objectId": object_id,
                "iconMappedImageId": portrait_id,
                "icon": icon,
                "commandMappedImageId": command_id,
                "commandIcon": command_icon,
            }
        )
    return replacements


def _narrow_base_resources(
    base_resources: Sequence[Mapping[str, Any]],
    road_resources: Sequence[Mapping[str, Any]],
    static_bindings: Sequence[Mapping[str, Any]],
) -> list[dict[str, Any]]:
    road_by_id = _resource_map(road_resources, "Road")
    result: list[dict[str, Any]] = []
    for raw in base_resources:
        resource_id = str(raw["id"])
        if resource_id in FULLY_PRUNED_BASE_RESOURCES:
            continue
        resource = deepcopy(dict(raw))
        if resource_id == PARTIAL_RESOURCE_ID:
            resource["patterns"] = list(PARTIAL_RETAINED_PATTERNS)
            resource["limit"] = len(PARTIAL_RETAINED_PATTERNS)
            resource["expected_count"] = len(PARTIAL_RETAINED_PATTERNS)
        if resource_id == MAP_RESOURCE_ID:
            road_map = road_by_id.get(MAP_RESOURCE_ID.casefold())
            if road_map is None:
                raise ValueError("Road profile has no exact Fords map resource")
            resource = deepcopy(dict(road_map))
            options = resource.get("options")
            if not isinstance(options, dict):
                raise ValueError("Fords map options must be an object")
            object_bindings = options.get("objectBindings")
            if not isinstance(object_bindings, dict):
                raise ValueError("Fords map objectBindings must be an object")
            existing_models = _array(
                object_bindings.get("models"), "base Fords object model bindings"
            )
            expected_old = [
                {
                    "typeName": "PTGrass15",
                    "matchMethod": "exact-type-name",
                    "glb": "assets/models/props/ptgrass15.glb",
                    "sourceVirtualModel": "art/w3d/pt/ptgrass15.w3d",
                }
            ]
            if existing_models != expected_old:
                raise ValueError("base Fords model binding contract changed")
            object_bindings["models"] = deepcopy(list(static_bindings))
        result.append(resource)
    expected_retained = EXPECTED_BASE_RESOURCE_COUNT - len(FULLY_PRUNED_BASE_RESOURCES)
    if len(result) != expected_retained:
        raise ValueError(
            f"base pruning count mismatch: expected {expected_retained}, found {len(result)}"
        )
    return result


def _validate_final_names(resources: Sequence[Mapping[str, Any]]) -> None:
    _resource_map(resources, "final")
    _all_pattern_owners(resources, "final")

    declared_outputs: dict[str, tuple[str, str]] = {}
    for resource in resources:
        resource_id = str(resource["id"])
        output = resource.get("output")
        if not isinstance(output, str) or not output:
            continue
        # Templates can intentionally be shared by chunked resources.  Their
        # concrete paths are checked after catalog resolution.
        if "{" in output or "}" in output:
            continue
        path = _safe_path(output, f"resource {resource_id!r} output")
        key = path.casefold()
        previous = declared_outputs.get(key)
        if previous is not None:
            raise ValueError(
                f"case-insensitive declared output collision: "
                f"{previous[0]!r}:{previous[1]!r}, {resource_id!r}:{path!r}"
            )
        declared_outputs[key] = (resource_id, path)


def _render_template(template: str, *, index: int, name: str) -> str:
    filename = PurePosixPath(name).name.casefold()
    stem = PurePosixPath(filename).stem.casefold()
    rendered = template
    for token, value in (
        ("{index}", str(index)),
        ("{stem}", stem),
        ("{name}", filename),
    ):
        rendered = rendered.replace(token, value)
    if "{" in rendered or "}" in rendered:
        raise ValueError(f"unsupported output template expression: {template!r}")
    return _safe_path(rendered, "rendered resource output")


def _concrete_resource_outputs(
    resource: Any,
) -> list[str]:
    rule = resource.rule
    output = rule.output
    if rule.converter == "hash-only":
        return []
    if rule.converter == "texture-atlas-crops":
        if output is None:
            raise ValueError(
                f"texture-atlas-crops resource {rule.id!r} has no output root"
            )
        crops = _array(rule.options.get("crops"), f"resource {rule.id!r} crops")
        return [
            _safe_path(
                f"{output}/{_text(_mapping(crop, 'atlas crop').get('output'), 'atlas crop output')}",
                f"resource {rule.id!r} crop output",
            )
            for crop in crops
        ]
    if rule.converter in _BUNDLE_CONVERTERS:
        if output is None:
            raise ValueError(f"bundle resource {rule.id!r} has no output")
        return [_safe_path(output, f"resource {rule.id!r} output")]
    template = output or "source/{name}"
    return [
        _render_template(template, index=index, name=entry.name)
        for index, entry in enumerate(resource.entries)
    ]


def _validate_resolved_profile(
    profile: ImportProfile,
    catalog: InstallCatalog,
    runtime_paths: Iterable[str],
) -> tuple[int, int]:
    resolved = resolve_profile(profile, catalog)
    problems: list[str] = []
    for resource in resolved.resources:
        if resource.missing_patterns:
            problems.append(
                f"{resource.rule.id}: missing {', '.join(resource.missing_patterns)}"
            )
        if resource.count_error:
            problems.append(f"{resource.rule.id}: {resource.count_error}")
        if resource.rule.required and not resource.entries:
            problems.append(f"{resource.rule.id}: no selected entries")
    if problems:
        raise ValueError("profile does not resolve exactly: " + "; ".join(problems))

    entry_owners: dict[tuple[str, str], str] = {}
    source_variants = {
        resource.rule.id.casefold(): str(
            resource.rule.options.get(W3D_SOURCE_VARIANT_OF_OPTION, "")
        )
        for resource in resolved.resources
        if W3D_SOURCE_VARIANT_OF_OPTION in resource.rule.options
    }
    for resource in resolved.resources:
        for entry in resource.entries:
            key = (entry.archive.casefold(), entry.name.casefold())
            previous = entry_owners.get(key)
            if previous is not None:
                if source_variants.get(resource.rule.id.casefold(), "").casefold() == (
                    previous.casefold()
                ):
                    continue
                raise ValueError(
                    f"resolved CatalogEntry collision: {entry.archive}:{entry.name} "
                    f"selected by {previous!r} and {resource.rule.id!r}"
                )
            entry_owners[key] = resource.rule.id

    runtime_keys = _case_unique(
        (_safe_path(path, "runtime output path") for path in runtime_paths),
        "runtime output path",
    )
    output_owners: dict[str, tuple[str, str]] = {}
    for resource in resolved.resources:
        for output in _concrete_resource_outputs(resource):
            key = output.casefold()
            previous = output_owners.get(key)
            if previous is not None:
                raise ValueError(
                    f"case-insensitive concrete output collision: "
                    f"{previous[0]!r}:{previous[1]!r}, "
                    f"{resource.rule.id!r}:{output!r}"
                )
            if key in runtime_keys:
                raise ValueError(
                    f"resource output collides with runtime document: {output!r}"
                )
            output_owners[key] = (resource.rule.id, output)
    return len(entry_owners), len(output_owners)


def _profile_from_payload(payload: Mapping[str, Any]) -> ImportProfile:
    encoded = _pretty_json_bytes(payload)
    if len(encoded) > MAX_PROFILE_BYTES:
        raise ValueError(
            f"composed profile exceeds {MAX_PROFILE_BYTES} byte ImportProfile limit"
        )
    with tempfile.TemporaryDirectory(prefix="openbfme-profile-compose-") as raw:
        path = Path(raw) / "profile.json"
        path.write_bytes(encoded)
        return ImportProfile.load(path)


def _audio_prune_coverage(
    base_profile: ImportProfile,
    faction_profile: ImportProfile,
    catalog: InstallCatalog,
) -> int:
    base_resolved = {
        item.rule.id: item for item in resolve_profile(base_profile, catalog).resources
    }
    faction_resolved = resolve_profile(faction_profile, catalog)
    faction_entries = {
        (entry.archive.casefold(), entry.name.casefold())
        for resource in faction_resolved.resources
        for entry in resource.entries
    }
    covered = 0
    for resource_id in sorted(AUDIO_PRUNE_IDS):
        item = base_resolved.get(resource_id)
        if item is None:
            raise ValueError(f"pruned audio resource vanished: {resource_id!r}")
        if item.missing_patterns or item.count_error or not item.entries:
            raise ValueError(
                f"pruned audio resource no longer resolves exactly: {resource_id!r}"
            )
        missing = [
            entry
            for entry in item.entries
            if (entry.archive.casefold(), entry.name.casefold()) not in faction_entries
        ]
        if missing:
            raise ValueError(
                f"full faction audio does not cover pruned resource {resource_id!r}: "
                + ", ".join(entry.name for entry in missing)
            )
        covered += len(item.entries)
    return covered


def compose_retail_slice_profile(
    base_profile_path: Path | str,
    road_profile_path: Path | str,
    faction_profile_path: Path | str,
    static_plan_path: Path | str,
    hierarchical_plan_path: Path | str,
    animated_plan_path: Path | str,
    catalog: InstallCatalog,
    *,
    profile_id: str = FULL_PROFILE_ID,
) -> ComposedRetailSliceProfile:
    """Compose and fully validate the exact Men/Fords private profile.

    The caller must supply the actual BFME2 catalog.  Composition without
    catalog resolution is intentionally unsupported because wildcard audio
    replacement and duplicate physical source ownership cannot otherwise be
    proven.
    """

    base_loaded, base, base_path = _load_profile(base_profile_path, "base profile")
    road_loaded, road, road_path = _load_profile(road_profile_path, "Road profile")
    faction_loaded, faction, faction_path = _load_profile(
        faction_profile_path, "faction profile"
    )
    plan, plan_path = _load_static_plan(static_plan_path)
    hierarchical_plan, hierarchical_plan_path = _load_hierarchical_plan(
        hierarchical_plan_path
    )
    animated_plan, animated_plan_path = _load_animated_plan(animated_plan_path)

    if base_loaded.id != BASE_PROFILE_ID or base.get("id") != BASE_PROFILE_ID:
        raise ValueError(f"unexpected base profile id: {base.get('id')!r}")
    if base_loaded.pack_id != PACK_ID:
        raise ValueError(f"base pack id must remain {PACK_ID!r}")
    if not isinstance(profile_id, str) or profile_id == BASE_PROFILE_ID:
        raise ValueError("composition requires an explicit new profile id")

    base_resources = [
        _mapping(value, f"base resource {index}")
        for index, value in enumerate(_array(base.get("resources"), "base resources"))
    ]
    source_variants = _declared_source_variants(base_resources, "base")
    if source_variants != EXPECTED_SOURCE_VARIANTS:
        raise ValueError(
            "base source-variant contract changed: "
            f"expected {EXPECTED_SOURCE_VARIANTS!r}, found {source_variants!r}"
        )
    _validate_named_prunes(base_resources)
    road_added_resources, road_document = _validate_road_profile(base, road)
    static_resources, static_bindings = _validate_static_plan(plan)
    hierarchical_resources, hierarchical_bindings = _validate_hierarchical_plan(
        hierarchical_plan
    )
    animated_resources, animated_bindings = _validate_animated_plan(animated_plan)
    faction_resources, faction_documents, complete_flags = _validate_faction_profile(
        faction
    )
    _validate_coverage(
        faction_resources=faction_resources,
        static_resources=static_resources,
    )
    ui_rows = _ui_manifest_rows(
        faction_resources, faction_documents["data/ui_manifest.json"]
    )
    audio_covered_entry_count = _audio_prune_coverage(
        base_loaded, faction_loaded, catalog
    )

    road_resources = [
        _mapping(value, f"Road resource {index}")
        for index, value in enumerate(_array(road.get("resources"), "Road resources"))
    ]
    retained_base = _narrow_base_resources(
        base_resources, road_resources, static_bindings
    )
    pre_hierarchy_resources = [
        *retained_base,
        *road_added_resources,
        *static_resources,
        *faction_resources,
    ]
    retained_hierarchical, reused_hierarchical = _dedupe_hierarchical_resources(
        base_payload=base,
        existing_resources=pre_hierarchy_resources,
        hierarchical_resources=hierarchical_resources,
        catalog=catalog,
    )
    pre_animated_bindings = [*static_bindings, *hierarchical_bindings]
    if len(pre_animated_bindings) != EXPECTED_PRE_ANIMATED_BINDING_COUNT:
        raise ValueError(
            "combined static/hierarchical binding count is not "
            f"{EXPECTED_PRE_ANIMATED_BINDING_COUNT}"
        )
    _case_unique(
        (
            _text(item.get("typeName"), "combined binding typeName")
            for item in pre_animated_bindings
        ),
        "combined binding typeName",
    )
    pre_animated_resources = [*pre_hierarchy_resources, *retained_hierarchical]
    retained_animated, reused_animated = _dedupe_animated_resources(
        base_payload=base,
        existing_resources=pre_animated_resources,
        animated_resources=animated_resources,
        catalog=catalog,
    )
    combined_bindings = [*pre_animated_bindings, *animated_bindings]
    if len(combined_bindings) != EXPECTED_FINAL_BINDING_COUNT:
        raise ValueError(
            "combined static/hierarchical/animated binding count is not "
            f"{EXPECTED_FINAL_BINDING_COUNT}"
        )
    _case_unique(
        (
            _text(item.get("typeName"), "final combined binding typeName")
            for item in combined_bindings
        ),
        "final combined binding typeName",
    )
    retained_map = next(
        item for item in retained_base if item.get("id") == MAP_RESOURCE_ID
    )
    retained_map["options"]["objectBindings"]["models"] = deepcopy(combined_bindings)
    resources = [*pre_animated_resources, *retained_animated]
    if len(resources) != EXPECTED_FINAL_RESOURCE_COUNT:
        raise ValueError(
            f"composed resource count mismatch: expected {EXPECTED_FINAL_RESOURCE_COUNT}, "
            f"found {len(resources)}"
        )
    if len(resources) > MAX_RESOURCES:
        raise ValueError(f"composed profile exceeds {MAX_RESOURCES} resources")
    _validate_final_names(resources)

    payload = deepcopy(base)
    payload["id"] = profile_id
    payload["title"] = "Men versus Men - Fords of Isen II full private composition"
    payload["resources"] = resources
    pack = payload.get("pack")
    if not isinstance(pack, dict) or pack.get("id") != PACK_ID:
        raise ValueError(f"composed pack id must remain {PACK_ID!r}")
    pack_files = pack.get("files")
    if not isinstance(pack_files, dict):
        raise ValueError("base pack.files must be an object")
    for name, path in _mapping(
        _mapping(faction.get("pack"), "faction pack").get("files"),
        "faction pack.files",
    ).items():
        if any(existing.casefold() == str(name).casefold() for existing in pack_files):
            raise ValueError(f"pack.files key collision while adding {name!r}")
        pack_files[str(name)] = str(path)

    road_runtime = _mapping(road.get("runtime_data"), "Road runtime_data")
    runtime_data = deepcopy(dict(road_runtime))
    expected_overlap = {"data/audio_events.json"}
    actual_overlap = {
        path
        for path in FACTION_RUNTIME_PATHS
        if any(existing.casefold() == path.casefold() for existing in runtime_data)
    }
    if actual_overlap != expected_overlap:
        raise ValueError(
            f"unexpected faction/runtime overlap: {sorted(actual_overlap)!r}"
        )
    for path in FACTION_RUNTIME_PATHS:
        runtime_data[path] = deepcopy(faction_documents[path])
    objects_document = runtime_data.get("data/objects.json")
    if not isinstance(objects_document, dict):
        raise ValueError("Road/base profile has no objects runtime document")
    icon_replacements = _replace_member_ui(objects_document, ui_rows)
    payload["runtime_data"] = runtime_data

    runtime_keys = _case_unique(
        (_safe_path(path, "final runtime path") for path in runtime_data),
        "final runtime path",
    )
    if ROAD_RUNTIME_PATH.casefold() not in runtime_keys:
        raise ValueError("composed profile lost Road runtime metadata")
    if runtime_data[ROAD_RUNTIME_PATH] != road_document:
        raise ValueError("composed profile changed exact Road runtime metadata")
    for path in FACTION_RUNTIME_PATHS:
        if runtime_data[path] != faction_documents[path]:
            raise ValueError(f"composed profile changed exact faction runtime {path!r}")

    loaded = _profile_from_payload(payload)
    if loaded.id != profile_id or loaded.pack_id != PACK_ID:
        raise ValueError("authoritative ImportProfile load changed composed identities")
    if len(loaded.resources) != EXPECTED_FINAL_RESOURCE_COUNT:
        raise ValueError("authoritative ImportProfile load changed resource count")
    selected_file_count, concrete_output_count = _validate_resolved_profile(
        loaded, catalog, runtime_data
    )

    map_resources = [
        resource for resource in resources if resource.get("id") == MAP_RESOURCE_ID
    ]
    if len(map_resources) != 1:
        raise ValueError("composed profile does not have one exact Fords map resource")
    final_map_options = _mapping(map_resources[0].get("options"), "final map options")
    final_object_bindings = _mapping(
        final_map_options.get("objectBindings"), "final objectBindings"
    )
    final_bindings = _array(final_object_bindings.get("models"), "final model bindings")
    if (
        final_bindings != combined_bindings
        or len(final_bindings) != EXPECTED_FINAL_BINDING_COUNT
    ):
        raise ValueError(
            "composed profile did not preserve the exact static, hierarchical, and animated bindings"
        )

    profile_sha256 = hashlib.sha256(_pretty_json_bytes(payload)).hexdigest()
    report: dict[str, Any] = {
        "schema": COMPOSITION_REPORT_SCHEMA,
        "schemaVersion": COMPOSITION_REPORT_SCHEMA_VERSION,
        "profileId": profile_id,
        "packId": PACK_ID,
        "profileSha256": profile_sha256,
        "inputs": {
            "baseProfileSha256": _file_sha256(base_path),
            "roadProfileSha256": _file_sha256(road_path),
            "factionProfileSha256": _file_sha256(faction_path),
            "staticPlanSha256": _file_sha256(plan_path),
            "staticPlanAggregateSha256": plan["aggregateSha256"],
            "hierarchicalPlanSha256": _file_sha256(hierarchical_plan_path),
            "hierarchicalPlanAggregateSha256": hierarchical_plan["aggregateSha256"],
            "animatedPlanSha256": _file_sha256(animated_plan_path),
            "animatedPlanAggregateSha256": animated_plan["aggregateSha256"],
        },
        "resources": {
            "base": len(base_resources),
            "retainedBase": len(retained_base),
            "prunedBase": len(FULLY_PRUNED_BASE_RESOURCES),
            "roadAdded": len(road_added_resources),
            "staticAdded": len(static_resources),
            "hierarchicalPlanned": len(hierarchical_resources),
            "hierarchicalReused": len(reused_hierarchical),
            "hierarchicalAdded": len(retained_hierarchical),
            "animatedPlanned": len(animated_resources),
            "animatedReused": len(reused_animated),
            "animatedAdded": len(retained_animated),
            "factionAdded": len(faction_resources),
            "final": len(resources),
            "maximum": MAX_RESOURCES,
            "sourceVariants": [
                {"resourceId": resource_id, "sourceOwnerResourceId": owner_id}
                for resource_id, owner_id in sorted(source_variants.items())
            ],
        },
        "pruning": {
            "exactRemoved": [
                {"id": resource_id, "patterns": list(patterns)}
                for resource_id, patterns in FULLY_PRUNED_BASE_RESOURCES.items()
            ],
            "narrowed": {
                "id": PARTIAL_RESOURCE_ID,
                "removedPatterns": list(PARTIAL_REMOVED_PATTERNS),
                "retainedPatterns": list(PARTIAL_RETAINED_PATTERNS),
            },
            "audioCatalogEntriesCoveredByFaction": audio_covered_entry_count,
        },
        "runtime": {
            "paths": sorted(runtime_data, key=lambda value: (value.casefold(), value)),
            "roadCount": road_document["roadCount"],
            "roadIds": [item["id"] for item in road_document["roads"]],
            "staticBindingCount": len(static_bindings),
            "hierarchicalBindingCount": len(hierarchical_bindings),
            "animatedBindingCount": len(animated_bindings),
            "finalBindingCount": len(final_bindings),
            "hierarchicalSharedResources": reused_hierarchical,
            "animatedSharedResources": reused_animated,
            "factionCompleteFlags": complete_flags,
            "exactFactionDocumentsPreserved": True,
            "iconReplacements": icon_replacements,
        },
        "resolution": {
            "selectedFileCount": selected_file_count,
            "concreteOutputCount": concrete_output_count,
            "duplicateCatalogEntryCount": 0,
            "sourcePatternCollisionCount": 0,
            "resourceIdCollisionCount": 0,
            "outputCollisionCount": 0,
            "runtimePathCollisionCount": 0,
            "bindingTypeCollisionCount": 0,
        },
    }
    report["aggregateSha256"] = _canonical_json_sha256(report)
    return ComposedRetailSliceProfile(payload, report)


def write_composed_retail_slice_profile(
    composed: ComposedRetailSliceProfile,
    profile_output: Path | str,
    report_output: Path | str | None = None,
) -> tuple[Path, Path | None]:
    """Atomically write validated profile/report metadata."""

    profile_path = Path(profile_output).expanduser().resolve()
    write_json_atomic(profile_path, composed.profile)
    if _file_sha256(profile_path) != composed.report["profileSha256"]:
        raise RuntimeError("written profile digest disagrees with composition report")
    report_path: Path | None = None
    if report_output is not None:
        report_path = Path(report_output).expanduser().resolve()
        write_json_atomic(report_path, composed.report)
    return profile_path, report_path


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Compose the strict private Men/Fords retail-slice ImportProfile"
    )
    parser.add_argument("--base", required=True, type=Path)
    parser.add_argument("--roads", required=True, type=Path)
    parser.add_argument("--faction", required=True, type=Path)
    parser.add_argument("--static-plan", required=True, type=Path)
    parser.add_argument("--hierarchical-plan", required=True, type=Path)
    parser.add_argument("--animated-plan", required=True, type=Path)
    parser.add_argument("--catalog", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--profile-id", default=FULL_PROFILE_ID)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    composed = compose_retail_slice_profile(
        args.base,
        args.roads,
        args.faction,
        args.static_plan,
        args.hierarchical_plan,
        args.animated_plan,
        InstallCatalog.load(args.catalog),
        profile_id=args.profile_id,
    )
    write_composed_retail_slice_profile(composed, args.output, args.report)
    summary = {
        "profile_id": composed.profile["id"],
        "pack_id": composed.profile["pack"]["id"],
        "profile_sha256": composed.report["profileSha256"],
        "resource_count": composed.report["resources"]["final"],
        "selected_file_count": composed.report["resolution"]["selectedFileCount"],
        "duplicate_catalog_entry_count": composed.report["resolution"][
            "duplicateCatalogEntryCount"
        ],
    }
    print(json.dumps(summary, sort_keys=True))
    return 0


if __name__ == "__main__":  # pragma: no cover - exercised by real generation
    raise SystemExit(main())


__all__ = [
    "BASE_PROFILE_ID",
    "COMPOSITION_REPORT_SCHEMA",
    "COMPOSITION_REPORT_SCHEMA_VERSION",
    "ComposedRetailSliceProfile",
    "EXPECTED_FINAL_RESOURCE_COUNT",
    "EXPECTED_FINAL_BINDING_COUNT",
    "EXPECTED_ANIMATED_BINDING_COUNT",
    "EXPECTED_HIERARCHICAL_BINDING_COUNT",
    "EXPECTED_ROADS",
    "EXPECTED_STATIC_BINDING_COUNT",
    "FULL_PROFILE_ID",
    "PACK_ID",
    "compose_retail_slice_profile",
    "write_composed_retail_slice_profile",
]
