"""Read-only, payload-free census of the frozen retail multiplayer-map corpus."""

from __future__ import annotations

from collections import Counter, defaultdict
import hashlib
from pathlib import Path
import re
from typing import Any

from .big import BigArchive, sha256_file
from .catalog import InstallCatalog
from .paths import safe_relative_parts
from .sage_map import MAX_SOURCE_BYTES, census_sage_map_bytes, parse_sage_map_bytes


MAPCACHE_VIRTUAL_PATH = "maps/mapcache.ini"
MAX_MAPCACHE_BYTES = 2 * 1024 * 1024
_MAPCACHE_HEADER = re.compile(r"^MapCache\s+(.+?)\s*$", re.IGNORECASE)
_MAPCACHE_FIELD = re.compile(r"^([A-Za-z][A-Za-z0-9_]*)\s*=\s*(.*?)\s*$")
_MAPCACHE_ESCAPE = re.compile(r"_([0-9A-Fa-f]{2})")
_ALLOWED_MAPCACHE_FIELDS = {
    "description",
    "displayname",
    "extentmax",
    "extentmin",
    "filecrc",
    "filesize",
    "initialcameraposition",
    "ismultiplayer",
    "isofficial",
    "isscenariomp",
    "numplayers",
    "player_1_start",
    "player_2_start",
    "player_3_start",
    "player_4_start",
    "player_5_start",
    "player_6_start",
    "player_7_start",
    "player_8_start",
    "timestamphi",
    "timestamplo",
}


def _decode_mapcache_path(value: str) -> str:
    decoded = _MAPCACHE_ESCAPE.sub(
        lambda match: chr(int(match.group(1), 16)), value.strip().strip('"')
    )
    normalized = decoded.replace("\\", "/").casefold()
    safe_relative_parts(normalized)
    if not normalized.endswith(".map"):
        raise ValueError("mapcache record does not resolve to a .map virtual path")
    return normalized


def _mapcache_bool(value: str, field: str) -> bool:
    folded = value.strip().casefold()
    if folded == "yes":
        return True
    if folded == "no":
        return False
    raise ValueError(f"mapcache {field} must be Yes or No")


def parse_mapcache_bytes(source: bytes) -> list[dict[str, Any]]:
    """Parse only the registry facts needed to define corpus membership."""

    if len(source) > MAX_MAPCACHE_BYTES:
        raise ValueError(f"mapcache exceeds {MAX_MAPCACHE_BYTES} byte limit")
    if b"\0" in source:
        raise ValueError("mapcache contains a NUL byte")
    try:
        text = source.decode("cp1252")
    except UnicodeDecodeError as exc:
        raise ValueError("mapcache has an unsupported encoding") from exc

    records: list[dict[str, Any]] = []
    current_path: str | None = None
    current_fields: dict[str, str] = {}
    for line_number, raw_line in enumerate(text.splitlines(), 1):
        line = raw_line.split(";", 1)[0].strip()
        if not line:
            continue
        header = _MAPCACHE_HEADER.fullmatch(line)
        if header:
            if current_path is not None:
                raise ValueError(
                    f"mapcache record before line {line_number} is missing End"
                )
            current_path = _decode_mapcache_path(header.group(1))
            current_fields = {}
            continue
        if line.casefold() == "end":
            if current_path is None:
                raise ValueError(f"unexpected mapcache End at line {line_number}")
            required = {"ismultiplayer", "isofficial", "isscenariomp", "numplayers"}
            missing = sorted(required - set(current_fields))
            if missing:
                raise ValueError(
                    "mapcache record is missing required field(s): " + ", ".join(missing)
                )
            try:
                player_count = int(current_fields["numplayers"], 10)
            except ValueError as exc:
                raise ValueError("mapcache numPlayers must be an integer") from exc
            if not 1 <= player_count <= 8:
                raise ValueError("mapcache numPlayers is outside 1..8")
            records.append(
                {
                    "virtualPath": current_path,
                    "isMultiplayer": _mapcache_bool(
                        current_fields["ismultiplayer"], "isMultiplayer"
                    ),
                    "isOfficial": _mapcache_bool(
                        current_fields["isofficial"], "isOfficial"
                    ),
                    "isScenarioMp": _mapcache_bool(
                        current_fields["isscenariomp"], "isScenarioMP"
                    ),
                    "playerCount": player_count,
                }
            )
            current_path = None
            current_fields = {}
            continue
        if current_path is None:
            raise ValueError(f"mapcache content outside a record at line {line_number}")
        field = _MAPCACHE_FIELD.fullmatch(line)
        if field is None:
            raise ValueError(f"unsupported mapcache syntax at line {line_number}")
        key = field.group(1).casefold()
        if key not in _ALLOWED_MAPCACHE_FIELDS:
            raise ValueError(f"unsupported mapcache field at line {line_number}: {key}")
        if key in current_fields:
            raise ValueError(f"duplicate mapcache field at line {line_number}: {key}")
        current_fields[key] = field.group(2)
    if current_path is not None:
        raise ValueError("unterminated mapcache record")
    if not records:
        raise ValueError("mapcache contains no records")
    paths = [str(record["virtualPath"]) for record in records]
    if len(paths) != len(set(paths)):
        raise ValueError("mapcache contains duplicate virtual paths")
    return records


