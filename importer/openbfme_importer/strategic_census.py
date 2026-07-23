"""Fail-closed census of the War-of-the-Ring and Create-a-Hero INI surface.

This admits the strategic-layer (LivingWorld*/AutoResolve*/StrategicHUD) and
Create-a-Hero definition families into the importer's census machinery as a
first-class data surface.  It enumerates the effective files, counts their
top-level blocks with the existing bounded readers, and accounts for every
deviation instead of dropping it: expected-but-missing files land in
``unresolvedFiles`` and block types outside the measured family vocabulary
land in ``unclassifiedBlocks``.
"""

from __future__ import annotations

from typing import Any

from .catalog import CatalogEntry, InstallCatalog
from .faction_census import _effective_entries, _read_document
from .game import retail_game
from .sage_ini import (
    _decode as _ini_decode,
    _strip_comments as _ini_strip_comments,
    parse_flat_named_blocks,
    parse_object_definitions,
)


STRATEGIC_HUD_PATH = "data/ini/strategichud.ini"
STRATEGIC_IMAGES_PATH = "data/ini/mappedimages/aptimages/strategicimages.ini"
LIVING_WORLD_PREFIX = "data/ini/livingworld"
CAH_OBJECT_PREFIX = "data/ini/object/createahero/"
CAH_MAP_PREFIX = "maps/createahero/"

CAH_EXACT_PATHS = (
    "data/ini/createaherosystem.ini",
    "data/ini/createaherospecialpowers.ini",
    "data/ini/mappedimages/aptimages/cahportraits.ini",
    "data/ini/mappedimages/aptimages/createaheroimages.ini",
)

MAX_STRATEGIC_DOCUMENTS = 256
MAX_TOTAL_STRATEGIC_BYTES = 64 * 1024 * 1024

_COMMON_WOTR_EXPECTED = (
    "data/ini/livingworld.ini",
    "data/ini/livingworldaitemplate.ini",
    "data/ini/livingworldautoresolvearmor.ini",
    "data/ini/livingworldautoresolvebody.ini",
    "data/ini/livingworldautoresolvecombatchain.ini",
    "data/ini/livingworldautoresolvehandicaps.ini",
    "data/ini/livingworldautoresolveleadership.ini",
    "data/ini/livingworldautoresolvereinforcementschedule.ini",
    "data/ini/livingworldautoresolveresourcebonus.ini",
    "data/ini/livingworldautoresolvesciencepurchasepointbonus.ini",
    "data/ini/livingworldautoresolveweapon.ini",
    "data/ini/livingworldbuildingicons.ini",
    "data/ini/livingworldbuildings.ini",
    "data/ini/livingworldbuildploticons.ini",
    "data/ini/livingworldicons/dwarficons.ini",
    "data/ini/livingworldicons/elficons.ini",
    "data/ini/livingworldicons/isengardicons.ini",
    "data/ini/livingworldicons/mordoricons.ini",
    "data/ini/livingworldicons/mowicons.ini",
    "data/ini/livingworldicons/wildicons.ini",
    "data/ini/livingworldlogic.ini",
    "data/ini/livingworldplayers.ini",
    "data/ini/livingworldregioneffects.ini",
    "data/ini/livingworldregions.ini",
    "data/ini/livingworldteststructures.ini",
    STRATEGIC_HUD_PATH,
    STRATEGIC_IMAGES_PATH,
)

# Measured ground truth: BFME2 1.06 ships 27 strategic files and 6 CaH files;
# RotWK 2.01 ships 29 strategic files (adds the Angmar/Arnor icon documents)
# and 6 CaH files — maps/createahero/map.ini ships only in the base game's
# archives, which the expansion mounts at runtime (the layered rotwk catalog
# mirrors that install shape).
EXPECTED_FILES: dict[str, dict[str, tuple[str, ...]]] = {
    "bfme2": {
        "wotr": _COMMON_WOTR_EXPECTED,
        "cah": (
            *CAH_EXACT_PATHS,
            "data/ini/object/createahero/createahero.ini",
            "maps/createahero/map.ini",
        ),
    },
    "rotwk": {
        "wotr": (
            *_COMMON_WOTR_EXPECTED,
            "data/ini/livingworldicons/angmaricons.ini",
            "data/ini/livingworldicons/arnoricons.ini",
        ),
        "cah": (
            *CAH_EXACT_PATHS,
            "data/ini/object/createahero/createahero.ini",
            "maps/createahero/map.ini",
        ),
    },
}

