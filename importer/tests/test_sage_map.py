from __future__ import annotations

import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest

from openbfme_importer.native_backtest import validate_cooked_sage_map
from openbfme_importer.sage_map import (
    SageMapError,
    census_sage_map_bytes,
    convert_sage_map,
    decode_sage_map_blob,
    parse_sage_base_template_bytes,
    parse_sage_map_bytes,
)


_TERRAIN_SOURCE_LAYERS = {
    "tileIndices": struct.pack("<6H", 0, 1, 2, 7, 511, 65_535),
    "blendCells": struct.pack("<6I", 0, 0x01020304, 0xFFFFFFFF, 7, 0x10203040, 0),
    "threeWayBlendCells": struct.pack("<6I", 0xAABBCCDD, 0, 1, 2, 3, 0xFFFFFFFF),
    "cliffCells": struct.pack("<6I", 0, 4, 0, 8, 16, 32),
    "blendDescriptions": bytes(range(36)),
    "cliffMappings": bytes(range(64, 102)),
}

_TERRAIN_LEGACY_16_BLEND_LAYERS = {
    "blendCells": struct.pack("<6H", 0, 0x0304, 0xFFFF, 7, 0x3040, 0),
    "threeWayBlendCells": struct.pack("<6H", 0xCCDD, 0, 1, 2, 3, 0xFFFF),
    "cliffCells": struct.pack("<6H", 0, 4, 0, 8, 16, 32),
}

_TERRAIN_SOURCE_PATHS = {
    "tileIndices": "terrain-tile-indices.u16",
    "blendCells": "terrain-blend-cells.u32",
    "threeWayBlendCells": "terrain-three-way-blend-cells.u32",
    "cliffCells": "terrain-cliff-cells.u32",
    "blendDescriptions": "terrain-blend-descriptions.bin",
    "cliffMappings": "terrain-cliff-mappings.bin",
}

_VERSIONED_BLEND_LAYER_ORDER = (
    "impassability",
    "impassabilityToPlayers",
    "passageWidths",
    "taintability",
    "extraPassability",
    "flammability",
    "visibility",
)
_VERSIONED_BLEND_LAYER_PATHS = {
    "impassability": "impassability.bit",
    "impassabilityToPlayers": "terrain-impassability-to-players.bit",
    "passageWidths": "terrain-passage-widths.bit",
    "taintability": "terrain-taintability.bit",
    "extraPassability": "terrain-extra-passability.bit",
    "flammability": "terrain-flammability.u8",
    "visibility": "terrain-visibility.bit",
}


def _versioned_blend_presence(version: int) -> dict[str, bool]:
    minimum_versions = {
        "impassability": 7,
        "impassabilityToPlayers": 10,
        "passageWidths": 11,
        "taintability": 14,
        "extraPassability": 15,
        "flammability": 16,
        "visibility": 17,
    }
    return {
        name: version >= minimum_versions[name] for name in _VERSIONED_BLEND_LAYER_ORDER
    }


def _u16_string(value: str) -> bytes:
    encoded = value.encode("cp1252")
    return struct.pack("<H", len(encoded)) + encoded


def _u16_unicode(value: str) -> bytes:
    encoded = value.encode("utf-16-le")
    return struct.pack("<H", len(value)) + encoded


def _trigger_areas(
    *,
    name: str = "SYNTHETIC_TRIGGER_SECRET",
    layer: str = "Trigger Layer",
    unique_id: int = 71,
    points: tuple[tuple[float, float], ...] = ((1.0, 2.0), (3.0, 4.0)),
    reserved: int = 0,
    trailing: bytes = b"",
) -> bytes:
    payload = struct.pack("<I", 1)
    payload += _u16_string(name) + _u16_string(layer)
    payload += struct.pack("<II", unique_id, len(points))
    payload += b"".join(struct.pack("<ff", *point) for point in points)
    payload += struct.pack("<I", reserved)
    return payload + trailing


def _standing_wave_areas(
    *,
    version: int = 2,
    name: str = "SYNTHETIC_WAVE_SECRET",
    layer: str = "Wave Layer",
    texture: str = "SYNTHETIC_WAVE_TEXTURE_SECRET.tga",
    unique_id: int = 72,
    uv_speed: float = 0.25,
    additive: int = 1,
    points: tuple[tuple[float, float], ...] = (),
    reserved: int = 0,
    settings: tuple[int, ...] = (10, 11, 12, 13, 14, 15, 16, 17, 18),
    pca_wave: int = 1,
    trailing: bytes = b"",
) -> bytes:
    if len(settings) != 9:
        raise AssertionError(
            "standing-wave synthetic settings must contain nine values"
        )
    payload = struct.pack("<I", 1)
    payload += struct.pack("<I", unique_id)
    payload += _u16_string(name) + _u16_string(layer)
    payload += struct.pack("<fB", uv_speed, additive)
    payload += struct.pack("<I", len(points))
    payload += b"".join(struct.pack("<ff", *point) for point in points)
    payload += struct.pack("<I", reserved)
    payload += struct.pack("<9I", *settings)
    payload += _u16_string(texture)
    if version == 2:
        payload += struct.pack("<I", pca_wave)
    return payload + trailing


def _dotnet_string(value: str) -> bytes:
    encoded = value.encode("utf-8")
    length = len(encoded)
    prefix = bytearray()
    while length >= 0x80:
        prefix.append((length & 0x7F) | 0x80)
        length >>= 7
    prefix.append(length)
    return bytes(prefix) + encoded


def _record(name: str, version: int, payload: bytes, indexes: dict[str, int]) -> bytes:
    return struct.pack("<IHI", indexes[name], version, len(payload)) + payload


def _property(name: str, kind: int, value: object, indexes: dict[str, int]) -> bytes:
    prefix = bytes([kind]) + indexes[name].to_bytes(3, "little")
    if kind == 0:
        return prefix + bytes([1 if value else 0])
    if kind == 1:
        return prefix + struct.pack("<i", int(value))
    if kind == 2:
        return prefix + struct.pack("<f", float(value))
    if kind in (3, 5):
        return prefix + _u16_string(str(value))
    if kind == 4:
        return prefix + _u16_unicode(str(value))
    raise AssertionError(kind)


def _property_collection(
    properties: list[tuple[str, int, object]], indexes: dict[str, int]
) -> bytes:
    return struct.pack("<H", len(properties)) + b"".join(
        _property(*item, indexes) for item in properties
    )


def _mp_position_info(
    indexes: dict[str, int],
    *,
    is_human: int = 1,
    is_computer: int = 1,
    load_ai_script: int = 1,
    team: int = 0,
    restrictions: tuple[str, ...] = (),
    child_name: str = "MPPositionInfo",
    child_version: int = 1,
    trailing: bytes = b"",
) -> bytes:
    payload = bytes([is_human, is_computer, load_ai_script])
    payload += struct.pack("<II", team, len(restrictions))
    payload += b"".join(_u16_string(value) for value in restrictions)
    return _record(child_name, child_version, payload + trailing, indexes)


def _sides_list(
    indexes: dict[str, int],
    *,
    version: int = 6,
    players: list[tuple[list[tuple[str, int, object]], list[bytes]]] | None = None,
    unknown_boolean: int = 0,
    declared_count: int | None = None,
    trailing: bytes = b"",
) -> bytes:
    if players is None:
        players = [
            (
                [
                    ("playerName", 3, "PlyrSynthetic"),
                    ("playerIsHuman", 0, False),
                    ("playerDisplayName", 4, "Synthetic"),
                ],
                [],
            )
        ]
    count = len(players) if declared_count is None else declared_count
    payload = b"" if version == 5 else bytes([unknown_boolean])
    payload += struct.pack("<i", count)
    if declared_count is None:
        for properties, build_list in players:
            payload += _property_collection(properties, indexes)
            payload += struct.pack("<I", len(build_list)) + b"".join(build_list)
    return payload + trailing


def _build_list_item(
    *,
    position: tuple[float, float, float] = (1.0, 2.0, 3.0),
    angle: float = 0.5,
    initially_built: int = 1,
    whiner: int = 0,
    unsellable: int = 1,
    repairable: int = 1,
) -> bytes:
    payload = _u16_string("SyntheticBuilding") + _u16_string("SyntheticTemplate")
    payload += struct.pack("<ffffB", *position, angle, initially_built)
    payload += struct.pack("<I", 2) + _u16_string("SyntheticScript")
    payload += struct.pack("<iBBB", 75, whiner, unsellable, repairable)
    return payload


def _river_areas(*, trailing: bytes = b"") -> bytes:
    payload = struct.pack("<I", 1)
    payload += struct.pack("<I", 12)
    payload += _u16_string("ford") + _u16_string("")
    payload += struct.pack("<fB", 0.05, 1)
    for value in ("river.tga", "noise.tga", "edge.tga", "sparkle.tga"):
        payload += _u16_string(value)
    payload += bytes([255, 254, 253, 0]) + struct.pack("<fI", 0.75, 43)
    payload += _u16_string("") + struct.pack("<I", 2)
    payload += struct.pack("<ffffffff", 0.0, 0.0, 0.0, 10.0, 10.0, 0.0, 10.0, 10.0)
    return payload + trailing


def _teams(
    indexes: dict[str, int],
    *,
    teams: list[list[tuple[str, int, object]]] | None = None,
    declared_count: int | None = None,
    trailing: bytes = b"",
) -> bytes:
    if teams is None:
        teams = [
            [
                ("teamName", 3, "teamPlyrSynthetic"),
                ("teamOwner", 3, "PlyrSynthetic"),
                ("exportWithScript", 0, False),
                ("teamInitialIdleSeconds", 1, 3),
                ("teamUnitExperienceLevel1", 1, 2),
                ("teamUnitUpgradeList1", 3, "UpgradeSynthetic"),
            ]
        ]
    count = len(teams) if declared_count is None else declared_count
    payload = struct.pack("<i", count)
    if declared_count is None:
        payload += b"".join(_property_collection(team, indexes) for team in teams)
    return payload + trailing


def _library_map_lists(
    indexes: dict[str, int],
    *,
    lists: list[tuple[str, ...]] | None = None,
    child_name: str = "LibraryMaps",
    child_version: int = 1,
    child_trailing: bytes = b"",
) -> bytes:
    if lists is None:
        lists = [("maps/map libraries/synthetic.map",)]
    result = b""
    for references in lists:
        payload = struct.pack("<I", len(references))
        payload += b"".join(_u16_string(value) for value in references)
        result += _record(child_name, child_version, payload + child_trailing, indexes)
    return result


def _map_object(
    type_name: str,
    position: tuple[float, float, float],
    properties: list[tuple[str, int, object]],
    indexes: dict[str, int],
    *,
    road_type: int = 0,
) -> bytes:
    payload = struct.pack("<ffffI", *position, 0.25, road_type)
    payload += _u16_string(type_name)
    payload += struct.pack("<H", len(properties))
    payload += b"".join(_property(*item, indexes) for item in properties)
    return _record("Object", 3, payload, indexes)