def _archive_info(catalog: InstallCatalog, relative: str) -> Any:
    for archive in catalog.archives:
        if archive.relative_path.casefold() == relative.casefold():
            return archive
    raise ValueError(f"catalog is missing archive metadata for {relative}")


def _neutral_set_sha256(domain: str, values: list[str]) -> str:
    """Content-address a source identity set without emitting source labels."""

    digest = hashlib.sha256()
    digest.update(domain.encode("ascii") + b"\0")
    for value in sorted(set(values), key=lambda item: (item.casefold(), item)):
        encoded = value.encode("utf-8")
        digest.update(len(encoded).to_bytes(4, "little"))
        digest.update(encoded)
    return digest.hexdigest()


def _setup_navigation_facts(parsed: Any, registry_player_capacity: int) -> dict[str, Any]:
    """Project cooked setup/waypoint contracts into payload-neutral census facts."""

    setup = parsed.setup
    library_lists = list(setup["libraryMapLists"])
    library_references = [
        reference
        for library_list in library_lists
        for reference in library_list["references"]
    ]
    library_identities = [
        str(reference["identitySha256"])
        for reference in library_references
        if reference.get("identitySha256") is not None
    ]
    duplicate_teams = list(setup["duplicateTeamNames"])
    duplicate_names = [str(item["name"]) for item in duplicate_teams]
    duplicate_entry_count = sum(len(item["teamIndices"]) for item in duplicate_teams)
    source_cross_checks = {
        str(name): bool(value) for name, value in setup["crossChecks"].items()
    }
    failed_cross_checks = sorted(
        (name for name, passed in source_cross_checks.items() if not passed),
        key=str.casefold,
    )
    declared_player_count = int(setup["declaredPlayerCount"])
    waypoint_edge_count = len(parsed.waypoint_edges)
    return {
        "status": "source-setup-and-waypoint-records-validated-runtime-pending",
        "registryPlayerCapacity": int(registry_player_capacity),
        "declaredPlayerCount": declared_player_count,
        "registryPlayerCapacityMatchesDeclaredStarts": (
            int(registry_player_capacity) == declared_player_count
        ),
        "lobbySlotCount": int(setup["lobbySlotCount"]),
        "scenarioPlayerCount": int(setup["scenarioPlayerCount"]),
        "teamCount": int(setup["teamCount"]),
        "extraTeamCount": int(setup["extraTeamCount"]),
        "duplicateTeamNameCount": len(duplicate_teams),
        "duplicateTeamEntryCount": duplicate_entry_count,
        "duplicateTeamNameSetSha256": _neutral_set_sha256(
            "openbfme.duplicate-team-name-set", duplicate_names
        ),
        "waypointCount": len(parsed.waypoints),
        "playerStartCount": len(parsed.player_starts),
        "waypointEdgeCount": waypoint_edge_count,
        "scriptListCount": int(setup["scriptListCount"]),
        "nonemptyScriptListCount": int(setup["nonemptyScriptListCount"]),
        "libraryListCount": len(library_lists),
        "libraryReferenceCount": len(library_references),
        "nonemptyLibraryReferenceCount": len(library_identities),
        "libraryReferenceIdentitySetSha256": _neutral_set_sha256(
            "openbfme.library-reference-identity-set", library_identities
        ),
        "libraryDependencyStatus": str(setup["libraryDependencyStatus"]),
        "libraryCycleValidationStatus": str(
            setup["libraryCycleValidationStatus"]
        ),
        "routingGraphStatus": (
            "source-edges-imported-runtime-pending"
            if waypoint_edge_count
            else "empty-no-authored-navmesh"
        ),
        "navigationMeshStatus": "not-generated-or-validated-by-census",
        "sourceCrossChecks": {
            "allPassed": not failed_cross_checks,
            "failed": failed_cross_checks,
            "teamNamesUnique": bool(source_cross_checks["teamNamesUnique"]),
        },
    }


