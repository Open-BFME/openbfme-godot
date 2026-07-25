"""Generate exact map conversion profiles from retail map sources.

Two lanes share one builder:

* the legacy private five-map development set (Fords plus four War-of-the-Ring
  maps) that bootstrapped terrain work, and
* the *skirmish* set, discovered from the shipped ``maps/mapcache.ini`` registry
  of whichever install is being imported, so BFME2 and RotWK each contribute the
  map set they actually ship instead of a hardcoded list.

Every fact in the emitted profile comes from the retail bytes: player capacity
is the authored ``Player_N_Start`` waypoint count, cross-checked against the
registry's ``numPlayers`` and the map's own multiplayer setup record. Maps that
cannot be resolved or parsed are recorded as rejections, never substituted.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import PurePosixPath
from typing import Any

from .catalog import CatalogEntry, InstallCatalog
from .map_census import (
    MAPCACHE_VIRTUAL_PATH,
    MAX_MAPCACHE_BYTES,
    parse_mapcache_bytes,
)
from .sage_map import (
    MAX_SOURCE_BYTES,
    ParsedSageMap,
    SageMapError,
    parse_sage_map_bytes,
)
from .terrain_materials import (
    MAX_TERRAIN_INI_BYTES,
    resolve_terrain_material_references,
)


TERRAIN_INI_PATH = "data/ini/terrain.ini"

#: Retail map directories are named ``map <kind> <name>``; the multiplayer
#: skirmish set is exactly the ``mp`` kind.
SKIRMISH_DIRECTORY_PREFIX = "map mp"

_DIRECTORY_KINDS = frozenset({"mp", "wor", "sp"})
_LOWERCASE_WORDS = frozenset({"of", "the", "and", "at", "on", "in", "to"})
_ROMAN_NUMERALS = frozenset({"ii", "iii", "iv", "vi", "vii", "viii", "ix", "xi"})


@dataclass(frozen=True, slots=True)
class MapTarget:
    slug: str
    display_name: str
    virtual_path: str
    #: ``numPlayers`` from ``mapcache.ini`` when the target came from the
    #: registry; ``None`` for hand-authored targets.
    registry_player_count: int | None = None


#: Historic alias: the five-map development set predates registry discovery.
FiveMapTarget = MapTarget


FIVE_MAP_TARGETS = (
    MapTarget(
        "fords-of-isen-ii",
        "Fords of Isen II",
        "maps/map mp fords of isen ii/map mp fords of isen ii.map",
    ),
    MapTarget(
        "rivendell",
        "Rivendell",
        "maps/map wor rivendell/map wor rivendell.map",
    ),
    MapTarget(
        "mount-doom",
        "Mount Doom",
        "maps/map wor mount doom/map wor mount doom.map",
    ),
    MapTarget(
        "dagorlad",
        "Dagorlad",
        "maps/map wor dagorlad/map wor dagorlad.map",
    ),
    MapTarget(
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


def _companion(target: MapTarget, suffix: str) -> str:
    source = PurePosixPath(target.virtual_path)
    stem = source.stem
    return (source.parent / f"{stem}{suffix}").as_posix()


def _name_words(directory: str) -> tuple[str, ...]:
    """Strip the retail ``map <kind>`` directory prefix, keeping the map name."""

    words = [word for word in directory.casefold().split() if word]
    if words and words[0] == "map":
        words = words[1:]
    if words and words[0] in _DIRECTORY_KINDS:
        words = words[1:]
    if not words:
        raise ValueError(f"map directory has no name component: {directory!r}")
    return tuple(words)


def _slug(directory: str) -> str:
    return "-".join(_name_words(directory))


def _display_name(directory: str) -> str:
    words = _name_words(directory)
    rendered: list[str] = []
    for index, word in enumerate(words):
        if word in _ROMAN_NUMERALS:
            rendered.append(word.upper())
        elif index > 0 and word in _LOWERCASE_WORDS:
            rendered.append(word)
        else:
            rendered.append(word[:1].upper() + word[1:])
    return " ".join(rendered)


def _map_directory(virtual_path: str) -> str:
    parts = PurePosixPath(virtual_path).parts
    if len(parts) < 2:
        raise ValueError(f"map path has no directory component: {virtual_path!r}")
    return parts[-2]


def discover_registry_map_targets(
    catalog: InstallCatalog,
    *,
    directory_prefix: str = SKIRMISH_DIRECTORY_PREFIX,
) -> tuple[tuple[MapTarget, ...], tuple[dict[str, Any], ...]]:
    """Discover the install's shipped map set from ``maps/mapcache.ini``.

    Returns the resolvable targets plus a rejection record for every registry
    row that names a map this install cannot convert (stale registry entries and
    maps whose ``map.ini`` / ``_art.tga`` / ``_pic.tga`` companions are absent).
    Nothing is substituted for a rejected map.
    """

    _, registry_source = _read(
        catalog, MAPCACHE_VIRTUAL_PATH, "map registry", MAX_MAPCACHE_BYTES
    )
    prefix = directory_prefix.casefold().strip()
    targets: list[MapTarget] = []
    rejections: list[dict[str, Any]] = []
    seen_slugs: dict[str, str] = {}
    for record in parse_mapcache_bytes(registry_source):
        virtual_path = str(record["virtualPath"])
        directory = _map_directory(virtual_path)
        if prefix and not directory.casefold().startswith(prefix):
            continue
        if not (
            bool(record["isMultiplayer"])
            and bool(record["isOfficial"])
            and not bool(record["isScenarioMp"])
        ):
            rejections.append(
                {
                    "virtualPath": virtual_path,
                    "status": "registry-not-official-multiplayer",
                }
            )
            continue
        slug = _slug(directory)
        if slug in seen_slugs:
            raise ValueError(
                f"map slug {slug!r} is claimed by {seen_slugs[slug]!r} and {virtual_path!r}"
            )
        missing = [
            candidate
            for candidate in (
                virtual_path,
                (PurePosixPath(virtual_path).parent / "map.ini").as_posix(),
                _companion_path(virtual_path, "_art.tga"),
                _companion_path(virtual_path, "_pic.tga"),
            )
            if catalog.resolve_exact(candidate) is None
        ]
        if missing:
            rejections.append(
                {
                    "virtualPath": virtual_path,
                    "status": (
                        "registry-stale-missing-payload"
                        if virtual_path in missing
                        else "missing-required-companion"
                    ),
                    "missing": missing,
                }
            )
            continue
        seen_slugs[slug] = virtual_path
        targets.append(
            MapTarget(
                slug,
                _display_name(directory),
                virtual_path,
                int(record["playerCount"]),
            )
        )
    targets.sort(key=lambda target: target.slug)
    return tuple(targets), tuple(rejections)


def _companion_path(virtual_path: str, suffix: str) -> str:
    source = PurePosixPath(virtual_path)
    return (source.parent / f"{source.stem}{suffix}").as_posix()


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


def _unbound_object_types(parsed: ParsedSageMap) -> list[str]:
    """Every non-road placement type that still needs an explicit binding.

    Waypoints are the one universally safe logical classification, so they are
    excluded; everything else is reported so the prop-binding gap per map is
    explicit rather than discovered at cook time.
    """

    types = {
        str(item["typeName"])
        for item in parsed.objects
        if int(item["roadType"]) == 0 and str(item["typeName"]) != WAYPOINT_TYPE_NAME
    }
    return sorted(types)


WAYPOINT_TYPE_NAME = "*Waypoints/Waypoint"


def _map_companion_entries(
    catalog: InstallCatalog, target: MapTarget
) -> tuple[CatalogEntry, CatalogEntry, CatalogEntry]:
    map_ini = (PurePosixPath(target.virtual_path).parent / "map.ini").as_posix()
    return (
        _entry(catalog, map_ini, f"{target.display_name} map.ini"),
        _entry(
            catalog, _companion(target, "_art.tga"), f"{target.display_name} map art"
        ),
        _entry(
            catalog,
            _companion(target, "_pic.tga"),
            f"{target.display_name} map preview",
        ),
    )


def _rejection_status(exc: Exception) -> str:
    if isinstance(exc, SageMapError):
        return "sage-map-parse-rejected"
    message = str(exc)
    if message.startswith("missing exact "):
        return "missing-required-source"
    if "duplicate terrain definition" in message:
        return "ambiguous-terrain-definition"
    if "registry player count" in message:
        return "player-count-disagreement"
    return "map-profile-rejected"


def build_map_profile(
    catalog: InstallCatalog,
    targets: tuple[MapTarget, ...],
    *,
    profile_id: str,
    title: str,
    pack_id: str,
    pack_version: str,
    terrain_output: str,
    map_id_prefix: str = "bfme2.map.",
    priority: int = 904,
    rejections: tuple[dict[str, Any], ...] = (),
    strict: bool = True,
) -> dict[str, Any]:
    """Return a deterministic profile generated only from exact retail facts."""

    if not targets:
        raise ValueError("map profile requires at least one target map")

    terrain_entry, terrain_source = _read(
        catalog, TERRAIN_INI_PATH, "terrain definition source", MAX_TERRAIN_INI_BYTES
    )
    resources: list[dict[str, Any]] = []
    map_catalog: list[dict[str, Any]] = []
    global_symbols: list[str] = []
    global_symbol_keys: set[str] = set()
    global_terrain_paths: list[str] = [terrain_entry.name]
    global_texture_keys: set[str] = set()
    unbound_types: dict[str, list[str]] = {}
    build_rejections: list[dict[str, Any]] = list(rejections)
    terrain_materials_output = f"{terrain_output}/terrain-materials.json"

    for target in targets:
        try:
            map_entry, map_source = _read(
                catalog,
                target.virtual_path,
                f"{target.display_name} map",
                MAX_SOURCE_BYTES,
            )
            parsed = parse_sage_map_bytes(map_source)
            player_count = len(parsed.player_starts)
            if (
                target.registry_player_count is not None
                and target.registry_player_count != player_count
            ):
                raise ValueError(
                    f"{target.display_name} registry player count "
                    f"{target.registry_player_count} disagrees with "
                    f"{player_count} authored player starts"
                )
            symbols = [str(row["name"]) for row in parsed.blend["textures"]]
            if len({symbol.casefold() for symbol in symbols}) != len(symbols):
                raise ValueError(f"{target.display_name} has duplicate terrain symbols")
            material_refs = resolve_terrain_material_references(terrain_source, symbols)
            # Stage the terrain closure locally so a map rejected further down
            # never contributes symbols or textures to the shared material set.
            staged_symbols: list[str] = []
            staged_texture_paths: list[str] = []
            for symbol, reference in zip(symbols, material_refs, strict=True):
                symbol_key = symbol.casefold()
                if symbol_key not in global_symbol_keys and symbol not in staged_symbols:
                    staged_symbols.append(symbol)
                requested = f"art/terrain/{reference.texture}"
                texture_entry = _entry(
                    catalog, requested, f"{target.display_name} terrain texture"
                )
                key = texture_entry.name.casefold()
                if (
                    key not in global_texture_keys
                    and texture_entry.name not in staged_texture_paths
                ):
                    staged_texture_paths.append(texture_entry.name)
            map_ini_entry, art_entry, preview_entry = _map_companion_entries(
                catalog, target
            )
        except (SageMapError, ValueError) as exc:
            if strict:
                raise
            build_rejections.append(
                {
                    "virtualPath": target.virtual_path,
                    "slug": target.slug,
                    "status": _rejection_status(exc),
                    "reason": str(exc),
                }
            )
            continue
        for symbol in staged_symbols:
            global_symbol_keys.add(symbol.casefold())
            global_symbols.append(symbol)
        for texture_path in staged_texture_paths:
            global_texture_keys.add(texture_path.casefold())
            global_terrain_paths.append(texture_path)

        output_root = f"maps/{target.slug}"
        map_id = f"{map_id_prefix}{target.slug}"
        art_output = f"assets/ui/maps/{target.slug}-art.png"
        preview_output = f"assets/ui/maps/{target.slug}-preview.png"
        unbound_types[target.slug] = _unbound_object_types(parsed)

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
                            "terrainMaterials": terrain_materials_output,
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
        row: dict[str, Any] = {
            "id": map_id,
            "displayName": target.display_name,
            "map": f"{output_root}/map.json",
            "preview": preview_output,
            "art": art_output,
            "terrainMaterials": terrain_materials_output,
            "playerCount": player_count,
            "routingGraphStatus": (
                "empty-no-authored-navmesh"
                if not parsed.waypoint_edges
                else "source-waypoint-edges-present-runtime-pending"
            ),
            "navigationMeshStatus": "not-generated-or-validated-by-map-profile",
        }
        if target.registry_player_count is not None:
            row["registryPlayerCount"] = int(target.registry_player_count)
        map_catalog.append(row)

    if not map_catalog:
        raise ValueError("map profile resolved no convertible maps")

    resources.append(
        {
            "id": f"{terrain_output.rsplit('/', 1)[-1]}-terrain-materials",
            "kind": "texture",
            "converter": "sage-terrain-materials",
            "patterns": global_terrain_paths,
            "output": terrain_output,
            "limit": len(global_terrain_paths),
            "expected_count": len(global_terrain_paths),
            "options": {"symbols": global_symbols},
        }
    )

    profile: dict[str, Any] = {
        "format": 1,
        "id": profile_id,
        "title": title,
        "pack": {
            "id": pack_id,
            "version": pack_version,
            "schema": "openbfme.content-pack",
            "schemaVersion": 0,
            "priority": priority,
            "vertical_slice_complete": False,
            "capability_maturity": "source-map-setup-terrain-cook-runtime-navigation-pending",
            "dataPolicy": {
                "externalPathsAllowed": False,
                "redistributable": False,
            },
            "files": {
                "entryMap": str(map_catalog[0]["map"]),
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
    # Planning evidence: ignored by ImportProfile.load, consumed by humans and
    # by the prop-binding lane that still has to close these gaps per map.
    profile["planning_evidence"] = {
        "schema": "openbfme.map-profile-planning-evidence",
        "schemaVersion": 0,
        "rejectedMaps": build_rejections,
        "unboundObjectTypes": {
            slug: {"count": len(types), "typeNames": types}
            for slug, types in sorted(unbound_types.items())
        },
        "objectBindingStatus": (
            "no-model-or-structure-bindings-declared-by-this-lane"
        ),
    }
    return profile


def build_five_map_profile(catalog: InstallCatalog) -> dict[str, Any]:
    """Return the legacy private five-map development profile."""

    profile = build_map_profile(
        catalog,
        FIVE_MAP_TARGETS,
        profile_id="bfme2-five-maps-106-generated",
        title="BFME II 1.06 five-map private generated pack",
        pack_id="bfme2-five-maps-106-private",
        pack_version="1.06-generated-v0",
        terrain_output="assets/terrain/five-maps",
    )
    # The five-map development pack has always entered on Fords.
    profile["pack"]["files"]["entryMap"] = "maps/fords-of-isen-ii/map.json"
    return profile


def build_skirmish_map_profile(
    catalog: InstallCatalog, *, game: str = "bfme2", strict: bool = False
) -> dict[str, Any]:
    """Return the profile for every skirmish map this install actually ships."""

    targets, rejections = discover_registry_map_targets(catalog)
    return build_map_profile(
        catalog,
        targets,
        profile_id=f"{game}-skirmish-maps-generated",
        title=f"{game} skirmish map private generated pack",
        pack_id=f"{game}-skirmish-maps-private",
        pack_version="skirmish-generated-v0",
        terrain_output="assets/terrain/skirmish-maps",
        priority=905,
        rejections=rejections,
        strict=strict,
    )