def _refpack_literals(body: bytes) -> bytes:
    compressed = bytearray([0x10, 0xFB])
    compressed.extend(len(body).to_bytes(3, "big"))
    position = 0
    while len(body) - position > 3:
        take = min(112, ((len(body) - position) // 4) * 4)
        compressed.append(0xE0 + take // 4 - 1)
        compressed.extend(body[position : position + take])
        position += take
    remaining = len(body) - position
    compressed.append(0xFC + remaining)
    compressed.extend(body[position:])
    return b"EAR\0" + struct.pack("<I", len(body)) + bytes(compressed)


def _synthetic_map(
    *,
    compressed: bool = True,
    trigger_payload: bytes | None = None,
    standing_wave_payload: bytes | None = None,
    river_payload: bytes | None = None,
    waypoint_list_payload: bytes | None = None,
    mp_position_payload: object | None = None,
    sides_payload: object | None = None,
    teams_payload: object | None = None,
    library_map_lists_payload: object | None = None,
    trigger_version: int = 1,
    standing_wave_version: int = 2,
    river_version: int = 2,
    mp_position_version: int = 0,
    sides_version: int = 6,
    teams_version: int = 1,
    library_map_lists_version: int = 1,
    player_scripts_version: int = 1,
    script_child_name: str = "ScriptList",
    script_child_version: int = 1,
    script_child_payload: bytes = b"",
    script_child_payloads: tuple[bytes, ...] | None = None,
    script_outer_trailing: bytes = b"",
    waypoint_list_version: int = 1,
    script_list_count: int = 1,
    duplicate_trigger: bool = False,
    duplicate_standing_wave: bool = False,
    duplicate_setup_chunk: str | None = None,
    omit_setup_chunk: str | None = None,
    start_name: str = "Player_1_Start",
    include_waypoint_id: bool = True,
    include_waypoint_unique_id: bool = True,
    waypoint_unique_id: str | None = None,
    waypoint_path_labels: tuple[str, ...] = (),
    extra_waypoints: tuple[tuple[int, str, tuple[float, float, float]], ...] = (),
    object_type_names: tuple[str, ...] = ("TreeTest",),
    road_objects: tuple[tuple[str, int, tuple[float, float, float]], ...] = (),
    height_dimensions: tuple[int, int] = (3, 2),
    blend_version: int = 18,
    blend_layer_payloads: dict[str, bytes] | None = None,
    blend_payload: bytes | None = None,
    blend_trailing: bytes = b"",
    extra_top_record: tuple[str, int, bytes] | None = None,
) -> tuple[bytes, bytes]:
    names = [
        "HeightMapData",
        "BlendTileData",
        "MPPositionList",
        "MPPositionInfo",
        "SidesList",
        "Teams",
        "LibraryMapLists",
        "LibraryMaps",
        "ObjectsList",
        "Object",
        "StandingWaterAreas",
        "RiverAreas",
        "TriggerAreas",
        "StandingWaveAreas",
        "PlayerScriptsList",
        "ScriptList",
        "WaypointsList",
        "waypointID",
        "waypointName",
        "uniqueID",
        "waypointPathLabel1",
        "waypointPathLabel2",
        "waypointPathLabel3",
        "playerName",
        "playerIsHuman",
        "playerDisplayName",
        "teamName",
        "teamOwner",
        "exportWithScript",
        "teamInitialIdleSeconds",
        "teamUnitExperienceLevel1",
        "teamUnitUpgradeList1",
    ]
    if extra_top_record is not None and extra_top_record[0] not in names:
        names.append(extra_top_record[0])
    indexes = {name: index + 1 for index, name in enumerate(names)}
    name_table = struct.pack("<I", len(names))
    for index in range(len(names), 0, -1):
        name_table += _dotnet_string(names[index - 1]) + struct.pack("<I", index)

    width, map_height = height_dimensions
    area = width * map_height
    default_blend_layers = {
        "impassability": b"\x05\x02",
        "impassabilityToPlayers": b"\x00\x00",
        "passageWidths": b"\x00\x00",
        "taintability": b"\x00\x00",
        "extraPassability": b"\x00\x00",
        "flammability": bytes([1, 0, 1, 0, 1, 0]),
        "visibility": b"\x07\x07",
    }
    supplied_blend_layers = dict(blend_layer_payloads or {})
    unknown_blend_layers = sorted(
        set(supplied_blend_layers) - set(default_blend_layers)
    )
    if unknown_blend_layers:
        raise AssertionError(
            f"unknown synthetic blend layers: {unknown_blend_layers!r}"
        )
    blend_layers = {**default_blend_layers, **supplied_blend_layers}
    if height_dimensions == (3, 2):
        elevations = [1000, 1100, 1200, 1300, 1400, 1500]
        height_payload = struct.pack("<IIIII", 3, 2, 0, 1, 3)
        height_payload += struct.pack("<I", 2)
        height_payload += struct.pack("<I", 6)
        height_payload += struct.pack("<6H", *elevations)

        blend_cell_layers = (
            _TERRAIN_LEGACY_16_BLEND_LAYERS
            if blend_version < 14
            else _TERRAIN_SOURCE_LAYERS
        )
        blend = struct.pack("<I", area)
        blend += _TERRAIN_SOURCE_LAYERS["tileIndices"]
        blend += blend_cell_layers["blendCells"]
        blend += blend_cell_layers["threeWayBlendCells"]
        blend += blend_cell_layers["cliffCells"]
        layer_presence = _versioned_blend_presence(blend_version)
        for name in _VERSIONED_BLEND_LAYER_ORDER:
            if layer_presence[name]:
                blend += blend_layers[name]
        blend += struct.pack("<IIII", 1, 3, 2, 1)
        blend += struct.pack("<IIII", 0, 1, 1, 0) + _u16_string("TestGrass")
        blend += struct.pack("<II", 0x12345678, 0)
        blend += _TERRAIN_SOURCE_LAYERS["blendDescriptions"]
        blend += _TERRAIN_SOURCE_LAYERS["cliffMappings"]
    else:
        elevations = [1000 + index * 100 for index in range(area)]
        encoded_elevations = struct.pack(f"<{area}H", *elevations)
        height_payload = struct.pack("<IIII", width, map_height, 0, 0)
        height_payload += struct.pack("<I", area) + encoded_elevations
        packed_grid = b"\x00" * (((width + 7) // 8) * map_height)
        blend_cell_format = "H" if blend_version < 14 else "I"
        blend = struct.pack("<I", area)
        blend += struct.pack(f"<{area}H", *range(area))
        blend += struct.pack(f"<{area}{blend_cell_format}", *([0] * area)) * 3
        layer_presence = _versioned_blend_presence(blend_version)
        for name in _VERSIONED_BLEND_LAYER_ORDER:
            if not layer_presence[name]:
                continue
            blend += b"\x00" * area if name == "flammability" else packed_grid
        blend += struct.pack("<IIII", 1, 1, 1, 1)
        blend += struct.pack("<IIII", 0, 1, 1, 0) + _u16_string("TestGrass")
        blend += struct.pack("<II", 0x12345678, 0)
    blend = blend + blend_trailing if blend_payload is None else blend_payload

    waypoint_properties: list[tuple[str, int, object]] = []
    if include_waypoint_id:
        waypoint_properties.append(("waypointID", 1, 7))
    waypoint_properties.append(("waypointName", 3, start_name))
    if include_waypoint_unique_id:
        waypoint_properties.append(
            (
                "uniqueID",
                3,
                start_name if waypoint_unique_id is None else waypoint_unique_id,
            )
        )
    waypoint_properties.extend(
        (f"waypointPathLabel{slot}", 3, value)
        for slot, value in enumerate(waypoint_path_labels, 1)
    )
    objects = _map_object(
        "*Waypoints/Waypoint",
        (5.0, 5.0, 0.0),
        waypoint_properties,
        indexes,
    )
    for waypoint_id, waypoint_name, position in extra_waypoints:
        objects += _map_object(
            "*Waypoints/Waypoint",
            position,
            [
                ("waypointID", 1, waypoint_id),
                ("waypointName", 3, waypoint_name),
                ("uniqueID", 3, waypoint_name),
            ],
            indexes,
        )
    for object_index, type_name in enumerate(object_type_names, 1):
        objects += _map_object(
            type_name,
            (10.0 + object_index, 0.0, 2.0),
            [("uniqueID", 3, f"{type_name} {object_index}")],
            indexes,
        )
    for road_index, (type_name, road_type, position) in enumerate(road_objects, 1):
        objects += _map_object(
            type_name,
            position,
            [("uniqueID", 3, f"{type_name} Road {road_index}")],
            indexes,
            road_type=road_type,
        )

    standing = struct.pack("<I", 1)
    standing += struct.pack("<I", 11)
    standing += _u16_string("Test Water") + _u16_string("")
    standing += struct.pack("<fB", 0.06, 0)
    standing += _u16_string("Bump.tga") + _u16_string("Sky.tga")
    standing += struct.pack("<I", 3)
    standing += struct.pack("<ffffff", 0.0, 0.0, 20.0, 0.0, 0.0, 20.0)
    standing += struct.pack("<I", 42)
    standing += _u16_string("water.w3d") + _u16_string("depth.tga")

    rivers = _river_areas() if river_payload is None else river_payload

    resolved_script_payloads = (
        (script_child_payload,) * script_list_count
        if script_child_payloads is None
        else script_child_payloads
    )
    if len(resolved_script_payloads) != script_list_count:
        raise AssertionError(
            "synthetic script payload count must match script_list_count"
        )
    scripts = (
        b"".join(
            _record(
                script_child_name, script_child_version, payload, indexes
            )
            for payload in resolved_script_payloads
        )
        + script_outer_trailing
    )
    trigger_payload = (
        struct.pack("<I", 0) if trigger_payload is None else trigger_payload
    )
    standing_wave_payload = (
        struct.pack("<I", 0) if standing_wave_payload is None else standing_wave_payload
    )
    waypoint_list_payload = (
        struct.pack("<I", 0) if waypoint_list_payload is None else waypoint_list_payload
    )
    if callable(mp_position_payload):
        mp_position_payload = mp_position_payload(indexes)
    if callable(sides_payload):
        sides_payload = sides_payload(indexes)
    if callable(teams_payload):
        teams_payload = teams_payload(indexes)
    if callable(library_map_lists_payload):
        library_map_lists_payload = library_map_lists_payload(indexes)
    mp_position_payload = (
        b"".join(_mp_position_info(indexes) for _ in range(8))
        if mp_position_payload is None
        else mp_position_payload
    )
    sides_payload = (
        _sides_list(indexes, version=sides_version)
        if sides_payload is None
        else sides_payload
    )
    teams_payload = _teams(indexes) if teams_payload is None else teams_payload
    library_map_lists_payload = (
        _library_map_lists(indexes)
        if library_map_lists_payload is None
        else library_map_lists_payload
    )
    if not all(
        isinstance(value, bytes)
        for value in (
            mp_position_payload,
            sides_payload,
            teams_payload,
            library_map_lists_payload,
        )
    ):
        raise AssertionError("synthetic setup payload factories must return bytes")
    setup_records = {
        "MPPositionList": _record(
            "MPPositionList", mp_position_version, mp_position_payload, indexes
        ),
        "SidesList": _record("SidesList", sides_version, sides_payload, indexes),
        "Teams": _record("Teams", teams_version, teams_payload, indexes),
        "LibraryMapLists": _record(
            "LibraryMapLists",
            library_map_lists_version,
            library_map_lists_payload,
            indexes,
        ),
    }
    if omit_setup_chunk is not None and omit_setup_chunk not in setup_records:
        raise AssertionError(f"unknown synthetic setup chunk: {omit_setup_chunk!r}")
    setup_sequence = [
        setup_records[name]
        for name in ("MPPositionList", "SidesList", "LibraryMapLists", "Teams")
        if name != omit_setup_chunk
    ]
    top = b"".join(
        [
            _record("HeightMapData", 5, height_payload, indexes),
            _record("BlendTileData", blend_version, blend, indexes),
            *setup_sequence,
            _record("ObjectsList", 3, objects, indexes),
            _record("TriggerAreas", trigger_version, trigger_payload, indexes),
            _record("StandingWaterAreas", 2, standing, indexes),
            _record("RiverAreas", river_version, rivers, indexes),
            _record(
                "StandingWaveAreas",
                standing_wave_version,
                standing_wave_payload,
                indexes,
            ),
            _record("PlayerScriptsList", player_scripts_version, scripts, indexes),
            _record(
                "WaypointsList", waypoint_list_version, waypoint_list_payload, indexes
            ),
        ]
    )
    if duplicate_trigger:
        top += _record("TriggerAreas", trigger_version, trigger_payload, indexes)
    if duplicate_standing_wave:
        top += _record(
            "StandingWaveAreas",
            standing_wave_version,
            standing_wave_payload,
            indexes,
        )
    if duplicate_setup_chunk is not None:
        top += setup_records[duplicate_setup_chunk]
    if extra_top_record is not None:
        top += _record(*extra_top_record, indexes)
    body = b"CkMp" + name_table + top
    return (
        _refpack_literals(body) if compressed else body,
        struct.pack(f"<{area}H", *elevations),
    )


def _synthetic_base_template(
    *,
    template_names: tuple[str, ...] = ("MenFortressCitadel",),
    object_type_names: tuple[str, ...] = ("MenFortressCitadel",),
    property_key: str = "Fortress_Men",
    castle_version: int = 5,
    trailing: bytes = b"",
) -> bytes:
    names = [
        "HeightMapData",
        "ObjectsList",
        "Object",
        "CastleTemplates",
        property_key,
    ]
    indexes = {name: index + 1 for index, name in enumerate(names)}
    name_table = struct.pack("<I", len(names))
    for index in range(len(names), 0, -1):
        name_table += _dotnet_string(names[index - 1]) + struct.pack("<I", index)

    height = struct.pack("<IIIII4H", 2, 2, 0, 0, 4, 100, 100, 100, 100)
    objects = b"".join(
        _map_object(type_name, (10.0, 10.0, 0.0), [], indexes)
        for type_name in object_type_names
    )
    castle = bytes([3]) + indexes[property_key].to_bytes(3, "little")
    castle += struct.pack("<I", len(template_names))
    for index, template_name in enumerate(template_names):
        castle += _u16_string("") + _u16_string(template_name)
        castle += struct.pack("<ffff", float(index), 0.0, 0.0, 0.0)
        if castle_version >= 4:
            castle += struct.pack("<II", 40, 1)
    if castle_version >= 2:
        castle += struct.pack("<I", 0)
    castle += trailing
    top = b"".join(
        [
            _record("HeightMapData", 5, height, indexes),
            _record("ObjectsList", 3, objects, indexes),
            _record("CastleTemplates", castle_version, castle, indexes),
        ]
    )
    return _refpack_literals(b"CkMp" + name_table + top)


class SageMapTests(unittest.TestCase):
    def test_parses_base_template_castle_handoff_exactly(self) -> None:
        source = _synthetic_base_template(
            template_names=(
                "MenFortressExpansionPadSide",
                "MenFortressCitadel",
            ),
            object_type_names=(
                "MenFortressCitadel",
                "MenFortressExpansionPadSide",
                "MenFortressExpansionPadSide",
            ),
        )
        first = parse_sage_base_template_bytes(source)
        second = parse_sage_base_template_bytes(source)
        self.assertEqual(first, second)
        self.assertEqual(
            first["castleTemplates"]["propertyKey"],
            {
                "name": "Fortress_Men",
                "wireType": "ascii-string",
                "wireTypeCode": 3,
            },
        )
        self.assertEqual(
            [item["templateName"] for item in first["castleTemplates"]["templates"]],
            ["MenFortressExpansionPadSide", "MenFortressCitadel"],
        )
        self.assertEqual(
            first["objects"]["typeCounts"],
            [
                {"typeName": "MenFortressCitadel", "count": 1},
                {"typeName": "MenFortressExpansionPadSide", "count": 2},
            ],
        )
        self.assertTrue(first["objects"]["allCastleTemplateTypesPresent"])
        self.assertEqual(first["source"]["sha256"], hashlib.sha256(source).hexdigest())
        self.assertFalse(first["source"]["packaged"])

    def test_base_template_rejects_unbound_or_malformed_castle_entries(self) -> None:
        with self.assertRaisesRegex(
            SageMapError,
            "references object types absent from ObjectsList: MenFortressCitadel",
        ):
            parse_sage_base_template_bytes(
                _synthetic_base_template(object_type_names=("DifferentObject",))
            )
        with self.assertRaisesRegex(SageMapError, "unexplained bytes"):
            parse_sage_base_template_bytes(_synthetic_base_template(trailing=b"opaque"))

    def test_default_and_explicit_multiplayer_profiles_are_byte_identical(self) -> None:
        source, _ = _synthetic_map()
        self.assertEqual(
            parse_sage_map_bytes(source),
            parse_sage_map_bytes(source, profile="multiplayer"),
        )
        self.assertEqual(
            census_sage_map_bytes(source),
            census_sage_map_bytes(source, profile="multiplayer"),
        )

        sparse, _ = _synthetic_map(start_name="Player_2_Start")
        errors: list[str] = []
        for explicit_profile in (False, True):
            with self.assertRaises(SageMapError) as caught:
                if explicit_profile:
                    parse_sage_map_bytes(sparse, profile="multiplayer")
                else:
                    parse_sage_map_bytes(sparse)
            errors.append(str(caught.exception))
        self.assertEqual(errors[0], errors[1])

        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            source_path = root / "test.map"
            source_path.write_bytes(source)
            default_output = root / "default"
            explicit_output = root / "explicit"
            metadata = {"id": "test.map.synthetic", "displayName": "Synthetic Map"}
            default_paths = convert_sage_map(source_path, default_output, metadata)
            explicit_paths = convert_sage_map(
                source_path,
                explicit_output,
                metadata,
                profile="multiplayer",
            )
            self.assertEqual(
                [path.relative_to(default_output) for path in default_paths],
                [path.relative_to(explicit_output) for path in explicit_paths],
            )
            for default_path, explicit_path in zip(
                default_paths, explicit_paths, strict=True
            ):
                self.assertEqual(default_path.read_bytes(), explicit_path.read_bytes())

    def test_current_chunk_versions_and_v18_cook_remain_byte_exact(self) -> None:
        source, _ = _synthetic_map()
        self.assertEqual(
            hashlib.sha256(source).hexdigest(),
            "223c7f15241dcb9c28fecb6ae86d6d36c4f8e11ad912b71fb31ff346bd1eb4dd",
        )
        parsed = parse_sage_map_bytes(source)
        self.assertEqual(parsed.source_chunk_layouts, {})

        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            source_path = root / "current.map"
            source_path.write_bytes(source)
            output = root / "cooked"
            paths = convert_sage_map(
                source_path,
                output,
                {"id": "test.map.synthetic", "displayName": "Synthetic Map"},
            )
            digest = hashlib.sha256()
            for path in paths:
                relative = path.relative_to(output).as_posix().encode("utf-8")
                payload = path.read_bytes()
                digest.update(relative + b"\0")
                digest.update(len(payload).to_bytes(8, "little"))
                digest.update(payload)
            self.assertEqual(
                digest.hexdigest(),
                "bb2e75d74d3b551ae91ef01056e9336a5942a9d76eb97f4a7ab604b00eb1434e",
            )
            # The map document publishes its own lobby capacity so a runtime
            # map list never has to depend on an optional catalog row.
            map_data = json.loads((output / "map.json").read_text(encoding="utf-8"))
            self.assertEqual(map_data["playerCount"], 1)
            self.assertEqual(
                map_data["playerCapacity"],
                {
                    "playerStartCount": 1,
                    "declaredPlayerCount": 1,
                    "lobbySlotCount": 8,
                    "scenarioPlayerCount": 1,
                    "source": "authored-player-start-waypoints",
                    "startBindings": [
                        {
                            "playerIndex": 1,
                            "waypointId": 7,
                            "waypointName": "Player_1_Start",
                        }
                    ],
                },
            )

    def test_blend_v8_v9_v11_preserve_exact_16_bit_layouts_and_present_grids(
        self,
    ) -> None:
        raw_layers = {
            "impassability": b"\x01\x02",
            "impassabilityToPlayers": b"\x02\x04",
            "passageWidths": b"\x04\x01",
            "taintability": b"\x03\x05",
            "extraPassability": b"\x06\x07",
            "flammability": bytes([0, 1, 2, 3, 4, 5]),
            "visibility": b"\x07\x00",
        }
        for version in (8, 9, 11):
            with self.subTest(version=version):
                presence = _versioned_blend_presence(version)
                source, _ = _synthetic_map(
                    blend_version=version,
                    blend_layer_payloads=raw_layers,
                )
                parsed = parse_sage_map_bytes(source)
                expected_layout = {
                    "sourceVersion": version,
                    "blendCellWordBits": 16,
                    "sourceLayerPresence": presence,
                    "structuralConversion": "lossless-source-layer-preservation",
                    "runtimeDefaultParity": "unproven",
                }
                self.assertEqual(
                    parsed.source_chunk_layouts, {"BlendTileData": expected_layout}
                )
                self.assertEqual(parsed.blend["sourceLayerPresence"], presence)
                self.assertEqual(
                    parsed.blend["structuralConversion"],
                    "lossless-source-layer-preservation",
                )
                self.assertEqual(parsed.blend["runtimeDefaultParity"], "unproven")
                for name, payload in _TERRAIN_LEGACY_16_BLEND_LAYERS.items():
                    self.assertEqual(parsed.terrain_source_layers[name], payload)
                for name, present in presence.items():
                    if present:
                        self.assertEqual(
                            parsed.terrain_source_layers[name], raw_layers[name]
                        )
                    else:
                        self.assertNotIn(name, parsed.terrain_source_layers)
                expected_grid_stats = {
                    summary_name
                    for name, summary_name in (
                        ("impassability", "impassable"),
                        ("impassabilityToPlayers", "impassableToPlayers"),
                        ("passageWidths", "passageWidth"),
                        ("taintability", "taintable"),
                        ("extraPassability", "extraPassability"),
                        ("visibility", "visible"),
                    )
                    if presence[name]
                }
                self.assertEqual(set(parsed.blend["gridStats"]), expected_grid_stats)
                self.assertNotIn("flammabilityCounts", parsed.blend)

                census = census_sage_map_bytes(source)
                self.assertTrue(census["strictCook"]["accepted"])
                row = next(
                    item for item in census["chunks"] if item["name"] == "BlendTileData"
                )
                self.assertEqual(row["version"], version)
                self.assertEqual(row["probeStatus"], "parsed")

                with tempfile.TemporaryDirectory() as raw:
                    root = Path(raw)
                    source_path = root / f"blend-v{version}.map"
                    source_path.write_bytes(source)
                    cooked_trees: list[dict[str, bytes]] = []
                    backtests: list[dict[str, object]] = []
                    for output_name in ("cooked-a", "cooked-b"):
                        output = root / output_name
                        convert_sage_map(
                            source_path,
                            output,
                            {
                                "id": f"test.map.blend-v{version}",
                                "displayName": f"Blend v{version}",
                            },
                        )
                        cooked_trees.append(
                            {
                                path.relative_to(output).as_posix(): path.read_bytes()
                                for path in output.iterdir()
                                if path.is_file()
                            }
                        )
                        backtest = validate_cooked_sage_map(output)
                        self.assertTrue(backtest["valid"], backtest["errors"])
                        self.assertEqual(
                            backtest["facts"]["checkedFileCount"],
                            backtest["facts"]["requiredFileCount"],
                        )
                        self.assertFalse(backtest["gameplayFidelityClaimed"])
                        backtests.append(backtest)

                        terrain = json.loads(
                            (output / "terrain.json").read_text(encoding="utf-8")
                        )
                        source_descriptors = terrain["sourceLayers"]["layers"]
                        for name, stem in (
                            ("blendCells", "terrain-blend-cells"),
                            (
                                "threeWayBlendCells",
                                "terrain-three-way-blend-cells",
                            ),
                            ("cliffCells", "terrain-cliff-cells"),
                        ):
                            descriptor = source_descriptors[name]
                            self.assertEqual(descriptor["path"], f"{stem}.u16")
                            self.assertEqual(descriptor["encoding"], "opaque-uint16")
                            self.assertEqual(descriptor["cellSizeBytes"], 2)
                            self.assertTrue((output / f"{stem}.u16").is_file())
                            self.assertFalse((output / f"{stem}.u32").exists())
                        contract = terrain["sourceLayers"]["versionedBlendLayers"]
                        self.assertEqual(contract["sourceVersion"], version)
                        self.assertEqual(contract["blendCellWordBits"], 16)
                        self.assertEqual(
                            contract["structuralConversion"],
                            "lossless-source-layer-preservation",
                        )
                        self.assertEqual(contract["runtimeDefaultParity"], "unproven")
                        for name, present in presence.items():
                            descriptor = contract["layers"][name]
                            target = output / _VERSIONED_BLEND_LAYER_PATHS[name]
                            if present:
                                self.assertTrue(descriptor["present"])
                                self.assertEqual(target.read_bytes(), raw_layers[name])
                            else:
                                self.assertEqual(
                                    descriptor,
                                    {
                                        "present": False,
                                        "absence": "not-present-in-source-version",
                                    },
                                )
                                self.assertFalse(target.exists())
                    self.assertEqual(cooked_trees[0], cooked_trees[1])
                    self.assertEqual(backtests[0], backtests[1])

    def test_blend_v14_v16_preserve_exact_present_layers_and_explicit_absence(
        self,
    ) -> None:
        raw_layers = {
            "impassability": b"\x01\x02",
            "impassabilityToPlayers": b"\x02\x04",
            "passageWidths": b"\x04\x01",
            "taintability": b"\x03\x05",
            "extraPassability": b"\x06\x07",
            "flammability": bytes([0, 1, 2, 3, 4, 5]),
            "visibility": b"\x07\x00",
        }
        common_source_layers = set(_TERRAIN_SOURCE_LAYERS)
        for version in (14, 15, 16):
            with self.subTest(version=version):
                presence = _versioned_blend_presence(version)
                source, _ = _synthetic_map(
                    blend_version=version,
                    blend_layer_payloads=raw_layers,
                )
                parsed = parse_sage_map_bytes(source)
                expected_layout = {
                    "sourceVersion": version,
                    "blendCellWordBits": 32,
                    "sourceLayerPresence": presence,
                    "structuralConversion": "lossless-source-layer-preservation",
                    "runtimeDefaultParity": "unproven",
                }
                self.assertEqual(
                    parsed.source_chunk_layouts, {"BlendTileData": expected_layout}
                )
                self.assertEqual(parsed.blend["sourceLayerPresence"], presence)
                self.assertEqual(
                    parsed.blend["structuralConversion"],
                    "lossless-source-layer-preservation",
                )
                self.assertEqual(parsed.blend["runtimeDefaultParity"], "unproven")
                self.assertEqual(
                    set(parsed.terrain_source_layers),
                    common_source_layers
                    | {name for name, present in presence.items() if present},
                )
                for name, present in presence.items():
                    if present:
                        self.assertEqual(
                            parsed.terrain_source_layers[name], raw_layers[name]
                        )
                    else:
                        self.assertNotIn(name, parsed.terrain_source_layers)
                self.assertEqual(
                    "extraPassability" in parsed.blend["gridStats"],
                    presence["extraPassability"],
                )
                self.assertEqual(
                    "flammabilityCounts" in parsed.blend,
                    presence["flammability"],
                )
                self.assertNotIn("visible", parsed.blend["gridStats"])

                census = census_sage_map_bytes(source)
                self.assertTrue(census["strictCook"]["accepted"])
                row = next(
                    item for item in census["chunks"] if item["name"] == "BlendTileData"
                )
                self.assertEqual(row["version"], version)
                self.assertEqual(row["probeStatus"], "parsed")

                with tempfile.TemporaryDirectory() as raw:
                    root = Path(raw)
                    source_path = root / f"blend-v{version}.map"
                    source_path.write_bytes(source)
                    output = root / "cooked"
                    convert_sage_map(
                        source_path,
                        output,
                        {
                            "id": f"test.map.blend-v{version}",
                            "displayName": f"Blend v{version}",
                        },
                    )
                    terrain = json.loads((output / "terrain.json").read_text("utf-8"))
                    contract = terrain["sourceLayers"]["versionedBlendLayers"]
                    self.assertEqual(contract["sourceVersion"], version)
                    self.assertEqual(contract["blendCellWordBits"], 32)
                    self.assertEqual(
                        contract["structuralConversion"],
                        "lossless-source-layer-preservation",
                    )
                    self.assertEqual(contract["runtimeDefaultParity"], "unproven")
                    self.assertEqual(set(contract["layers"]), set(presence))
                    for name, present in presence.items():
                        descriptor = contract["layers"][name]
                        target = output / _VERSIONED_BLEND_LAYER_PATHS[name]
                        if present:
                            self.assertTrue(descriptor["present"])
                            self.assertEqual(target.read_bytes(), raw_layers[name])
                            self.assertEqual(
                                descriptor["sha256"],
                                hashlib.sha256(raw_layers[name]).hexdigest(),
                            )
                        else:
                            self.assertEqual(
                                descriptor,
                                {
                                    "present": False,
                                    "absence": "not-present-in-source-version",
                                },
                            )
                            self.assertFalse(target.exists())
                    backtest = validate_cooked_sage_map(output)
                    self.assertTrue(backtest["valid"], backtest["errors"])
                    self.assertEqual(
                        backtest["facts"]["checkedFileCount"],
                        backtest["facts"]["requiredFileCount"],
                    )
                    self.assertFalse(backtest["gameplayFidelityClaimed"])

    def test_field_exact_legacy_cluster_parses_censuses_cooks_and_backtests(
        self,
    ) -> None:
        script_sentinel = b"DO_NOT_CONVERT_LEGACY_SCRIPT_BODY"
        source, _ = _synthetic_map(
            blend_version=17,
            sides_version=5,
            player_scripts_version=5,
            script_child_payload=script_sentinel,
            river_version=1,
            standing_wave_version=1,
            standing_wave_payload=_standing_wave_areas(
                version=1,
                points=((5.5, 6.5),),
            ),
        )
        parsed = parse_sage_map_bytes(source)
        expected_layouts = {
            "BlendTileData": {
                "sourceVersion": 17,
                "layoutCompatibleWithVersion": 18,
            },
            "SidesList": {
                "sourceVersion": 5,
                "unknownBooleanPresent": False,
            },
            "RiverAreas": {
                "sourceVersion": 1,
                "layoutCompatibleWithVersion": 2,
            },
            "StandingWaveAreas": {
                "sourceVersion": 1,
                "pcaWaveFieldPresent": False,
            },
            "PlayerScriptsList": {
                "sourceVersion": 5,
                "nestedScriptListVersion": 1,
            },
        }
        self.assertEqual(parsed.source_chunk_layouts, expected_layouts)
        self.assertIsNone(parsed.setup["sidesUnknownBoolean"])
        self.assertFalse(parsed.setup["sidesUnknownBooleanPresent"])
        self.assertEqual(parsed.setup["sourceVersions"]["SidesList"], 5)
        self.assertEqual(parsed.setup["sourceVersions"]["PlayerScriptsList"], 5)
        self.assertEqual(parsed.setup["sourceVersions"]["ScriptList"], 1)
        self.assertEqual(parsed.setup["sourceChunkLayouts"], expected_layouts)
        self.assertEqual(
            parsed.script_summary,
            {"listCount": 1, "nonemptyListCount": 1},
        )
        wave = parsed.standing_waves[0]
        self.assertFalse(wave["pcaWaveFieldPresent"])
        self.assertNotIn("pcaWave", wave)
        chunks = {item["name"]: item for item in parsed.chunks}
        self.assertFalse(chunks["SidesList"]["unknownBooleanPresent"])
        self.assertFalse(chunks["StandingWaveAreas"]["pcaWaveFieldPresent"])

        census = census_sage_map_bytes(source)
        self.assertTrue(census["strictCook"]["accepted"])
        for name, version in (
            ("BlendTileData", 17),
            ("SidesList", 5),
            ("RiverAreas", 1),
            ("StandingWaveAreas", 1),
            ("PlayerScriptsList", 5),
        ):
            row = next(
                item
                for item in census["chunks"]
                if item["name"] == name and item["version"] == version
            )
            self.assertEqual(row["probeStatus"], "parsed")

        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            source_path = root / "legacy.map"
            source_path.write_bytes(source)
            output = root / "cooked"
            paths = convert_sage_map(
                source_path,
                output,
                {"id": "test.map.legacy", "displayName": "Legacy Layout"},
            )
            setup = json.loads((output / "setup.json").read_text("utf-8"))
            inventory = json.loads((output / "chunks.json").read_text("utf-8"))
            map_data = json.loads((output / "map.json").read_text("utf-8"))
            self.assertEqual(setup["sourceChunkLayouts"], expected_layouts)
            self.assertEqual(
                inventory["conversionEvidence"]["sourceChunkLayouts"],
                expected_layouts,
            )
            self.assertEqual(
                map_data["conversionEvidence"]["sourceChunkLayouts"],
                expected_layouts,
            )
            self.assertEqual(map_data["conversionStatus"]["scripts"], "not-converted")
            self.assertNotIn(
                script_sentinel, b"".join(path.read_bytes() for path in paths)
            )
            backtest = validate_cooked_sage_map(output)
            self.assertTrue(backtest["valid"], backtest["errors"])

    def test_legacy_layout_fields_match_their_current_counterparts(self) -> None:
        blend17 = parse_sage_map_bytes(_synthetic_map(blend_version=17)[0])
        blend18 = parse_sage_map_bytes(_synthetic_map(blend_version=18)[0])
        self.assertEqual(blend17.impassability, blend18.impassability)
        self.assertEqual(blend17.terrain_source_layers, blend18.terrain_source_layers)
        blend17_summary = dict(blend17.blend)
        blend18_summary = dict(blend18.blend)
        self.assertEqual(blend17_summary.pop("version"), 17)
        self.assertEqual(blend18_summary.pop("version"), 18)
        self.assertEqual(blend17_summary, blend18_summary)

        river1 = parse_sage_map_bytes(_synthetic_map(river_version=1)[0])
        river2 = parse_sage_map_bytes(_synthetic_map(river_version=2)[0])
        self.assertEqual(river1.rivers, river2.rivers)

        sides5 = parse_sage_map_bytes(_synthetic_map(sides_version=5)[0])
        sides6 = parse_sage_map_bytes(_synthetic_map(sides_version=6)[0])
        self.assertEqual(
            sides5.setup["scenarioPlayers"],
            sides6.setup["scenarioPlayers"],
        )
        self.assertIsNone(sides5.setup["sidesUnknownBoolean"])
        self.assertFalse(sides6.setup["sidesUnknownBoolean"])

        wave1 = parse_sage_map_bytes(
            _synthetic_map(
                standing_wave_version=1,
                standing_wave_payload=_standing_wave_areas(version=1),
            )[0]
        ).standing_waves[0]
        wave2 = parse_sage_map_bytes(
            _synthetic_map(
                standing_wave_version=2,
                standing_wave_payload=_standing_wave_areas(version=2, pca_wave=0),
            )[0]
        ).standing_waves[0]
        self.assertFalse(wave1.pop("pcaWaveFieldPresent"))
        self.assertFalse(wave2.pop("pcaWave"))
        self.assertEqual(wave1, wave2)

    def test_player_script_outer_v5_and_v6_keep_nested_v1_framing(self) -> None:
        for version in (5, 6):
            with self.subTest(version=version):
                source, _ = _synthetic_map(
                    player_scripts_version=version,
                    script_child_payload=b"opaque-script-body",
                )
                parsed = parse_sage_map_bytes(source)
                self.assertEqual(
                    parsed.script_summary,
                    {"listCount": 1, "nonemptyListCount": 1},
                )
                self.assertEqual(
                    parsed.source_chunk_layouts["PlayerScriptsList"],
                    {
                        "sourceVersion": version,
                        "nestedScriptListVersion": 1,
                    },
                )
                census = census_sage_map_bytes(source)
                self.assertTrue(census["strictCook"]["accepted"])
                row = next(
                    item
                    for item in census["chunks"]
                    if item["name"] == "PlayerScriptsList"
                )
                self.assertEqual(row["probeStatus"], "parsed")
                with tempfile.TemporaryDirectory() as raw:
                    root = Path(raw)
                    source_path = root / f"scripts-v{version}.map"
                    source_path.write_bytes(source)
                    output = root / "cooked"
                    convert_sage_map(
                        source_path,
                        output,
                        {
                            "id": f"test.map.scripts-v{version}",
                            "displayName": f"Scripts v{version}",
                        },
                    )
                    map_data = json.loads((output / "map.json").read_text("utf-8"))
                    self.assertEqual(
                        map_data["conversionStatus"]["scripts"],
                        "not-converted",
                    )
                    backtest = validate_cooked_sage_map(output)
                    self.assertTrue(backtest["valid"], backtest["errors"])

    def test_legacy_version_boundaries_keep_strict_and_census_in_agreement(
        self,
    ) -> None:
        cases = (
            ("BlendTileData", 10, _synthetic_map(blend_version=10)[0]),
            ("BlendTileData", 12, _synthetic_map(blend_version=12)[0]),
            ("BlendTileData", 13, _synthetic_map(blend_version=13)[0]),
            ("BlendTileData", 19, _synthetic_map(blend_version=19)[0]),
            (
                "SidesList",
                4,
                _synthetic_map(sides_version=4, sides_payload=b"")[0],
            ),
            (
                "PlayerScriptsList",
                2,
                _synthetic_map(player_scripts_version=2)[0],
            ),
            ("RiverAreas", 3, _synthetic_map(river_version=3)[0]),
            (
                "StandingWaveAreas",
                3,
                _synthetic_map(standing_wave_version=3)[0],
            ),
        )
        for name, version, source in cases:
            with self.subTest(name=name, version=version):
                with self.assertRaisesRegex(
                    SageMapError, f"unsupported {name} version"
                ):
                    parse_sage_map_bytes(source)
                census = census_sage_map_bytes(source)
                row = next(
                    item
                    for item in census["chunks"]
                    if item["name"] == name and item["version"] == version
                )
                self.assertEqual(row["probeStatus"], "unsupported-version")
                self.assertFalse(census["strictCook"]["accepted"])
                self.assertIn(
                    f"unsupported {name} version",
                    census["strictCook"]["reason"],
                )

    def test_legacy_layouts_reject_truncation_and_extra_fields(self) -> None:
        cases = {
            "blend8-truncated": _synthetic_map(
                blend_version=8,
                blend_payload=b"",
            )[0],
            "blend8-extra": _synthetic_map(
                blend_version=8,
                blend_trailing=b"x",
            )[0],
            "blend9-truncated": _synthetic_map(
                blend_version=9,
                blend_payload=b"",
            )[0],
            "blend9-extra": _synthetic_map(
                blend_version=9,
                blend_trailing=b"x",
            )[0],
            "blend11-truncated": _synthetic_map(
                blend_version=11,
                blend_payload=b"",
            )[0],
            "blend11-extra": _synthetic_map(
                blend_version=11,
                blend_trailing=b"x",
            )[0],
            "blend14-truncated": _synthetic_map(
                blend_version=14,
                blend_payload=b"",
            )[0],
            "blend14-extra": _synthetic_map(
                blend_version=14,
                blend_trailing=b"x",
            )[0],
            "blend15-truncated": _synthetic_map(
                blend_version=15,
                blend_payload=b"",
            )[0],
            "blend15-extra": _synthetic_map(
                blend_version=15,
                blend_trailing=b"x",
            )[0],
            "blend16-truncated": _synthetic_map(
                blend_version=16,
                blend_payload=b"",
            )[0],
            "blend16-extra": _synthetic_map(
                blend_version=16,
                blend_trailing=b"x",
            )[0],
            "blend17-truncated": _synthetic_map(
                blend_version=17,
                blend_payload=b"",
            )[0],
            "blend17-extra": _synthetic_map(
                blend_version=17,
                blend_trailing=b"x",
            )[0],
            "sides5-truncated": _synthetic_map(
                sides_version=5,
                sides_payload=b"",
            )[0],
            "sides5-extra": _synthetic_map(
                sides_version=5,
                sides_payload=lambda indexes: _sides_list(
                    indexes,
                    version=5,
                    trailing=b"x",
                ),
            )[0],
            "river1-truncated": _synthetic_map(
                river_version=1,
                river_payload=b"\0\0\0",
            )[0],
            "river1-extra": _synthetic_map(
                river_version=1,
                river_payload=_river_areas(trailing=b"x"),
            )[0],
            "wave1-truncated": _synthetic_map(
                standing_wave_version=1,
                standing_wave_payload=_standing_wave_areas(version=1)[:-1],
            )[0],
            "wave1-extra-pca": _synthetic_map(
                standing_wave_version=1,
                standing_wave_payload=(
                    _standing_wave_areas(version=1) + struct.pack("<I", 0)
                ),
            )[0],
            "player-scripts5-outer-extra": _synthetic_map(
                player_scripts_version=5,
                script_outer_trailing=b"x",
            )[0],
        }
        for name, source in cases.items():
            with self.subTest(name=name):
                with self.assertRaisesRegex(
                    SageMapError, "truncated|unexplained bytes"
                ):
                    parse_sage_map_bytes(source)

    def test_scenario_profile_preserves_zero_and_sparse_player_start_indices(
        self,
    ) -> None:
        zero_start_source, _ = _synthetic_map(start_name="Scenario_Entry")
        with self.assertRaisesRegex(SageMapError, "no player-start waypoints"):
            parse_sage_map_bytes(zero_start_source)
        zero_start = parse_sage_map_bytes(zero_start_source, profile="scenario")
        self.assertEqual(zero_start.setup["playerStartIndices"], [])
        self.assertEqual(zero_start.setup["declaredPlayerCount"], 0)
        self.assertEqual(zero_start.setup["lobbyRuleStatus"], "not-applicable")
        self.assertTrue(zero_start.setup["runnable"])

        sparse_source, _ = _synthetic_map(
            start_name="Player_2_Start",
            extra_waypoints=((8, "Player_4_Start", (15.0, 5.0, 0.0)),),
        )
        with self.assertRaisesRegex(SageMapError, "noncontiguous"):
            parse_sage_map_bytes(sparse_source)
        sparse = parse_sage_map_bytes(sparse_source, profile="scenario")
        self.assertEqual(sparse.setup["playerStartIndices"], [2, 4])
        self.assertEqual(
            sorted(int(item["playerIndex"]) for item in sparse.player_starts.values()),
            [2, 4],
        )
        for field in (
            "playerStartsContiguous",
            "startCountWithinLobbySlots",
            "startCountWithinScenarioPlayers",
        ):
            self.assertEqual(sparse.setup["crossChecks"][field], "not-applicable")
        strict_cook = census_sage_map_bytes(sparse_source, profile="scenario")[
            "strictCook"
        ]
        self.assertTrue(strict_cook["accepted"])
        self.assertEqual(strict_cook["mapKind"], "scenario")
        self.assertEqual(strict_cook["profileVersion"], 1)
        self.assertTrue(strict_cook["runnable"])

        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            source_path = root / "scenario.map"
            source_path.write_bytes(sparse_source)
            output = root / "cooked"
            convert_sage_map(
                source_path,
                output,
                {"id": "test.map.scenario", "displayName": "Synthetic Scenario"},
                profile="scenario",
            )
            waypoints = json.loads((output / "waypoints.json").read_text("utf-8"))
            self.assertEqual(
                [item["playerIndex"] for item in waypoints["playerStartBindings"]],
                [2, 4],
            )

    def test_library_and_placeholder_profiles_cook_one_cell_structural_maps(
        self,
    ) -> None:
        source, elevations = _synthetic_map(height_dimensions=(1, 1))
        for strict_profile in ("multiplayer", "scenario"):
            with self.subTest(strict_profile=strict_profile):
                with self.assertRaisesRegex(
                    SageMapError, "invalid heightmap dimensions: 1x1"
                ):
                    parse_sage_map_bytes(source, profile=strict_profile)

        for structural_profile in ("library", "placeholder"):
            with self.subTest(structural_profile=structural_profile):
                parsed = parse_sage_map_bytes(source, profile=structural_profile)
                self.assertEqual(
                    (parsed.heightmap.width, parsed.heightmap.height), (1, 1)
                )
                self.assertFalse(parsed.setup["runnable"])
                self.assertEqual(
                    parsed.setup["structuralStatus"], "non-runnable-structural-map"
                )
                strict_cook = census_sage_map_bytes(source, profile=structural_profile)[
                    "strictCook"
                ]
                self.assertTrue(strict_cook["accepted"])
                self.assertFalse(strict_cook["runnable"])

        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            source_path = root / "placeholder.map"
            source_path.write_bytes(source)
            output = root / "cooked"
            convert_sage_map(
                source_path,
                output,
                {
                    "id": "test.map.placeholder",
                    "displayName": "Synthetic Placeholder",
                },
                profile="placeholder",
            )
            self.assertEqual((output / "heightmap.r16").read_bytes(), elevations)
            self.assertEqual((output / "impassability.bit").read_bytes(), b"\x00")
            setup = json.loads((output / "setup.json").read_text("utf-8"))
            map_data = json.loads((output / "map.json").read_text("utf-8"))
            inventory = json.loads((output / "chunks.json").read_text("utf-8"))
            expected_evidence = {
                "mapKind": "placeholder",
                "profileVersion": 1,
                "runnable": False,
                "structuralStatus": "non-runnable-structural-map",
            }
            for key, value in expected_evidence.items():
                self.assertEqual(setup[key], value)
                self.assertEqual(map_data[key], value)
                self.assertEqual(map_data["conversionEvidence"][key], value)
                self.assertEqual(inventory["conversionEvidence"][key], value)
            self.assertEqual(
                map_data["conversionStatus"]["setup"],
                "non-runnable-structural-map",
            )

    def test_missing_lobby_chunk_is_lossless_but_does_not_relax_starts(self) -> None:
        source, _ = _synthetic_map(omit_setup_chunk="MPPositionList")
        expected_layout = {
            "sourceVersion": None,
            "present": False,
            "absence": "not-present-in-source",
            "structuralConversion": "lossless-source-absence-preservation",
            "runtimeDefaultParity": (
                "not-applicable-runtime-does-not-consult-chunk"
            ),
        }
        for profile in ("multiplayer", "scenario", "library", "placeholder"):
            with self.subTest(profile=profile):
                parsed = parse_sage_map_bytes(source, profile=profile)
                self.assertEqual(parsed.setup["lobbySlotCount"], 0)
                self.assertEqual(parsed.setup["lobbySlots"], [])
                self.assertEqual(
                    parsed.setup["lobbySourceStatus"], "not-present-in-source"
                )
                self.assertNotIn("MPPositionList", parsed.setup["sourceVersions"])
                self.assertNotIn("MPPositionInfo", parsed.setup["sourceVersions"])
                self.assertEqual(
                    parsed.setup["sourceChunkLayouts"]["MPPositionList"],
                    expected_layout,
                )
                self.assertEqual(
                    parsed.setup["crossChecks"]["startCountWithinLobbySlots"],
                    "not-applicable-source-absent",
                )

        zero_start_source, _ = _synthetic_map(
            omit_setup_chunk="MPPositionList",
            start_name="OtherWaypoint",
        )
        with self.assertRaisesRegex(SageMapError, "no player-start waypoints"):
            parse_sage_map_bytes(zero_start_source, profile="multiplayer")

        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            source_path = root / "library.bse"
            source_path.write_bytes(source)
            output = root / "cooked"
            convert_sage_map(
                source_path,
                output,
                {"id": "test.map.library", "displayName": "Synthetic Library"},
                profile="library",
            )
            setup = json.loads((output / "setup.json").read_text("utf-8"))
            chunks = json.loads((output / "chunks.json").read_text("utf-8"))
            self.assertEqual(setup["lobbySourceStatus"], "not-present-in-source")
            self.assertEqual(
                chunks["conversionEvidence"]["sourceChunkLayouts"][
                    "MPPositionList"
                ],
                expected_layout,
            )
            self.assertFalse(
                any(item["name"] == "MPPositionList" for item in chunks["chunks"])
            )
            backtest = validate_cooked_sage_map(output)
            self.assertTrue(backtest["valid"], backtest["errors"])
            self.assertTrue(
                backtest["facts"]["lobbySourceAbsenceAttested"]
            )

    def test_runtime_identity_and_blend_anomalies_remain_rejected_across_profiles(
        self,
    ) -> None:
        anomaly_sources = {
            "duplicate-waypoint-id": _synthetic_map(
                extra_waypoints=((7, "Other", (15.0, 5.0, 0.0)),)
            )[0],
            "missing-waypoint-id": _synthetic_map(include_waypoint_id=False)[0],
            "unknown-blend-version": _synthetic_map(blend_version=19)[0],
        }
        expected_messages = {
            "duplicate-waypoint-id": "duplicate waypointID",
            "missing-waypoint-id": "missing an integer waypointID",
            "unknown-blend-version": "unsupported BlendTileData version",
        }
        for profile in ("multiplayer", "scenario", "library", "placeholder"):
            for name, source in anomaly_sources.items():
                with self.subTest(profile=profile, anomaly=name):
                    with self.assertRaisesRegex(SageMapError, expected_messages[name]):
                        parse_sage_map_bytes(source, profile=profile)

        mismatched_source, _ = _synthetic_map(waypoint_unique_id="Different_Waypoint")
        for profile in ("multiplayer", "scenario", "library", "placeholder"):
            with self.subTest(profile=profile, anomaly="authored-identity-mismatch"):
                parsed = parse_sage_map_bytes(mismatched_source, profile=profile)
                self.assertEqual(
                    parsed.waypoints[0]["authoredUniqueId"],
                    "Different_Waypoint",
                )
                self.assertEqual(parsed.waypoints[0]["id"], 7)

    def test_duplicate_side_names_use_ea_first_exact_lookup_and_script_ordinals(
        self,
    ) -> None:
        first_script = b"FIRST_DUPLICATE_PLAYER_SCRIPT"
        second_script = b"SECOND_DUPLICATE_PLAYER_SCRIPT"
        source, _ = _synthetic_map(
            sides_payload=lambda indexes: _sides_list(
                indexes,
                players=[
                    (
                        [
                            ("playerName", 3, "PlyrSynthetic"),
                            ("playerIsHuman", 0, True),
                        ],
                        [],
                    ),
                    (
                        [
                            ("playerName", 3, "PlyrSynthetic"),
                            ("playerIsHuman", 0, False),
                        ],
                        [],
                    ),
                ],
            ),
            teams_payload=lambda indexes: _teams(
                indexes,
                teams=[
                    [
                        ("teamName", 3, "teamPlyrSynthetic"),
                        ("teamOwner", 3, "PlyrSynthetic"),
                    ],
                    [
                        ("teamName", 3, "teamPlyrSynthetic"),
                        ("teamOwner", 3, "PlyrSynthetic"),
                    ],
                ],
            ),
            library_map_lists_payload=lambda indexes: _library_map_lists(
                indexes, lists=[(), ()]
            ),
            script_list_count=2,
            script_child_payloads=(first_script, second_script),
        )

        for profile in ("multiplayer", "scenario", "library", "placeholder"):
            with self.subTest(profile=profile):
                parsed = parse_sage_map_bytes(source, profile=profile)
                players = parsed.setup["scenarioPlayers"]
                self.assertEqual([item["index"] for item in players], [0, 1])
                self.assertEqual(
                    [item["name"] for item in players],
                    ["PlyrSynthetic", "PlyrSynthetic"],
                )
                self.assertTrue(players[0]["properties"][1]["value"])
                self.assertFalse(players[1]["properties"][1]["value"])
                semantics = parsed.setup["sideRuntimeSemantics"]
                self.assertEqual(
                    semantics["nameLookupPolicy"],
                    "exact-case-sensitive-first-source-wins",
                )
                self.assertEqual(
                    semantics["nameLookup"],
                    [{"name": "PlyrSynthetic", "sourceIndex": 0}],
                )
                self.assertEqual(
                    semantics["duplicateNameGroups"],
                    [
                        {
                            "name": "PlyrSynthetic",
                            "records": [{"sourceIndex": 0}, {"sourceIndex": 1}],
                        }
                    ],
                )
                self.assertEqual(semantics["caseFoldCollisionGroups"], [])
                self.assertEqual(
                    semantics["scriptBindings"],
                    [
                        {
                            "sourceOrdinal": 0,
                            "playerSourceIndex": 0,
                            "playerName": "PlyrSynthetic",
                        },
                        {
                            "sourceOrdinal": 1,
                            "playerSourceIndex": 1,
                            "playerName": "PlyrSynthetic",
                        },
                    ],
                )
                source_scripts = parsed.setup["sourceScriptLists"]
                self.assertEqual(
                    [item["sourceOrdinal"] for item in source_scripts], [0, 1]
                )
                self.assertEqual(
                    [item["payloadSha256"] for item in source_scripts],
                    [
                        hashlib.sha256(first_script).hexdigest(),
                        hashlib.sha256(second_script).hexdigest(),
                    ],
                )

        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            source_path = root / "duplicate-sides.map"
            source_path.write_bytes(source)
            output = root / "cooked"
            convert_sage_map(
                source_path,
                output,
                {
                    "id": "test.map.duplicate-sides",
                    "displayName": "Duplicate Sides",
                },
            )
            clean = validate_cooked_sage_map(output)
            self.assertTrue(clean["valid"], clean["errors"])
            self.assertTrue(clean["facts"]["sideSemanticsAttested"])
            self.assertFalse(clean["gameplayFidelityClaimed"])

            setup_path = output / "setup.json"
            original = json.loads(setup_path.read_text(encoding="utf-8"))

            def write_setup(value: dict[str, object]) -> None:
                setup_path.write_text(
                    json.dumps(value, indent=2, sort_keys=True) + "\n",
                    encoding="utf-8",
                )

            tampered = json.loads(json.dumps(original))
            tampered["sideRuntimeSemantics"]["nameLookup"][0]["sourceIndex"] = 1
            write_setup(tampered)
            wrong_lookup = validate_cooked_sage_map(output)
            self.assertFalse(wrong_lookup["valid"])
            self.assertTrue(
                any("derived side name lookup" in item for item in wrong_lookup["errors"]),
                wrong_lookup["errors"],
            )

            tampered = json.loads(json.dumps(original))
            tampered["scenarioPlayers"].reverse()
            write_setup(tampered)
            reordered_players = validate_cooked_sage_map(output)
            self.assertFalse(reordered_players["valid"])
            self.assertTrue(
                any(
                    "ordered scenario player record" in item
                    for item in reordered_players["errors"]
                ),
                reordered_players["errors"],
            )

            tampered = json.loads(json.dumps(original))
            tampered["sourceScriptLists"].reverse()
            write_setup(tampered)
            reordered_scripts = validate_cooked_sage_map(output)
            self.assertFalse(reordered_scripts["valid"])
            self.assertTrue(
                any(
                    "source script-list ordinal evidence" in item
                    for item in reordered_scripts["errors"]
                ),
                reordered_scripts["errors"],
            )

            tampered = json.loads(json.dumps(original))
            tampered["sideRuntimeSemantics"]["scriptBindings"][0][
                "playerSourceIndex"
            ] = 1
            write_setup(tampered)
            wrong_binding = validate_cooked_sage_map(output)
            self.assertFalse(wrong_binding["valid"])
            self.assertTrue(
                any(
                    "derived player script bindings" in item
                    for item in wrong_binding["errors"]
                ),
                wrong_binding["errors"],
            )

    def test_case_only_side_names_remain_distinct_exact_lookup_entries(self) -> None:
        source, _ = _synthetic_map(
            sides_payload=lambda indexes: _sides_list(
                indexes,
                players=[
                    ([("playerName", 3, "PlyrCase")], []),
                    ([("playerName", 3, "plyrcase")], []),
                ],
            ),
            teams_payload=lambda indexes: _teams(
                indexes,
                teams=[
                    [
                        ("teamName", 3, "teamPlyrCase"),
                        ("teamOwner", 3, "PlyrCase"),
                    ],
                    [
                        ("teamName", 3, "teamplyrcase"),
                        ("teamOwner", 3, "plyrcase"),
                    ],
                ],
            ),
            library_map_lists_payload=lambda indexes: _library_map_lists(
                indexes, lists=[(), ()]
            ),
            script_list_count=2,
        )
        parsed = parse_sage_map_bytes(source)
        semantics = parsed.setup["sideRuntimeSemantics"]
        self.assertEqual(
            semantics["nameLookup"],
            [
                {"name": "PlyrCase", "sourceIndex": 0},
                {"name": "plyrcase", "sourceIndex": 1},
            ],
        )
        self.assertEqual(semantics["duplicateNameGroups"], [])
        self.assertEqual(
            semantics["caseFoldCollisionGroups"],
            [
                {
                    "records": [
                        {"sourceIndex": 0, "name": "PlyrCase"},
                        {"sourceIndex": 1, "name": "plyrcase"},
                    ]
                }
            ],
        )

        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            source_path = root / "case-only-sides.map"
            source_path.write_bytes(source)
            output = root / "cooked"
            convert_sage_map(
                source_path,
                output,
                {
                    "id": "test.map.case-only-sides",
                    "displayName": "Case-only Sides",
                },
            )
            result = validate_cooked_sage_map(output)
            self.assertTrue(result["valid"], result["errors"])
            self.assertTrue(result["facts"]["sideSemanticsAttested"])

    def test_invalid_map_profile_is_rejected_before_conversion_outputs(self) -> None:
        source, _ = _synthetic_map()
        for operation in (
            lambda: parse_sage_map_bytes(source, profile="campaign"),
            lambda: census_sage_map_bytes(source, profile="campaign"),
        ):
            with self.assertRaisesRegex(SageMapError, "unsupported SAGE map profile"):
                operation()

        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            source_path = root / "test.map"
            source_path.write_bytes(source)
            output = root / "cooked"
            with self.assertRaisesRegex(SageMapError, "unsupported SAGE map profile"):
                convert_sage_map(source_path, output, profile="campaign")
            self.assertFalse(output.exists())

    def test_payload_free_census_preserves_neutral_supported_facts(self) -> None:
        source, _ = _synthetic_map(object_type_names=("UNIQUE_SECRET_SENTINEL",))
        census = census_sage_map_bytes(source)
        self.assertTrue(census["strictCook"]["accepted"])
        self.assertEqual(
            census["features"]["height"], {"width": 3, "height": 2, "borderWidth": 0}
        )
        self.assertEqual(census["features"]["objects"]["placementCount"], 2)
        self.assertEqual(census["features"]["objects"]["uniqueTypeCount"], 2)
        self.assertEqual(census["features"]["lobbySlotCount"], 8)
        self.assertEqual(census["features"]["scenarioPlayerCount"], 1)
        self.assertEqual(census["features"]["teamCount"], 1)
        self.assertEqual(census["features"]["libraryListCount"], 1)
        serialized = json.dumps(census)
        for secret in (
            "UNIQUE_SECRET_SENTINEL",
            "PlyrSynthetic",
            "teamPlyrSynthetic",
            "synthetic.map",
            "UpgradeSynthetic",
        ):
            with self.subTest(secret=secret):
                self.assertNotIn(secret, serialized)

    def test_census_parses_trigger_and_wave_without_leaking_payload_strings(
        self,
    ) -> None:
        source, _ = _synthetic_map(
            trigger_payload=_trigger_areas(),
            standing_wave_payload=_standing_wave_areas(),
        )
        census = census_sage_map_bytes(source)
        self.assertEqual(census["features"]["triggerAreaCount"], 1)
        self.assertEqual(census["features"]["standingWaveAreaCount"], 1)
        self.assertTrue(census["strictCook"]["accepted"])
        serialized = json.dumps(census)
        self.assertNotIn("SYNTHETIC_TRIGGER_SECRET", serialized)
        self.assertNotIn("SYNTHETIC_WAVE_SECRET", serialized)
        self.assertNotIn("SYNTHETIC_WAVE_TEXTURE_SECRET", serialized)

    def test_census_classifies_unknown_top_level_chunk(self) -> None:
        source, _ = _synthetic_map(extra_top_record=("FutureChunk", 7, b"opaque"))
        census = census_sage_map_bytes(source)
        future = next(
            item for item in census["chunks"] if item["name"] == "FutureChunk"
        )
        self.assertEqual(future["version"], 7)
        self.assertEqual(future["probeStatus"], "unclassified")
        self.assertTrue(census["strictCook"]["accepted"])

    def test_parses_bounded_required_slice(self) -> None:
        source, elevations = _synthetic_map()
        parsed = parse_sage_map_bytes(source)
        self.assertEqual((parsed.heightmap.width, parsed.heightmap.height), (3, 2))
        self.assertEqual(parsed.heightmap.encoded, elevations)
        self.assertEqual(parsed.impassability, b"\x05\x02")
        self.assertEqual(parsed.blend["gridStats"]["impassable"], 3)
        self.assertEqual(parsed.blend["textures"][0]["name"], "TestGrass")
        self.assertEqual(parsed.blend["rawBlendCount"], 3)
        self.assertEqual(parsed.blend["blendDescriptionCount"], 2)
        self.assertEqual(parsed.blend["rawCliffCount"], 2)
        self.assertEqual(parsed.blend["cliffMappingCount"], 1)
        self.assertEqual(parsed.terrain_source_layers, _TERRAIN_SOURCE_LAYERS)
        self.assertEqual(len(parsed.standing_water), 1)
        self.assertEqual(parsed.standing_water[0]["waterHeight"], 42)
        self.assertEqual(len(parsed.rivers), 1)
        self.assertEqual(parsed.rivers[0]["waterHeight"], 43)
        self.assertEqual(len(parsed.objects), 2)
        self.assertIn("Player_1_Start", parsed.player_starts)
        self.assertEqual(parsed.trigger_count, 0)
        self.assertEqual(parsed.triggers, [])
        self.assertEqual(parsed.standing_waves, [])
        self.assertEqual(
            parsed.script_summary, {"listCount": 1, "nonemptyListCount": 0}
        )
        self.assertEqual(parsed.setup["declaredPlayerCount"], 1)
        self.assertEqual(parsed.setup["lobbySlotCount"], 8)
        self.assertEqual(parsed.setup["scenarioPlayerCount"], 1)
        self.assertEqual(parsed.setup["teamCount"], 1)
        self.assertEqual(parsed.setup["extraTeamCount"], 0)
        self.assertEqual(
            {
                item["name"]: (item["wireTypeCode"], item["occurrenceCount"])
                for item in parsed.setup["unresolvedSemanticFields"]
            },
            {
                "exportWithScript": (0, 1),
                "teamInitialIdleSeconds": (1, 1),
                "teamUnitExperienceLevel1": (1, 1),
                "teamUnitUpgradeList1": (3, 1),
            },
        )
        self.assertTrue(all(parsed.setup["crossChecks"].values()))

    def test_uncompressed_ckmp_is_supported(self) -> None:
        source, _ = _synthetic_map(compressed=False)
        body, metadata = decode_sage_map_blob(source)
        self.assertEqual(body, source)
        self.assertEqual(metadata["kind"], "uncompressed")

    def test_rejects_bad_refpack_reference(self) -> None:
        compressed = bytes([0x10, 0xFB, 0, 0, 3, 0x00, 0x00])
        source = b"EAR\0" + struct.pack("<I", 3) + compressed
        with self.assertRaisesRegex(SageMapError, "back-reference"):
            decode_sage_map_blob(source)

    def test_rejects_disagreeing_envelope_size(self) -> None:
        source, _ = _synthetic_map()
        tampered = source[:4] + struct.pack("<I", 123) + source[8:]
        with self.assertRaisesRegex(SageMapError, "disagrees"):
            decode_sage_map_blob(tampered)

    def test_parses_nonempty_trigger_and_standing_wave_records_exactly(self) -> None:
        source, _ = _synthetic_map(
            trigger_payload=_trigger_areas(points=((1.5, 2.5), (3.5, 4.5))),
            standing_wave_payload=_standing_wave_areas(
                points=((5.5, 6.5),), additive=0, pca_wave=1
            ),
        )
        parsed = parse_sage_map_bytes(source)
        self.assertEqual(parsed.trigger_count, 1)
        self.assertEqual(
            parsed.triggers[0],
            {
                "id": 71,
                "name": "SYNTHETIC_TRIGGER_SECRET",
                "layer": "Trigger Layer",
                "sagePoints": [[1.5, 2.5], [3.5, 4.5]],
                "godotXZPoints": [[1.5, -2.5], [3.5, -4.5]],
            },
        )
        self.assertEqual(parsed.standing_wave_count, 1)
        wave = parsed.standing_waves[0]
        self.assertEqual(wave["id"], 72)
        self.assertEqual(wave["sagePoints"], [[5.5, 6.5]])
        self.assertEqual(wave["godotXZPoints"], [[5.5, -6.5]])
        self.assertFalse(wave["additive"])
        self.assertTrue(wave["pcaWave"])
        self.assertEqual(wave["settings"]["distanceFromShore"], 18)

    def test_nonempty_waypoint_edges_are_exact_and_endpoint_validated(self) -> None:
        source, _ = _synthetic_map(
            waypoint_list_payload=struct.pack("<Iii", 1, 7, 8),
            extra_waypoints=((8, "Path_End", (15.0, 5.0, 0.0)),),
        )
        parsed = parse_sage_map_bytes(source)
        self.assertEqual(parsed.waypoint_edges, [{"startId": 7, "endId": 8}])
        self.assertEqual(parsed.waypoint_path_count, 1)

    def test_waypoint_identity_quirks_are_lossless_and_runtime_derivations_are_attested(
        self,
    ) -> None:
        source, _ = _synthetic_map(
            waypoint_unique_id="Authored_Player_Start_Metadata",
            waypoint_list_payload=struct.pack(
                "<Iiiiiii",
                3,
                7,
                8,
                8,
                99,
                98,
                99,
            ),
            extra_waypoints=(
                (8, "Route", (15.0, 5.0, 0.0)),
                (9, "Route", (16.0, 5.0, 0.0)),
                (10, "route", (17.0, 5.0, 0.0)),
                (11, "", (18.0, 5.0, 0.0)),
            ),
        )
        parsed = parse_sage_map_bytes(source)
        self.assertEqual(
            [item["name"] for item in parsed.waypoints],
            ["Player_1_Start", "Route", "Route", "route", ""],
        )
        self.assertEqual(
            parsed.waypoints[0]["authoredUniqueId"],
            "Authored_Player_Start_Metadata",
        )
        self.assertNotIn("playerIndex", parsed.waypoints[-1])
        self.assertEqual(
            parsed.waypoint_edges,
            [
                {"startId": 7, "endId": 8},
                {"startId": 8, "endId": 99, "resolved": False},
                {"startId": 98, "endId": 99, "resolved": False},
            ],
        )
        self.assertFalse(parsed.setup["crossChecks"]["waypointEdgeEndpointsResolved"])

        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            source_path = root / "waypoint-quirks.map"
            source_path.write_bytes(source)
            output = root / "cooked"
            convert_sage_map(
                source_path,
                output,
                {
                    "id": "test.map.waypoint-quirks",
                    "displayName": "Waypoint Quirks",
                },
            )
            waypoint_path = output / "waypoints.json"
            document = json.loads(waypoint_path.read_text("utf-8"))
            semantics = document["runtimeSemantics"]
            evidence = semantics["evidence"]
            self.assertEqual(
                semantics["nameLookupPolicy"],
                "exact-case-sensitive-last-source-wins",
            )
            lookup = {item["name"]: item for item in semantics["nameLookup"]}
            self.assertEqual(lookup["Route"]["waypointId"], 9)
            self.assertEqual(lookup["Route"]["sourceIndex"], 2)
            self.assertEqual(lookup["route"]["waypointId"], 10)
            self.assertEqual(lookup[""]["waypointId"], 11)
            self.assertEqual(
                semantics["runtimeAdjacency"],
                [{"startId": 7, "endIds": [8]}],
            )
            self.assertEqual(evidence["rawEdgeCount"], 3)
            self.assertEqual(evidence["resolvedEdgeCount"], 1)
            self.assertEqual(evidence["unresolvedEdgeCount"], 2)
            self.assertEqual(evidence["duplicateNameGroupCount"], 1)
            self.assertEqual(evidence["duplicateNameRecordCount"], 2)
            self.assertEqual(evidence["caseFoldCollisionGroupCount"], 1)
            self.assertEqual(evidence["caseFoldCollisionRecordCount"], 3)
            self.assertEqual(evidence["emptyNameCount"], 1)
            self.assertEqual(evidence["authoredIdentityMismatchCount"], 1)
            self.assertEqual(
                [item["waypointName"] for item in document["playerStartBindings"]],
                ["Player_1_Start"],
            )

            clean = validate_cooked_sage_map(output)
            self.assertTrue(clean["valid"], clean["errors"])
            self.assertEqual(clean["facts"]["waypointSemanticsEvidence"], evidence)
            neutral = json.dumps(clean["facts"], sort_keys=True)
            for authored_identifier in (
                "Route",
                "route",
                "Authored_Player_Start_Metadata",
            ):
                self.assertNotIn(authored_identifier, neutral)

            original = json.loads(json.dumps(document))
            document["runtimeSemantics"]["nameLookup"][1]["waypointId"] = 8
            waypoint_path.write_text(
                json.dumps(document, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )
            tampered_lookup = validate_cooked_sage_map(output)
            self.assertFalse(tampered_lookup["valid"])
            self.assertTrue(
                any("derived name lookup" in item for item in tampered_lookup["errors"])
            )

            document = json.loads(json.dumps(original))
            document["edges"][1]["resolved"] = True
            waypoint_path.write_text(
                json.dumps(document, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )
            fabricated_resolution = validate_cooked_sage_map(output)
            self.assertFalse(fabricated_resolution["valid"])
            self.assertTrue(
                any(
                    "resolution is fabricated" in item
                    for item in fabricated_resolution["errors"]
                )
            )

            document = json.loads(json.dumps(original))
            document["runtimeSemantics"]["runtimeAdjacency"].append(
                {"startId": 8, "endIds": [99]}
            )
            waypoint_path.write_text(
                json.dumps(document, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )
            tampered_adjacency = validate_cooked_sage_map(output)
            self.assertFalse(tampered_adjacency["valid"])
            self.assertTrue(
                any(
                    "derived runtime adjacency" in item
                    for item in tampered_adjacency["errors"]
                )
            )

            document = json.loads(json.dumps(original))
            document["waypoints"][1], document["waypoints"][2] = (
                document["waypoints"][2],
                document["waypoints"][1],
            )
            waypoint_path.write_text(
                json.dumps(document, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )
            reordered_records = validate_cooked_sage_map(output)
            self.assertFalse(reordered_records["valid"])
            self.assertTrue(
                any(
                    "ordered" in item or "name lookup" in item
                    for item in reordered_records["errors"]
                )
            )

    def test_retail_waypoint_quirks_are_preserved_without_fabricating_routing(
        self,
    ) -> None:
        collision_source, _ = _synthetic_map(
            extra_waypoints=((8, "player_1_start", (15.0, 5.0, 0.0)),)
        )
        collision = parse_sage_map_bytes(collision_source)
        self.assertEqual(
            [item["name"] for item in collision.waypoints],
            ["Player_1_Start", "player_1_start"],
        )
        self.assertEqual(collision.setup["playerStartIndices"], [1])

        unresolved_source, _ = _synthetic_map(
            waypoint_list_payload=struct.pack("<Iii", 1, 7, 99)
        )
        unresolved = parse_sage_map_bytes(unresolved_source)
        self.assertEqual(
            unresolved.waypoint_edges,
            [{"startId": 7, "endId": 99, "resolved": False}],
        )
        self.assertFalse(
            unresolved.setup["crossChecks"]["waypointEdgeEndpointsResolved"]
        )

        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            source_path = root / "quirks.map"
            source_path.write_bytes(unresolved_source)
            output = root / "cooked"
            convert_sage_map(
                source_path,
                output,
                {"id": "test.map.quirks", "displayName": "Waypoint Quirks"},
            )
            waypoints = json.loads((output / "waypoints.json").read_text("utf-8"))
            self.assertEqual(
                waypoints["edges"],
                [
                    {
                        "sourceIndex": 0,
                        "startId": 7,
                        "endId": 99,
                        "resolved": False,
                    }
                ],
            )
            self.assertEqual(waypoints["runtimeSemantics"]["runtimeAdjacency"], [])
            self.assertEqual(
                waypoints["runtimeSemantics"]["evidence"]["unresolvedEdgeCount"],
                1,
            )

    def test_setup_preserves_typed_properties_build_lists_and_path_labels(self) -> None:
        source, _ = _synthetic_map(
            sides_payload=lambda indexes: _sides_list(
                indexes,
                players=[
                    (
                        [
                            ("playerName", 3, "PlyrSynthetic"),
                            ("playerIsHuman", 0, True),
                            ("playerDisplayName", 4, "Synthetic Player"),
                        ],
                        [_build_list_item()],
                    )
                ],
            ),
            waypoint_path_labels=("Path A", ""),
        )
        parsed = parse_sage_map_bytes(source)
        player = parsed.setup["scenarioPlayers"][0]
        typed = {item["name"]: item for item in player["properties"]}
        self.assertEqual(typed["playerName"]["wireType"], "ascii-string")
        self.assertEqual(typed["playerIsHuman"]["wireType"], "boolean")
        self.assertEqual(typed["playerDisplayName"]["wireType"], "unicode-string")
        build = player["buildList"][0]
        self.assertEqual(build["sagePosition"], [1.0, 2.0, 3.0])
        self.assertEqual(build["godotPosition"], [1.0, 3.0, -2.0])
        self.assertTrue(build["initiallyBuilt"])
        self.assertEqual(
            parsed.waypoints[0]["pathLabels"],
            [{"slot": 1, "value": "Path A"}, {"slot": 2, "value": ""}],
        )

    def test_setup_versions_nested_assets_and_payloads_fail_closed(self) -> None:
        cases = [
            (
                "mp-outer-version",
                {"mp_position_version": 1},
                "unsupported MPPositionList version",
            ),
            (
                "sides-version",
                {"sides_version": 4, "sides_payload": b""},
                "unsupported SidesList version",
            ),
            (
                "teams-version",
                {"teams_version": 2},
                "unsupported Teams version",
            ),
            (
                "library-outer-version",
                {"library_map_lists_version": 2},
                "unsupported LibraryMapLists version",
            ),
            (
                "player-scripts-version",
                {"player_scripts_version": 2},
                "unsupported PlayerScriptsList version",
            ),
            (
                "waypoints-version",
                {"waypoint_list_version": 2},
                "unsupported WaypointsList version",
            ),
            (
                "mp-child-name",
                {
                    "mp_position_payload": lambda indexes: _mp_position_info(
                        indexes, child_name="LibraryMaps"
                    )
                },
                "unsupported MPPositionList child",
            ),
            (
                "mp-child-version",
                {
                    "mp_position_payload": lambda indexes: _mp_position_info(
                        indexes, child_version=2
                    )
                },
                "unsupported MPPositionList child",
            ),
            (
                "library-child-name",
                {
                    "library_map_lists_payload": lambda indexes: _library_map_lists(
                        indexes, child_name="MPPositionInfo"
                    )
                },
                "unsupported LibraryMapLists child",
            ),
            (
                "library-child-version",
                {
                    "library_map_lists_payload": lambda indexes: _library_map_lists(
                        indexes, child_version=2
                    )
                },
                "unsupported LibraryMapLists child",
            ),
            (
                "script-child-name",
                {"script_child_name": "LibraryMaps"},
                "unsupported player script child",
            ),
            (
                "script-child-version",
                {"script_child_version": 2},
                "unsupported player script child",
            ),
            (
                "mp-trailing",
                {
                    "mp_position_payload": lambda indexes: _mp_position_info(
                        indexes, trailing=b"x"
                    )
                },
                "unexplained bytes",
            ),
            (
                "sides-trailing",
                {"sides_payload": lambda indexes: _sides_list(indexes, trailing=b"x")},
                "unexplained bytes",
            ),
            (
                "teams-trailing",
                {"teams_payload": lambda indexes: _teams(indexes, trailing=b"x")},
                "unexplained bytes",
            ),
            (
                "library-child-trailing",
                {
                    "library_map_lists_payload": lambda indexes: _library_map_lists(
                        indexes, child_trailing=b"x"
                    )
                },
                "unexplained bytes",
            ),
            (
                "waypoints-trailing",
                {"waypoint_list_payload": struct.pack("<I", 0) + b"x"},
                "unexplained bytes",
            ),
        ]
        for name, arguments, message in cases:
            with self.subTest(name=name):
                source, _ = _synthetic_map(**arguments)
                with self.assertRaisesRegex(SageMapError, message):
                    parse_sage_map_bytes(source)

    def test_setup_types_booleans_floats_counts_and_duplicates_fail_closed(
        self,
    ) -> None:
        cases = [
            (
                "mp-boolean",
                {
                    "mp_position_payload": lambda indexes: _mp_position_info(
                        indexes, is_human=2
                    )
                },
                "invalid boolean",
            ),
            (
                "sides-boolean",
                {
                    "sides_payload": lambda indexes: _sides_list(
                        indexes, unknown_boolean=2
                    )
                },
                "invalid boolean",
            ),
            (
                "build-float",
                {
                    "sides_payload": lambda indexes: _sides_list(
                        indexes,
                        players=[
                            (
                                [("playerName", 3, "PlyrSynthetic")],
                                [_build_list_item(position=(float("nan"), 2.0, 3.0))],
                            )
                        ],
                    )
                },
                "non-finite float",
            ),
            (
                "build-boolean",
                {
                    "sides_payload": lambda indexes: _sides_list(
                        indexes,
                        players=[
                            (
                                [("playerName", 3, "PlyrSynthetic")],
                                [_build_list_item(repairable=2)],
                            )
                        ],
                    )
                },
                "invalid boolean",
            ),
            (
                "team-owner-type",
                {
                    "teams_payload": lambda indexes: _teams(
                        indexes,
                        teams=[
                            [
                                ("teamName", 3, "teamPlyrSynthetic"),
                                ("teamOwner", 1, 7),
                            ]
                        ],
                    )
                },
                "expected 3",
            ),
            (
                "unresolved-type",
                {
                    "teams_payload": lambda indexes: _teams(
                        indexes,
                        teams=[
                            [
                                ("teamName", 3, "teamPlyrSynthetic"),
                                ("teamOwner", 3, "PlyrSynthetic"),
                                ("exportWithScript", 1, 0),
                            ]
                        ],
                    )
                },
                "expected 0",
            ),
            (
                "sides-count",
                {"sides_payload": bytes([0]) + struct.pack("<i", 1_025)},
                "player count exceeds limit",
            ),
            (
                "teams-count",
                {"teams_payload": struct.pack("<i", 65_537)},
                "Teams count exceeds limit",
            ),
            (
                "duplicate-player-property",
                {
                    "sides_payload": lambda indexes: _sides_list(
                        indexes,
                        players=[
                            (
                                [
                                    ("playerName", 3, "PlyrSynthetic"),
                                    ("playerName", 3, "PlyrSynthetic"),
                                ],
                                [],
                            )
                        ],
                    )
                },
                "duplicate player property",
            ),
            (
                "duplicate-team-property",
                {
                    "teams_payload": lambda indexes: _teams(
                        indexes,
                        teams=[
                            [
                                ("teamName", 3, "teamPlyrSynthetic"),
                                ("teamOwner", 3, "PlyrSynthetic"),
                                ("teamOwner", 3, "PlyrSynthetic"),
                            ]
                        ],
                    )
                },
                "duplicate team property",
            ),
            (
                "duplicate-library-reference",
                {
                    "library_map_lists_payload": lambda indexes: _library_map_lists(
                        indexes,
                        lists=[("maps/library.map", "MAPS\\LIBRARY.MAP")],
                    )
                },
                "duplicate or ambiguous LibraryMaps reference",
            ),
            (
                "unsafe-library-reference",
                {
                    "library_map_lists_payload": lambda indexes: _library_map_lists(
                        indexes, lists=[("../outside.map",)]
                    )
                },
                "unsafe or ambiguous LibraryMaps reference",
            ),
            (
                "duplicate-waypoint-id",
                {"extra_waypoints": ((7, "Other", (15.0, 5.0, 0.0)),)},
                "duplicate waypointID",
            ),
            (
                "duplicate-edge",
                {
                    "waypoint_list_payload": struct.pack("<Iiiii", 2, 7, 8, 7, 8),
                    "extra_waypoints": ((8, "Path_End", (15.0, 5.0, 0.0)),),
                },
                "duplicate WaypointsList edge",
            ),
            (
                "duplicate-mp-top-level",
                {"duplicate_setup_chunk": "MPPositionList"},
                "duplicate MPPositionList",
            ),
            (
                "duplicate-sides-top-level",
                {"duplicate_setup_chunk": "SidesList"},
                "duplicate SidesList",
            ),
            (
                "duplicate-teams-top-level",
                {"duplicate_setup_chunk": "Teams"},
                "duplicate Teams",
            ),
            (
                "duplicate-library-top-level",
                {"duplicate_setup_chunk": "LibraryMapLists"},
                "duplicate LibraryMapLists",
            ),
            (
                "duplicate-waypoints-top-level",
                {
                    "extra_top_record": (
                        "WaypointsList",
                        1,
                        struct.pack("<I", 0),
                    )
                },
                "duplicate WaypointsList",
            ),
            (
                "duplicate-player-scripts-top-level",
                {"extra_top_record": ("PlayerScriptsList", 1, b"")},
                "duplicate PlayerScriptsList",
            ),
        ]
        for name, arguments, message in cases:
            with self.subTest(name=name):
                source, _ = _synthetic_map(**arguments)
                with self.assertRaisesRegex(SageMapError, message):
                    parse_sage_map_bytes(source)

    def test_setup_cross_chunk_relationships_fail_closed(self) -> None:
        cases = [
            (
                "library-player-count",
                {
                    "library_map_lists_payload": lambda indexes: _library_map_lists(
                        indexes, lists=[(), ()]
                    )
                },
                "LibraryMapLists list count does not match",
            ),
            (
                "script-player-count",
                {"script_list_count": 0},
                "PlayerScriptsList list count does not match",
            ),
            (
                "owner",
                {
                    "teams_payload": lambda indexes: _teams(
                        indexes,
                        teams=[
                            [
                                ("teamName", 3, "teamMissing"),
                                ("teamOwner", 3, "Missing"),
                            ]
                        ],
                    )
                },
                "owner does not resolve",
            ),
            (
                "default-team",
                {
                    "teams_payload": lambda indexes: _teams(
                        indexes,
                        teams=[
                            [
                                ("teamName", 3, "AuthoredExtra"),
                                ("teamOwner", 3, "PlyrSynthetic"),
                            ]
                        ],
                    )
                },
                "missing a default team",
            ),
            (
                "noncontiguous-starts",
                {"start_name": "Player_2_Start"},
                "player-start waypoints are noncontiguous",
            ),
            (
                "missing-starts",
                {"start_name": "OtherWaypoint"},
                "no player-start waypoints",
            ),
            (
                "start-over-lobby-slots",
                {
                    "mp_position_payload": lambda indexes: b"",
                },
                "player-start count exceeds",
            ),
        ]
        for name, arguments, message in cases:
            with self.subTest(name=name):
                source, _ = _synthetic_map(**arguments)
                with self.assertRaisesRegex(SageMapError, message):
                    parse_sage_map_bytes(source)

    def test_default_team_owner_repair_is_preserved_and_attested(self) -> None:
        source, _ = _synthetic_map(
            sides_payload=lambda indexes: _sides_list(
                indexes,
                players=[
                    ([("playerName", 3, "PlyrSynthetic")], []),
                    ([("playerName", 3, "PlyrOther")], []),
                ],
            ),
            teams_payload=lambda indexes: _teams(
                indexes,
                teams=[
                    [
                        ("teamName", 3, "teamPlyrSynthetic"),
                        ("teamOwner", 3, "PlyrOther"),
                    ],
                    [
                        ("teamName", 3, "teamPlyrOther"),
                        ("teamOwner", 3, "PlyrOther"),
                    ],
                ],
            ),
            library_map_lists_payload=lambda indexes: _library_map_lists(
                indexes, lists=[(), ()]
            ),
            script_list_count=2,
        )
        parsed = parse_sage_map_bytes(source)

        self.assertEqual(parsed.setup["teams"][0]["owner"], "PlyrOther")
        semantics = parsed.setup["teamRuntimeSemantics"]
        self.assertEqual(
            semantics["defaultTeamOwnerRepairs"],
            [
                {
                    "playerSourceIndex": 0,
                    "teamSourceIndex": 0,
                    "teamName": "teamPlyrSynthetic",
                    "authoredOwner": "PlyrOther",
                    "runtimeOwner": "PlyrSynthetic",
                }
            ],
        )
        self.assertEqual(
            semantics["defaultTeamLookupPolicy"],
            "exact-case-sensitive-first-source-wins",
        )
        self.assertEqual(
            semantics["ownerRepairPolicy"],
            "ea-validate-sides-default-team-owner-repair",
        )
        self.assertEqual(
            semantics["evidence"]["defaultTeamOwnerRepairCount"], 1
        )

        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            source_path = root / "owner-repair.map"
            source_path.write_bytes(source)
            output = root / "cooked"
            convert_sage_map(source_path, output)
            backtest = validate_cooked_sage_map(output)
            self.assertTrue(backtest["valid"], backtest["errors"])
            self.assertTrue(backtest["facts"]["teamSemanticsAttested"])

    def test_retail_quirk_duplicate_team_names_are_preserved_not_silently_merged(
        self,
    ) -> None:
        source, _ = _synthetic_map(
            sides_payload=lambda indexes: _sides_list(
                indexes,
                players=[
                    ([("playerName", 3, "PlyrSynthetic")], []),
                    ([("playerName", 3, "PlyrOther")], []),
                ],
            ),
            teams_payload=lambda indexes: _teams(
                indexes,
                teams=[
                    [
                        ("teamName", 3, "teamPlyrSynthetic"),
                        ("teamOwner", 3, "PlyrSynthetic"),
                    ],
                    [
                        ("teamName", 3, "teamPlyrSynthetic"),
                        ("teamOwner", 3, "PlyrOther"),
                    ],
                    [
                        ("teamName", 3, "teamPlyrOther"),
                        ("teamOwner", 3, "PlyrOther"),
                    ],
                ],
            ),
            library_map_lists_payload=lambda indexes: _library_map_lists(
                indexes, lists=[(), ()]
            ),
            script_list_count=2,
        )
        parsed = parse_sage_map_bytes(source)
        self.assertEqual(parsed.setup["teamCount"], 3)
        self.assertEqual(len(parsed.setup["duplicateTeamNames"]), 1)
        self.assertFalse(parsed.setup["crossChecks"]["teamNamesUnique"])

    def test_trigger_and_wave_records_fail_closed(self) -> None:
        cases = [
            (
                "trigger-version",
                {"trigger_payload": _trigger_areas(), "trigger_version": 2},
                "unsupported TriggerAreas version",
            ),
            (
                "wave-version",
                {
                    "standing_wave_payload": _standing_wave_areas(),
                    "standing_wave_version": 3,
                },
                "unsupported StandingWaveAreas version",
            ),
            (
                "trigger-reserved",
                {"trigger_payload": _trigger_areas(reserved=1)},
                "reserved marker",
            ),
            (
                "wave-reserved",
                {"standing_wave_payload": _standing_wave_areas(reserved=1)},
                "reserved marker",
            ),
            (
                "wave-additive",
                {"standing_wave_payload": _standing_wave_areas(additive=2)},
                "invalid boolean",
            ),
            (
                "wave-pca",
                {"standing_wave_payload": _standing_wave_areas(pca_wave=2)},
                "invalid 32-bit boolean",
            ),
            (
                "trigger-trailing",
                {"trigger_payload": _trigger_areas(trailing=b"x")},
                "unexplained bytes",
            ),
            (
                "wave-trailing",
                {"standing_wave_payload": _standing_wave_areas(trailing=b"x")},
                "unexplained bytes",
            ),
            (
                "duplicate-trigger",
                {
                    "trigger_payload": _trigger_areas(),
                    "duplicate_trigger": True,
                },
                "duplicate TriggerAreas",
            ),
            (
                "duplicate-wave",
                {
                    "standing_wave_payload": _standing_wave_areas(),
                    "duplicate_standing_wave": True,
                },
                "duplicate StandingWaveAreas",
            ),
        ]
        for name, arguments, message in cases:
            with self.subTest(name=name):
                source, _ = _synthetic_map(**arguments)
                with self.assertRaisesRegex(SageMapError, message):
                    parse_sage_map_bytes(source)

    def test_trigger_and_wave_counts_are_bounded_before_record_reads(self) -> None:
        cases = [
            ("trigger", {"trigger_payload": struct.pack("<I", 16_385)}),
            ("wave", {"standing_wave_payload": struct.pack("<I", 16_385)}),
        ]
        for name, arguments in cases:
            with self.subTest(name=name):
                source, _ = _synthetic_map(**arguments)
                with self.assertRaisesRegex(SageMapError, "count exceeds limit"):
                    parse_sage_map_bytes(source)

    def test_rejects_truncated_gameplay_record_counts(self) -> None:
        cases = {
            "TriggerAreas": {"trigger_payload": b"\x00\x00\x00"},
            "StandingWaveAreas": {"standing_wave_payload": b"\x00"},
            "WaypointsList": {"waypoint_list_payload": b""},
            "MPPositionList": {"mp_position_payload": b"\x00"},
            "SidesList": {"sides_payload": b""},
            "Teams": {"teams_payload": b"\x00\x00\x00"},
            "LibraryMapLists": {"library_map_lists_payload": b"\x00"},
        }
        for name, arguments in cases.items():
            with self.subTest(name=name):
                source, _ = _synthetic_map(**arguments)
                with self.assertRaisesRegex(SageMapError, "truncated"):
                    parse_sage_map_bytes(source)

    def test_conversion_writes_only_cooked_outputs(self) -> None:
        source, elevations = _synthetic_map(
            trigger_payload=_trigger_areas(),
            standing_wave_payload=_standing_wave_areas(),
        )
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            source_path = root / "test.map"
            source_path.write_bytes(source)
            output = root / "pack" / "maps" / "test-map"
            paths = convert_sage_map(
                source_path,
                output,
                {
                    "id": "test.map.synthetic",
                    "displayName": "Synthetic Map",
                    "preview": "preview.png",
                },
            )
            self.assertEqual(len(paths), 18)
            self.assertEqual(
                [path.name for path in paths],
                [
                    "heightmap.r16",
                    "impassability.bit",
                    *_TERRAIN_SOURCE_PATHS.values(),
                    "terrain.json",
                    "water.json",
                    "triggers.json",
                    "objects.json",
                    "roads.json",
                    "object-bindings.json",
                    "waypoints.json",
                    "setup.json",
                    "chunks.json",
                    "map.json",
                ],
            )
            self.assertEqual((output / "heightmap.r16").read_bytes(), elevations)
            self.assertEqual((output / "impassability.bit").read_bytes(), b"\x05\x02")
            for key, relative_path in _TERRAIN_SOURCE_PATHS.items():
                with self.subTest(source_layer=key):
                    cooked_path = output / relative_path
                    self.assertEqual(
                        cooked_path.read_bytes(), _TERRAIN_SOURCE_LAYERS[key]
                    )
                    self.assertEqual(
                        cooked_path.stat().st_size,
                        len(_TERRAIN_SOURCE_LAYERS[key]),
                    )
            self.assertFalse((output / "test.map").exists())
            output_root = output.resolve()
            for path in paths:
                with self.subTest(contained_path=path.name):
                    self.assertEqual(
                        path.resolve().relative_to(output_root).parts[0], path.name
                    )
            map_data = json.loads((output / "map.json").read_text(encoding="utf-8"))
            self.assertTrue(map_data["sourceBinaryImported"])
            self.assertFalse(map_data["sourceBinaryPackaged"])
            self.assertEqual(
                map_data["conversionStatus"]["passability"], "source-grid-imported"
            )
            self.assertEqual(map_data["conversionStatus"]["scripts"], "empty")
            self.assertEqual(
                map_data["conversionStatus"]["triggers"],
                "source-records-imported-runtime-pending",
            )
            self.assertEqual(
                map_data["conversionStatus"]["standingWaves"],
                "source-records-imported-runtime-pending",
            )
            self.assertEqual(map_data["triggers"], "triggers.json")
            self.assertEqual(map_data["setup"], "setup.json")
            self.assertEqual(
                map_data["conversionStatus"]["setup"],
                "source-records-imported-runtime-pending",
            )
            setup = json.loads((output / "setup.json").read_text(encoding="utf-8"))
            self.assertEqual(setup["declaredPlayerCount"], 1)
            self.assertEqual(setup["lobbySlotCount"], 8)
            self.assertEqual(setup["scenarioPlayerCount"], 1)
            self.assertEqual(
                setup["libraryDependencyStatus"],
                "references-preserved-not-flattened",
            )
            self.assertTrue(all(setup["crossChecks"].values()))
            trigger_data = json.loads(
                (output / "triggers.json").read_text(encoding="utf-8")
            )
            self.assertEqual(trigger_data["count"], 1)
            self.assertEqual(
                trigger_data["status"], "source-records-imported-runtime-pending"
            )
            self.assertEqual(
                trigger_data["areas"][0]["name"], "SYNTHETIC_TRIGGER_SECRET"
            )
            water = json.loads((output / "water.json").read_text(encoding="utf-8"))
            self.assertEqual(water["standingWaves"][0]["name"], "SYNTHETIC_WAVE_SECRET")
            self.assertEqual(
                water["standingWaveStatus"],
                "source-records-imported-runtime-pending",
            )
            objects = json.loads((output / "objects.json").read_text(encoding="utf-8"))
            self.assertEqual(objects["count"], 2)
            bindings = json.loads(
                (output / "object-bindings.json").read_text(encoding="utf-8")
            )
            self.assertEqual(bindings["summary"]["placementCount"], 2)
            self.assertEqual(bindings["summary"]["typeCount"], 2)
            self.assertEqual(bindings["summary"]["unresolvedTypeCount"], 2)
            self.assertEqual(bindings["summary"]["resolutionStatus"], "partial")
            self.assertEqual(
                [item["typeName"] for item in bindings["records"]],
                ["*Waypoints/Waypoint", "TreeTest"],
            )
            self.assertEqual(map_data["objectBindings"], "object-bindings.json")
            self.assertEqual(map_data["roads"], "roads.json")
            self.assertEqual(map_data["roadSummary"]["controlPointCount"], 0)
            self.assertEqual(map_data["conversionStatus"]["roads"], "empty")
            self.assertEqual(map_data["objectResolution"], bindings["summary"])
            self.assertEqual(
                map_data["conversionStatus"]["objects"],
                "placements-imported-object-resolution-partial",
            )
            terrain = json.loads((output / "terrain.json").read_text(encoding="utf-8"))
            source_layers = terrain["sourceLayers"]
            self.assertEqual(
                source_layers["schema"], "openbfme.sage-terrain-source-layers"
            )
            self.assertEqual(source_layers["schemaVersion"], 0)
            self.assertEqual(
                (source_layers["gridWidth"], source_layers["gridHeight"]), (3, 2)
            )
            self.assertEqual(source_layers["cellCount"], 6)
            descriptors = {
                **source_layers["layers"],
                **source_layers["descriptionTables"],
            }
            for key, expected_payload in _TERRAIN_SOURCE_LAYERS.items():
                with self.subTest(source_metadata=key):
                    descriptor = descriptors[key]
                    relative_path = Path(descriptor["path"])
                    self.assertFalse(relative_path.is_absolute())
                    self.assertNotIn("..", relative_path.parts)
                    self.assertEqual(descriptor["path"], _TERRAIN_SOURCE_PATHS[key])
                    self.assertEqual(descriptor["byteLength"], len(expected_payload))
                    self.assertEqual(
                        descriptor["sha256"],
                        hashlib.sha256(expected_payload).hexdigest(),
                    )
                    self.assertTrue(descriptor["sourceExact"])
            self.assertEqual(source_layers["layers"]["tileIndices"]["cellCount"], 6)
            self.assertEqual(source_layers["layers"]["tileIndices"]["cellSizeBytes"], 2)
            self.assertEqual(source_layers["layers"]["blendCells"]["cellSizeBytes"], 4)
            self.assertEqual(
                source_layers["descriptionTables"]["blendDescriptions"]["recordCount"],
                2,
            )
            self.assertEqual(
                source_layers["descriptionTables"]["blendDescriptions"][
                    "recordSizeBytes"
                ],
                18,
            )
            self.assertEqual(
                source_layers["descriptionTables"]["cliffMappings"]["recordCount"],
                1,
            )
            self.assertEqual(
                source_layers["descriptionTables"]["cliffMappings"]["recordSizeBytes"],
                38,
            )

    def test_expected_semantic_invariants_fail_before_outputs(self) -> None:
        source, _ = _synthetic_map()
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            source_path = root / "test.map"
            source_path.write_bytes(source)
            output = root / "cooked"
            with self.assertRaisesRegex(SageMapError, "width mismatch"):
                convert_sage_map(source_path, output, {}, {"width": 415})
            self.assertFalse(output.exists())

    def test_profile_metadata_cannot_override_computed_attestations(self) -> None:
        source, _ = _synthetic_map()
        protected_fields = {
            "sourceBinaryImported": False,
            "sourceBinaryPackaged": True,
            "source": {"packaged": True, "sha256": "forged"},
            "package": {"retailBytesIncluded": True},
            "conversionStatus": {"passability": "forged"},
        }
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            source_path = root / "test.map"
            source_path.write_bytes(source)
            for field, value in protected_fields.items():
                with self.subTest(field=field):
                    output = root / field
                    with self.assertRaisesRegex(
                        SageMapError, rf"protected field\(s\): {field}"
                    ):
                        convert_sage_map(source_path, output, {field: value})
                    self.assertFalse(output.exists())

    def test_presentation_metadata_preserves_computed_attestations(self) -> None:
        source, _ = _synthetic_map()
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            source_path = root / "test.map"
            source_path.write_bytes(source)
            output = root / "cooked"
            convert_sage_map(
                source_path,
                output,
                {
                    "id": "test.map.presentation",
                    "displayName": "Presentation",
                    "preview": "preview.png",
                    "art": "art.png",
                    "terrainMaterials": "materials/terrain-materials.json",
                    "roadMaterials": "road-materials.json",
                    "knownEnvironment": {"waterReflectionPlaneZ": 42.0},
                },
            )
            map_data = json.loads((output / "map.json").read_text(encoding="utf-8"))
            self.assertEqual(map_data["id"], "test.map.presentation")
            self.assertEqual(map_data["preview"], "preview.png")
            self.assertEqual(
                map_data["terrainMaterials"], "materials/terrain-materials.json"
            )
            self.assertEqual(map_data["roadMaterials"], "road-materials.json")
            self.assertTrue(map_data["sourceBinaryImported"])
            self.assertFalse(map_data["sourceBinaryPackaged"])
            self.assertFalse(map_data["source"]["packaged"])
            self.assertEqual(
                map_data["conversionStatus"]["passability"], "source-grid-imported"
            )

    def test_object_bindings_are_exact_counted_and_pack_relative(self) -> None:
        source, _ = _synthetic_map(
            object_type_names=("TreeTest", "TreeTest", "RockTest")
        )
        object_bindings = {
            "logical": [
                {
                    "typeName": "*Waypoints/Waypoint",
                    "classification": "waypoint",
                }
            ],
            "models": [
                {
                    "typeName": "TreeTest",
                    "sourceVirtualModel": "art\\w3d\\pttree01.w3d",
                    "glb": "assets/models/props/pttree01.glb",
                    "matchMethod": "exact-type-name",
                }
            ],
        }
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            source_path = root / "test.map"
            source_path.write_bytes(source)
            output = root / "pack" / "maps" / "test-map"
            paths = convert_sage_map(source_path, output, {}, {}, object_bindings)
            bindings_path = output / "object-bindings.json"
            self.assertIn(bindings_path, paths)
            data = json.loads(bindings_path.read_text(encoding="utf-8"))
            self.assertEqual(data["matchPolicy"], "explicit-exact-type-name-only")
            self.assertEqual(
                [record["typeName"] for record in data["records"]],
                ["*Waypoints/Waypoint", "RockTest", "TreeTest"],
            )
            by_type = {record["typeName"]: record for record in data["records"]}
            self.assertEqual(by_type["*Waypoints/Waypoint"]["status"], "logical")
            self.assertEqual(
                by_type["*Waypoints/Waypoint"]["classification"], "waypoint"
            )
            self.assertEqual(by_type["RockTest"]["status"], "unresolved")
            self.assertEqual(by_type["RockTest"]["classification"], "unknown")
            self.assertEqual(by_type["TreeTest"]["status"], "bound")
            self.assertEqual(by_type["TreeTest"]["classification"], "renderable")
            self.assertEqual(by_type["TreeTest"]["placementCount"], 2)
            self.assertEqual(
                by_type["TreeTest"]["sourceVirtualModel"],
                "art/w3d/pttree01.w3d",
            )
            self.assertEqual(
                by_type["TreeTest"]["glb"], "assets/models/props/pttree01.glb"
            )
            self.assertFalse(Path(by_type["TreeTest"]["glb"]).is_absolute())
            self.assertNotIn("..", Path(by_type["TreeTest"]["glb"]).parts)
            self.assertEqual(
                data["summary"],
                {
                    "resolutionStatus": "partial",
                    "typeCount": 3,
                    "placementCount": 4,
                    "resolvedTypeCount": 2,
                    "resolvedPlacementCount": 3,
                    "boundTypeCount": 1,
                    "boundPlacementCount": 2,
                    "logicalTypeCount": 1,
                    "logicalPlacementCount": 1,
                    "unresolvedTypeCount": 1,
                    "unresolvedPlacementCount": 1,
                },
            )

    def test_lifecycle_structure_bindings_preserve_explicit_content_identity(
        self,
    ) -> None:
        source, _ = _synthetic_map(
            object_type_names=("CaveTrollLair", "CaveTrollLair", "TreeTest")
        )
        object_bindings = {
            "logical": [
                {"typeName": "*Waypoints/Waypoint", "classification": "waypoint"}
            ],
            "models": [],
            "structures": [
                {
                    "typeName": "CaveTrollLair",
                    "sourceVirtualModel": "art/w3d/nb/nbtrolllair.w3d",
                    "glb": "assets/models/structures/neutral-cave-troll-lair/intact.glb",
                    "objectId": "bfme2.object.neutral-cave-troll-lair",
                    "matchMethod": "exact-type-name",
                }
            ],
        }
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            source_path = root / "test.map"
            source_path.write_bytes(source)
            output = root / "cooked"
            convert_sage_map(source_path, output, {}, {}, object_bindings)
            data = json.loads(
                (output / "object-bindings.json").read_text(encoding="utf-8")
            )
            by_type = {record["typeName"]: record for record in data["records"]}
            structure = by_type["CaveTrollLair"]
            self.assertEqual(structure["status"], "bound")
            self.assertEqual(structure["classification"], "lifecycle-structure")
            self.assertEqual(
                structure["objectId"], "bfme2.object.neutral-cave-troll-lair"
            )
            self.assertEqual(
                structure["sourceVirtualModel"],
                "art/w3d/nb/nbtrolllair.w3d",
            )
            self.assertEqual(
                structure["glb"],
                "assets/models/structures/neutral-cave-troll-lair/intact.glb",
            )
            self.assertEqual(data["summary"]["boundTypeCount"], 1)
            self.assertEqual(data["summary"]["boundPlacementCount"], 2)

    def test_road_control_points_are_separate_exact_source_order_pairs(self) -> None:
        source, _ = _synthetic_map(
            road_objects=(
                ("RoadAlpha", 2, (1.0, 2.0, 0.0)),
                ("RoadAlpha", 4, (3.0, 4.0, 1.0)),
                ("RoadBeta", 2, (5.0, 6.0, 2.0)),
                ("RoadBeta", 4, (7.0, 8.0, 3.0)),
            )
        )
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            source_path = root / "test.map"
            source_path.write_bytes(source)
            output = root / "cooked"
            convert_sage_map(source_path, output)

            objects = json.loads((output / "objects.json").read_text(encoding="utf-8"))
            roads = json.loads((output / "roads.json").read_text(encoding="utf-8"))
            bindings = json.loads(
                (output / "object-bindings.json").read_text(encoding="utf-8")
            )
            map_data = json.loads((output / "map.json").read_text(encoding="utf-8"))

            self.assertEqual(objects["count"], 6)
            self.assertEqual(bindings["summary"]["placementCount"], 2)
            self.assertEqual(bindings["summary"]["typeCount"], 2)
            self.assertEqual(roads["roadIds"], ["RoadAlpha", "RoadBeta"])
            self.assertEqual(
                roads["summary"],
                {
                    "status": "exact-paired-control-points",
                    "roadIdCount": 2,
                    "controlPointCount": 4,
                    "pairedControlPointCount": 4,
                    "unresolvedControlPointCount": 0,
                    "segmentCount": 2,
                    "unresolvedDiagnosticCount": 0,
                },
            )
            self.assertEqual(
                [(item["wireType"], item["role"]) for item in roads["controlPoints"]],
                [
                    (2, "segment-start"),
                    (4, "segment-end"),
                    (2, "segment-start"),
                    (4, "segment-end"),
                ],
            )
            source_by_index = {item["index"]: item for item in objects["objects"]}
            for segment in roads["segments"]:
                start = source_by_index[segment["startSourceIndex"]]
                end = source_by_index[segment["endSourceIndex"]]
                self.assertEqual(segment["roadId"], start["typeName"])
                self.assertEqual(segment["roadId"], end["typeName"])
                self.assertEqual(segment["sageStart"], start["sagePosition"])
                self.assertEqual(segment["sageEnd"], end["sagePosition"])
                self.assertEqual(segment["godotStart"], start["godotPosition"])
                self.assertEqual(segment["godotEnd"], end["godotPosition"])
            self.assertEqual(map_data["roadSummary"], roads["summary"])
            self.assertEqual(
                map_data["conversionStatus"]["roads"],
                "source-control-point-pairs-imported-rendering-pending",
            )

    def test_unknown_and_unpaired_road_wires_remain_explicitly_unresolved(self) -> None:
        source, _ = _synthetic_map(
            road_objects=(
                ("RoadUnknown", 7, (1.0, 2.0, 0.0)),
                ("RoadUnpaired", 2, (3.0, 4.0, 0.0)),
            )
        )
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            source_path = root / "test.map"
            source_path.write_bytes(source)
            output = root / "cooked"
            convert_sage_map(source_path, output)
            roads = json.loads((output / "roads.json").read_text(encoding="utf-8"))
            map_data = json.loads((output / "map.json").read_text(encoding="utf-8"))
            self.assertEqual(roads["summary"]["controlPointCount"], 2)
            self.assertEqual(roads["summary"]["pairedControlPointCount"], 0)
            self.assertEqual(roads["summary"]["unresolvedControlPointCount"], 2)
            self.assertEqual(roads["summary"]["segmentCount"], 0)
            self.assertEqual(
                [item["reason"] for item in roads["unresolvedDiagnostics"]],
                [
                    "unsupported-road-control-wire-type",
                    "unpaired-segment-start",
                ],
            )
            self.assertEqual(
                map_data["conversionStatus"]["roads"],
                "source-control-points-preserved-unresolved",
            )

    def test_object_binding_complete_resolution_is_reported_by_map(self) -> None:
        source, _ = _synthetic_map()
        object_bindings = {
            "logical": [
                {
                    "typeName": "*Waypoints/Waypoint",
                    "classification": "waypoint",
                }
            ],
            "models": [
                {
                    "typeName": "TreeTest",
                    "sourceVirtualModel": "pttree01.w3d",
                    "glb": "assets/models/props/pttree01.glb",
                    "matchMethod": "exact-type-name",
                }
            ],
        }
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            source_path = root / "test.map"
            source_path.write_bytes(source)
            output = root / "cooked"
            convert_sage_map(source_path, output, {}, {}, object_bindings)
            map_data = json.loads((output / "map.json").read_text(encoding="utf-8"))
            self.assertEqual(
                map_data["objectResolution"]["resolutionStatus"], "complete"
            )
            self.assertEqual(map_data["objectResolution"]["unresolvedTypeCount"], 0)
            self.assertEqual(
                map_data["conversionStatus"]["objects"],
                "placements-imported-object-resolution-complete",
            )

    def test_object_binding_declarations_fail_closed_before_outputs(self) -> None:
        source, _ = _synthetic_map()
        model = {
            "typeName": "TreeTest",
            "sourceVirtualModel": "pttree01.w3d",
            "glb": "assets/models/props/pttree01.glb",
            "matchMethod": "exact-type-name",
        }
        logical = {"typeName": "TreeTest", "classification": "prop"}
        structure = {
            "typeName": "TreeTest",
            "sourceVirtualModel": "art/w3d/nb/nbtree.w3d",
            "glb": "assets/models/structures/tree/intact.glb",
            "objectId": "bfme2.object.neutral-tree",
            "matchMethod": "exact-type-name",
        }
        cases = [
            (
                "unknown",
                {"logical": [{**logical, "typeName": "MissingType"}], "models": []},
                "unknown declared object typeName",
            ),
            (
                "no-prefix-match",
                {"logical": [{**logical, "typeName": "Tree"}], "models": []},
                "unknown declared object typeName",
            ),
            (
                "case-mismatch",
                {"logical": [{**logical, "typeName": "treetest"}], "models": []},
                "match cooked case exactly",
            ),
            (
                "duplicate",
                {"logical": [logical, dict(logical)], "models": []},
                "duplicate logical object binding",
            ),
            (
                "case-collision",
                {
                    "logical": [
                        logical,
                        {"typeName": "treetest", "classification": "prop"},
                    ],
                    "models": [],
                },
                "case-colliding logical object bindings",
            ),
            (
                "logical-model-conflict",
                {"logical": [logical], "models": [model]},
                "conflicting logical/model",
            ),
            (
                "unsafe-glb",
                {"logical": [], "models": [{**model, "glb": "../escape.glb"}]},
                "unsafe objectBindings.models.glb",
            ),
            (
                "unsafe-source",
                {
                    "logical": [],
                    "models": [{**model, "sourceVirtualModel": "C:\\retail\\tree.w3d"}],
                },
                "unsafe objectBindings.models.sourceVirtualModel",
            ),
            (
                "non-glb",
                {"logical": [], "models": [{**model, "glb": "tree.obj"}]},
                "must name a .glb file",
            ),
            (
                "fuzzy-method",
                {"logical": [], "models": [{**model, "matchMethod": "prefix"}]},
                "matchMethod='exact-type-name'",
            ),
            (
                "confused-entry",
                {
                    "logical": [
                        {
                            **logical,
                            "glb": "assets/models/props/pttree01.glb",
                        }
                    ],
                    "models": [],
                },
                "unsupported field",
            ),
            (
                "unknown-container-field",
                {"logical": [], "models": [], "fallbacks": []},
                "unsupported field",
            ),
            (
                "unsafe-structure-object-id",
                {
                    "logical": [],
                    "models": [],
                    "structures": [{**structure, "objectId": "../escape"}],
                },
                "unsafe objectBindings.structures.objectId",
            ),
            (
                "fuzzy-structure-method",
                {
                    "logical": [],
                    "models": [],
                    "structures": [{**structure, "matchMethod": "prefix"}],
                },
                "matchMethod='exact-type-name'",
            ),
            (
                "structure-model-conflict",
                {
                    "logical": [],
                    "models": [model],
                    "structures": [structure],
                },
                "conflicting object binding",
            ),
        ]
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            source_path = root / "test.map"
            source_path.write_bytes(source)
            for name, bindings, message in cases:
                with self.subTest(name=name):
                    output = root / name
                    with self.assertRaisesRegex(SageMapError, message):
                        convert_sage_map(source_path, output, {}, {}, bindings)
                    self.assertFalse(output.exists())

    def test_object_binding_declaration_count_is_bounded(self) -> None:
        source, _ = _synthetic_map()
        bindings = {
            "logical": [
                {"typeName": "TreeTest", "classification": "prop"} for _ in range(4097)
            ],
            "models": [],
        }
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            source_path = root / "test.map"
            source_path.write_bytes(source)
            output = root / "cooked"
            with self.assertRaisesRegex(SageMapError, "count exceeds limit"):
                convert_sage_map(source_path, output, {}, {}, bindings)
            self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
