"""Deterministic script-list extraction from SAGE ``.map``/``.scb`` sources.

Emits the pack-facing ``openbfme.map-scripts`` document consumed by the
runtime script interpreter (``retail_map_scripts.gd``): every WorldBuilder
Script payload in source order, verbatim in the byte-exact ``sage_scb``
native shape, plus a fail-closed opcode census.  Wire parsing is delegated
entirely to the proven ``sage_map``/``sage_scb`` decoders; any structural
surprise raises instead of degrading, and the emitted JSON is byte-stable
(sorted keys, no timestamps, no absolute paths).
"""

from __future__ import annotations

from collections import Counter
import copy
import hashlib
from pathlib import Path
from typing import Any

from .sage_map import (
    MAX_TOP_LEVEL_RECORDS,
    SageMapError,
    _Cursor,
    _parse_height,
    _parse_name_table,
    _parse_objects,
    _parse_sides,
    _parse_teams,
    _parse_trigger_areas,
    _parse_waypoint_edges,
    _records,
    decode_sage_map_blob,
)
from .sage_scb import (
    SageScbError,
    _map_error_code,
    _parse_player_scripts,
    convert_sage_scb_bytes,
)
from .util import write_json_atomic


MAP_SCRIPTS_SCHEMA = "openbfme.map-scripts"
MAP_SCRIPTS_SCHEMA_VERSION = 1
MAP_SCRIPTS_COMPOSITE_SCHEMA_VERSION = 2

# Containers this converter admits, keyed by source suffix (casefolded).
_CONTAINERS = {".map": "map", ".scb": "scb"}

# Retail .map files carry PlayerScriptsList v1/5/6 with identical ScriptList
# v1 children (the versions independently observed and admitted by
# sage_map).  Standalone .scb containers are v1 only, enforced by sage_scb.
_MAP_PLAYER_SCRIPTS_VERSIONS = frozenset({1, 5, 6})

_ACTION_RECORD_NAMES = frozenset({"ScriptAction", "ScriptActionFalse"})

# ---------------------------------------------------------------------------
# Runtime opcode contract.
#
# These two tables are the single authoritative declaration of what the Godot
# interpreter (``game/src/retail_slice/retail_map_scripts.gd``) actually
# honours.  ``tests/test_sage_scripts.py`` parses that GDScript source and
# asserts the sets are identical, so the census emitted here can never drift
# into claiming coverage the runtime does not have.
#
# "semantic" opcodes change interpreter or simulation state.
# "recorded" opcodes are presentation-only in retail (UI, audio, camera,
# objectives).  The runtime emits a deterministic, bounded event for each one
# instead of mutating the simulation; they are reported separately and are
# never counted as gameplay coverage.
# ---------------------------------------------------------------------------

# Must match IMPLEMENTED_* / SEMANTIC_* tables in the archived Godot
# interpreter game/src/retail_slice/retail_map_scripts.gd. The live vocabulary
# path is game/src/script/; this contract only covers the archived reference.
SEMANTIC_CONDITION_OPCODES = frozenset(
    {
        "CONDITION_TRUE",
        "CONDITION_FALSE",
        "COUNTER",
        "FLAG",
        "TIMER_EXPIRED",
    }
)

SEMANTIC_ACTION_OPCODES = frozenset(
    {
        "SET_COUNTER",
        "INCREMENT_COUNTER",
        "SET_FLAG",
        "SET_MILLISECOND_TIMER",
        "PLAYER_SET_MONEY",
    }
)

# Archived runtime emits no presentation-only recorded path; empty is honest.
RECORDED_ACTION_OPCODES: frozenset[str] = frozenset()

RECORDED_CONDITION_OPCODES: frozenset[str] = frozenset()

# Object properties naming an authored map object that scripts address by name.
_OBJECT_NAME_PROPERTY = "objectName"
_OBJECT_OWNER_PROPERTY = "originalOwner"
_WAYPOINT_TYPE_NAME = "*Waypoints/Waypoint"

# Exact tactical-marker object types measured on the catalog-winning official
# skirmish maps of BFME2 1.06 and RotWK 2.01.  This is deliberately a closed
# set: a new type makes marker-only attestation disappear until a new source
# census establishes that type's semantics.
_INHERITANCE_TEAM_NAMES = frozenset(
    f"Player_{index}_Inherit" for index in range(1, 9)
)
_INHERITANCE_TACTICAL_MARKER_TYPES = frozenset(
    {
        "Backdoor1",
        "Backdoor2",
        "Backdoor3",
        "Center1",
        "Center2",
        "Center3",
        "Flank1",
        "Flank2",
        "Flank3",
    }
)