# Known family vocabulary, measured in the RotWK 2.01 gap report (sections
# 2d/2e).  Keys are canonical authored spellings; values are report families.
WOTR_BLOCK_FAMILIES: dict[str, str] = {
    "LivingWorldAITemplate": "livingWorld",
    "LivingWorldAnimObject": "livingWorld",
    "LivingWorldArmyIcon": "livingWorld",
    "LivingWorldAutoResolveResourceBonus": "livingWorld",
    "LivingWorldAutoResolveSciencePurchasePointBonus": "livingWorld",
    "LivingWorldBuilding": "livingWorld",
    "LivingWorldBuildingIcon": "livingWorld",
    "LivingWorldBuildPlotIcon": "livingWorld",
    "LivingWorldMapInfo": "livingWorld",
    "LivingWorldObject": "livingWorld",
    "LivingWorldPlayerTemplate": "livingWorld",
    "LivingWorldRegionEffects": "livingWorld",
    "LivingWorldSound": "livingWorld",
    "MappedImage": "livingWorld",
    "RegionCampain": "livingWorld",
    "StrategicHUD": "livingWorld",
    "AutoResolveArmor": "autoResolve",
    "AutoResolveBody": "autoResolve",
    "AutoResolveCombatChain": "autoResolve",
    "AutoResolveHandicapLevel": "autoResolve",
    "AutoResolveLeadership": "autoResolve",
    "AutoResolveReinforcementSchedule": "autoResolve",
    "AutoResolveWeapon": "autoResolve",
}

CAH_BLOCK_FAMILIES: dict[str, str] = {
    "ChildObject": "cah",
    "CreateAHeroSystem": "cah",
    "MappedImage": "cah",
    "Object": "cah",
    "ShadowMap": "cah",
    "SpecialPower": "cah",
    "Weather": "cah",
}

_OBJECT_FAMILY_KINDS = frozenset({"object", "childobject", "objectreskin"})


def _top_level_block_headers(source: bytes) -> list[tuple[str, int]]:
    """Return ``(blockType, headerTokenCount)`` per top-level block header.

    Retail strategic INI authors block headers at column zero and indents
    interior detail, so only column-zero opener lines are depth-tracked;
    preprocessor lines (``#define``/``#include``) declare no body and
    assignment lines never open sections here.  Closing ``End`` lines are
    structural at any indentation (retail closes some top-level blocks with a
    tab-indented ``End``); an ``End`` with no tracked opener belongs to an
    untracked indented section and is ignored rather than guessed at.  This
    forgiving balance can only ever overcount headers (never drop one), and
    every overcount is caught by the fail-closed cross-check against the
    bounded readers in ``_count_top_level_blocks``.
    """

    headers: list[tuple[str, int]] = []
    depth = 0
    for raw in _ini_decode(source).splitlines():
        line = _ini_strip_comments(raw)
        if not line:
            continue
        if line.casefold() == "end":
            if depth > 0:
                depth -= 1
            continue
        if raw[0] in " \t":
            continue
        if line.startswith("#"):
            continue
        if "=" in line:
            continue
        tokens = line.split()
        if depth == 0:
            headers.append((tokens[0], len(tokens)))
        depth += 1
    if depth != 0:
        raise ValueError("unterminated block at end of strategic INI")
    return headers


def _count_top_level_blocks(source: bytes) -> dict[str, dict[str, Any]]:
    """Count top-level blocks per authored block type, fail-closed.

    Named block counts are verified against the existing bounded readers
    (``parse_flat_named_blocks`` for flat families, ``parse_object_definitions``
    for the Object family); any disagreement raises instead of guessing.  Two
    authored quirks are outside the flat reader's exact-two-token header
    grammar and are counted by the scanner alone: anonymous singletons
    (``StrategicHUD``, ``LivingWorldMapInfo``, ``CreateAHeroSystem``,
    ``Weather``, ...) and names containing whitespace (the retail
    ``MappedImage CahAward_VeteranWa Hero`` typo).
    """

    total: dict[str, int] = {}
    flat_named: dict[str, int] = {}
    spellings: dict[str, str] = {}
    for kind, token_count in _top_level_block_headers(source):
        key = kind.casefold()
        spellings.setdefault(key, kind)
        total[key] = total.get(key, 0) + 1
        if token_count == 2:
            flat_named[key] = flat_named.get(key, 0) + 1

    object_kind_counts: dict[str, int] | None = None
    for key in sorted(total):
        if key in _OBJECT_FAMILY_KINDS:
            if object_kind_counts is None:
                object_kind_counts = {}
                for block in parse_object_definitions(source):
                    block_key = block.kind.casefold()
                    object_kind_counts[block_key] = (
                        object_kind_counts.get(block_key, 0) + 1
                    )
            expected, parsed = total[key], object_kind_counts.get(key, 0)
        else:
            expected = flat_named.get(key, 0)
            if expected == 0:
                continue
            parsed = len(parse_flat_named_blocks(source, spellings[key]))
        if parsed != expected:
            raise ValueError(
                "strategic census block count disagreement for "
                f"{spellings[key]}: scanner={expected} reader={parsed}"
            )

    return {
        key: {"spelling": spellings[key], "count": total[key]}
        for key in sorted(total)
    }