def _aggregate_summary(maps: list[dict[str, Any]]) -> dict[str, Any]:
    signatures: dict[tuple[str, int], dict[str, Any]] = {}
    rejection_counts: Counter[str] = Counter()
    counters = Counter()
    player_counts: Counter[int] = Counter()
    dimensions: Counter[tuple[int, int, int]] = Counter()
    placements: list[int] = []
    unique_types: list[int] = []
    terrain_symbols: list[int] = []
    standing_water_counts: list[int] = []
    river_counts: list[int] = []
    setup_statuses: Counter[str] = Counter()
    library_dependency_statuses: Counter[str] = Counter()
    library_cycle_statuses: Counter[str] = Counter()
    routing_statuses: Counter[str] = Counter()
    navigation_mesh_statuses: Counter[str] = Counter()
    setup_totals: Counter[str] = Counter()
    for item in maps:
        player_counts[int(item["registry"]["playerCount"])] += 1
        strict = item["strictCook"]
        if bool(strict["accepted"]):
            counters["strictCookAccepted"] += 1
        else:
            counters["strictCookRejected"] += 1
            rejection_counts[str(strict["reason"])] += 1
        features = item["features"]
        height = features.get("height", {})
        if height:
            dimensions[
                (
                    int(height["width"]),
                    int(height["height"]),
                    int(height["borderWidth"]),
                )
            ] += 1
        objects = features.get("objects", {})
        if objects:
            placements.append(int(objects["placementCount"]))
            unique_types.append(int(objects["uniqueTypeCount"]))
        terrain = features.get("terrain", {})
        if terrain:
            terrain_symbols.append(int(terrain["textureSymbolCount"]))
        if features.get("standingWaterCount") is not None:
            standing_water_counts.append(int(features["standingWaterCount"]))
        if features.get("riverCount") is not None:
            river_counts.append(int(features["riverCount"]))
        if int(features.get("triggerAreaCount") or 0) > 0:
            counters["mapsWithNonzeroTriggers"] += 1
        if int(features.get("nonemptyScriptListCount") or 0) > 0:
            counters["mapsWithNonemptyScripts"] += 1
        if int(features.get("standingWaveAreaCount") or 0) > 0:
            counters["mapsWithStandingWaves"] += 1
        if int(features.get("waypointPathCount") or 0) > 0:
            counters["mapsWithWaypointPaths"] += 1
        setup_navigation = item["setupNavigation"]
        setup_status = str(setup_navigation["status"])
        setup_statuses[setup_status] += 1
        if setup_status == "source-setup-and-waypoint-records-validated-runtime-pending":
            counters["mapsWithValidatedSetupNavigationFacts"] += 1
            if not bool(
                setup_navigation["registryPlayerCapacityMatchesDeclaredStarts"]
            ):
                counters["mapsWithRegistryCapacityMismatch"] += 1
            if int(setup_navigation["duplicateTeamNameCount"]) > 0:
                counters["mapsWithDuplicateTeamNames"] += 1
            if int(setup_navigation["waypointEdgeCount"]) > 0:
                counters["mapsWithAuthoredWaypointEdges"] += 1
            for field in (
                "declaredPlayerCount",
                "lobbySlotCount",
                "scenarioPlayerCount",
                "teamCount",
                "extraTeamCount",
                "waypointCount",
                "playerStartCount",
                "waypointEdgeCount",
                "scriptListCount",
                "nonemptyScriptListCount",
                "libraryListCount",
                "libraryReferenceCount",
                "nonemptyLibraryReferenceCount",
            ):
                setup_totals[field] += int(setup_navigation[field])
            library_dependency_statuses[
                str(setup_navigation["libraryDependencyStatus"])
            ] += 1
            library_cycle_statuses[
                str(setup_navigation["libraryCycleValidationStatus"])
            ] += 1
            routing_statuses[str(setup_navigation["routingGraphStatus"])] += 1
        navigation_mesh_statuses[
            str(setup_navigation["navigationMeshStatus"])
        ] += 1
        statuses: set[str] = set()
        for chunk in item["chunks"]:
            key = (str(chunk["name"]), int(chunk["version"]))
            signature = signatures.setdefault(
                key,
                {
                    "name": key[0],
                    "version": key[1],
                    "mapPaths": set(),
                    "occurrenceCount": 0,
                },
            )
            signature["mapPaths"].add(str(item["virtualPath"]))
            signature["occurrenceCount"] += int(chunk["occurrences"])
            statuses.add(str(chunk["probeStatus"]))
        if "unclassified" in statuses:
            counters["mapsWithUnclassifiedChunks"] += 1
        if "unsupported-version" in statuses:
            counters["mapsWithUnsupportedVersions"] += 1
        if "semantic-rejected" in statuses or "mixed" in statuses:
            counters["mapsWithSemanticProbeRejections"] += 1

    chunk_rows: list[dict[str, Any]] = []
    for signature in signatures.values():
        paths = signature.pop("mapPaths")
        signature["mapCount"] = len(paths)
        chunk_rows.append(signature)
    chunk_rows.sort(key=lambda item: (item["name"].casefold(), item["version"]))
    return {
        "mapCount": len(maps),
        **{key: int(counters[key]) for key in (
            "strictCookAccepted",
            "strictCookRejected",
            "mapsWithNonzeroTriggers",
            "mapsWithNonemptyScripts",
            "mapsWithStandingWaves",
            "mapsWithWaypointPaths",
            "mapsWithUnclassifiedChunks",
            "mapsWithUnsupportedVersions",
            "mapsWithSemanticProbeRejections",
        )},
        "playerCountDistribution": [
            {"players": players, "mapCount": count}
            for players, count in sorted(player_counts.items())
        ],
        "mapDimensions": [
            {
                "width": key[0],
                "height": key[1],
                "borderWidth": key[2],
                "mapCount": count,
            }
            for key, count in sorted(dimensions.items())
        ],
        "featureRanges": {
            "objectPlacements": {
                "minimum": min(placements) if placements else None,
                "maximum": max(placements) if placements else None,
                "total": sum(placements),
            },
            "uniqueObjectTypes": {
                "minimum": min(unique_types) if unique_types else None,
                "maximum": max(unique_types) if unique_types else None,
            },
            "terrainSymbols": {
                "minimum": min(terrain_symbols) if terrain_symbols else None,
                "maximum": max(terrain_symbols) if terrain_symbols else None,
            },
            "standingWaterAreas": {
                "minimum": min(standing_water_counts) if standing_water_counts else None,
                "maximum": max(standing_water_counts) if standing_water_counts else None,
                "mapsWithNonzero": sum(value > 0 for value in standing_water_counts),
            },
            "rivers": {
                "minimum": min(river_counts) if river_counts else None,
                "maximum": max(river_counts) if river_counts else None,
                "mapsWithNonzero": sum(value > 0 for value in river_counts),
            },
        },
        "setupNavigation": {
            "validatedMapCount": int(
                counters["mapsWithValidatedSetupNavigationFacts"]
            ),
            "unavailableMapCount": (
                len(maps) - int(counters["mapsWithValidatedSetupNavigationFacts"])
            ),
            "registryCapacityMismatchMapCount": int(
                counters["mapsWithRegistryCapacityMismatch"]
            ),
            "duplicateTeamNameMapCount": int(
                counters["mapsWithDuplicateTeamNames"]
            ),
            "statusDistribution": [
                {"status": status, "mapCount": count}
                for status, count in sorted(
                    setup_statuses.items(), key=lambda item: item[0].casefold()
                )
            ],
            "totals": {
                field: int(setup_totals[field])
                for field in (
                    "declaredPlayerCount",
                    "lobbySlotCount",
                    "scenarioPlayerCount",
                    "teamCount",
                    "extraTeamCount",
                    "waypointCount",
                    "playerStartCount",
                    "scriptListCount",
                    "nonemptyScriptListCount",
                )
            },
            "libraryDependencies": {
                "listCount": int(setup_totals["libraryListCount"]),
                "referenceCount": int(setup_totals["libraryReferenceCount"]),
                "nonemptyReferenceCount": int(
                    setup_totals["nonemptyLibraryReferenceCount"]
                ),
                "dependencyStatusDistribution": [
                    {"status": status, "mapCount": count}
                    for status, count in sorted(
                        library_dependency_statuses.items(),
                        key=lambda item: item[0].casefold(),
                    )
                ],
                "cycleValidationStatusDistribution": [
                    {"status": status, "mapCount": count}
                    for status, count in sorted(
                        library_cycle_statuses.items(),
                        key=lambda item: item[0].casefold(),
                    )
                ],
            },
            "navigation": {
                "waypointCount": int(setup_totals["waypointCount"]),
                "playerStartCount": int(setup_totals["playerStartCount"]),
                "waypointEdgeCount": int(setup_totals["waypointEdgeCount"]),
                "mapsWithAuthoredWaypointEdges": int(
                    counters["mapsWithAuthoredWaypointEdges"]
                ),
                "routingGraphStatusDistribution": [
                    {"status": status, "mapCount": count}
                    for status, count in sorted(
                        routing_statuses.items(), key=lambda item: item[0].casefold()
                    )
                ],
                "navigationMeshStatusDistribution": [
                    {"status": status, "mapCount": count}
                    for status, count in sorted(
                        navigation_mesh_statuses.items(),
                        key=lambda item: item[0].casefold(),
                    )
                ],
            },
        },
        "chunkSignatures": chunk_rows,
        "strictCookRejections": [
            {"reason": reason, "mapCount": count}
            for reason, count in sorted(
                rejection_counts.items(), key=lambda item: item[0].casefold()
            )
        ],
    }