# Authored team unit composition.  WorldBuilder writes a Teams entry's
# reinforcement template as seven parallel property triples
# ``teamUnitType<N>`` / ``teamUnitMinCount<N>`` / ``teamUnitMaxCount<N>``;
# ``CREATE_REINFORCEMENT_TEAM`` instantiates exactly that template at a
# waypoint, so without it the opcode has nothing to create.  Slot ordinals run
# 1..7 in every retail map observed (the eighth is never authored).
_TEAM_UNIT_SLOTS = range(1, 8)


def _team_property(team: dict[str, Any], name: str) -> Any:
    for item in team.get("properties", []):
        if item.get("name") == name:
            return item.get("value")
    return None


def _team_units(team: dict[str, Any]) -> list[dict[str, Any]]:
    """Read one Teams entry's authored unit composition, in slot order.

    A slot with no type name is not authored and is skipped.  Counts are
    clamped to be non-negative and ordered (``max >= min``) rather than
    guessed at; a slot whose max is zero creates nothing, which is what retail
    does with an empty template.
    """

    units: list[dict[str, Any]] = []
    for slot in _TEAM_UNIT_SLOTS:
        type_name = _team_property(team, f"teamUnitType{slot}")
        if not isinstance(type_name, str) or not type_name:
            continue
        minimum = _team_property(team, f"teamUnitMinCount{slot}")
        maximum = _team_property(team, f"teamUnitMaxCount{slot}")
        low = max(0, int(minimum) if isinstance(minimum, int) else 0)
        high = max(0, int(maximum) if isinstance(maximum, int) else 0)
        units.append(
            {
                "slot": slot,
                "type": type_name,
                "minCount": min(low, high),
                "maxCount": max(low, high),
            }
        )
    return units


def _split_owner(value: Any) -> tuple[str, str]:
    """Split an authored ``originalOwner`` into (player, team).

    WorldBuilder writes object ownership as ``<player>/<team>`` (for example
    ``PlyrDwarves/Dale Buildings``); the default team of the nameless player
    is written ``/team``.  Team names are unique within a map, so the team
    half is the key script ``TEAM_*`` opcodes address.  A value without a
    separator is a bare team name and carries no player.
    """

    if not isinstance(value, str) or not value:
        return "", ""
    player, separator, team = value.partition("/")
    if not separator:
        return "", player
    return player, team


def _marker_only_team_attested(
    team_row: dict[str, Any], object_rows: list[dict[str, Any]]
) -> bool:
    """True only for a fully enumerated retail inheritance-marker team.

    ``objectCount`` alone is not evidence: every counted member must have a
    corresponding emitted ``world.objects`` row, every row must retain the
    exact qualified owner/team identity, and every type must belong to the
    closed two-tree tactical-marker census above.  Empty inheritance teams are
    safe and attested when their source count and enumeration are both zero.
    """

    team_name = team_row.get("name")
    owner = team_row.get("owner")
    if (
        owner != "PlyrCivilian"
        or team_name not in _INHERITANCE_TEAM_NAMES
        or not isinstance(team_row.get("objectCount"), int)
        or not isinstance(team_row.get("namedMembers"), list)
    ):
        return False
    members = [
        row
        for row in object_rows
        if row.get("owner") == owner and row.get("team") == team_name
    ]
    if team_row["objectCount"] != len(members):
        return False
    if sorted(
        row.get("name")
        for row in members
        if isinstance(row.get("name"), str) and row["name"]
    ) != team_row["namedMembers"]:
        return False
    return all(
        row.get("owner") == owner
        and row.get("team") == team_name
        and row.get("typeName") in _INHERITANCE_TACTICAL_MARKER_TYPES
        for row in members
    )