def _is_wotr_path(folded: str) -> bool:
    if folded in {STRATEGIC_HUD_PATH, STRATEGIC_IMAGES_PATH}:
        return True
    return folded.startswith(LIVING_WORLD_PREFIX) and folded.endswith(".ini")


def _is_cah_path(folded: str) -> bool:
    if folded in {path.casefold() for path in CAH_EXACT_PATHS}:
        return True
    if folded.startswith(CAH_OBJECT_PREFIX) and folded.endswith(".ini"):
        return True
    return folded.startswith(CAH_MAP_PREFIX) and folded.endswith(".ini")


def _select_entries(catalog: InstallCatalog) -> dict[str, tuple[str, CatalogEntry]]:
    selected: dict[str, tuple[str, CatalogEntry]] = {}
    for entry in _effective_entries(catalog).values():
        folded = entry.name.casefold()
        if _is_wotr_path(folded):
            selected[folded] = ("wotr", entry)
        elif _is_cah_path(folded):
            selected[folded] = ("cah", entry)
    if len(selected) > MAX_STRATEGIC_DOCUMENTS:
        raise ValueError("strategic census document count exceeds limit")
    if sum(entry.size for _, entry in selected.values()) > MAX_TOTAL_STRATEGIC_BYTES:
        raise ValueError("strategic census document bytes exceed limit")
    return selected


def census_strategic_surface(catalog: InstallCatalog, game: str) -> dict[str, Any]:
    """Census the WotR + CaH INI surface of the effective catalog tree.

    Returns per-file rows (path, winning archive, block-type counts), family
    totals (LivingWorld*, AutoResolve*, CaH), and fail-closed accounting:
    ``unresolvedFiles`` lists expected-but-missing documents and
    ``unclassifiedBlocks`` enumerates every top-level block type outside the
    measured family vocabulary.  Nothing is silently dropped.
    """

    retail_game(game)
    expected = EXPECTED_FILES[game]
    selected = _select_entries(catalog)

    file_rows: list[dict[str, Any]] = []
    family_totals = {"livingWorld": 0, "autoResolve": 0, "cah": 0}
    unclassified: list[dict[str, Any]] = []
    for folded in sorted(selected):
        surface, entry = selected[folded]
        vocabulary = WOTR_BLOCK_FAMILIES if surface == "wotr" else CAH_BLOCK_FAMILIES
        canonical = {kind.casefold(): kind for kind in vocabulary}
        document = _read_document(catalog, entry.name)
        counts = _count_top_level_blocks(document.source)
        block_counts: dict[str, int] = {}
        for key, item in counts.items():
            spelling, count = str(item["spelling"]), int(item["count"])
            known = canonical.get(key)
            if known is None:
                unclassified.append(
                    {
                        "virtualPath": entry.name,
                        "blockType": spelling,
                        "count": count,
                    }
                )
                block_counts[spelling] = count
                continue
            family_totals[vocabulary[known]] += count
            block_counts[known] = count
        file_rows.append(
            {
                **document.public(),
                "surface": surface,
                "blockCounts": dict(sorted(block_counts.items())),
                "blockTotal": sum(block_counts.values()),
            }
        )

    unresolved: list[dict[str, str]] = []
    for surface in ("wotr", "cah"):
        for path in expected[surface]:
            if path.casefold() not in selected:
                unresolved.append({"virtualPath": path, "surface": surface})
    unresolved.sort(key=lambda row: row["virtualPath"])
    unclassified.sort(
        key=lambda row: (row["virtualPath"], str(row["blockType"]).casefold())
    )

    return {
        "format": 1,
        "schema": "openbfme.strategic-census",
        "schemaVersion": 1,
        "game": game,
        "fileCount": len(file_rows),
        "files": file_rows,
        "surfaceFileCounts": {
            "wotr": sum(1 for row in file_rows if row["surface"] == "wotr"),
            "cah": sum(1 for row in file_rows if row["surface"] == "cah"),
        },
        "familyTotals": family_totals,
        "unresolvedFiles": unresolved,
        "unclassifiedBlocks": unclassified,
    }