def census_multiplayer_maps(catalog: InstallCatalog) -> dict[str, Any]:
    """Census every shipped map selected by the frozen retail mapcache registry."""

    registry_entry = catalog.resolve_exact(MAPCACHE_VIRTUAL_PATH)
    if registry_entry is None:
        raise FileNotFoundError(f"retail registry is missing: {MAPCACHE_VIRTUAL_PATH}")
    registry_archive = catalog.open_archive_for(registry_entry)
    registry_source = registry_archive.read_entry(
        catalog.as_entry(registry_entry), max_bytes=MAX_MAPCACHE_BYTES
    )
    registry = parse_mapcache_bytes(registry_source)
    if not all(bool(item["isOfficial"]) for item in registry):
        raise ValueError("frozen mapcache contains a non-official record")
    if any(bool(item["isScenarioMp"]) for item in registry):
        raise ValueError("frozen mapcache contains an unsupported scenario-MP record")
    selected_registry = [item for item in registry if bool(item["isMultiplayer"])]
    if not selected_registry:
        raise ValueError("mapcache contains no multiplayer records")

    stale: list[dict[str, Any]] = []
    selected: list[tuple[dict[str, Any], Any]] = []
    for record in selected_registry:
        entry = catalog.resolve_exact(str(record["virtualPath"]))
        if entry is None:
            stale.append(
                {
                    "virtualPath": record["virtualPath"],
                    "playerCount": record["playerCount"],
                    "status": "registry-stale-missing-payload",
                }
            )
        else:
            selected.append((record, entry))
    if not selected:
        raise ValueError("no multiplayer registry record resolves to a shipped map")

    archive_cache: dict[str, BigArchive] = {}
    maps: list[dict[str, Any]] = []
    for record, entry in sorted(
        selected, key=lambda pair: str(pair[0]["virtualPath"]).casefold()
    ):
        key = entry.archive.casefold()
        archive = archive_cache.get(key)
        if archive is None:
            archive = catalog.open_archive_for(entry)
            archive_cache[key] = archive
        source = archive.read_entry(catalog.as_entry(entry), max_bytes=MAX_SOURCE_BYTES)
        facts = census_sage_map_bytes(source)
        if bool(facts["strictCook"]["accepted"]):
            setup_navigation = _setup_navigation_facts(
                parse_sage_map_bytes(source), int(record["playerCount"])
            )
        else:
            setup_navigation = {
                "status": "unavailable-strict-cook-rejected",
                "registryPlayerCapacity": int(record["playerCount"]),
                "navigationMeshStatus": "not-generated-or-validated-by-census",
            }
        facts.update(
            {
                "virtualPath": entry.name,
                "archive": entry.archive,
                "registry": {"playerCount": int(record["playerCount"])},
                "setupNavigation": setup_navigation,
            }
        )
        maps.append(facts)

    source_archives: list[dict[str, Any]] = []
    for key, archive in sorted(archive_cache.items()):
        info = _archive_info(catalog, archive.path.relative_to(catalog.install_root).as_posix())
        source_archives.append(
            {
                "archive": info.relative_path,
                "sha256": sha256_file(archive.path),
                "directorySha256": info.directory_sha256,
            }
        )
    stale.sort(key=lambda item: str(item["virtualPath"]).casefold())
    registry_info = _archive_info(catalog, registry_entry.archive)
    return {
        "schema": "openbfme.sage-multiplayer-map-census",
        "format": 1,
        "scope": {
            "kind": "retail-mapcache-registry",
            "registryVirtualPath": MAPCACHE_VIRTUAL_PATH,
            "registryArchive": registry_entry.archive,
            "registryArchiveDirectorySha256": registry_info.directory_sha256,
            "registrySha256": hashlib.sha256(registry_source).hexdigest(),
            "registryRecordCount": len(registry),
            "registryMultiplayerRecordCount": len(selected_registry),
            "selectedMapCount": len(maps),
            "staleMissingPayloadCount": len(stale),
            "selectionRule": "isMultiplayer=yes, isOfficial=yes, isScenarioMP=no, exact catalog winner exists",
        },
        "sourceArchives": source_archives,
        "staleRegistryRecords": stale,
        "summary": _aggregate_summary(maps),
        "maps": maps,
    }