def _map_chunks_and_world(
    source: bytes,
) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    """Decode a .map blob into its PlayerScriptsList plus the script world.

    The script world is every map chunk a WorldBuilder script can address by
    name: SidesList players, Teams, waypoints (and their authored paths),
    TriggerAreas polygons, and the named ObjectsList entries.  Without it a
    converted mission is unrunnable, because all but the counter/flag/timer
    opcode families take a waypoint, area, team, player, or object name.
    """

    try:
        body, _envelope = decode_sage_map_blob(source)
        cursor = _Cursor(body, label="map script extraction")
        if cursor.bytes(4) != b"CkMp":
            raise SageScbError("invalid-magic", "decoded map lacks CkMp magic")
        names = _parse_name_table(cursor)
        budget = {"nodes": 0}
        chunks: list[dict[str, Any]] = []
        heightmap: Any = None
        raw_objects: list[dict[str, Any]] = []
        raw_waypoints: list[dict[str, Any]] = []
        trigger_areas: list[dict[str, Any]] = []
        teams: dict[str, Any] = {}
        sides: dict[str, Any] = {}
        waypoint_edges: list[dict[str, int]] = []
        for record in _records(
            cursor, names, cap=MAX_TOP_LEVEL_RECORDS, label="MapFile"
        ):
            if record.name == "PlayerScriptsList":
                if record.version not in _MAP_PLAYER_SCRIPTS_VERSIONS:
                    raise SageScbError(
                        "unsupported-top-level-version",
                        f"unsupported PlayerScriptsList version: {record.version}",
                    )
                chunks.append(
                    {
                        "name": record.name,
                        "version": record.version,
                        "value": _parse_player_scripts(
                            record, names, budget=budget
                        ),
                    }
                )
            elif record.name == "HeightMapData":
                # Script libraries are structural maps and legitimately use a
                # one-cell heightmap. This lane emits no runnable terrain; the
                # multiplayer/scenario map profiles retain their 2x2 minimum.
                heightmap = _parse_height(record, minimum_dimension=1)
            elif record.name == "ObjectsList":
                if heightmap is None:
                    # Retail maps always carry HeightMapData first; a
                    # non-empty ObjectsList without one is a wire surprise and
                    # must not be guessed at. An empty one carries no world.
                    if record.payload.remaining:
                        raise SageScbError(
                            "objects-before-heightmap",
                            "ObjectsList appears before HeightMapData",
                        )
                else:
                    raw_objects, raw_waypoints, _starts = _parse_objects(
                        record, names, heightmap
                    )
            elif record.name == "TriggerAreas":
                trigger_areas = _parse_trigger_areas(record)
            elif record.name == "Teams":
                teams = _parse_teams(record, names)
            elif record.name == "SidesList":
                sides = _parse_sides(record, names)
            elif record.name == "WaypointsList":
                waypoint_edges = _parse_waypoint_edges(record)
            else:
                record.payload.skip(record.payload.remaining)
        cursor.finish()
    except SageScbError:
        raise
    except SageMapError as exc:
        raise SageScbError(
            _map_error_code(exc), "map wire parsing failed"
        ) from exc
    if len(chunks) != 1:
        raise SageScbError(
            "missing-player-scripts",
            f"map contains {len(chunks)} PlayerScriptsList chunk(s); expected 1",
        )
    if heightmap is None:
        # No terrain chunk means no resolvable world geometry. Emit the
        # unavailable world rather than a half-populated one; the runtime
        # leaves every world-dependent opcode unimplemented in that case.
        return chunks, _empty_world()
    world = _build_world(
        sides=sides,
        teams=teams,
        objects=raw_objects,
        waypoints=raw_waypoints,
        waypoint_edges=waypoint_edges,
        trigger_areas=trigger_areas,
    )
    return chunks, world


def _side_property(player: dict[str, Any], name: str) -> Any:
    for item in player.get("properties", []):
        if item.get("name") == name:
            return item.get("value")
    return None


