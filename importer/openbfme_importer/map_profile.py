"""Generate the exact private BFME2 1.06 five-map conversion profile."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import PurePosixPath
from typing import Any

from .catalog import CatalogEntry, InstallCatalog
from .sage_map import MAX_SOURCE_BYTES, ParsedSageMap, parse_sage_map_bytes
from .terrain_materials import (
    MAX_TERRAIN_INI_BYTES,
    resolve_terrain_material_references,
)


TERRAIN_INI_PATH = "data/ini/terrain.ini"


@dataclass(frozen=True, slots=True)
class FiveMapTarget:
    slug: str
    display_name: str
    virtual_path: str


FIVE_MAP_TARGETS = (
    FiveMapTarget(
        "fords-of-isen-ii",
        "Fords of Isen II",
        "maps/map mp fords of isen ii/map mp fords of isen ii.map",
    ),
    FiveMapTarget(
        "rivendell",
        "Rivendell",
        "maps/map wor rivendell/map wor rivendell.map",
    ),
    FiveMapTarget(
        "mount-doom",
        "Mount Doom",
        "maps/map wor mount doom/map wor mount doom.map",
    ),
    FiveMapTarget(
        "dagorlad",
        "Dagorlad",
        "maps/map wor dagorlad/map wor dagorlad.map",
    ),
    FiveMapTarget(
        "mordor",
        "Mordor",
        "maps/map wor mordor/map wor mordor.map",
    ),
)


def _entry(catalog: InstallCatalog, virtual_path: str, label: str) -> CatalogEntry:
    entry = catalog.resolve_exact(virtual_path)
    if entry is None:
        raise ValueError(f"missing exact {label}: {virtual_path}")
    return entry


def _read(
    catalog: InstallCatalog, virtual_path: str, label: str, maximum: int
) -> tuple[CatalogEntry, bytes]:
    entry = _entry(catalog, virtual_path, label)
    archive = catalog.open_archive_for(entry)
    return entry, archive.read_entry(catalog.as_entry(entry), max_bytes=maximum)


def _companion(target: FiveMapTarget, suffix: str) -> str:
    source = PurePosixPath(target.virtual_path)
    stem = source.stem
    return (source.parent / f"{stem}{suffix}").as_posix()


def _expected(parsed: ParsedSageMap) -> dict[str, Any]:
    return {
        "width": parsed.heightmap.width,
        "height": parsed.heightmap.height,
        "borderWidth": parsed.heightmap.border_width,
        "impassableCount": int(parsed.blend["gridStats"]["impassable"]),
        "terrainTextureCount": len(parsed.blend["textures"]),
        "standingWaterCount": len(parsed.standing_water),
        "riverCount": len(parsed.rivers),
        "objectCount": len(parsed.objects),
        "waypointCount": len(parsed.waypoints),
        "playerStartNames": sorted(parsed.player_starts),
        "scriptListCount": int(parsed.script_summary["listCount"]),
        "nonemptyScriptListCount": int(parsed.script_summary["nonemptyListCount"]),
        "triggerCount": parsed.trigger_count,
        "standingWaveCount": parsed.standing_wave_count,
        "waypointPathCount": parsed.waypoint_path_count,
    }


def build_five_map_profile(catalog: InstallCatalog) -> dict[str, Any]:
    """Return a deterministic profile generated only from exact retail facts."""

    terrain_entry, terrain_source = _read(
        catalog, TERRAIN_INI_PATH, "terrain definition source", MAX_TERRAIN_INI_BYTES
    )
    resources: list[dict[str, Any]] = []
    map_catalog: list[dict[str, Any]] = []
    global_symbols: list[str] = []
    global_symbol_keys: set[str] = set()
    global_terrain_paths: list[str] = [terrain_entry.name]
    global_texture_keys: set[str] = set()

    for target in FIVE_MAP_TARGETS:
        map_entry, map_source = _read(
            catalog, target.virtual_path, f"{target.display_name} map", MAX_SOURCE_BYTES
        )
        parsed = parse_sage_map_bytes(map_source)
        symbols = [str(row["name"]) for row in parsed.blend["textures"]]
        if len({symbol.casefold() for symbol in symbols}) != len(symbols):
            raise ValueError(f"{target.display_name} has duplicate terrain symbols")
        material_refs = resolve_terrain_material_references(terrain_source, symbols)
        for symbol, reference in zip(symbols, material_refs, strict=True):
            symbol_key = symbol.casefold()
            if symbol_key not in global_symbol_keys:
                global_symbol_keys.add(symbol_key)
                global_symbols.append(symbol)
            requested = f"art/terrain/{reference.texture}"
            texture_entry = _entry(
                catalog, requested, f"{target.display_name} terrain texture"
            )
            key = texture_entry.name.casefold()
            if key not in global_texture_keys:
                global_texture_keys.add(key)
                global_terrain_paths.append(texture_entry.name)

        map_ini = (PurePosixPath(target.virtual_path).parent / "map.ini").as_posix()
        map_ini_entry = _entry(catalog, map_ini, f"{target.display_name} map.ini")
        art_entry = _entry(
            catalog, _companion(target, "_art.tga"), f"{target.display_name} map art"
        )
        preview_entry = _entry(
            catalog,
            _companion(target, "_pic.tga"),
            f"{target.display_name} map preview",
        )
        output_root = f"maps/{target.slug}"
        map_id = f"bfme2.map.{target.slug}"
        art_output = f"assets/ui/maps/{target.slug}-art.png"
        preview_output = f"assets/ui/maps/{target.slug}-preview.png"

        resources.extend(
            [
                {
                    "id": f"map-{target.slug}-binary",
                    "kind": "map",
                    "converter": "sage-map",
                    "patterns": [map_entry.name],
                    "output": output_root,
                    "limit": 1,
                    "expected_count": 1,
                    "options": {
                        "metadata": {
                            "id": map_id,
                            "displayName": target.display_name,
                            "preview": preview_output,
                            "art": art_output,
                            "terrainMaterials": "assets/terrain/five-maps/terrain-materials.json",
                        },
                        "expected": _expected(parsed),
                    },
                },
                {
                    "id": f"map-{target.slug}-config",
                    "kind": "map",
                    "converter": "hash-only",
                    "patterns": [map_ini_entry.name],
                    "limit": 1,
                    "expected_count": 1,
                },
                {
                    "id": f"map-{target.slug}-art",
                    "kind": "ui",
                    "converter": "texture",
                    "patterns": [art_entry.name],
                    "output": art_output,
                    "limit": 1,
                    "expected_count": 1,
                },
                {
                    "id": f"map-{target.slug}-preview",
                    "kind": "ui",
                    "converter": "texture",
                    "patterns": [preview_entry.name],
                    "output": preview_output,
                    "limit": 1,
                    "expected_count": 1,
                },
            ]
        )
        map_catalog.append(
            {
                "id": map_id,
                "displayName": target.display_name,
                "map": f"{output_root}/map.json",
                "preview": preview_output,
                "art": art_output,
                "terrainMaterials": "assets/terrain/five-maps/terrain-materials.json",
                "playerCount": len(parsed.player_starts),
                "routingGraphStatus": (
                    "empty-no-authored-navmesh"
                    if not parsed.waypoint_edges
                    else "source-waypoint-edges-present-runtime-pending"
                ),
                "navigationMeshStatus": "not-generated-or-validated-by-map-profile",
            }
        )

    resources.append(
        {
            "id": "five-maps-terrain-materials",
            "kind": "texture",
            "converter": "sage-terrain-materials",
            "patterns": global_terrain_paths,
            "output": "assets/terrain/five-maps",
            "limit": len(global_terrain_paths),
            "expected_count": len(global_terrain_paths),
            "options": {"symbols": global_symbols},
        }
    )

    return {
        "format": 1,
        "id": "bfme2-five-maps-106-generated",
        "title": "BFME II 1.06 five-map private generated pack",
        "pack": {
            "id": "bfme2-five-maps-106-private",
            "version": "1.06-generated-v0",
            "schema": "openbfme.content-pack",
            "schemaVersion": 0,
            "priority": 904,
            "vertical_slice_complete": False,
            "capability_maturity": "source-map-setup-terrain-cook-runtime-navigation-pending",
            "dataPolicy": {
                "externalPathsAllowed": False,
                "redistributable": False,
            },
            "files": {
                "entryMap": "maps/fords-of-isen-ii/map.json",
                "mapCatalog": "data/maps.json",
            },
        },
        "resources": resources,
        "runtime_data": {
            "data/maps.json": {
                "schema": "openbfme.map-catalog",
                "schemaVersion": 0,
                "maps": map_catalog,
            }
        },
    }
