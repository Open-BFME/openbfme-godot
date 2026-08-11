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

from collections.abc import Callable, Iterable, Mapping
from dataclasses import dataclass
from pathlib import PurePosixPath
import re
from typing import Any

from .catalog import CatalogEntry, InstallCatalog
from .map_census import (
    MAPCACHE_VIRTUAL_PATH,
    MAX_MAPCACHE_BYTES,
    load_map_display_names,
    parse_mapcache_bytes,
    resolve_map_display_name,
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
AI_INITIALIZE_LIBRARY_PATH = "libraries/ai_initialize/ai_initialize.map"
AI_MP_INHERIT_LIBRARY_PATH = (
    "libraries/ai_mp_inherit_management/ai_mp_inherit_management.map"
)
GOLLUM_SPAWN_LIBRARY_PATH = "libraries/lib_gollumspawn/lib_gollumspawn.map"
from .profile import is_canonical_multiplayer_map_virtual_path


def _gollum_spawn_library_referenced(setup: Mapping[str, Any]) -> bool:
    """Whether a parsed map delegates its Gollum scripts to libraries.big.

    Some retail maps inline Lib_GollumSpawn.  Adding the library to those maps
    as well would run the spawn logic twice, so composition follows the exact
    LibraryMapLists dependency rather than applying a corpus-wide default.
    """

    lists = setup.get("libraryMapLists")
    if not isinstance(lists, list):
        raise ValueError("parsed map libraryMapLists contract is invalid")
    references = [
        reference
        for row in lists
        if isinstance(row, Mapping)
        for reference in row.get("references", [])
        if isinstance(reference, Mapping)
    ]
    return any(
        str(reference.get("normalized", "")).casefold()
        == GOLLUM_SPAWN_LIBRARY_PATH
        for reference in references
    )

#: Retail map directories are named ``map <kind> <name>``; the multiplayer
#: skirmish set is exactly the ``mp`` kind.
SKIRMISH_DIRECTORY_PREFIX = "map mp"

_DIRECTORY_KINDS = frozenset({"mp", "wor", "sp", "good", "evil", "ang", "cin"})
_LOWERCASE_WORDS = frozenset({"of", "the", "and", "at", "on", "in", "to"})
_ROMAN_NUMERALS = frozenset({"ii", "iii", "iv", "vi", "vii", "viii", "ix", "xi"})

#: Every retail map category this lane can discover, keyed by the shipped
#: ``maps/<directory>`` naming convention.  ``mapcache.ini`` registers all of
#: them (BFME2 72 records, RotWK 2.01 122 records); only the ``mp`` and ``wor``
#: kinds carry ``isMultiplayer = Yes``.
SKIRMISH_CATEGORY = "skirmish"
WOTR_BATTLE_CATEGORY = "wotr-battle"
CAMPAIGN_CATEGORY = "campaign"
CINEMATIC_CATEGORY = "cinematic"
TUTORIAL_CATEGORY = "tutorial"
SHELL_CATEGORY = "shell"
SYSTEM_CATEGORY = "system"

MAP_CATEGORIES = (
    SKIRMISH_CATEGORY,
    WOTR_BATTLE_CATEGORY,
    CAMPAIGN_CATEGORY,
    CINEMATIC_CATEGORY,
    TUTORIAL_CATEGORY,
    SHELL_CATEGORY,
    SYSTEM_CATEGORY,
)

#: Retail castle/siege maps which are lobby-authored multiplayer maps despite
#: living under ``map wor`` directories.  The old directory-only admission
#: rule treated every one as strategic-layer-only.  This finite census is
#: pinned to RotWK's official map registry and parsed object documents; it is
#: deliberately not a wall-name heuristic that could sweep ordinary WOTR maps
#: into the skirmish lobby.
_CASTLE_GAMEPLAY_BLOCKERS = (
    "walkable-walls",
    "defendable-gates",
    "wall-garrisons",
    "wall-mounted-defenses",
    "skirmish-ai-libraries",
)


def _castle_runtime_contract() -> dict[str, Any]:
    return {
        "family": "retail-castle-siege-skirmish",
        "gameplayStatus": "blocked-named-gaps",
        "blockers": list(_CASTLE_GAMEPLAY_BLOCKERS),
        "admissionPolicy": "document-loadable-lobby-visible-gameplay-fails-closed",
    }


CASTLE_SIEGE_MAPS: dict[str, dict[str, Any]] = {
    path: {
        "displayName": display_name,
        "playerCount": player_count,
        "runtimeContract": _castle_runtime_contract(),
    }
    for path, display_name, player_count in (
        ("maps/map wor ang carn dum/map wor ang carn dum.map", "Carn Dum", 2),
        ("maps/map wor ang fornost/map wor ang fornost.map", "Fornost", 3),
        ("maps/map wor black gate/map wor black gate.map", "Black Gate", 3),
        ("maps/map wor dol guldur/map wor dol guldur.map", "Dol Guldur", 4),
        ("maps/map wor erebor/map wor erebor.map", "Erebor", 2),
        ("maps/map wor grey havens/map wor grey havens.map", "Grey Havens", 3),
        ("maps/map wor helms deep/map wor helms deep.map", "Helm's Deep", 4),
        ("maps/map wor isengard/map wor isengard.map", "Isengard", 2),
        ("maps/map wor minas morgul/map wor minas morgul.map", "Minas Morgul", 3),
        ("maps/map wor minas tirith/map wor minas tirith.map", "Minas Tirith", 4),
    )
}


def castle_siege_map_evidence(virtual_path: str) -> dict[str, Any] | None:
    """Return the pinned retail castle admission row for an exact map path."""

    return CASTLE_SIEGE_MAPS.get(virtual_path.replace("\\", "/").casefold())

#: Categories whose registry ``isMultiplayer`` flag is meaningful, and whose
#: ``numPlayers`` therefore has to agree with the authored player starts.
_MULTIPLAYER_CATEGORIES = frozenset({SKIRMISH_CATEGORY, WOTR_BATTLE_CATEGORY})

#: The SAGE map profile each category is cooked with.  Only lobby categories
#: enforce the lobby start rules; a campaign, cinematic, tutorial or shell map
#: legitimately ships zero ``Player_N_Start`` waypoints.
_CATEGORY_MAP_KINDS = {
    SKIRMISH_CATEGORY: "multiplayer",
    WOTR_BATTLE_CATEGORY: "multiplayer",
    CAMPAIGN_CATEGORY: "scenario",
    CINEMATIC_CATEGORY: "scenario",
    TUTORIAL_CATEGORY: "scenario",
    SHELL_CATEGORY: "scenario",
    SYSTEM_CATEGORY: "scenario",
}

_KIND_CATEGORIES = {
    "mp": SKIRMISH_CATEGORY,
    "wor": WOTR_BATTLE_CATEGORY,
    "good": CAMPAIGN_CATEGORY,
    "evil": CAMPAIGN_CATEGORY,
    "ang": CAMPAIGN_CATEGORY,
}


def classify_map_directory(directory: str) -> str:
    """Return the retail category of one ``maps/<directory>`` name.

    The classification is purely the shipped directory naming convention; no
    map bytes are read.  ``map wor ang fornost`` is a WOTR battle map for the
    Angmar campaign's strategic layer and stays in ``wotr-battle``, while
    ``map ang fornost`` is the campaign mission itself.
    """

    words = [word for word in directory.casefold().split() if word]
    if not words:
        raise ValueError(f"empty map directory name: {directory!r}")
    if words[0] == "cin":
        return CINEMATIC_CATEGORY
    if words[0] == "map":
        rest = words[1:]
        if not rest:
            raise ValueError(f"map directory has no name component: {directory!r}")
        category = _KIND_CATEGORIES.get(rest[0])
        if category is not None:
            return category
        if rest[-1] == "tutorial":
            return TUTORIAL_CATEGORY
        return SYSTEM_CATEGORY
    if words[0].startswith("shellmap"):
        return SHELL_CATEGORY
    return SYSTEM_CATEGORY


#: Where a target's ``display_name`` came from.  ``authored`` is retail's own
#: ``data/lotr.str`` string table (or a hand-authored constant); a name that had
#: to be title-cased from the retail directory is recorded so a pack can never
#: quietly ship an invented name ("Fall Back 4p" for retail's "Stonewain
#: Valley") without the planning evidence saying so.
DISPLAY_NAME_AUTHORED = "authored"
DISPLAY_NAME_DERIVED = "derived-from-directory"


@dataclass(frozen=True, slots=True)
class MapTarget:
    slug: str
    display_name: str
    virtual_path: str
    #: ``numPlayers`` from ``mapcache.ini`` when the target came from the
    #: registry; ``None`` for hand-authored targets.
    registry_player_count: int | None = None
    category: str = SKIRMISH_CATEGORY
    display_name_source: str = DISPLAY_NAME_AUTHORED


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
        category=WOTR_BATTLE_CATEGORY,
    ),
    MapTarget(
        "mount-doom",
        "Mount Doom",
        "maps/map wor mount doom/map wor mount doom.map",
        category=WOTR_BATTLE_CATEGORY,
    ),
    MapTarget(
        "dagorlad",
        "Dagorlad",
        "maps/map wor dagorlad/map wor dagorlad.map",
        category=WOTR_BATTLE_CATEGORY,
    ),
    MapTarget(
        "mordor",
        "Mordor",
        "maps/map wor mordor/map wor mordor.map",
        category=WOTR_BATTLE_CATEGORY,
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
    words = [word for word in words if word.strip("-_")]
    if not words:
        raise ValueError(f"map directory has no name component: {directory!r}")
    return tuple(words)


def _slugify(words: Iterable[str]) -> str:
    slug = re.sub(r"[^a-z0-9]+", "-", " ".join(words).casefold()).strip("-")
    if not slug:
        raise ValueError(f"map name has no slug component: {list(words)!r}")
    return slug


def _slug(directory: str) -> str:
    """Return the pack-stable slug for one retail map directory.

    The skirmish set keeps its historic bare slug (``fords-of-isen-ii``) so
    already-cooked map ids never move.  Every other category retains its retail
    kind word, because the retail corpus reuses map names freely across
    categories: ``map good erebor``, ``map evil erebor`` and ``map wor erebor``
    are three different maps, and RotWK ships both ``map ang fornost`` (the
    campaign mission) and ``map wor ang fornost`` (its WOTR battle map).
    """

    words = [word for word in directory.casefold().split() if word]
    if classify_map_directory(directory) == SKIRMISH_CATEGORY:
        return _slugify(_name_words(directory))
    if words and words[0] == "map":
        words = words[1:]
    return _slugify(words)


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
    categories: Iterable[str] = (SKIRMISH_CATEGORY,),
    directory_prefix: str | None = None,
) -> tuple[tuple[MapTarget, ...], tuple[dict[str, Any], ...]]:
    """Discover the install's shipped map set from ``maps/mapcache.ini``.

    ``categories`` selects which retail map families to admit; the registry
    registers every family, not only ``map mp``.  ``directory_prefix`` remains
    available as an additional exact filter for callers that want one retail
    directory family.

    Returns the resolvable targets plus a rejection record for every registry
    row that names a map this install cannot convert (stale registry entries,
    unofficial or scenario-MP rows, and multiplayer rows whose payload is
    absent).  Nothing is substituted for a rejected map.  The ``map.ini`` /
    ``_art.tga`` / ``_pic.tga`` companions are recorded as present or absent
    rather than required: 24 of the 68 BFME2 map directories and 60 of the 179
    RotWK ones ship without a ``_pic.tga``, and campaign/cinematic directories
    routinely ship without a preview at all.
    """

    selected_categories = frozenset(categories)
    unknown = sorted(selected_categories - set(MAP_CATEGORIES))
    if unknown:
        raise ValueError("unknown map categories: " + ", ".join(unknown))
    if not selected_categories:
        raise ValueError("map discovery requires at least one category")
    _, registry_source = _read(
        catalog, MAPCACHE_VIRTUAL_PATH, "map registry", MAX_MAPCACHE_BYTES
    )
    prefix = (directory_prefix or "").casefold().strip()
    # Retail authors every map's readable name in ``data/lotr.str`` and points
    # each registry row at it. Title-casing the directory instead produces
    # "Fall Back 4p" for a map retail calls "Stonewain Valley", so the authored
    # string wins wherever the registry supplies a key.
    display_names = load_map_display_names(catalog)
    targets: list[MapTarget] = []
    rejections: list[dict[str, Any]] = []
    seen_slugs: dict[str, str] = {}
    for record in parse_mapcache_bytes(registry_source):
        virtual_path = str(record["virtualPath"])
        directory = _map_directory(virtual_path)
        category = classify_map_directory(directory)
        if castle_siege_map_evidence(virtual_path) is not None:
            category = SKIRMISH_CATEGORY
        if category not in selected_categories:
            continue
        if prefix and not directory.casefold().startswith(prefix):
            continue
        if not bool(record["isOfficial"]) or bool(record["isScenarioMp"]):
            rejections.append(
                {
                    "virtualPath": virtual_path,
                    "category": category,
                    "status": "registry-not-official-or-scenario-mp",
                }
            )
            continue
        if category in _MULTIPLAYER_CATEGORIES and not bool(record["isMultiplayer"]):
            rejections.append(
                {
                    "virtualPath": virtual_path,
                    "category": category,
                    "status": "registry-not-multiplayer",
                }
            )
            continue
        slug = _slug(directory)
        if slug in seen_slugs:
            raise ValueError(
                f"map slug {slug!r} is claimed by {seen_slugs[slug]!r} and {virtual_path!r}"
            )
        if catalog.resolve_exact(virtual_path) is None:
            rejections.append(
                {
                    "virtualPath": virtual_path,
                    "category": category,
                    "status": "registry-stale-missing-payload",
                    "missing": [virtual_path],
                }
            )
            continue
        seen_slugs[slug] = virtual_path
        authored_name = resolve_map_display_name(
            display_names, str(record.get("displayNameKey") or "")
        )
        targets.append(
            MapTarget(
                slug,
                authored_name or _display_name(directory),
                virtual_path,
                int(record["playerCount"]),
                category=category,
                display_name_source=(
                    DISPLAY_NAME_AUTHORED if authored_name else DISPLAY_NAME_DERIVED
                ),
            )
        )
    targets.sort(key=lambda target: target.slug)
    return tuple(targets), tuple(rejections)