def _waypoint_paths(
    waypoints: list[dict[str, Any]], edges: list[dict[str, int]]
) -> list[dict[str, Any]]:
    """Resolve authored waypoint path labels into ordered waypoint id lists.

    A path is ordered by walking the WaypointsList edge set restricted to the
    path's own members.  When that restricted graph is not a simple chain the
    path is emitted with ``ordered: false`` and the runtime refuses it, rather
    than inventing an order.
    """

    members: dict[str, list[int]] = {}
    for waypoint in waypoints:
        for label in waypoint.get("pathLabels", []):
            value = str(label.get("value", ""))
            if value:
                members.setdefault(value, []).append(int(waypoint["id"]))

    paths: list[dict[str, Any]] = []
    for label in sorted(members):
        ids = sorted(set(members[label]))
        member_set = set(ids)
        successors: dict[int, list[int]] = {}
        indegree = dict.fromkeys(ids, 0)
        for edge in edges:
            start, end = int(edge["startId"]), int(edge["endId"])
            if start in member_set and end in member_set:
                successors.setdefault(start, []).append(end)
                indegree[end] += 1
        ordered = True
        if len(ids) == 1:
            chain = list(ids)
        else:
            heads = [i for i in ids if indegree[i] == 0]
            chain = []
            if len(heads) == 1 and all(
                len(successors.get(i, ())) <= 1 for i in ids
            ):
                node: int | None = heads[0]
                visited: set[int] = set()
                while node is not None and node not in visited:
                    visited.add(node)
                    chain.append(node)
                    following = successors.get(node, ())
                    node = following[0] if following else None
                if len(chain) != len(ids):
                    ordered = False
            else:
                ordered = False
            if not ordered:
                chain = list(ids)
        paths.append(
            {
                "label": label,
                "ordered": ordered,
                "waypointIds": chain,
            }
        )
    return paths


def _build_world(
    *,
    sides: dict[str, Any],
    teams: dict[str, Any],
    objects: list[dict[str, Any]],
    waypoints: list[dict[str, Any]],
    waypoint_edges: list[dict[str, int]],
    trigger_areas: list[dict[str, Any]],
) -> dict[str, Any]:
    players = [
        {
            "index": int(player.get("index", index)),
            "name": str(player.get("name", "")),
            "displayName": str(_side_property(player, "playerDisplayName") or ""),
            "faction": str(_side_property(player, "playerFaction") or ""),
            "isHuman": bool(_side_property(player, "playerIsHuman") or False),
            "allies": str(_side_property(player, "playerAllies") or ""),
            "enemies": str(_side_property(player, "playerEnemies") or ""),
        }
        for index, player in enumerate(sides.get("players", []))
    ]
    object_rows = []
    named_objects = []
    for item in objects:
        if item.get("typeName") == _WAYPOINT_TYPE_NAME:
            continue
        properties = item.get("properties", {})
        owner_value = properties.get(_OBJECT_OWNER_PROPERTY, "") or ""
        player_name, team_name = _split_owner(owner_value)
        object_name_value = properties.get(_OBJECT_NAME_PROPERTY)
        object_name = (
            object_name_value
            if isinstance(object_name_value, str)
            else ""
        )
        row = {
            "name": object_name,
            "typeName": str(item.get("typeName", "")),
            "godotPosition": list(item["godotPosition"]),
            "godotYawRadians": float(item["godotYawRadians"]),
            "originalOwner": str(owner_value),
            "owner": player_name,
            "team": team_name,
        }
        object_rows.append(row)
        if object_name:
            named_objects.append(dict(row))
    object_rows.sort(
        key=lambda row: (
            row["team"],
            row["name"],
            row["typeName"],
            row["godotPosition"],
        )
    )
    named_objects.sort(key=lambda row: (row["name"], row["typeName"]))

    # Authored team membership.  Every non-waypoint object on the map belongs
    # to exactly one team through its ``originalOwner`` property, and retail
    # ``TEAM_HAS_UNITS`` / ``TEAM_DESTROYED`` answer over all of them, not just
    # the named ones.  Derive the count and named subset from the same emitted
    # object rows used by marker-only attestation, so a lossy parallel census
    # cannot accidentally certify an incomplete team.
    team_rows = []
    for index, team in enumerate(teams.get("teams", [])):
        team_name = str(team.get("name", ""))
        team_owner = str(team.get("owner", ""))
        members = [
            row
            for row in object_rows
            if row["owner"] == team_owner and row["team"] == team_name
        ]
        team_row = {
            "index": int(team.get("index", index)),
            "name": team_name,
            "owner": team_owner,
            "objectCount": len(members),
            "namedMembers": sorted(
                row["name"] for row in members if row["name"]
            ),
            "units": _team_units(team),
        }
        if _marker_only_team_attested(team_row, object_rows):
            team_row["markerOnly"] = True
        team_rows.append(team_row)
    waypoint_rows = [
        {
            "id": int(waypoint["id"]),
            "name": str(waypoint["name"]),
            "godotPosition": list(waypoint["godotPosition"]),
        }
        for waypoint in waypoints
    ]
    area_rows = [
        {
            "id": int(area["id"]),
            "name": str(area["name"]),
            "godotXZPoints": [list(point) for point in area["godotXZPoints"]],
        }
        for area in trigger_areas
    ]
    return {
        "available": True,
        "players": players,
        "teams": team_rows,
        "waypoints": sorted(waypoint_rows, key=lambda row: row["id"]),
        "waypointPaths": _waypoint_paths(waypoints, waypoint_edges),
        "triggerAreas": sorted(area_rows, key=lambda row: row["id"]),
        # Every non-waypoint map object, including unnamed tactical markers.
        # Script-team authority decisions must not infer their type from a
        # lossy objectCount alone.
        "objects": object_rows,
        "namedObjects": named_objects,
    }


