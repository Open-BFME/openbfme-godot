from __future__ import annotations

import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest

from openbfme_importer.sage_map import (
    SageMapError,
    census_sage_map_bytes,
    convert_sage_map,
    decode_sage_map_blob,
    parse_sage_map_bytes,
)


_TERRAIN_SOURCE_LAYERS = {
    "tileIndices": struct.pack("<6H", 0, 1, 2, 7, 511, 65_535),
    "blendCells": struct.pack(
        "<6I", 0, 0x01020304, 0xFFFFFFFF, 7, 0x10203040, 0
    ),
    "threeWayBlendCells": struct.pack(
        "<6I", 0xAABBCCDD, 0, 1, 2, 3, 0xFFFFFFFF
    ),
    "cliffCells": struct.pack("<6I", 0, 4, 0, 8, 16, 32),
    "blendDescriptions": bytes(range(36)),
    "cliffMappings": bytes(range(64, 102)),
}

_TERRAIN_SOURCE_PATHS = {
    "tileIndices": "terrain-tile-indices.u16",
    "blendCells": "terrain-blend-cells.u32",
    "threeWayBlendCells": "terrain-three-way-blend-cells.u32",
    "cliffCells": "terrain-cliff-cells.u32",
    "blendDescriptions": "terrain-blend-descriptions.bin",
    "cliffMappings": "terrain-cliff-mappings.bin",
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
        raise AssertionError("standing-wave synthetic settings must contain nine values")
    payload = struct.pack("<I", 1)
    payload += struct.pack("<I", unique_id)
    payload += _u16_string(name) + _u16_string(layer)
    payload += struct.pack("<fB", uv_speed, additive)
    payload += struct.pack("<I", len(points))
    payload += b"".join(struct.pack("<ff", *point) for point in points)
    payload += struct.pack("<I", reserved)
    payload += struct.pack("<9I", *settings)
    payload += _u16_string(texture)
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
    payload = bytes([unknown_boolean]) + struct.pack("<i", count)
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
        result += _record(
            child_name, child_version, payload + child_trailing, indexes
        )
    return result


def _map_object(
    type_name: str,
    position: tuple[float, float, float],
    properties: list[tuple[str, int, object]],
    indexes: dict[str, int],
) -> bytes:
    payload = struct.pack("<ffffI", *position, 0.25, 0)
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
    waypoint_list_payload: bytes | None = None,
    mp_position_payload: object | None = None,
    sides_payload: object | None = None,
    teams_payload: object | None = None,
    library_map_lists_payload: object | None = None,
    trigger_version: int = 1,
    standing_wave_version: int = 2,
    mp_position_version: int = 0,
    sides_version: int = 6,
    teams_version: int = 1,
    library_map_lists_version: int = 1,
    player_scripts_version: int = 1,
    script_child_name: str = "ScriptList",
    script_child_version: int = 1,
    waypoint_list_version: int = 1,
    script_list_count: int = 1,
    duplicate_trigger: bool = False,
    duplicate_standing_wave: bool = False,
    duplicate_setup_chunk: str | None = None,
    start_name: str = "Player_1_Start",
    waypoint_path_labels: tuple[str, ...] = (),
    extra_waypoints: tuple[
        tuple[int, str, tuple[float, float, float]], ...
    ] = (),
    object_type_names: tuple[str, ...] = ("TreeTest",),
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

    elevations = [1000, 1100, 1200, 1300, 1400, 1500]
    height = struct.pack("<IIIII", 3, 2, 0, 1, 3)
    height += struct.pack("<I", 2)
    height += struct.pack("<I", 6)
    height += struct.pack("<6H", *elevations)

    area = 6
    blend = struct.pack("<I", area)
    blend += _TERRAIN_SOURCE_LAYERS["tileIndices"]
    blend += _TERRAIN_SOURCE_LAYERS["blendCells"]
    blend += _TERRAIN_SOURCE_LAYERS["threeWayBlendCells"]
    blend += _TERRAIN_SOURCE_LAYERS["cliffCells"]
    blend += b"\x05\x02"  # three exact impassable cells, row padded
    blend += b"\x00\x00" * 4
    blend += bytes([1, 0, 1, 0, 1, 0])
    blend += b"\x07\x07"
    blend += struct.pack("<IIII", 1, 3, 2, 1)
    blend += struct.pack("<IIII", 0, 1, 1, 0) + _u16_string("TestGrass")
    blend += struct.pack("<II", 0x12345678, 0)
    blend += _TERRAIN_SOURCE_LAYERS["blendDescriptions"]
    blend += _TERRAIN_SOURCE_LAYERS["cliffMappings"]

    waypoint_properties: list[tuple[str, int, object]] = [
        ("waypointID", 1, 7),
        ("waypointName", 3, start_name),
        ("uniqueID", 3, start_name),
    ]
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

    standing = struct.pack("<I", 1)
    standing += struct.pack("<I", 11)
    standing += _u16_string("Test Water") + _u16_string("")
    standing += struct.pack("<fB", 0.06, 0)
    standing += _u16_string("Bump.tga") + _u16_string("Sky.tga")
    standing += struct.pack("<I", 3)
    standing += struct.pack("<ffffff", 0.0, 0.0, 20.0, 0.0, 0.0, 20.0)
    standing += struct.pack("<I", 42)
    standing += _u16_string("water.w3d") + _u16_string("depth.tga")

    rivers = struct.pack("<I", 1)
    rivers += struct.pack("<I", 12)
    rivers += _u16_string("ford") + _u16_string("")
    rivers += struct.pack("<fB", 0.05, 1)
    for value in ("river.tga", "noise.tga", "edge.tga", "sparkle.tga"):
        rivers += _u16_string(value)
    rivers += bytes([255, 254, 253, 0]) + struct.pack("<fI", 0.75, 43)
    rivers += _u16_string("") + struct.pack("<I", 2)
    rivers += struct.pack("<ffffffff", 0.0, 0.0, 0.0, 10.0, 10.0, 0.0, 10.0, 10.0)

    scripts = b"".join(
        _record(script_child_name, script_child_version, b"", indexes)
        for _ in range(script_list_count)
    )
    trigger_payload = struct.pack("<I", 0) if trigger_payload is None else trigger_payload
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
    sides_payload = _sides_list(indexes) if sides_payload is None else sides_payload
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
    top = b"".join(
        [
            _record("HeightMapData", 5, height, indexes),
            _record("BlendTileData", 18, blend, indexes),
            setup_records["MPPositionList"],
            setup_records["SidesList"],
            setup_records["LibraryMapLists"],
            setup_records["Teams"],
            _record("ObjectsList", 3, objects, indexes),
            _record("TriggerAreas", trigger_version, trigger_payload, indexes),
            _record("StandingWaterAreas", 2, standing, indexes),
            _record("RiverAreas", 2, rivers, indexes),
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
    return (_refpack_literals(body) if compressed else body), struct.pack("<6H", *elevations)


class SageMapTests(unittest.TestCase):
    def test_payload_free_census_preserves_neutral_supported_facts(self) -> None:
        source, _ = _synthetic_map(object_type_names=("UNIQUE_SECRET_SENTINEL",))
        census = census_sage_map_bytes(source)
        self.assertTrue(census["strictCook"]["accepted"])
        self.assertEqual(census["features"]["height"], {"width": 3, "height": 2, "borderWidth": 0})
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

    def test_census_parses_trigger_and_wave_without_leaking_payload_strings(self) -> None:
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
        future = next(item for item in census["chunks"] if item["name"] == "FutureChunk")
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
        self.assertEqual(parsed.script_summary, {"listCount": 1, "nonemptyListCount": 0})
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
                {"sides_version": 5},
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

    def test_setup_types_booleans_floats_counts_and_duplicates_fail_closed(self) -> None:
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
                "duplicate-waypoint-name",
                {
                    "extra_waypoints": (
                        (8, "player_1_start", (15.0, 5.0, 0.0)),
                    )
                },
                "duplicate waypointName",
            ),
            (
                "duplicate-edge",
                {
                    "waypoint_list_payload": struct.pack(
                        "<Iiiii", 2, 7, 8, 7, 8
                    ),
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
            (
                "missing-edge-endpoint",
                {"waypoint_list_payload": struct.pack("<Iii", 1, 7, 99)},
                "missing endpoint",
            ),
        ]
        for name, arguments, message in cases:
            with self.subTest(name=name):
                source, _ = _synthetic_map(**arguments)
                with self.assertRaisesRegex(SageMapError, message):
                    parse_sage_map_bytes(source)

    def test_retail_quirk_duplicate_team_names_are_preserved_not_silently_merged(self) -> None:
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
            self.assertEqual(len(paths), 17)
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
                    self.assertEqual(path.resolve().relative_to(output_root).parts[0], path.name)
            map_data = json.loads((output / "map.json").read_text(encoding="utf-8"))
            self.assertTrue(map_data["sourceBinaryImported"])
            self.assertFalse(map_data["sourceBinaryPackaged"])
            self.assertEqual(map_data["conversionStatus"]["passability"], "source-grid-imported")
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
            self.assertEqual(trigger_data["areas"][0]["name"], "SYNTHETIC_TRIGGER_SECRET")
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
                        descriptor["sha256"], hashlib.sha256(expected_payload).hexdigest()
                    )
                    self.assertTrue(descriptor["sourceExact"])
            self.assertEqual(source_layers["layers"]["tileIndices"]["cellCount"], 6)
            self.assertEqual(source_layers["layers"]["tileIndices"]["cellSizeBytes"], 2)
            self.assertEqual(
                source_layers["layers"]["blendCells"]["cellSizeBytes"], 4
            )
            self.assertEqual(
                source_layers["descriptionTables"]["blendDescriptions"]["recordCount"],
                2,
            )
            self.assertEqual(
                source_layers["descriptionTables"]["blendDescriptions"]["recordSizeBytes"],
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
                    "knownEnvironment": {"waterReflectionPlaneZ": 42.0},
                },
            )
            map_data = json.loads((output / "map.json").read_text(encoding="utf-8"))
            self.assertEqual(map_data["id"], "test.map.presentation")
            self.assertEqual(map_data["preview"], "preview.png")
            self.assertEqual(
                map_data["terrainMaterials"], "materials/terrain-materials.json"
            )
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
            paths = convert_sage_map(
                source_path, output, {}, {}, object_bindings
            )
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
            self.assertEqual(map_data["objectResolution"]["resolutionStatus"], "complete")
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
                {"typeName": "TreeTest", "classification": "prop"}
                for _ in range(4097)
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