def discover_catalog_only_map_targets(
    catalog: InstallCatalog,
    *,
    categories: Iterable[str] = (SKIRMISH_CATEGORY,),
) -> tuple[tuple[MapTarget, ...], tuple[dict[str, Any], ...]]:
    """Discover shipped map directories that the registry does not register.

    RotWK 2.01's ``_patch201maps.big`` replaces ``mapcache.ini`` wholesale, so a
    layered install still ships six BFME2 map payloads (both tutorials, both
    shell maps, Weather Hills and WOTR Gondor) that no registry row names.  Such
    a target carries no ``numPlayers`` cross-check; its player capacity comes
    from the authored player starts alone.
    """

    selected_categories = frozenset(categories)
    unknown = sorted(selected_categories - set(MAP_CATEGORIES))
    if unknown:
        raise ValueError("unknown map categories: " + ", ".join(unknown))
    _, registry_source = _read(
        catalog, MAPCACHE_VIRTUAL_PATH, "map registry", MAX_MAPCACHE_BYTES
    )
    registered = {
        str(record["virtualPath"]).casefold()
        for record in parse_mapcache_bytes(registry_source)
    }
    targets: list[MapTarget] = []
    rejections: list[dict[str, Any]] = []
    seen_slugs: dict[str, str] = {}
    for entry in catalog.entries:
        name = entry.name
        parts = PurePosixPath(name).parts
        if len(parts) != 3 or parts[0].casefold() != "maps":
            continue
        if PurePosixPath(name).suffix.casefold() != ".map":
            continue
        if name.casefold() in registered:
            continue
        directory = parts[1]
        try:
            category = classify_map_directory(directory)
            slug = _slug(directory)
        except ValueError as exc:
            rejections.append(
                {
                    "virtualPath": name,
                    "status": "unclassifiable-map-directory",
                    "reason": str(exc),
                }
            )
            continue
        if category not in selected_categories:
            continue
        if slug in seen_slugs:
            raise ValueError(
                f"map slug {slug!r} is claimed by {seen_slugs[slug]!r} and {name!r}"
            )
        seen_slugs[slug] = name
        targets.append(
            MapTarget(
                slug,
                _display_name(directory),
                name,
                None,
                category=category,
                # No registry row means no ``displayName`` key to resolve, so an
                # unregistered payload's name is always a derivation and says so.
                display_name_source=DISPLAY_NAME_DERIVED,
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
) -> tuple[CatalogEntry | None, CatalogEntry | None, CatalogEntry | None]:
    """Resolve the optional ``map.ini`` / ``_art.tga`` / ``_pic.tga`` companions.

    Only the map binary itself is required.  A missing companion is recorded as
    an absent optional resource, never substituted, and never a rejection: the
    retail corpus ships plenty of maps without a preview or art thumbnail.
    """

    map_ini = (PurePosixPath(target.virtual_path).parent / "map.ini").as_posix()
    return (
        catalog.resolve_exact(map_ini),
        catalog.resolve_exact(_companion(target, "_art.tga")),
        catalog.resolve_exact(_companion(target, "_pic.tga")),
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


def _no_object_bindings(
    target: MapTarget, parsed: ParsedSageMap
) -> tuple[list[dict[str, Any]], dict[str, Any] | None, dict[str, Any] | None]:
    """Default binder: declare no bindings, exactly as this lane always has."""

    return [], None, None


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
    binder: Callable[
        [MapTarget, ParsedSageMap],
        tuple[list[dict[str, Any]], dict[str, Any] | None, dict[str, Any] | None],
    ] = _no_object_bindings,
) -> dict[str, Any]:
    """Return a deterministic profile generated only from exact retail facts.

    ``binder`` plans one map's ``options.objectBindings`` and the conversion
    resources those bindings depend on.  It defaults to declaring none, so a
    caller without an extracted effective-assets tree produces exactly the
    profile this lane produced before automatic prop binding existed.
    """

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
    derived_name_slugs: list[str] = []
    build_rejections: list[dict[str, Any]] = list(rejections)
    terrain_materials_output = f"{terrain_output}/terrain-materials.json"
    prop_binding_evidence: dict[str, Any] = {}
    binding_failures: list[dict[str, Any]] = []
    # Prop conversion resources are keyed by their exact retail source, so two
    # maps that place the same tree declare byte-identical resources. One owner
    # is kept; a same-id resource that is not identical is a real conflict.
    shared_binding_resources: dict[str, dict[str, Any]] = {}

    for target in targets:
        try:
            map_entry, map_source = _read(
                catalog,
                target.virtual_path,
                f"{target.display_name} map",
                MAX_SOURCE_BYTES,
            )
            map_kind = _CATEGORY_MAP_KINDS[target.category]
            parsed = parse_sage_map_bytes(map_source, profile=map_kind)
            player_count = len(parsed.player_starts)
            # ``numPlayers`` is the lobby capacity, so it is only a binding
            # cross-check for the categories that use a lobby.  A campaign or
            # cinematic map's registry row still reports a number; it is
            # recorded beside the authored start count instead of gating the
            # map, because the authored starts are the exact fact.
            registry_agrees: bool | None = None
            if target.registry_player_count is not None:
                registry_agrees = target.registry_player_count == player_count
                if not registry_agrees and target.category in _MULTIPLAYER_CATEGORIES:
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

        metadata: dict[str, Any] = {
            "id": map_id,
            "displayName": target.display_name,
            "terrainMaterials": terrain_materials_output,
        }
        castle_evidence = castle_siege_map_evidence(target.virtual_path)
        if castle_evidence is not None:
            metadata["castleSiege"] = dict(castle_evidence["runtimeContract"])
        if preview_entry is not None:
            metadata["preview"] = preview_output
        if art_entry is not None:
            metadata["art"] = art_output
        map_resources: list[dict[str, Any]] = [
            {
                "id": f"map-{target.slug}-binary",
                "kind": "map",
                "converter": "sage-map",
                "patterns": [map_entry.name],
                "output": output_root,
                "limit": 1,
                "expected_count": 1,
                "options": {
                    "metadata": metadata,
                    "expected": _expected(parsed),
                },
            }
        ]
        if (
            target.category == SKIRMISH_CATEGORY
            and is_canonical_multiplayer_map_virtual_path(map_entry.name)
        ):
            script_libraries = [
                AI_INITIALIZE_LIBRARY_PATH,
                AI_MP_INHERIT_LIBRARY_PATH,
            ]
            if _gollum_spawn_library_referenced(parsed.setup):
                script_libraries.append(GOLLUM_SPAWN_LIBRARY_PATH)
            map_resources.append(
                {
                    "id": f"map-{target.slug}-scripts",
                    "kind": "map",
                    "converter": "sage-script-composite",
                    "patterns": [
                        map_entry.name,
                        *script_libraries,
                    ],
                    "output": f"{output_root}/scripts.json",
                    "limit": 1 + len(script_libraries),
                    "expected_count": 1 + len(script_libraries),
                    "options": {
                        "mapVirtualPath": map_entry.name,
                        "libraryVirtualPaths": script_libraries,
                    },
                }
            )
        if map_kind != "multiplayer":
            map_resources[0]["options"]["profile"] = map_kind
        if map_ini_entry is not None:
            map_resources.append(
                {
                    "id": f"map-{target.slug}-config",
                    "kind": "map",
                    "converter": "hash-only",
                    "patterns": [map_ini_entry.name],
                    "limit": 1,
                    "expected_count": 1,
                }
            )
        if art_entry is not None:
            map_resources.append(
                {
                    "id": f"map-{target.slug}-art",
                    "kind": "ui",
                    "converter": "texture",
                    "patterns": [art_entry.name],
                    "output": art_output,
                    "limit": 1,
                    "expected_count": 1,
                }
            )
        if preview_entry is not None:
            map_resources.append(
                {
                    "id": f"map-{target.slug}-preview",
                    "kind": "ui",
                    "converter": "texture",
                    "patterns": [preview_entry.name],
                    "output": preview_output,
                    "limit": 1,
                    "expected_count": 1,
                }
            )
        try:
            binding_resources, binding_rows, binding_evidence = binder(target, parsed)
        except (SageMapError, ValueError) as exc:
            if strict:
                raise
            binding_resources, binding_rows, binding_evidence = [], None, None
            binding_failures.append(
                {
                    "slug": target.slug,
                    "category": target.category,
                    "status": "prop-binding-rejected",
                    "reason": str(exc),
                }
            )
        # A prop conversion resource is keyed by its exact retail source, so two
        # maps that place the same prop declare byte-identical resources and one
        # owner is kept. The animated planner, though, factors a hierarchy W3D
        # shared between two animated targets *in the same map* into its own
        # resource, which narrows that map's bundle. A map whose bundle shape
        # therefore disagrees with an already-declared one cannot be merged
        # without silently changing what the earlier map cooks, so this map's
        # bindings are dropped whole and recorded rather than half-applied.
        conflict = next(
            (
                str(resource["id"])
                for resource in binding_resources
                if str(resource["id"]) in shared_binding_resources
                and shared_binding_resources[str(resource["id"])] != resource
            ),
            None,
        )
        if conflict is not None:
            if strict:
                raise ValueError(
                    f"prop conversion resource {conflict!r} is declared with "
                    "two different definitions"
                )
            binding_resources, binding_rows, binding_evidence = [], None, None
            binding_failures.append(
                {
                    "slug": target.slug,
                    "category": target.category,
                    "status": "prop-binding-resource-conflict",
                    "resourceId": conflict,
                }
            )
        if binding_rows is not None:
            map_resources[0]["options"]["objectBindings"] = binding_rows
        for resource in binding_resources:
            resource_id = str(resource["id"])
            if resource_id not in shared_binding_resources:
                shared_binding_resources[resource_id] = resource
                map_resources.append(resource)
        if binding_evidence is not None:
            prop_binding_evidence[target.slug] = binding_evidence
        resources.extend(map_resources)
        row: dict[str, Any] = {
            "id": map_id,
            "displayName": target.display_name,
            "category": target.category,
            "map": f"{output_root}/map.json",
            "terrainMaterials": terrain_materials_output,
            "playerCount": player_count,
            "routingGraphStatus": (
                "empty-no-authored-navmesh"
                if not parsed.waypoint_edges
                else "source-waypoint-edges-present-runtime-pending"
            ),
            "navigationMeshStatus": "not-generated-or-validated-by-map-profile",
        }
        if castle_evidence is not None:
            row["castleSiege"] = dict(castle_evidence["runtimeContract"])
        if preview_entry is not None:
            row["preview"] = preview_output
        if art_entry is not None:
            row["art"] = art_output
        if target.registry_player_count is not None:
            row["registryPlayerCount"] = int(target.registry_player_count)
            row["registryPlayerCountAgrees"] = bool(registry_agrees)
        if target.display_name_source == DISPLAY_NAME_DERIVED:
            derived_name_slugs.append(target.slug)
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
    category_counts: dict[str, int] = {}
    for row in map_catalog:
        category = str(row["category"])
        category_counts[category] = category_counts.get(category, 0) + 1
    # Planning evidence: ignored by ImportProfile.load, consumed by humans and
    # by the lanes that still have to close the remaining gaps per map.
    profile["planning_evidence"] = {
        "schema": "openbfme.map-profile-planning-evidence",
        "schemaVersion": 0,
        "categoryCounts": [
            {"category": category, "mapCount": category_counts[category]}
            for category in MAP_CATEGORIES
            if category in category_counts
        ],
        "rejectedMaps": build_rejections,
        # Authored names come from retail's own string table; a derived fallback
        # is recorded per map so a fresh cook can never quietly reintroduce
        # directory-derived names ("Fall Back 4p" for retail's "Stonewain
        # Valley") without this evidence saying so.
        "displayNameSource": {
            "authoredTable": "data/lotr.str",
            "authoredKeyField": "mapcache displayName",
            "authoredCount": len(map_catalog) - len(derived_name_slugs),
            "derivedFromDirectoryCount": len(derived_name_slugs),
            "derivedFromDirectorySlugs": sorted(derived_name_slugs),
        },
        "unboundObjectTypes": {
            slug: {"count": len(types), "typeNames": types}
            for slug, types in sorted(unbound_types.items())
        },
        "objectBindingStatus": (
            "generic-visual-closure-bindings-declared-per-map"
            if prop_binding_evidence
            else "no-model-or-structure-bindings-declared-by-this-lane"
        ),
        "propBindings": {
            slug: prop_binding_evidence[slug]
            for slug in sorted(prop_binding_evidence)
        },
        "propBindingFailures": binding_failures,
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


#: Named map sets a caller can ask for, and the categories each admits.
MAP_SETS: dict[str, tuple[str, ...]] = {
    "skirmish": (SKIRMISH_CATEGORY,),
    "campaign": (CAMPAIGN_CATEGORY,),
    "cinematic": (CINEMATIC_CATEGORY,),
    "wotr-battle": (WOTR_BATTLE_CATEGORY,),
    "tutorial": (TUTORIAL_CATEGORY,),
    "playable": (SKIRMISH_CATEGORY, WOTR_BATTLE_CATEGORY),
    "single-player": (CAMPAIGN_CATEGORY, CINEMATIC_CATEGORY, TUTORIAL_CATEGORY),
    "all": MAP_CATEGORIES,
}


def make_effective_assets_binder(
    effective_assets_root: Any,
    effective_assets_manifest: dict[str, Any],
) -> Callable[
    [MapTarget, ParsedSageMap],
    tuple[list[dict[str, Any]], dict[str, Any] | None, dict[str, Any] | None],
]:
    """Return a binder that plans real prop bindings from an assets tree."""

    from .map_prop_bindings import build_map_prop_binding_plan

    # One owner per retail texture source across the whole profile, not per map.
    texture_owners: dict[str, str] = {}

    def bind(
        target: MapTarget, parsed: ParsedSageMap
    ) -> tuple[list[dict[str, Any]], dict[str, Any] | None, dict[str, Any] | None]:
        plan = build_map_prop_binding_plan(
            parsed,
            effective_assets_root=effective_assets_root,
            effective_assets_manifest=effective_assets_manifest,
            map_pattern=target.virtual_path,
            output_root=f"maps/{target.slug}",
            texture_owners=texture_owners,
        )
        return (
            list(plan["resources"]),
            dict(plan["objectBindings"]),
            dict(plan["evidence"]),
        )

    return bind


def build_category_map_profile(
    catalog: InstallCatalog,
    *,
    game: str = "bfme2",
    map_set: str = "skirmish",
    strict: bool = False,
    include_unregistered: bool = True,
    target_limit: int | None = None,
    binder: Callable[
        [MapTarget, ParsedSageMap],
        tuple[list[dict[str, Any]], dict[str, Any] | None, dict[str, Any] | None],
    ] = _no_object_bindings,
) -> dict[str, Any]:
    """Return the profile for one named retail map set this install ships."""

    if map_set not in MAP_SETS:
        raise ValueError(
            f"unknown map set {map_set!r}; expected one of "
            + ", ".join(sorted(MAP_SETS))
        )
    if target_limit is not None and target_limit <= 0:
        raise ValueError("target_limit must be greater than zero")
    categories = MAP_SETS[map_set]
    targets, rejections = discover_registry_map_targets(
        catalog, categories=categories
    )
    if include_unregistered:
        extra_targets, extra_rejections = discover_catalog_only_map_targets(
            catalog, categories=categories
        )
        claimed = {target.slug for target in targets}
        collisions = sorted(
            target.slug for target in extra_targets if target.slug in claimed
        )
        if collisions:
            raise ValueError(
                "unregistered map slug collides with a registered map: "
                + ", ".join(collisions)
            )
        targets = tuple(
            sorted(targets + extra_targets, key=lambda item: item.slug)
        )
        rejections = rejections + extra_rejections
    if target_limit is not None:
        targets = targets[:target_limit]
    label = map_set.replace("-", " ")
    return build_map_profile(
        catalog,
        targets,
        profile_id=f"{game}-{map_set}-maps-generated",
        title=f"{game} {label} map private generated pack",
        pack_id=f"{game}-{map_set}-maps-private",
        pack_version=f"{map_set}-generated-v0",
        terrain_output=f"assets/terrain/{map_set}-maps",
        map_id_prefix=f"{game}.map.",
        priority=905,
        rejections=rejections,
        strict=strict,
        binder=binder,
    )


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
        map_id_prefix=f"{game}.map.",
        priority=905,
        rejections=rejections,
        strict=strict,
    )