def _empty_world() -> dict[str, Any]:
    return {
        "available": False,
        "players": [],
        "teams": [],
        "waypoints": [],
        "waypointPaths": [],
        "triggerAreas": [],
        "objects": [],
        "namedObjects": [],
    }


def _collect_scripts(chunks: list[dict[str, Any]]) -> list[dict[str, Any]]:
    """Flatten every Script node with its owning player index and group path.

    ``PlayerScriptsList`` holds one ``ScriptList`` per ``SidesList`` player in
    source order; the list ordinal is that player's index and is preserved so
    a mission's per-player script environments stay separable.
    """

    scripts: list[dict[str, Any]] = []

    def visit(node: Any, player_index: int, path: list[str]) -> None:
        if isinstance(node, dict):
            name = node.get("name")
            value = node.get("value")
            if name == "Script" and isinstance(value, dict):
                scripts.append(
                    {
                        "playerIndex": player_index,
                        "groupPath": "/".join(path),
                        "script": value,
                    }
                )
                return
            if name == "ScriptGroup" and isinstance(value, dict):
                for child in value.get("records", []):
                    visit(
                        child,
                        player_index,
                        path + [str(value.get("name", "?"))],
                    )
                return
            for child in node.values():
                visit(child, player_index, path)
        elif isinstance(node, (list, tuple)):
            for child in node:
                visit(child, player_index, path)

    for chunk in chunks:
        lists = chunk.get("value", {}).get("records", [])
        for ordinal, script_list in enumerate(lists):
            visit(script_list, ordinal, [])
    return scripts


def _coverage(actions: Counter, conditions: Counter) -> dict[str, Any]:
    """Split the census against the declared runtime opcode contract."""

    def split(
        histogram: Counter, semantic: frozenset[str], recorded: frozenset[str]
    ) -> dict[str, Any]:
        semantic_slots = sum(v for k, v in histogram.items() if k in semantic)
        recorded_slots = sum(v for k, v in histogram.items() if k in recorded)
        missing = {
            k: v
            for k, v in histogram.items()
            if k not in semantic and k not in recorded
        }
        return {
            "totalSlots": sum(histogram.values()),
            "semanticSlots": semantic_slots,
            "recordedSlots": recorded_slots,
            "unsupportedSlots": sum(missing.values()),
            "distinctUnsupported": len(missing),
            # A list, not a mapping: write_json_atomic sorts object keys, and
            # the frequency order is the whole point of this census.
            "unsupportedByFrequency": [
                {"opcode": opcode, "slots": slots}
                for opcode, slots in sorted(
                    missing.items(), key=lambda kv: (-kv[1], kv[0])
                )
            ],
        }

    return {
        "actions": split(
            actions, SEMANTIC_ACTION_OPCODES, RECORDED_ACTION_OPCODES
        ),
        "conditions": split(
            conditions, SEMANTIC_CONDITION_OPCODES, RECORDED_CONDITION_OPCODES
        ),
    }


def _script_census(script: dict[str, Any]) -> tuple[Counter, Counter, int, int]:
    """Count action/condition opcode occurrences and slots in one script."""

    actions: Counter = Counter()
    conditions: Counter = Counter()
    action_slots = 0
    condition_slots = 0

    def visit(node: Any) -> None:
        nonlocal action_slots, condition_slots
        if isinstance(node, dict):
            name = node.get("name")
            value = node.get("value")
            if name in _ACTION_RECORD_NAMES and isinstance(value, dict):
                internal = value.get("internalName")
                opcode = (
                    internal.get("name") if isinstance(internal, dict) else None
                )
                if not isinstance(opcode, str) or not opcode:
                    raise SageScbError(
                        "missing-opcode-name",
                        f"{name} record lacks an internal opcode name",
                    )
                actions[opcode] += 1
                action_slots += 1
            elif name == "Condition" and isinstance(value, dict):
                internal = value.get("internalName")
                opcode = (
                    internal.get("name") if isinstance(internal, dict) else None
                )
                if not isinstance(opcode, str) or not opcode:
                    raise SageScbError(
                        "missing-opcode-name",
                        "Condition record lacks an internal opcode name",
                    )
                conditions[opcode] += 1
                condition_slots += 1
            for child in node.values():
                visit(child)
        elif isinstance(node, (list, tuple)):
            for child in node:
                visit(child)

    visit(script)
    return actions, conditions, action_slots, condition_slots


def map_scripts_document(source: bytes, *, container: str) -> dict[str, Any]:
    """Build the deterministic ``openbfme.map-scripts`` document."""

    if container == "scb":
        chunks = convert_sage_scb_bytes(source)["chunks"]
        world = _empty_world()
    elif container == "map":
        chunks, world = _map_chunks_and_world(source)
    else:
        raise ValueError(f"unsupported sage-scripts container: {container!r}")

    rows: list[dict[str, Any]] = []
    total_actions: Counter = Counter()
    total_conditions: Counter = Counter()
    total_action_slots = 0
    total_condition_slots = 0
    for item in _collect_scripts(chunks):
        script = item["script"]
        actions, conditions, action_slots, condition_slots = _script_census(
            script
        )
        total_actions.update(actions)
        total_conditions.update(conditions)
        total_action_slots += action_slots
        total_condition_slots += condition_slots
        rows.append(
            {
                "name": str(script.get("name", "")),
                "playerIndex": int(item["playerIndex"]),
                "groupPath": item["groupPath"],
                "isActive": bool(script.get("isActive", False)),
                "isSubroutine": bool(script.get("isSubroutine", False)),
                "deactivateUponSuccess": bool(
                    script.get("deactivateUponSuccess", False)
                ),
                "actionSlots": action_slots,
                "conditionSlots": condition_slots,
                "actionOpcodes": dict(sorted(actions.items())),
                "conditionOpcodes": dict(sorted(conditions.items())),
                "payload": script,
            }
        )

    return {
        "schema": MAP_SCRIPTS_SCHEMA,
        "schemaVersion": MAP_SCRIPTS_SCHEMA_VERSION,
        "source": {
            "container": container,
            "sourceBytes": len(source),
            "sourceSha256": hashlib.sha256(source).hexdigest(),
        },
        "counts": {
            "scripts": len(rows),
            "actionSlots": total_action_slots,
            "conditionSlots": total_condition_slots,
            "distinctActionOpcodes": len(total_actions),
            "distinctConditionOpcodes": len(total_conditions),
            "players": len(world["players"]),
            "teams": len(world["teams"]),
            "waypoints": len(world["waypoints"]),
            "waypointPaths": len(world["waypointPaths"]),
            "triggerAreas": len(world["triggerAreas"]),
            "namedObjects": len(world["namedObjects"]),
        },
        "coverage": _coverage(total_actions, total_conditions),
        "actionOpcodes": dict(sorted(total_actions.items())),
        "conditionOpcodes": dict(sorted(total_conditions.items())),
        "world": world,
        "scripts": rows,
    }


def compose_map_scripts_document(
    map_document: dict[str, Any],
    library_documents: list[dict[str, Any]],
) -> dict[str, Any]:
    """Compose one decoded map world with its attached retail script libraries.

    A library ``.map`` file authors exactly one named placeholder player and
    hangs every script and team off it.  Retail binds that placeholder by
    name, and the name decides the binding class:

    ``Player``
        the per-player template class (``ai_initialize``,
        ``ai_mp_inherit_management``).  SAGE instantiates the library once for
        every active skirmish player, so concatenating its scripts into the
        map document would lose that execution context and collide
        library-local team names.

    any other name
        the map-player class (``lib_gollumspawn`` authors ``PlyrCreeps``).
        Retail binds it once to the map's own player of that exact name.  The
        eight retail maps that inline Lib_GollumSpawn instead of attaching it
        carry precisely those scripts under their ``PlyrCreeps`` player, which
        is the oracle for this class.

    Schema v2 therefore preserves each library as an explicit template that
    names its placeholder and its instantiation class; the runtime binds the
    placeholder and its local teams inside the concrete executor(s).
    """

    def require_v1(document: dict[str, Any], label: str) -> None:
        if (
            not isinstance(document, dict)
            or document.get("schema") != MAP_SCRIPTS_SCHEMA
            or document.get("schemaVersion") != MAP_SCRIPTS_SCHEMA_VERSION
            or not isinstance(document.get("world"), dict)
            or not isinstance(document.get("scripts"), list)
        ):
            raise ValueError(f"{label} is not an openbfme.map-scripts v1 document")

    require_v1(map_document, "map document")
    map_world = map_document["world"]
    if not map_world.get("available"):
        raise ValueError("map document has no decoded script world")
    map_player_names = {
        str(row.get("name"))
        for row in map_world.get("players", [])
        if isinstance(row, dict) and isinstance(row.get("name"), str)
    }

    templates: list[dict[str, Any]] = []
    identities: set[str] = set()
    for index, library in enumerate(library_documents):
        label = f"library document {index}"
        require_v1(library, label)
        source = library.get("source")
        if not isinstance(source, dict):
            raise ValueError(f"{label} has no source identity")
        identity = source.get("sourceSha256")
        if (
            not isinstance(identity, str)
            or len(identity) != 64
            or any(character not in "0123456789abcdef" for character in identity)
        ):
            raise ValueError(f"{label} has an invalid sourceSha256")
        if identity in identities:
            raise ValueError(f"duplicate library source identity {identity}")
        identities.add(identity)

        world = library["world"]
        players = world.get("players")
        if not isinstance(players, list):
            raise ValueError(f"{label} has no player table")
        player_rows = {
            row.get("index"): row.get("name")
            for row in players
            if isinstance(row, dict)
        }
        placeholder_rows = [
            (player_index, player_name)
            for player_index, player_name in player_rows.items()
            if isinstance(player_name, str) and player_name != ""
        ]
        if len(placeholder_rows) != 1:
            raise ValueError(f"{label} must declare exactly one player placeholder")
        placeholder_index, placeholder_name = placeholder_rows[0]
        if placeholder_name == "Player":
            instantiate_for = "aiPlayers"
        else:
            # A library that names a concrete retail player (Lib_GollumSpawn
            # names PlyrCreeps) is bound once, by name, to the map's own
            # player.  Refuse the composition when the map does not declare
            # that player: the scripts would have no executor.
            instantiate_for = "mapPlayer"
            if placeholder_name not in map_player_names:
                raise ValueError(
                    f"{label} binds player {placeholder_name!r} which the map "
                    "document does not declare"
                )
        for script in library["scripts"]:
            if (
                not isinstance(script, dict)
                or script.get("playerIndex") != placeholder_index
                or not isinstance(script.get("payload"), dict)
            ):
                raise ValueError(
                    f"{label} carries a script outside the "
                    f"{placeholder_name} placeholder"
                )
        for team in world.get("teams", []):
            if (
                not isinstance(team, dict)
                or team.get("owner") not in ("", placeholder_name)
            ):
                raise ValueError(
                    f"{label} carries a team outside the "
                    f"{placeholder_name} placeholder"
                )
        templates.append(
            {
                "identity": identity,
                "instantiateFor": instantiate_for,
                "playerPlaceholder": placeholder_name,
                "world": copy.deepcopy(world),
                "scripts": copy.deepcopy(library["scripts"]),
            }
        )

    return {
        "schema": MAP_SCRIPTS_SCHEMA,
        "schemaVersion": MAP_SCRIPTS_COMPOSITE_SCHEMA_VERSION,
        "source": {
            "container": "composite",
            "map": copy.deepcopy(map_document.get("source", {})),
            "libraries": [
                copy.deepcopy(library.get("source", {}))
                for library in library_documents
            ],
        },
        "world": copy.deepcopy(map_world),
        "scripts": copy.deepcopy(map_document["scripts"]),
        "libraryTemplates": templates,
    }


def convert_map_scripts(source: Path, target: Path) -> list[Path]:
    """Convert one .map/.scb source into ``<name>.scripts.json``."""

    container = _CONTAINERS.get(source.suffix.casefold())
    if container is None:
        raise ValueError(
            f"sage-scripts source must be a .map or .scb file: {source.name}"
        )
    document = map_scripts_document(source.read_bytes(), container=container)
    write_json_atomic(target, document)
    return [target]
