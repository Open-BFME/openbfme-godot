"""Bounded, independent reader for the verified BFME II multiplayer map facts.

The importer deliberately supports only versions proven by the frozen retail
multiplayer corpus. It rejects unsupported variants instead of guessing, and
emits cooked data rather than carrying retail map binaries into a pack.
"""

from __future__ import annotations

from array import array
from collections import Counter
from dataclasses import dataclass
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import sys
from typing import Any, Iterator

from .castle_capabilities import CastleCapabilityError, validate_capability_subset
from .paths import safe_relative_parts
from .util import write_json_atomic


MAX_SOURCE_BYTES = 64 * 1024 * 1024
MAX_DECOMPRESSED_BYTES = 256 * 1024 * 1024
MAX_ASSET_NAMES = 4_096
MAX_ASSET_NAME_BYTES = 1_024
MAX_TOP_LEVEL_RECORDS = 4_096
MAX_GRID_CELLS = 16_000_000
MAX_OBJECTS = 100_000
MAX_PROPERTIES_PER_OBJECT = 512
MAX_STRING_BYTES = 1_048_576
MAX_WATER_AREAS = 16_384
MAX_TRIGGER_AREAS = 16_384
MAX_STANDING_WAVE_AREAS = 16_384
MAX_POINTS = 1_000_000
MAX_MP_POSITIONS = 64
MAX_SIDE_RESTRICTIONS = 1_024
MAX_SCENARIO_PLAYERS = 1_024
MAX_BUILD_LIST_ITEMS = 100_000
MAX_TEAMS = 65_536
MAX_LIBRARY_LISTS = 1_024
MAX_LIBRARY_REFERENCES = 4_096
MAX_CASTLE_TEMPLATES = 4_096
MAX_BACK_REFERENCE = 131_072
MAX_OBJECT_BINDING_TYPES = 4_096
MAX_OBJECT_BINDING_TEXT = 1_024
OBJECT_BINDING_MATCH_METHOD = "exact-type-name"
SAGE_MAP_PROFILE_VERSION = 1

_SAGE_MAP_PROFILE_SPECS = {
    "multiplayer": {
        "minimumTerrainDimension": 2,
        "enforceLobbyStartRules": True,
        "runnable": True,
    },
    "scenario": {
        "minimumTerrainDimension": 2,
        "enforceLobbyStartRules": False,
        "runnable": True,
    },
    "library": {
        "minimumTerrainDimension": 1,
        "enforceLobbyStartRules": False,
        "runnable": False,
    },
    "placeholder": {
        "minimumTerrainDimension": 1,
        "enforceLobbyStartRules": False,
        "runnable": False,
    },
}

_PROPERTY_WIRE_TYPES = {
    0: "boolean",
    1: "integer",
    2: "real",
    3: "ascii-string",
    4: "unicode-string",
    5: "ascii-string-type-5",
}

_PLAYER_PROPERTY_TYPES = {
    "playerName": 3,
    "playerIsHuman": 0,
    "playerIsSkirmish": 0,
    "playerFaction": 3,
    "playerDisplayName": 4,
    "playerEnemies": 3,
    "playerAllies": 3,
    "playerStartMoney": 1,
    "playerColor": 1,
    "playerNightColor": 1,
    "multiplayerStartIndex": 1,
    "skirmishDifficulty": 1,
    "multiplayerIsLocal": 0,
    "playerIsPreorder": 0,
}

_TEAM_PROPERTY_TYPES = {
    "teamName": 3,
    "teamOwner": 3,
    "teamIsSingleton": 0,
    "teamHome": 3,
    **{
        field: wire_type
        for index in range(1, 8)
        for field, wire_type in (
            (f"teamUnitType{index}", 3),
            (f"teamUnitMinCount{index}", 1),
            (f"teamUnitMaxCount{index}", 1),
        )
    },
    "teamOnCreateScript": 3,
    "teamOnIdleScript": 3,
    "teamInitialIdleFrames": 1,
    "teamOnUnitDestroyedScript": 3,
    "teamOnDestroyedScript": 3,
    "teamDestroyedThreshold": 2,
    "teamEnemySightedScript": 3,
    "teamAllClearScript": 3,
    "teamAutoReinforce": 0,
    "teamIsAIRecruitable": 0,
    "teamIsBaseDefense": 0,
    "teamIsPerimeterDefense": 0,
    "teamAggressiveness": 1,
    "teamTransportsReturn": 0,
    "teamAvoidThreats": 0,
    "teamAttackCommonTarget": 0,
    "teamMaxInstances": 1,
    "teamDescription": 3,
    "teamProductionCondition": 3,
    "teamProductionPriority": 1,
    "teamProductionPrioritySuccessIncrease": 1,
    "teamProductionPriorityFailureDecrease": 1,
    "teamTransport": 3,
    "teamReinforcementOrigin": 3,
    "teamStartsFull": 0,
    "teamTransportsExit": 0,
    "teamVeterancy": 1,
    "teamExecutesActionsOnCreate": 0,
    "teamGenericScriptHook": 3,
    # These four fields occur in the frozen BFME II multiplayer corpus but are
    # not modeled by the pinned OpenSAGE Team reader. Their wire types were
    # independently checked across all 46 shipped maps; semantics stay unresolved.
    "exportWithScript": 0,
    "teamInitialIdleSeconds": 1,
    "teamUnitExperienceLevel1": 1,
    "teamUnitUpgradeList1": 3,
}

_UNRESOLVED_TEAM_FIELDS = {
    name: _TEAM_PROPERTY_TYPES[name]
    for name in (
        "exportWithScript",
        "teamInitialIdleSeconds",
        "teamUnitExperienceLevel1",
        "teamUnitUpgradeList1",
    )
}
ALLOWED_MAP_METADATA = frozenset(
    {
        "id",
        "displayName",
        "preview",
        "art",
        "terrainMaterials",
        "roadMaterials",
        "knownEnvironment",
        "castleSiege",
    }
)

_SUPPORTED_CHUNK_VERSIONS = {
    "HeightMapData": frozenset({5}),
    "BlendTileData": frozenset({8, 9, 11, 14, 15, 16, 17, 18}),
    "MPPositionList": frozenset({0}),
    "SidesList": frozenset({5, 6}),
    "LibraryMapLists": frozenset({1}),
    "Teams": frozenset({1}),
    "ObjectsList": frozenset({3}),
    "StandingWaterAreas": frozenset({2}),
    "RiverAreas": frozenset({1, 2}),
    "TriggerAreas": frozenset({1}),
    "StandingWaveAreas": frozenset({1, 2}),
    "WaypointsList": frozenset({1}),
    "PlayerScriptsList": frozenset({1, 5, 6}),
}

_BLEND_VERSIONED_LAYER_ORDER = (
    "impassability",
    "impassabilityToPlayers",
    "passageWidths",
    "taintability",
    "extraPassability",
    "flammability",
    "visibility",
)
_BLEND_VERSIONED_LAYER_MIN_VERSION = {
    # Pinned OpenSAGE BlendTileData.Parse reads these fields at >6, >=10,
    # and >=11 respectively.  Only source-proven versions are accepted above.
    "impassability": 7,
    "impassabilityToPlayers": 10,
    "passageWidths": 11,
    "taintability": 14,
    "extraPassability": 15,
    "flammability": 16,
    "visibility": 17,
}
_BLEND_VERSIONED_LAYER_PATHS = {
    "impassability": "impassability.bit",
    "impassabilityToPlayers": "terrain-impassability-to-players.bit",
    "passageWidths": "terrain-passage-widths.bit",
    "taintability": "terrain-taintability.bit",
    "extraPassability": "terrain-extra-passability.bit",
    "flammability": "terrain-flammability.u8",
    "visibility": "terrain-visibility.bit",
}
_BLEND_CELL_WORD_BITS_BY_VERSION = {
    8: 16,
    9: 16,
    11: 16,
    14: 32,
    15: 32,
    16: 32,
    17: 32,
    18: 32,
}
_LOSSLESS_LEGACY_BLEND_VERSIONS = frozenset({8, 9, 11, 14, 15, 16})
_BLEND_ABSENCE_REASON = "not-present-in-source-version"
_BLEND_STRUCTURAL_CONVERSION = "lossless-source-layer-preservation"
_BLEND_RUNTIME_DEFAULT_PARITY = "unproven"


def _blend_source_layer_presence(version: int) -> dict[str, bool]:
    return {
        name: version >= _BLEND_VERSIONED_LAYER_MIN_VERSION[name]
        for name in _BLEND_VERSIONED_LAYER_ORDER
    }


def _legacy_blend_layout_evidence(version: int) -> dict[str, Any]:
    return {
        "sourceVersion": version,
        "blendCellWordBits": _BLEND_CELL_WORD_BITS_BY_VERSION[version],
        "sourceLayerPresence": _blend_source_layer_presence(version),
        "structuralConversion": _BLEND_STRUCTURAL_CONVERSION,
        "runtimeDefaultParity": _BLEND_RUNTIME_DEFAULT_PARITY,
    }


class SageMapError(ValueError):
    """Raised when an input is unsupported, malformed, or exceeds a bound."""


@dataclass(frozen=True, slots=True)
class _SageMapProfile:
    map_kind: str
    version: int
    minimum_terrain_dimension: int
    enforce_lobby_start_rules: bool
    runnable: bool


def _resolve_map_profile(profile: str) -> _SageMapProfile:
    if not isinstance(profile, str) or profile not in _SAGE_MAP_PROFILE_SPECS:
        raise SageMapError(f"unsupported SAGE map profile: {profile!r}")
    spec = _SAGE_MAP_PROFILE_SPECS[profile]
    return _SageMapProfile(
        map_kind=profile,
        version=SAGE_MAP_PROFILE_VERSION,
        minimum_terrain_dimension=int(spec["minimumTerrainDimension"]),
        enforce_lobby_start_rules=bool(spec["enforceLobbyStartRules"]),
        runnable=bool(spec["runnable"]),
    )


def _profile_evidence(profile: _SageMapProfile) -> dict[str, Any]:
    return {
        "mapKind": profile.map_kind,
        "profileVersion": profile.version,
        "runnable": profile.runnable,
        "structuralStatus": (
            "runnable-structure" if profile.runnable else "non-runnable-structural-map"
        ),
    }


class _Cursor:
    __slots__ = ("_data", "position", "limit", "label")

    def __init__(
        self,
        data: bytes | bytearray | memoryview,
        *,
        position: int = 0,
        limit: int | None = None,
        label: str = "data",
    ) -> None:
        self._data = memoryview(data)
        self.position = position
        self.limit = len(self._data) if limit is None else limit
        self.label = label
        if position < 0 or self.limit < position or self.limit > len(self._data):
            raise SageMapError(f"invalid cursor bounds for {label}")

    @property
    def remaining(self) -> int:
        return self.limit - self.position

    def _claim(self, count: int) -> int:
        if count < 0 or count > self.remaining:
            raise SageMapError(
                f"{self.label} is truncated at {self.position}: "
                f"need {count} bytes, have {self.remaining}"
            )
        start = self.position
        self.position += count
        return start

    def bytes(self, count: int) -> bytes:
        start = self._claim(count)
        return bytes(self._data[start : start + count])

    def view(self, count: int) -> memoryview:
        start = self._claim(count)
        return self._data[start : start + count]

    def skip(self, count: int) -> None:
        self._claim(count)

    def child(self, count: int, label: str) -> "_Cursor":
        start = self._claim(count)
        return _Cursor(self._data, position=start, limit=start + count, label=label)

    def u8(self) -> int:
        return self._data[self._claim(1)]

    def bool8(self) -> bool:
        value = self.u8()
        if value not in (0, 1):
            raise SageMapError(f"invalid boolean {value} in {self.label}")
        return bool(value)

    def u16(self) -> int:
        return struct.unpack_from("<H", self._data, self._claim(2))[0]

    def u24(self) -> int:
        start = self._claim(3)
        return int.from_bytes(self._data[start : start + 3], "little")

    def u32(self) -> int:
        return struct.unpack_from("<I", self._data, self._claim(4))[0]

    def i32(self) -> int:
        return struct.unpack_from("<i", self._data, self._claim(4))[0]

    def f32(self) -> float:
        value = struct.unpack_from("<f", self._data, self._claim(4))[0]
        if not math.isfinite(value):
            raise SageMapError(f"non-finite float in {self.label}")
        return value

    def ascii16(self) -> str:
        length = self.u16()
        if length > MAX_STRING_BYTES:
            raise SageMapError(f"oversized string in {self.label}: {length}")
        try:
            return self.bytes(length).decode("cp1252")
        except UnicodeDecodeError as exc:
            raise SageMapError(f"invalid ANSI string in {self.label}") from exc

    def unicode16(self) -> str:
        characters = self.u16()
        byte_count = characters * 2
        if byte_count > MAX_STRING_BYTES:
            raise SageMapError(
                f"oversized Unicode string in {self.label}: {characters}"
            )
        try:
            return self.bytes(byte_count).decode("utf-16-le")
        except UnicodeDecodeError as exc:
            raise SageMapError(f"invalid Unicode string in {self.label}") from exc

    def finish(self) -> None:
        if self.remaining:
            raise SageMapError(
                f"{self.label} has {self.remaining} unexplained bytes at {self.position}"
            )


def _read_big_endian(cursor: _Cursor, width: int) -> int:
    return int.from_bytes(cursor.bytes(width), "big")


def _append_literals(
    cursor: _Cursor, output: bytearray, count: int, expected: int
) -> None:
    if len(output) + count > expected:
        raise SageMapError("RefPack literal exceeds declared output size")
    output.extend(cursor.bytes(count))


def _append_reference(
    output: bytearray, length: int, distance: int, expected: int
) -> None:
    if distance < 1 or distance > len(output) or distance > MAX_BACK_REFERENCE:
        raise SageMapError(f"invalid RefPack back-reference distance: {distance}")
    if length < 1 or len(output) + length > expected:
        raise SageMapError("RefPack reference exceeds declared output size")
    for _ in range(length):
        output.append(output[-distance])


def _decode_refpack(payload: bytes, expected_size: int) -> tuple[bytes, dict[str, Any]]:
    cursor = _Cursor(payload, label="RefPack stream")
    flags = cursor.u8()
    marker = cursor.u8()
    if flags & 0x3E != 0x10 or marker != 0xFB:
        raise SageMapError("invalid RefPack header")
    size_width = 4 if flags & 0x80 else 3
    declared_compressed = _read_big_endian(cursor, size_width) if flags & 0x01 else None
    declared_output = _read_big_endian(cursor, size_width)
    if declared_output != expected_size:
        raise SageMapError(
            f"RefPack size {declared_output} disagrees with envelope size {expected_size}"
        )
    if declared_output > MAX_DECOMPRESSED_BYTES:
        raise SageMapError(f"RefPack output exceeds limit: {declared_output}")

    output = bytearray()
    commands = 0
    stopped = False
    while not stopped:
        commands += 1
        if commands > declared_output + 1:
            raise SageMapError("RefPack command count exceeds output bound")
        control = cursor.u8()
        if control < 0x80:
            second = cursor.u8()
            literal_count = control & 0x03
            match_count = ((control & 0x1C) >> 2) + 3
            distance = ((control & 0x60) << 3) + second + 1
            _append_literals(cursor, output, literal_count, declared_output)
            _append_reference(output, match_count, distance, declared_output)
        elif control < 0xC0:
            second = cursor.u8()
            third = cursor.u8()
            literal_count = (second & 0xC0) >> 6
            match_count = (control & 0x3F) + 4
            distance = ((second & 0x3F) << 8) + third + 1
            _append_literals(cursor, output, literal_count, declared_output)
            _append_reference(output, match_count, distance, declared_output)
        elif control < 0xE0:
            second = cursor.u8()
            third = cursor.u8()
            fourth = cursor.u8()
            literal_count = control & 0x03
            match_count = ((control & 0x0C) << 6) + fourth + 5
            distance = ((control & 0x10) << 12) + (second << 8) + third + 1
            _append_literals(cursor, output, literal_count, declared_output)
            _append_reference(output, match_count, distance, declared_output)
        elif control < 0xFC:
            literal_count = ((control & 0x1F) + 1) * 4
            _append_literals(cursor, output, literal_count, declared_output)
        else:
            _append_literals(cursor, output, control & 0x03, declared_output)
            stopped = True

    if len(output) != declared_output:
        raise SageMapError(
            f"RefPack stopped at {len(output)} bytes; expected {declared_output}"
        )
    cursor.finish()
    return bytes(output), {
        "kind": "ear-refpack",
        "declaredCompressedSize": declared_compressed,
        "decompressedSize": declared_output,
        "commandCount": commands,
    }


def decode_sage_map_blob(source: bytes) -> tuple[bytes, dict[str, Any]]:
    """Decode an EAR/RefPack envelope or validate an uncompressed CkMp blob."""

    if len(source) > MAX_SOURCE_BYTES:
        raise SageMapError(f"map source exceeds limit: {len(source)}")
    if source.startswith(b"CkMp"):
        if len(source) > MAX_DECOMPRESSED_BYTES:
            raise SageMapError(f"map body exceeds limit: {len(source)}")
        return source, {"kind": "uncompressed", "decompressedSize": len(source)}
    if not source.startswith(b"EAR\0"):
        raise SageMapError("unsupported SAGE map envelope")
    if len(source) < 10:
        raise SageMapError("truncated EAR map envelope")
    declared_size = struct.unpack_from("<I", source, 4)[0]
    if declared_size > MAX_DECOMPRESSED_BYTES:
        raise SageMapError(f"EAR output exceeds limit: {declared_size}")
    body, metadata = _decode_refpack(source[8:], declared_size)
    if not body.startswith(b"CkMp"):
        raise SageMapError("decoded map does not begin with CkMp")
    metadata["compressedSize"] = len(source)
    return body, metadata


def _dotnet_string(cursor: _Cursor) -> str:
    length = 0
    shift = 0
    for _ in range(5):
        value = cursor.u8()
        length |= (value & 0x7F) << shift
        if value < 0x80:
            break
        shift += 7
    else:
        raise SageMapError("invalid 7-bit string length")
    if length > MAX_ASSET_NAME_BYTES:
        raise SageMapError(f"asset name exceeds limit: {length}")
    try:
        return cursor.bytes(length).decode("utf-8")
    except UnicodeDecodeError as exc:
        raise SageMapError("invalid UTF-8 asset name") from exc


def _parse_name_table(cursor: _Cursor) -> dict[int, str]:
    count = cursor.u32()
    if count > MAX_ASSET_NAMES:
        raise SageMapError(f"asset-name count exceeds limit: {count}")
    names: dict[int, str] = {}
    seen_names: set[str] = set()
    for expected_index in range(count, 0, -1):
        name = _dotnet_string(cursor)
        index = cursor.u32()
        if index != expected_index:
            raise SageMapError(
                f"asset-name index {index} is out of sequence; expected {expected_index}"
            )
        if name in seen_names:
            raise SageMapError(f"duplicate asset name: {name!r}")
        names[index] = name
        seen_names.add(name)
    return names


@dataclass(frozen=True, slots=True)
class _Record:
    name: str
    version: int
    size: int
    offset: int
    payload: _Cursor


def _read_record(cursor: _Cursor, names: dict[int, str], label: str) -> _Record:
    header_offset = cursor.position
    index = cursor.u32()
    try:
        name = names[index]
    except KeyError as exc:
        raise SageMapError(f"unknown asset-name index {index} in {label}") from exc
    version = cursor.u16()
    size = cursor.u32()
    payload = cursor.child(size, f"{label}/{name} v{version}")
    return _Record(name, version, size, header_offset, payload)


def _records(
    cursor: _Cursor, names: dict[int, str], *, cap: int, label: str
) -> Iterator[_Record]:
    count = 0
    while cursor.remaining:
        count += 1
        if count > cap:
            raise SageMapError(f"{label} record count exceeds limit: {cap}")
        yield _read_record(cursor, names, label)


@dataclass(slots=True)
class _HeightMap:
    version: int
    width: int
    height: int
    border_width: int
    borders: list[dict[str, int]]
    elevations: array
    encoded: bytes

    @property
    def area(self) -> int:
        return self.width * self.height

    @property
    def vertical_scale(self) -> float:
        return 0.0390625 if self.version >= 5 else 0.625

    def value(self, x: int, y: int) -> int:
        return int(self.elevations[y * self.width + x])

    def sample_world_z(self, world_x: float, world_y: float) -> float:
        grid_x = min(max(world_x / 10.0 + self.border_width, 0.0), self.width - 1.0)
        grid_y = min(max(world_y / 10.0 + self.border_width, 0.0), self.height - 1.0)
        x0 = int(math.floor(grid_x))
        y0 = int(math.floor(grid_y))
        x1 = min(x0 + 1, self.width - 1)
        y1 = min(y0 + 1, self.height - 1)
        fx = grid_x - x0
        fy = grid_y - y0
        low = self.value(x0, y0) * (1.0 - fx) + self.value(x1, y0) * fx
        high = self.value(x0, y1) * (1.0 - fx) + self.value(x1, y1) * fx
        return (low * (1.0 - fy) + high * fy) * self.vertical_scale


def _parse_height(record: _Record, *, minimum_dimension: int = 2) -> _HeightMap:
    if record.version != 5:
        raise SageMapError(f"unsupported HeightMapData version: {record.version}")
    cursor = record.payload
    width = cursor.u32()
    height = cursor.u32()
    border_width = cursor.u32()
    if (
        width < minimum_dimension
        or height < minimum_dimension
        or width * height > MAX_GRID_CELLS
    ):
        raise SageMapError(f"invalid heightmap dimensions: {width}x{height}")
    if border_width * 2 >= min(width, height):
        raise SageMapError(f"invalid heightmap border width: {border_width}")
    border_count = cursor.u32()
    if border_count > 1_024:
        raise SageMapError(f"heightmap border count exceeds limit: {border_count}")
    borders: list[dict[str, int]] = []
    for _ in range(border_count):
        borders.append({"x": cursor.u32(), "y": cursor.u32()})
    area = cursor.u32()
    if area != width * height:
        raise SageMapError(f"heightmap area {area} does not match {width}x{height}")
    encoded = cursor.bytes(area * 2)
    cursor.finish()
    elevations = array("H")
    elevations.frombytes(encoded)
    if sys.byteorder != "little":
        elevations.byteswap()
    return _HeightMap(
        record.version, width, height, border_width, borders, elevations, encoded
    )


def _count_nonzero_words(values: bytes, word_bits: int) -> int:
    format_code = {16: "H", 32: "I"}.get(word_bits)
    if format_code is None:
        raise SageMapError(f"unsupported BlendTileData word width: {word_bits}")
    return sum(
        value[0] != 0 for value in struct.iter_unpack(f"<{format_code}", values)
    )


def _count_grid_bits(raw: bytes, width: int, height: int) -> int:
    row_bytes = (width + 7) // 8
    valid_last_bits = width % 8
    total = 0
    for row in range(height):
        segment = raw[row * row_bytes : (row + 1) * row_bytes]
        if not segment:
            raise SageMapError("truncated packed-bit grid")
        total += sum(value.bit_count() for value in segment[:-1])
        mask = (1 << valid_last_bits) - 1 if valid_last_bits else 0xFF
        total += (segment[-1] & mask).bit_count()
    return total


def _packed_grid(cursor: _Cursor, width: int, height: int) -> tuple[bytes, int]:
    raw = cursor.bytes(((width + 7) // 8) * height)
    return raw, _count_grid_bits(raw, width, height)


def _parse_blend(
    record: _Record, heightmap: _HeightMap
) -> tuple[bytes, dict[str, Any], dict[str, bytes]]:
    if record.version not in _SUPPORTED_CHUNK_VERSIONS["BlendTileData"]:
        raise SageMapError(f"unsupported BlendTileData version: {record.version}")
    cursor = record.payload
    area = heightmap.area
    num_tiles = cursor.u32()
    if num_tiles != area:
        raise SageMapError(
            f"BlendTileData tile count {num_tiles} does not match {area}"
        )
    tiles = cursor.bytes(area * 2)
    max_tile = max((item[0] for item in struct.iter_unpack("<H", tiles)), default=0)
    blend_cell_word_bits = _BLEND_CELL_WORD_BITS_BY_VERSION[record.version]
    blend_cell_size = blend_cell_word_bits // 8
    blends = cursor.bytes(area * blend_cell_size)
    three_way = cursor.bytes(area * blend_cell_size)
    cliffs = cursor.bytes(area * blend_cell_size)

    layer_presence = _blend_source_layer_presence(record.version)
    versioned_payloads: dict[str, bytes] = {}
    versioned_counts: dict[str, int] = {}
    for name in _BLEND_VERSIONED_LAYER_ORDER:
        if not layer_presence[name]:
            continue
        if name == "flammability":
            versioned_payloads[name] = cursor.bytes(area)
            continue
        payload, nonzero_count = _packed_grid(
            cursor, heightmap.width, heightmap.height
        )
        versioned_payloads[name] = payload
        versioned_counts[name] = nonzero_count
    impassability = versioned_payloads["impassability"]

    texture_cell_count = cursor.u32()
    raw_blend_count = cursor.u32()
    raw_cliff_count = cursor.u32()
    texture_count = cursor.u32()
    if texture_count > 4_096:
        raise SageMapError(f"terrain texture count exceeds limit: {texture_count}")
    textures: list[dict[str, Any]] = []
    for _ in range(texture_count):
        cell_start = cursor.u32()
        cell_count = cursor.u32()
        cell_size = cursor.u32()
        magic = cursor.u32()
        if cell_size * cell_size != cell_count or magic != 0:
            raise SageMapError("invalid terrain texture table entry")
        textures.append(
            {
                "name": cursor.ascii16(),
                "cellStart": cell_start,
                "cellCount": cell_count,
                "cellSize": cell_size,
            }
        )
    magic1 = cursor.u32()
    magic2 = cursor.u32()
    if magic2 != 0:
        raise SageMapError(f"unsupported BlendTileData trailing marker: {magic2}")
    blend_description_count = max(raw_blend_count - 1, 0)
    cliff_mapping_count = max(raw_cliff_count - 1, 0)
    blend_descriptions = cursor.bytes(blend_description_count * 18)
    cliff_mappings = cursor.bytes(cliff_mapping_count * 38)
    cursor.finish()

    flammability_counts: dict[str, int] = {}
    for value in versioned_payloads.get("flammability", b""):
        key = str(value)
        flammability_counts[key] = flammability_counts.get(key, 0) + 1
    grid_stats = {
        "impassable": versioned_counts["impassability"],
    }
    for source_name, summary_name in (
        ("impassabilityToPlayers", "impassableToPlayers"),
        ("passageWidths", "passageWidth"),
        ("taintability", "taintable"),
        ("extraPassability", "extraPassability"),
        ("visibility", "visible"),
    ):
        if layer_presence[source_name]:
            grid_stats[summary_name] = versioned_counts[source_name]
    summary = {
        "version": record.version,
        "numTiles": num_tiles,
        "maxTileValue": max_tile,
        "nonzeroBlendCells": _count_nonzero_words(blends, blend_cell_word_bits),
        "nonzeroThreeWayBlendCells": _count_nonzero_words(
            three_way, blend_cell_word_bits
        ),
        "nonzeroCliffCells": _count_nonzero_words(cliffs, blend_cell_word_bits),
        "gridStats": grid_stats,
        "textureCellCount": texture_cell_count,
        "rawBlendCount": raw_blend_count,
        "rawCliffCount": raw_cliff_count,
        "blendDescriptionCount": blend_description_count,
        "cliffMappingCount": cliff_mapping_count,
        "magicValue": magic1,
        "textures": textures,
        "sourceBuildabilityPresent": False,
    }
    if layer_presence["flammability"]:
        summary["flammabilityCounts"] = flammability_counts
    if record.version in _LOSSLESS_LEGACY_BLEND_VERSIONS:
        summary.update(
            {
                "sourceLayerPresence": layer_presence,
                "structuralConversion": _BLEND_STRUCTURAL_CONVERSION,
                "runtimeDefaultParity": _BLEND_RUNTIME_DEFAULT_PARITY,
            }
        )
    source_layers = {
        "tileIndices": tiles,
        "blendCells": blends,
        "threeWayBlendCells": three_way,
        "cliffCells": cliffs,
        "blendDescriptions": blend_descriptions,
        "cliffMappings": cliff_mappings,
    }
    if record.version in _LOSSLESS_LEGACY_BLEND_VERSIONS:
        source_layers.update(
            {
                name: versioned_payloads[name]
                for name, present in layer_presence.items()
                if present
            }
        )
    return impassability, summary, source_layers


def _parse_property(cursor: _Cursor, names: dict[int, str]) -> tuple[str, Any]:
    value_type = cursor.u8()
    name_index = cursor.u24()
    try:
        name = names[name_index]
    except KeyError as exc:
        raise SageMapError(f"unknown property-name index: {name_index}") from exc
    return name, _property_value(cursor, value_type, label="object")


def _parse_properties(cursor: _Cursor, names: dict[int, str]) -> dict[str, Any]:
    count = cursor.u16()
    if count > MAX_PROPERTIES_PER_OBJECT:
        raise SageMapError(f"object property count exceeds limit: {count}")
    result: dict[str, Any] = {}
    for _ in range(count):
        name, value = _parse_property(cursor, names)
        if name in result:
            raise SageMapError(f"duplicate object property: {name!r}")
        result[name] = value
    return result


def _property_value(cursor: _Cursor, value_type: int, *, label: str) -> Any:
    if value_type == 0:
        return cursor.bool8()
    if value_type == 1:
        return cursor.i32()
    if value_type == 2:
        return cursor.f32()
    if value_type in (3, 5):
        return cursor.ascii16()
    if value_type == 4:
        return cursor.unicode16()
    raise SageMapError(f"unsupported {label} property type: {value_type}")


def _parse_typed_properties(
    cursor: _Cursor,
    names: dict[int, str],
    *,
    label: str,
    expected_types: dict[str, int],
) -> list[dict[str, Any]]:
    count = cursor.u16()
    if count > MAX_PROPERTIES_PER_OBJECT:
        raise SageMapError(f"{label} property count exceeds limit: {count}")
    canonical_names = {name.casefold(): name for name in expected_types}
    seen: set[str] = set()
    result: list[dict[str, Any]] = []
    for _ in range(count):
        value_type = cursor.u8()
        name_index = cursor.u24()
        try:
            name = names[name_index]
        except KeyError as exc:
            raise SageMapError(
                f"unknown {label} property-name index: {name_index}"
            ) from exc
        key = name.casefold()
        if key in seen:
            raise SageMapError(f"duplicate {label} property: {name!r}")
        seen.add(key)
        canonical = canonical_names.get(key)
        if canonical is not None and name != canonical:
            raise SageMapError(
                f"noncanonical {label} property spelling: {name!r}; expected {canonical!r}"
            )
        expected_type = expected_types.get(name)
        if expected_type is not None and value_type != expected_type:
            raise SageMapError(
                f"{label} property {name!r} has wire type {value_type}; "
                f"expected {expected_type}"
            )
        value = _property_value(cursor, value_type, label=label)
        result.append(
            {
                "name": name,
                "wireType": _PROPERTY_WIRE_TYPES[value_type],
                "wireTypeCode": value_type,
                "value": value,
            }
        )
    return result


def _typed_properties_by_name(
    properties: list[dict[str, Any]],
) -> dict[str, dict[str, Any]]:
    return {str(item["name"]): item for item in properties}


def _required_typed_property(
    properties: dict[str, dict[str, Any]],
    name: str,
    *,
    label: str,
) -> Any:
    try:
        return properties[name]["value"]
    except KeyError as exc:
        raise SageMapError(f"{label} is missing required property {name!r}") from exc


def _parse_mp_positions(record: _Record, names: dict[int, str]) -> dict[str, Any]:
    if record.version != 0:
        raise SageMapError(f"unsupported MPPositionList version: {record.version}")
    slots: list[dict[str, Any]] = []
    total_restrictions = 0
    for child in _records(
        record.payload, names, cap=MAX_MP_POSITIONS, label="MPPositionList"
    ):
        if child.name != "MPPositionInfo" or child.version != 1:
            raise SageMapError(
                f"unsupported MPPositionList child: {child.name} v{child.version}"
            )
        cursor = child.payload
        is_human = cursor.bool8()
        is_computer = cursor.bool8()
        load_ai_script = cursor.bool8()
        team = cursor.u32()
        restriction_count = cursor.u32()
        total_restrictions += restriction_count
        if total_restrictions > MAX_SIDE_RESTRICTIONS:
            raise SageMapError("MPPositionList side-restriction count exceeds limit")
        restrictions = [cursor.ascii16() for _ in range(restriction_count)]
        if len({value.casefold() for value in restrictions}) != len(restrictions):
            raise SageMapError("duplicate MPPositionInfo side restriction")
        cursor.finish()
        slots.append(
            {
                "index": len(slots) + 1,
                "sourceVersion": child.version,
                "isHuman": is_human,
                "isComputer": is_computer,
                "loadAIScript": load_ai_script,
                "team": team,
                "sideRestrictions": restrictions,
            }
        )
    record.payload.finish()
    return {"version": record.version, "slots": slots}


def _parse_build_list_item(cursor: _Cursor) -> dict[str, Any]:
    building_name = cursor.ascii16()
    template_name = cursor.ascii16()
    x, y, z = cursor.f32(), cursor.f32(), cursor.f32()
    angle = cursor.f32()
    initially_built = cursor.bool8()
    rebuild_count = cursor.u32()
    script = cursor.ascii16()
    health = cursor.i32()
    whiner = cursor.bool8()
    unsellable = cursor.bool8()
    repairable = cursor.bool8()
    return {
        "buildingName": building_name,
        "templateName": template_name,
        "sagePosition": [x, y, z],
        "godotPosition": [x, z, -y],
        "sageAngleRadians": angle,
        "godotYawRadians": angle,
        "initiallyBuilt": initially_built,
        "rebuildCount": rebuild_count,
        "script": script,
        "health": health,
        "whiner": whiner,
        "unsellable": unsellable,
        "repairable": repairable,
    }


def _parse_sides(record: _Record, names: dict[int, str]) -> dict[str, Any]:
    if record.version not in _SUPPORTED_CHUNK_VERSIONS["SidesList"]:
        raise SageMapError(f"unsupported SidesList version: {record.version}")
    cursor = record.payload
    unknown_boolean_present = record.version == 6
    unknown_boolean = cursor.bool8() if unknown_boolean_present else None
    player_count = cursor.i32()
    if player_count < 0 or player_count > MAX_SCENARIO_PLAYERS:
        raise SageMapError(f"SidesList player count exceeds limit: {player_count}")
    players: list[dict[str, Any]] = []
    total_build_items = 0
    for index in range(player_count):
        properties = _parse_typed_properties(
            cursor,
            names,
            label="player",
            expected_types=_PLAYER_PROPERTY_TYPES,
        )
        by_name = _typed_properties_by_name(properties)
        player_name = _required_typed_property(
            by_name, "playerName", label=f"SidesList player {index}"
        )
        if not isinstance(player_name, str):
            raise SageMapError("SidesList playerName is not a string")
        build_count = cursor.u32()
        total_build_items += build_count
        if total_build_items > MAX_BUILD_LIST_ITEMS:
            raise SageMapError("SidesList build-list count exceeds limit")
        build_list = [_parse_build_list_item(cursor) for _ in range(build_count)]
        players.append(
            {
                "index": index,
                "name": player_name,
                "properties": properties,
                "buildList": build_list,
            }
        )
    cursor.finish()
    return {
        "version": record.version,
        "unknownBoolean": unknown_boolean,
        "unknownBooleanPresent": unknown_boolean_present,
        "players": players,
    }


def _parse_teams(record: _Record, names: dict[int, str]) -> dict[str, Any]:
    if record.version != 1:
        raise SageMapError(f"unsupported Teams version: {record.version}")
    cursor = record.payload
    team_count = cursor.i32()
    if team_count < 0 or team_count > MAX_TEAMS:
        raise SageMapError(f"Teams count exceeds limit: {team_count}")
    teams: list[dict[str, Any]] = []
    team_name_counts: dict[str, int] = {}
    for index in range(team_count):
        properties = _parse_typed_properties(
            cursor,
            names,
            label="team",
            expected_types=_TEAM_PROPERTY_TYPES,
        )
        by_name = _typed_properties_by_name(properties)
        team_name = _required_typed_property(
            by_name, "teamName", label=f"Teams item {index}"
        )
        owner = _required_typed_property(
            by_name, "teamOwner", label=f"Teams item {index}"
        )
        if not isinstance(team_name, str) or not team_name:
            raise SageMapError("Teams teamName must be a nonempty string")
        if not isinstance(owner, str):
            raise SageMapError("Teams teamOwner is not a string")
        team_key = team_name.casefold()
        name_occurrence = team_name_counts.get(team_key, 0) + 1
        team_name_counts[team_key] = name_occurrence
        teams.append(
            {
                "index": index,
                "name": team_name,
                "nameOccurrence": name_occurrence,
                "owner": owner,
                "properties": properties,
            }
        )
    cursor.finish()
    return {"version": record.version, "teams": teams}


def _library_reference(value: str) -> dict[str, Any]:
    if not value:
        return {"source": "", "normalized": "", "identitySha256": None}
    if value != value.strip() or "\x00" in value:
        raise SageMapError("unsafe or ambiguous LibraryMaps reference")
    normalized_input = value.replace("\\", "/")
    try:
        normalized = "/".join(safe_relative_parts(normalized_input))
    except ValueError as exc:
        raise SageMapError("unsafe or ambiguous LibraryMaps reference") from exc
    identity = hashlib.sha256(
        b"openbfme.library-map-reference\0" + normalized.casefold().encode("utf-8")
    ).hexdigest()
    return {
        "source": value,
        "normalized": normalized,
        "identitySha256": identity,
    }


def _parse_library_map_lists(record: _Record, names: dict[int, str]) -> dict[str, Any]:
    if record.version != 1:
        raise SageMapError(f"unsupported LibraryMapLists version: {record.version}")
    lists: list[dict[str, Any]] = []
    total_references = 0
    for child in _records(
        record.payload, names, cap=MAX_LIBRARY_LISTS, label="LibraryMapLists"
    ):
        if child.name != "LibraryMaps" or child.version != 1:
            raise SageMapError(
                f"unsupported LibraryMapLists child: {child.name} v{child.version}"
            )
        count = child.payload.u32()
        total_references += count
        if total_references > MAX_LIBRARY_REFERENCES:
            raise SageMapError("LibraryMaps reference count exceeds limit")
        references = [_library_reference(child.payload.ascii16()) for _ in range(count)]
        child.payload.finish()
        nonempty = [
            str(item["normalized"]).casefold()
            for item in references
            if item["normalized"]
        ]
        if len(nonempty) != len(set(nonempty)):
            raise SageMapError("duplicate or ambiguous LibraryMaps reference")
        lists.append(
            {
                "index": len(lists),
                "sourceVersion": child.version,
                "references": references,
            }
        )
    record.payload.finish()
    return {"version": record.version, "lists": lists}


def _parse_waypoint_edges(record: _Record) -> list[dict[str, int]]:
    if record.version != 1:
        raise SageMapError(f"unsupported WaypointsList version: {record.version}")
    count = record.payload.u32()
    if count > MAX_POINTS:
        raise SageMapError(f"WaypointsList count exceeds limit: {count}")
    edges: list[dict[str, int]] = []
    seen: set[tuple[int, int]] = set()
    for _ in range(count):
        start_id = record.payload.i32()
        end_id = record.payload.i32()
        key = (start_id, end_id)
        if key in seen:
            raise SageMapError(f"duplicate WaypointsList edge: {start_id}->{end_id}")
        if start_id == end_id:
            raise SageMapError(f"self-referential WaypointsList edge: {start_id}")
        seen.add(key)
        edges.append({"startId": start_id, "endId": end_id})
    record.payload.finish()
    return edges


def _parse_objects(
    record: _Record,
    names: dict[int, str],
    heightmap: _HeightMap,
) -> tuple[list[dict[str, Any]], list[dict[str, Any]], dict[str, dict[str, Any]]]:
    if record.version != 3:
        raise SageMapError(f"unsupported ObjectsList version: {record.version}")
    objects: list[dict[str, Any]] = []
    waypoints: list[dict[str, Any]] = []
    player_starts: dict[str, dict[str, Any]] = {}
    waypoint_ids: set[int] = set()
    for child in _records(record.payload, names, cap=MAX_OBJECTS, label="ObjectsList"):
        if child.name != "Object" or child.version != 3:
            raise SageMapError(
                f"unsupported ObjectsList child: {child.name} v{child.version}"
            )
        cursor = child.payload
        x, y, z_offset = cursor.f32(), cursor.f32(), cursor.f32()
        angle = cursor.f32()
        road_type = cursor.u32()
        type_name = cursor.ascii16()
        properties = _parse_properties(cursor, names)
        cursor.finish()
        ground_z = heightmap.sample_world_z(x, y)
        world_z = ground_z + z_offset
        item = {
            "index": len(objects),
            "typeName": type_name,
            "sagePosition": [x, y, z_offset],
            "sageAngleRadians": angle,
            "roadType": road_type,
            "properties": properties,
            "sampledTerrainZ": ground_z,
            "worldZ": world_z,
            "godotPosition": [x, world_z, -y],
            "godotYawRadians": angle,
        }
        objects.append(item)
        if type_name == "*Waypoints/Waypoint":
            waypoint_id = properties.get("waypointID")
            waypoint_name = properties.get("waypointName")
            unique_id = properties.get("uniqueID")
            if not isinstance(waypoint_id, int) or isinstance(waypoint_id, bool):
                raise SageMapError("waypoint is missing an integer waypointID")
            if waypoint_id <= 0:
                raise SageMapError(f"invalid nonpositive waypointID: {waypoint_id}")
            if not isinstance(waypoint_name, str):
                raise SageMapError("waypoint is missing a string waypointName")
            if not isinstance(unique_id, str):
                raise SageMapError("waypoint is missing a string uniqueID")
            if waypoint_id in waypoint_ids:
                raise SageMapError(f"duplicate waypointID: {waypoint_id}")
            waypoint_ids.add(waypoint_id)
            path_labels: list[dict[str, Any]] = []
            for property_name, value in properties.items():
                match = re.fullmatch(r"waypointPathLabel([1-9][0-9]*)", property_name)
                if match is None:
                    continue
                slot = int(match.group(1))
                if slot > 64 or not isinstance(value, str):
                    raise SageMapError("invalid waypoint path-label property")
                path_labels.append({"slot": slot, "value": value})
            path_labels.sort(key=lambda item: int(item["slot"]))
            if path_labels and [item["slot"] for item in path_labels] != list(
                range(1, len(path_labels) + 1)
            ):
                raise SageMapError("noncontiguous waypoint path-label slots")
            waypoint = {
                "id": waypoint_id,
                "name": waypoint_name,
                "pathLabels": path_labels,
                "sagePosition": [x, y, z_offset],
                "sampledTerrainZ": ground_z,
                "worldZ": world_z,
                "godotPosition": [x, world_z, -y],
            }
            if unique_id != waypoint_name:
                # The authored identity string is metadata. Runtime path identity and
                # WaypointsList edges are keyed by the integer waypointID instead.
                waypoint["authoredUniqueId"] = unique_id
            waypoints.append(waypoint)
            if waypoint_name.startswith("Player_") and waypoint_name.endswith("_Start"):
                start_match = re.fullmatch(r"Player_([1-9][0-9]*)_Start", waypoint_name)
                if start_match is None:
                    raise SageMapError(
                        f"noncanonical player-start waypoint name: {waypoint_name!r}"
                    )
                waypoint["playerIndex"] = int(start_match.group(1))
                # EA's prepend-based lookup and OpenSAGE's dictionary overwrite both
                # select the last source record for an exact duplicate name.
                player_starts[waypoint_name] = waypoint
    record.payload.finish()
    return objects, waypoints, player_starts


def _point2(cursor: _Cursor) -> list[float]:
    return [cursor.f32(), cursor.f32()]


def _godot_water_point(point: list[float], height: int) -> list[float]:
    return [point[0], float(height), -point[1]]


def _parse_standing_water(record: _Record) -> list[dict[str, Any]]:
    if record.version != 2:
        raise SageMapError(f"unsupported StandingWaterAreas version: {record.version}")
    cursor = record.payload
    count = cursor.u32()
    if count > MAX_WATER_AREAS:
        raise SageMapError(f"standing-water count exceeds limit: {count}")
    areas: list[dict[str, Any]] = []
    total_points = 0
    for _ in range(count):
        unique_id = cursor.u32()
        name = cursor.ascii16()
        layer = cursor.ascii16()
        uv_speed = cursor.f32()
        additive = cursor.bool8()
        bump_map = cursor.ascii16()
        sky_texture = cursor.ascii16()
        point_count = cursor.u32()
        total_points += point_count
        if total_points > MAX_POINTS:
            raise SageMapError("standing-water point count exceeds limit")
        points = [_point2(cursor) for _ in range(point_count)]
        water_height = cursor.u32()
        shader = cursor.ascii16()
        depth_colors = cursor.ascii16()
        areas.append(
            {
                "id": unique_id,
                "name": name,
                "layer": layer,
                "uvScrollSpeed": uv_speed,
                "additive": additive,
                "bumpMap": bump_map,
                "skyTexture": sky_texture,
                "waterHeight": water_height,
                "shader": shader,
                "depthColors": depth_colors,
                "sagePoints": points,
                "godotPoints": [
                    _godot_water_point(point, water_height) for point in points
                ],
            }
        )
    cursor.finish()
    return areas


def _parse_rivers(record: _Record) -> list[dict[str, Any]]:
    if record.version not in _SUPPORTED_CHUNK_VERSIONS["RiverAreas"]:
        raise SageMapError(f"unsupported RiverAreas version: {record.version}")
    cursor = record.payload
    count = cursor.u32()
    if count > MAX_WATER_AREAS:
        raise SageMapError(f"river count exceeds limit: {count}")
    rivers: list[dict[str, Any]] = []
    total_lines = 0
    for _ in range(count):
        unique_id = cursor.u32()
        name = cursor.ascii16()
        layer = cursor.ascii16()
        uv_speed = cursor.f32()
        additive = cursor.bool8()
        textures = {
            "river": cursor.ascii16(),
            "noise": cursor.ascii16(),
            "alphaEdge": cursor.ascii16(),
            "sparkle": cursor.ascii16(),
        }
        color = [cursor.u8(), cursor.u8(), cursor.u8()]
        if cursor.u8() != 0:
            raise SageMapError("unsupported RiverAreas color marker")
        alpha = cursor.f32()
        water_height = cursor.u32()
        minimum_lod = cursor.ascii16()
        line_count = cursor.u32()
        total_lines += line_count
        if total_lines > MAX_POINTS:
            raise SageMapError("river line count exceeds limit")
        lines: list[dict[str, Any]] = []
        for _ in range(line_count):
            v0 = _point2(cursor)
            v1 = _point2(cursor)
            lines.append(
                {
                    "sageV0": v0,
                    "sageV1": v1,
                    "godotV0": _godot_water_point(v0, water_height),
                    "godotV1": _godot_water_point(v1, water_height),
                }
            )
        rivers.append(
            {
                "id": unique_id,
                "name": name,
                "layer": layer,
                "uvScrollSpeed": uv_speed,
                "additive": additive,
                "textures": textures,
                "colorRgb": color,
                "alpha": alpha,
                "waterHeight": water_height,
                "minimumWaterLod": minimum_lod,
                "crossSections": lines,
            }
        )
    cursor.finish()
    return rivers


def _godot_planar_point(point: list[float]) -> list[float]:
    return [point[0], -point[1]]


def _parse_trigger_areas(record: _Record) -> list[dict[str, Any]]:
    if record.version != 1:
        raise SageMapError(f"unsupported TriggerAreas version: {record.version}")
    cursor = record.payload
    count = cursor.u32()
    if count > MAX_TRIGGER_AREAS:
        raise SageMapError(f"trigger-area count exceeds limit: {count}")
    areas: list[dict[str, Any]] = []
    total_points = 0
    for _ in range(count):
        name = cursor.ascii16()
        layer = cursor.ascii16()
        unique_id = cursor.u32()
        point_count = cursor.u32()
        total_points += point_count
        if total_points > MAX_POINTS:
            raise SageMapError("trigger-area point count exceeds limit")
        points = [_point2(cursor) for _ in range(point_count)]
        if cursor.u32() != 0:
            raise SageMapError("unsupported TriggerAreas reserved marker")
        areas.append(
            {
                "id": unique_id,
                "name": name,
                "layer": layer,
                "sagePoints": points,
                "godotXZPoints": [_godot_planar_point(point) for point in points],
            }
        )
    cursor.finish()
    return areas


def _parse_standing_wave_areas(record: _Record) -> list[dict[str, Any]]:
    if record.version not in _SUPPORTED_CHUNK_VERSIONS["StandingWaveAreas"]:
        raise SageMapError(f"unsupported StandingWaveAreas version: {record.version}")
    cursor = record.payload
    count = cursor.u32()
    if count > MAX_STANDING_WAVE_AREAS:
        raise SageMapError(f"standing-wave count exceeds limit: {count}")
    areas: list[dict[str, Any]] = []
    total_points = 0
    setting_names = (
        "finalWidth",
        "finalHeight",
        "initialWidthFraction",
        "initialHeightFraction",
        "initialVelocity",
        "timeToFade",
        "timeToCompress",
        "secondWaveTimeOffset",
        "distanceFromShore",
    )
    for _ in range(count):
        unique_id = cursor.u32()
        name = cursor.ascii16()
        layer = cursor.ascii16()
        uv_speed = cursor.f32()
        additive = cursor.bool8()
        point_count = cursor.u32()
        total_points += point_count
        if total_points > MAX_POINTS:
            raise SageMapError("standing-wave point count exceeds limit")
        points = [_point2(cursor) for _ in range(point_count)]
        if cursor.u32() != 0:
            raise SageMapError("unsupported StandingWaveAreas reserved marker")
        settings = {name: cursor.u32() for name in setting_names}
        texture = cursor.ascii16()
        area = {
            "id": unique_id,
            "name": name,
            "layer": layer,
            "uvScrollSpeed": uv_speed,
            "additive": additive,
            "texture": texture,
        }
        if record.version == 2:
            pca_raw = cursor.u32()
            if pca_raw not in (0, 1):
                raise SageMapError(
                    f"invalid 32-bit boolean {pca_raw} in StandingWaveAreas"
                )
            area["pcaWave"] = bool(pca_raw)
        else:
            area["pcaWaveFieldPresent"] = False
        area.update(
            {
                "settings": settings,
                "sagePoints": points,
                "godotXZPoints": [_godot_planar_point(point) for point in points],
            }
        )
        areas.append(area)
    cursor.finish()
    return areas


def _parse_player_scripts(record: _Record, names: dict[int, str]) -> dict[str, Any]:
    if record.version not in _SUPPORTED_CHUNK_VERSIONS["PlayerScriptsList"]:
        raise SageMapError(f"unsupported PlayerScriptsList version: {record.version}")
    lists = 0
    nonempty = 0
    source_lists: list[dict[str, Any]] = []
    for child in _records(record.payload, names, cap=1_024, label="PlayerScriptsList"):
        if child.name != "ScriptList" or child.version != 1:
            raise SageMapError(
                f"unsupported player script child: {child.name} v{child.version}"
            )
        payload = child.payload.bytes(child.payload.remaining)
        source_lists.append(
            {
                "sourceOrdinal": lists,
                "sourceVersion": child.version,
                "payloadByteLength": len(payload),
                "payloadSha256": hashlib.sha256(payload).hexdigest(),
                "nonempty": bool(payload),
            }
        )
        lists += 1
        if payload:
            nonempty += 1
        child.payload.finish()
    record.payload.finish()
    return {
        "listCount": lists,
        "nonemptyListCount": nonempty,
        "sourceLists": source_lists,
    }


def _side_runtime_semantics(
    players: list[dict[str, Any]],
    source_script_lists: list[dict[str, Any]],
) -> dict[str, Any] | None:
    """Attest EA's ordered side lookup and ordinal script binding rules."""

    exact_names: dict[str, list[dict[str, int]]] = {}
    folded_names: dict[str, list[dict[str, Any]]] = {}
    lookup_by_name: dict[str, dict[str, Any]] = {}
    for source_index, player in enumerate(players):
        name = str(player["name"])
        reference = {"sourceIndex": source_index}
        exact_names.setdefault(name, []).append(reference)
        folded_names.setdefault(name.casefold(), []).append(
            {**reference, "name": name}
        )
        # EA SidesList::findSideInfo scans from index zero and returns the first
        # exact AsciiString match. Do not use OpenSAGE's replacement dictionary.
        lookup_by_name.setdefault(name, {"name": name, **reference})

    duplicate_groups = [
        {"name": name, "records": records}
        for name, records in exact_names.items()
        if len(records) > 1
    ]
    casefold_collision_groups = [
        {"records": records}
        for records in folded_names.values()
        if len({str(item["name"]) for item in records}) > 1
    ]
    if not duplicate_groups and not casefold_collision_groups:
        return None

    name_lookup = list(lookup_by_name.values())
    script_bindings = [
        {
            "sourceOrdinal": source_ordinal,
            "playerSourceIndex": source_ordinal,
            "playerName": str(players[source_ordinal]["name"]),
        }
        for source_ordinal in range(len(source_script_lists))
    ]
    evidence = {
        "playerRecordCount": len(players),
        "orderedPlayerRecordsSha256": _canonical_json_sha256(players),
        "nameLookupCount": len(name_lookup),
        "nameLookupSha256": _canonical_json_sha256(name_lookup),
        "sourceScriptListCount": len(source_script_lists),
        "sourceScriptListsSha256": _canonical_json_sha256(source_script_lists),
        "scriptBindingCount": len(script_bindings),
        "scriptBindingsSha256": _canonical_json_sha256(script_bindings),
        "duplicateNameGroupCount": len(duplicate_groups),
        "duplicateNameRecordCount": sum(
            len(item["records"]) for item in duplicate_groups
        ),
        "duplicateNamesSha256": _canonical_json_sha256(duplicate_groups),
        "caseFoldCollisionGroupCount": len(casefold_collision_groups),
        "caseFoldCollisionRecordCount": sum(
            len(item["records"]) for item in casefold_collision_groups
        ),
        "caseFoldCollisionsSha256": _canonical_json_sha256(
            casefold_collision_groups
        ),
    }
    return {
        "schema": "openbfme.sage-side-runtime-semantics",
        "schemaVersion": 0,
        "rawPlayerPolicy": "source-order-preserved-no-synthesis-rename-or-merge",
        "nameLookupPolicy": "exact-case-sensitive-first-source-wins",
        "scriptBindingPolicy": "script-source-ordinal-equals-player-source-index",
        "nameLookup": name_lookup,
        "scriptBindings": script_bindings,
        "duplicateNameGroups": duplicate_groups,
        "caseFoldCollisionGroups": casefold_collision_groups,
        "evidence": evidence,
    }


def _team_runtime_semantics(
    players: list[dict[str, Any]],
    teams: list[dict[str, Any]],
) -> dict[str, Any] | None:
    """Attest EA's load-time repair of authored default-team owners."""

    team_lookup: dict[str, dict[str, Any]] = {}
    runtime_owners: dict[int, str] = {}
    for team in teams:
        team_index = int(team["index"])
        team_lookup.setdefault(str(team["name"]), team)
        runtime_owners[team_index] = str(team["owner"])

    repairs: list[dict[str, Any]] = []
    for player_source_index, player in enumerate(players):
        player_name = str(player["name"])
        team = team_lookup.get("team" + player_name)
        if team is None:
            continue
        team_source_index = int(team["index"])
        if runtime_owners[team_source_index] == player_name:
            continue
        repairs.append(
            {
                "playerSourceIndex": player_source_index,
                "teamSourceIndex": team_source_index,
                "teamName": str(team["name"]),
                "authoredOwner": str(team["owner"]),
                "runtimeOwner": player_name,
            }
        )
        runtime_owners[team_source_index] = player_name

    if not repairs:
        return None
    evidence = {
        "playerRecordCount": len(players),
        "orderedPlayerRecordsSha256": _canonical_json_sha256(players),
        "teamRecordCount": len(teams),
        "orderedTeamRecordsSha256": _canonical_json_sha256(teams),
        "defaultTeamOwnerRepairCount": len(repairs),
        "defaultTeamOwnerRepairsSha256": _canonical_json_sha256(repairs),
    }
    return {
        "schema": "openbfme.sage-team-runtime-semantics",
        "schemaVersion": 0,
        "rawTeamPolicy": "source-order-preserved-no-synthesis-rename-or-merge",
        "defaultTeamLookupPolicy": "exact-case-sensitive-first-source-wins",
        "ownerRepairPolicy": "ea-validate-sides-default-team-owner-repair",
        "defaultTeamOwnerRepairs": repairs,
        "evidence": evidence,
    }


def _validate_multiplayer_setup(
    *,
    profile: _SageMapProfile,
    mp_positions: dict[str, Any] | None,
    sides: dict[str, Any] | None,
    teams: dict[str, Any] | None,
    library_map_lists: dict[str, Any] | None,
    player_scripts_present: bool,
    player_scripts_version: int | None,
    script_summary: dict[str, int],
    source_script_lists: list[dict[str, Any]],
    waypoints_present: bool,
    waypoint_edges: list[dict[str, int]],
    waypoints: list[dict[str, Any]],
    player_starts: dict[str, dict[str, Any]],
) -> dict[str, Any]:
    missing: list[str] = []
    if sides is None:
        missing.append("SidesList")
    if teams is None:
        missing.append("Teams")
    if library_map_lists is None:
        missing.append("LibraryMapLists")
    if not player_scripts_present:
        missing.append("PlayerScriptsList")
    if not waypoints_present:
        missing.append("WaypointsList")
    if missing:
        raise SageMapError(
            f"{profile.map_kind} map is missing required setup chunk(s): "
            + ", ".join(missing)
        )
    assert sides is not None
    assert teams is not None
    assert library_map_lists is not None
    assert player_scripts_version is not None

    players = list(sides["players"])
    team_items = list(teams["teams"])
    library_lists = list(library_map_lists["lists"])
    scenario_player_count = len(players)
    if scenario_player_count == 0:
        raise SageMapError("SidesList contains no scenario players")
    if len(library_lists) != scenario_player_count:
        raise SageMapError(
            "LibraryMapLists list count does not match SidesList player count: "
            f"{len(library_lists)} != {scenario_player_count}"
        )
    if int(script_summary["listCount"]) != scenario_player_count:
        raise SageMapError(
            "PlayerScriptsList list count does not match SidesList player count: "
            f"{script_summary['listCount']} != {scenario_player_count}"
        )

    player_lookup: dict[str, dict[str, Any]] = {}
    for player in players:
        player_lookup.setdefault(str(player["name"]), player)
    team_lookup: dict[str, dict[str, Any]] = {}
    for team in team_items:
        team_lookup.setdefault(str(team["name"]), team)
    for team in team_items:
        owner = str(team["owner"])
        if owner not in player_lookup:
            raise SageMapError(
                f"Teams owner does not resolve to a SidesList player: {owner!r}"
            )
    missing_defaults: list[str] = []
    seen_missing_defaults: set[str] = set()
    for player in players:
        player_name = str(player["name"])
        default_team = team_lookup.get("team" + player_name)
        if default_team is None:
            if player_name not in seen_missing_defaults:
                seen_missing_defaults.add(player_name)
            missing_defaults.append(player_name)
            continue
    if missing_defaults:
        names = ", ".join(repr(name) for name in missing_defaults)
        raise SageMapError(f"SidesList player is missing a default team: {names}")

    side_runtime_semantics = _side_runtime_semantics(
        players,
        source_script_lists,
    )
    team_runtime_semantics = _team_runtime_semantics(players, team_items)

    start_indices = sorted(
        int(item["playerIndex"])
        for item in player_starts.values()
        if "playerIndex" in item
    )
    if profile.enforce_lobby_start_rules:
        if not start_indices:
            raise SageMapError("multiplayer map has no player-start waypoints")
        expected_indices = list(range(1, len(start_indices) + 1))
        if start_indices != expected_indices:
            raise SageMapError(
                f"player-start waypoints are noncontiguous: {start_indices!r}"
            )
        if mp_positions is not None and len(start_indices) > len(
            mp_positions["slots"]
        ):
            raise SageMapError(
                "player-start count exceeds MPPositionList lobby-slot count: "
                f"{len(start_indices)} > {len(mp_positions['slots'])}"
            )
        if len(start_indices) > scenario_player_count:
            raise SageMapError(
                "player-start count exceeds SidesList player count: "
                f"{len(start_indices)} > {scenario_player_count}"
            )

    waypoint_ids = {int(item["id"]) for item in waypoints}
    unresolved_waypoint_edges = 0
    for edge in waypoint_edges:
        start_id = int(edge["startId"])
        end_id = int(edge["endId"])
        if start_id not in waypoint_ids or end_id not in waypoint_ids:
            # Keep the authored edge as evidence, but do not fabricate a runtime
            # endpoint. The cooked adjacency projection omits this record.
            edge["resolved"] = False
            unresolved_waypoint_edges += 1

    unresolved_fields: list[dict[str, Any]] = []
    for name, wire_type in _UNRESOLVED_TEAM_FIELDS.items():
        occurrences = sum(
            1
            for team in team_items
            for prop in team["properties"]
            if prop["name"] == name
        )
        unresolved_fields.append(
            {
                "scope": "team",
                "name": name,
                "wireType": _PROPERTY_WIRE_TYPES[wire_type],
                "wireTypeCode": wire_type,
                "occurrenceCount": occurrences,
                "status": "semantic-unresolved",
            }
        )

    teams_by_name: dict[str, list[dict[str, Any]]] = {}
    for team in team_items:
        teams_by_name.setdefault(str(team["name"]).casefold(), []).append(team)
    duplicate_team_names = [
        {
            "name": str(items[0]["name"]),
            "teamIndices": [int(item["index"]) for item in items],
        }
        for items in teams_by_name.values()
        if len(items) > 1
    ]
    duplicate_team_names.sort(key=lambda item: str(item["name"]).casefold())

    profile_evidence = _profile_evidence(profile)
    lobby_start_cross_check: bool | str = (
        True if profile.enforce_lobby_start_rules else "not-applicable"
    )
    lobby_slot_cross_check: bool | str = lobby_start_cross_check
    if mp_positions is None:
        lobby_slot_cross_check = "not-applicable-source-absent"
    source_versions = {
        "SidesList": int(sides["version"]),
        "Teams": int(teams["version"]),
        "LibraryMapLists": int(library_map_lists["version"]),
        "LibraryMaps": 1,
        "PlayerScriptsList": int(player_scripts_version),
        "WaypointsList": 1,
    }
    if mp_positions is not None:
        source_versions = {
            "MPPositionList": int(mp_positions["version"]),
            "MPPositionInfo": 1,
            **source_versions,
        }
    if player_scripts_version in (5, 6):
        source_versions["ScriptList"] = 1
    setup = {
        "schema": "openbfme.sage-multiplayer-setup",
        "schemaVersion": 0,
        **profile_evidence,
        "sourceVersions": source_versions,
        "declaredPlayerCount": len(start_indices),
        "playerStartIndices": start_indices,
        "lobbySlotCount": len(mp_positions["slots"]) if mp_positions else 0,
        "lobbySlots": mp_positions["slots"] if mp_positions else [],
        "lobbyRuleStatus": (
            "enforced" if profile.enforce_lobby_start_rules else "not-applicable"
        ),
        "scenarioPlayerCount": scenario_player_count,
        "scenarioPlayers": players,
        "teamCount": len(team_items),
        "extraTeamCount": len(team_items) - scenario_player_count,
        "teams": team_items,
        "duplicateTeamNames": duplicate_team_names,
        "sidesUnknownBoolean": (
            bool(sides["unknownBoolean"])
            if bool(sides["unknownBooleanPresent"])
            else None
        ),
        "libraryMapLists": library_lists,
        "libraryDependencyStatus": "references-preserved-not-flattened",
        "libraryCycleValidationStatus": "pending-external-dependency-graph",
        "scriptListCount": int(script_summary["listCount"]),
        "nonemptyScriptListCount": int(script_summary["nonemptyListCount"]),
        "unresolvedSemanticFields": unresolved_fields,
        "crossChecks": {
            "libraryPlayerCountMatches": True,
            "scriptPlayerCountMatches": True,
            "teamOwnersResolved": True,
            "defaultTeamsResolved": True,
            "teamNamesUnique": not duplicate_team_names,
            "playerStartsContiguous": lobby_start_cross_check,
            "startCountWithinLobbySlots": lobby_slot_cross_check,
            "startCountWithinScenarioPlayers": lobby_start_cross_check,
            "waypointEdgeEndpointsResolved": unresolved_waypoint_edges == 0,
        },
        "runtimeStatus": (
            "source-records-imported-runtime-pending"
            if profile.runnable
            else "non-runnable-structural-map"
        ),
    }
    if not bool(sides["unknownBooleanPresent"]):
        setup["sidesUnknownBooleanPresent"] = False
    if mp_positions is None:
        setup["lobbySourceStatus"] = "not-present-in-source"
    if side_runtime_semantics is not None:
        setup["sourceScriptLists"] = source_script_lists
        setup["sideRuntimeSemantics"] = side_runtime_semantics
    if team_runtime_semantics is not None:
        setup["teamRuntimeSemantics"] = team_runtime_semantics
    return setup


@dataclass(slots=True)
class ParsedSageMap:
    profile: dict[str, Any]
    source_chunk_layouts: dict[str, dict[str, Any]]
    source_sha256: str
    body_sha256: str
    envelope: dict[str, Any]
    chunks: list[dict[str, Any]]
    heightmap: _HeightMap
    impassability: bytes
    blend: dict[str, Any]
    terrain_source_layers: dict[str, bytes]
    standing_water: list[dict[str, Any]]
    rivers: list[dict[str, Any]]
    objects: list[dict[str, Any]]
    waypoints: list[dict[str, Any]]
    player_starts: dict[str, dict[str, Any]]
    script_summary: dict[str, int]
    triggers: list[dict[str, Any]]
    standing_waves: list[dict[str, Any]]
    waypoint_edges: list[dict[str, int]]
    setup: dict[str, Any]

    @property
    def trigger_count(self) -> int:
        return len(self.triggers)

    @property
    def standing_wave_count(self) -> int:
        return len(self.standing_waves)

    @property
    def waypoint_path_count(self) -> int:
        return len(self.waypoint_edges)


def parse_sage_map_bytes(
    source: bytes, *, profile: str = "multiplayer"
) -> ParsedSageMap:
    resolved_profile = _resolve_map_profile(profile)
    body, envelope = decode_sage_map_blob(source)
    cursor = _Cursor(body, label="CkMp map")
    if cursor.bytes(4) != b"CkMp":
        raise SageMapError("missing CkMp magic")
    names = _parse_name_table(cursor)

    chunks: list[dict[str, Any]] = []
    source_chunk_layouts: dict[str, dict[str, Any]] = {}
    heightmap: _HeightMap | None = None
    impassability: bytes | None = None
    blend: dict[str, Any] | None = None
    terrain_source_layers: dict[str, bytes] | None = None
    standing_water: list[dict[str, Any]] = []
    rivers: list[dict[str, Any]] = []
    objects: list[dict[str, Any]] = []
    waypoints: list[dict[str, Any]] = []
    player_starts: dict[str, dict[str, Any]] = {}
    script_summary = {"listCount": 0, "nonemptyListCount": 0}
    source_script_lists: list[dict[str, Any]] = []
    triggers: list[dict[str, Any]] = []
    standing_waves: list[dict[str, Any]] = []
    waypoint_edges: list[dict[str, int]] = []
    player_scripts_version: int | None = None
    mp_positions: dict[str, Any] | None = None
    sides: dict[str, Any] | None = None
    teams: dict[str, Any] | None = None
    library_map_lists: dict[str, Any] | None = None
    trigger_areas_seen = False
    standing_wave_areas_seen = False
    objects_seen = False
    waypoints_list_seen = False
    player_scripts_seen = False

    for record in _records(cursor, names, cap=MAX_TOP_LEVEL_RECORDS, label="MapFile"):
        chunk = {
            "name": record.name,
            "version": record.version,
            "payloadSize": record.size,
            "recordOffset": record.offset,
        }
        chunks.append(chunk)
        if record.name == "HeightMapData":
            if heightmap is not None:
                raise SageMapError("duplicate HeightMapData")
            heightmap = _parse_height(
                record,
                minimum_dimension=resolved_profile.minimum_terrain_dimension,
            )
        elif record.name == "BlendTileData":
            if heightmap is None:
                raise SageMapError("BlendTileData appears before HeightMapData")
            if blend is not None:
                raise SageMapError("duplicate BlendTileData")
            impassability, blend, terrain_source_layers = _parse_blend(
                record, heightmap
            )
            if record.version == 17:
                chunk["layoutCompatibleWithVersion"] = 18
                source_chunk_layouts[record.name] = {
                    "sourceVersion": 17,
                    "layoutCompatibleWithVersion": 18,
                }
            elif record.version in _LOSSLESS_LEGACY_BLEND_VERSIONS:
                layout_evidence = _legacy_blend_layout_evidence(record.version)
                chunk.update(
                    {
                        "blendCellWordBits": layout_evidence["blendCellWordBits"],
                        "sourceLayerPresence": layout_evidence["sourceLayerPresence"],
                        "structuralConversion": layout_evidence["structuralConversion"],
                        "runtimeDefaultParity": layout_evidence["runtimeDefaultParity"],
                    }
                )
                source_chunk_layouts[record.name] = layout_evidence
        elif record.name == "ObjectsList":
            if heightmap is None:
                raise SageMapError("ObjectsList appears before HeightMapData")
            if objects_seen:
                raise SageMapError("duplicate ObjectsList")
            objects_seen = True
            objects, waypoints, player_starts = _parse_objects(record, names, heightmap)
        elif record.name == "MPPositionList":
            if mp_positions is not None:
                raise SageMapError("duplicate MPPositionList")
            mp_positions = _parse_mp_positions(record, names)
        elif record.name == "SidesList":
            if sides is not None:
                raise SageMapError("duplicate SidesList")
            sides = _parse_sides(record, names)
            if record.version == 5:
                chunk["unknownBooleanPresent"] = False
                source_chunk_layouts[record.name] = {
                    "sourceVersion": 5,
                    "unknownBooleanPresent": False,
                }
        elif record.name == "Teams":
            if teams is not None:
                raise SageMapError("duplicate Teams")
            teams = _parse_teams(record, names)
        elif record.name == "LibraryMapLists":
            if library_map_lists is not None:
                raise SageMapError("duplicate LibraryMapLists")
            library_map_lists = _parse_library_map_lists(record, names)
        elif record.name == "StandingWaterAreas":
            standing_water = _parse_standing_water(record)
        elif record.name == "RiverAreas":
            rivers = _parse_rivers(record)
            if record.version == 1:
                chunk["layoutCompatibleWithVersion"] = 2
                source_chunk_layouts[record.name] = {
                    "sourceVersion": 1,
                    "layoutCompatibleWithVersion": 2,
                }
        elif record.name == "TriggerAreas":
            if trigger_areas_seen:
                raise SageMapError("duplicate TriggerAreas")
            trigger_areas_seen = True
            triggers = _parse_trigger_areas(record)
        elif record.name == "StandingWaveAreas":
            if standing_wave_areas_seen:
                raise SageMapError("duplicate StandingWaveAreas")
            standing_wave_areas_seen = True
            standing_waves = _parse_standing_wave_areas(record)
            if record.version == 1:
                chunk["pcaWaveFieldPresent"] = False
                source_chunk_layouts[record.name] = {
                    "sourceVersion": 1,
                    "pcaWaveFieldPresent": False,
                }
        elif record.name == "WaypointsList":
            if waypoints_list_seen:
                raise SageMapError("duplicate WaypointsList")
            waypoints_list_seen = True
            waypoint_edges = _parse_waypoint_edges(record)
        elif record.name == "PlayerScriptsList":
            if player_scripts_seen:
                raise SageMapError("duplicate PlayerScriptsList")
            player_scripts_seen = True
            player_scripts_version = record.version
            parsed_scripts = _parse_player_scripts(record, names)
            script_summary = {
                "listCount": int(parsed_scripts["listCount"]),
                "nonemptyListCount": int(parsed_scripts["nonemptyListCount"]),
            }
            source_script_lists = list(parsed_scripts["sourceLists"])
            if record.version in (5, 6):
                chunk["nestedScriptListVersion"] = 1
                source_chunk_layouts[record.name] = {
                    "sourceVersion": record.version,
                    "nestedScriptListVersion": 1,
                }
        else:
            record.payload.skip(record.payload.remaining)
    cursor.finish()
    if (
        heightmap is None
        or impassability is None
        or blend is None
        or terrain_source_layers is None
    ):
        raise SageMapError("map is missing required height or blend data")
    if not objects_seen:
        raise SageMapError(
            f"{resolved_profile.map_kind} map is missing required ObjectsList"
        )
    if mp_positions is None:
        # Pinned OpenSAGE MapFile parsing/writing treats MPPositionList as
        # conditional, while UserMapCache derives multiplayer capacity from
        # consecutive exact Player_N_Start waypoints instead.
        source_chunk_layouts["MPPositionList"] = {
            "sourceVersion": None,
            "present": False,
            "absence": "not-present-in-source",
            "structuralConversion": "lossless-source-absence-preservation",
            "runtimeDefaultParity": "not-applicable-runtime-does-not-consult-chunk",
        }
    setup = _validate_multiplayer_setup(
        profile=resolved_profile,
        mp_positions=mp_positions,
        sides=sides,
        teams=teams,
        library_map_lists=library_map_lists,
        player_scripts_present=player_scripts_seen,
        player_scripts_version=player_scripts_version,
        script_summary=script_summary,
        source_script_lists=source_script_lists,
        waypoints_present=waypoints_list_seen,
        waypoint_edges=waypoint_edges,
        waypoints=waypoints,
        player_starts=player_starts,
    )
    if source_chunk_layouts:
        setup["sourceChunkLayouts"] = source_chunk_layouts
    return ParsedSageMap(
        profile=_profile_evidence(resolved_profile),
        source_chunk_layouts=source_chunk_layouts,
        source_sha256=hashlib.sha256(source).hexdigest(),
        body_sha256=hashlib.sha256(body).hexdigest(),
        envelope=envelope,
        chunks=chunks,
        heightmap=heightmap,
        impassability=impassability,
        blend=blend,
        terrain_source_layers=terrain_source_layers,
        standing_water=standing_water,
        rivers=rivers,
        objects=objects,
        waypoints=waypoints,
        player_starts=player_starts,
        script_summary=script_summary,
        triggers=triggers,
        standing_waves=standing_waves,
        waypoint_edges=waypoint_edges,
        setup=setup,
    )


def parse_sage_map_file(
    path: Path | str, *, profile: str = "multiplayer"
) -> ParsedSageMap:
    source = Path(path)
    size = source.stat().st_size
    if size > MAX_SOURCE_BYTES:
        raise SageMapError(f"map source exceeds limit: {size}")
    return parse_sage_map_bytes(source.read_bytes(), profile=profile)


def _parse_castle_templates(record: _Record, names: dict[int, str]) -> dict[str, Any]:
    """Decode the BFME castle-template payload embedded in a ``.bse`` map.

    The shipped BFME II base templates use version 5, while the field layout is
    stable across the BFME-era versions documented by the frozen corpus.  Keep
    the supported range explicit and reject future layouts instead of guessing.
    """

    if record.version < 1 or record.version > 5:
        raise SageMapError(f"unsupported CastleTemplates version: {record.version}")
    cursor = record.payload
    property_type = cursor.u8()
    if property_type not in _PROPERTY_WIRE_TYPES:
        raise SageMapError(
            f"unsupported CastleTemplates property type: {property_type}"
        )
    property_name_index = cursor.u24()
    try:
        property_name = names[property_name_index]
    except KeyError as exc:
        raise SageMapError(
            "CastleTemplates references an unknown property-name index: "
            f"{property_name_index}"
        ) from exc

    count = cursor.u32()
    if count > MAX_CASTLE_TEMPLATES:
        raise SageMapError(f"castle-template count exceeds limit: {count}")
    templates: list[dict[str, Any]] = []
    for index in range(count):
        name = cursor.ascii16()
        template_name = cursor.ascii16()
        if not template_name:
            raise SageMapError(
                f"castle template {index} has an empty object template name"
            )
        offset = [cursor.f32(), cursor.f32(), cursor.f32()]
        angle = cursor.f32()
        priority = cursor.u32() if record.version >= 4 else 0
        phase = cursor.u32() if record.version >= 4 else 0
        templates.append(
            {
                "index": index,
                "name": name,
                "templateName": template_name,
                "offset": offset,
                "angleRadians": angle,
                "priority": priority,
                "phase": phase,
            }
        )

    perimeter: dict[str, Any] | None = None
    if record.version >= 2:
        has_perimeter_raw = cursor.u32()
        if has_perimeter_raw not in (0, 1):
            raise SageMapError(
                "CastleTemplates perimeter marker must be a 32-bit boolean"
            )
        points: list[list[float | int]] = []
        if has_perimeter_raw:
            point_count = cursor.u32()
            if point_count > MAX_POINTS:
                raise SageMapError(
                    f"castle perimeter point count exceeds limit: {point_count}"
                )
            for _ in range(point_count):
                if record.version >= 3:
                    points.append([cursor.f32(), cursor.f32(), 0.0])
                else:
                    points.append([cursor.i32(), cursor.i32(), cursor.i32()])
        perimeter = {
            "present": bool(has_perimeter_raw),
            "points": points,
        }
    cursor.finish()
    return {
        "version": record.version,
        "propertyKey": {
            "name": property_name,
            "wireType": _PROPERTY_WIRE_TYPES[property_type],
            "wireTypeCode": property_type,
        },
        "templates": templates,
        "perimeter": perimeter,
    }


def parse_sage_base_template_bytes(source: bytes) -> dict[str, Any]:
    """Return payload-free, source-proven facts from a BFME ``.bse`` file."""

    if len(source) > MAX_SOURCE_BYTES:
        raise SageMapError(f"base-template source exceeds limit: {len(source)}")
    body, envelope = decode_sage_map_blob(source)
    cursor = _Cursor(body, label="CkMp base template")
    if cursor.bytes(4) != b"CkMp":
        raise SageMapError("missing CkMp magic")
    names = _parse_name_table(cursor)

    heightmap: _HeightMap | None = None
    objects: list[dict[str, Any]] | None = None
    castle_templates: dict[str, Any] | None = None
    chunks: list[dict[str, Any]] = []
    for record in _records(
        cursor, names, cap=MAX_TOP_LEVEL_RECORDS, label="BaseTemplate"
    ):
        chunks.append(
            {
                "name": record.name,
                "version": record.version,
                "payloadSize": record.size,
                "recordOffset": record.offset,
            }
        )
        if record.name == "HeightMapData":
            if heightmap is not None:
                raise SageMapError("duplicate HeightMapData in base template")
            heightmap = _parse_height(record, minimum_dimension=1)
        elif record.name == "ObjectsList":
            if heightmap is None:
                raise SageMapError(
                    "base-template ObjectsList appears before HeightMapData"
                )
            if objects is not None:
                raise SageMapError("duplicate ObjectsList in base template")
            objects, _, _ = _parse_objects(record, names, heightmap)
        elif record.name == "CastleTemplates":
            if castle_templates is not None:
                raise SageMapError("duplicate CastleTemplates in base template")
            castle_templates = _parse_castle_templates(record, names)
        else:
            record.payload.skip(record.payload.remaining)
    cursor.finish()

    if heightmap is None:
        raise SageMapError("base template is missing HeightMapData")
    if objects is None:
        raise SageMapError("base template is missing ObjectsList")
    if castle_templates is None:
        raise SageMapError("base template is missing CastleTemplates")

    object_type_counts = Counter(str(item["typeName"]) for item in objects)
    unresolved = sorted(
        {
            str(item["templateName"])
            for item in castle_templates["templates"]
            if str(item["templateName"]) not in object_type_counts
        },
        key=lambda value: (value.casefold(), value),
    )
    if unresolved:
        raise SageMapError(
            "CastleTemplates references object types absent from ObjectsList: "
            + ", ".join(unresolved)
        )

    return {
        "schema": "openbfme.sage-base-template-evidence",
        "schemaVersion": 0,
        "source": {
            "byteCount": len(source),
            "sha256": hashlib.sha256(source).hexdigest(),
            "bodySha256": hashlib.sha256(body).hexdigest(),
            "packaged": False,
            "envelope": envelope,
        },
        "heightmap": {
            "width": heightmap.width,
            "height": heightmap.height,
            "borderWidth": heightmap.border_width,
        },
        "castleTemplates": castle_templates,
        "objects": {
            "count": len(objects),
            "typeCounts": [
                {"typeName": name, "count": object_type_counts[name]}
                for name in sorted(
                    object_type_counts, key=lambda value: (value.casefold(), value)
                )
            ],
            "allCastleTemplateTypesPresent": True,
        },
        "chunks": chunks,
    }


def parse_sage_base_template_file(path: Path | str) -> dict[str, Any]:
    source = Path(path)
    size = source.stat().st_size
    if size > MAX_SOURCE_BYTES:
        raise SageMapError(f"base-template source exceeds limit: {size}")
    return parse_sage_base_template_bytes(source.read_bytes())


def _census_set_sha256(domain: str, values: list[str]) -> str:
    digest = hashlib.sha256()
    digest.update(domain.encode("ascii") + b"\0")
    for value in sorted(set(values), key=lambda item: (item.casefold(), item)):
        encoded = value.encode("utf-8")
        digest.update(len(encoded).to_bytes(4, "little"))
        digest.update(encoded)
    return digest.hexdigest()


def census_sage_map_bytes(
    source: bytes, *, profile: str = "multiplayer"
) -> dict[str, Any]:
    """Return bounded payload-free facts without weakening the strict map cook."""

    resolved_profile = _resolve_map_profile(profile)
    body, envelope = decode_sage_map_blob(source)
    cursor = _Cursor(body, label="CkMp census")
    if cursor.bytes(4) != b"CkMp":
        raise SageMapError("missing CkMp magic")
    names = _parse_name_table(cursor)

    chunks: dict[tuple[str, int], dict[str, Any]] = {}
    features: dict[str, Any] = {
        "height": {},
        "terrain": {},
        "objects": {},
        "standingWaterCount": None,
        "riverCount": None,
        "triggerAreaCount": None,
        "standingWaveAreaCount": None,
        "waypointPathCount": None,
        "scriptListCount": None,
        "nonemptyScriptListCount": None,
        "lobbySlotCount": None,
        "scenarioPlayerCount": None,
        "teamCount": None,
        "libraryListCount": None,
    }
    heightmap: _HeightMap | None = None

    for record in _records(cursor, names, cap=MAX_TOP_LEVEL_RECORDS, label="MapFile"):
        key = (record.name, record.version)
        chunk = chunks.setdefault(
            key,
            {
                "name": record.name,
                "version": record.version,
                "occurrences": 0,
                "payloadBytes": 0,
                "probeStatuses": set(),
                "probeRejections": {},
            },
        )
        chunk["occurrences"] += 1
        chunk["payloadBytes"] += record.size

        supported_versions = _SUPPORTED_CHUNK_VERSIONS.get(record.name)
        if supported_versions is None:
            status = "unclassified"
            rejection = None
        elif record.version not in supported_versions:
            status = "unsupported-version"
            rejection = None
        else:
            status = "parsed"
            rejection = None
            try:
                if record.name == "HeightMapData":
                    heightmap = _parse_height(
                        record,
                        minimum_dimension=resolved_profile.minimum_terrain_dimension,
                    )
                    features["height"] = {
                        "width": heightmap.width,
                        "height": heightmap.height,
                        "borderWidth": heightmap.border_width,
                    }
                elif record.name == "BlendTileData":
                    if heightmap is None:
                        raise SageMapError(
                            "BlendTileData census requires earlier HeightMapData"
                        )
                    _, blend, _ = _parse_blend(record, heightmap)
                    texture_names = [str(item["name"]) for item in blend["textures"]]
                    features["terrain"] = {
                        "textureSymbolCount": len(texture_names),
                        "textureSymbolSetSha256": _census_set_sha256(
                            "openbfme.terrain-symbol-set", texture_names
                        ),
                        "impassableCount": int(blend["gridStats"]["impassable"]),
                    }
                elif record.name == "ObjectsList":
                    if heightmap is None:
                        raise SageMapError(
                            "ObjectsList census requires earlier HeightMapData"
                        )
                    objects, waypoints, starts = _parse_objects(
                        record, names, heightmap
                    )
                    type_names = [str(item["typeName"]) for item in objects]
                    features["objects"] = {
                        "placementCount": len(objects),
                        "uniqueTypeCount": len(set(type_names)),
                        "typeSetSha256": _census_set_sha256(
                            "openbfme.object-type-set", type_names
                        ),
                        "waypointCount": len(waypoints),
                        "playerStartCount": len(starts),
                    }
                elif record.name == "MPPositionList":
                    positions = _parse_mp_positions(record, names)
                    features["lobbySlotCount"] = len(positions["slots"])
                elif record.name == "SidesList":
                    sides = _parse_sides(record, names)
                    features["scenarioPlayerCount"] = len(sides["players"])
                elif record.name == "Teams":
                    teams = _parse_teams(record, names)
                    features["teamCount"] = len(teams["teams"])
                elif record.name == "LibraryMapLists":
                    libraries = _parse_library_map_lists(record, names)
                    features["libraryListCount"] = len(libraries["lists"])
                elif record.name == "StandingWaterAreas":
                    features["standingWaterCount"] = len(_parse_standing_water(record))
                elif record.name == "RiverAreas":
                    features["riverCount"] = len(_parse_rivers(record))
                elif record.name == "PlayerScriptsList":
                    summary = _parse_player_scripts(record, names)
                    features["scriptListCount"] = int(summary["listCount"])
                    features["nonemptyScriptListCount"] = int(
                        summary["nonemptyListCount"]
                    )
                elif record.name == "TriggerAreas":
                    features["triggerAreaCount"] = len(_parse_trigger_areas(record))
                elif record.name == "StandingWaveAreas":
                    features["standingWaveAreaCount"] = len(
                        _parse_standing_wave_areas(record)
                    )
                elif record.name == "WaypointsList":
                    features["waypointPathCount"] = len(_parse_waypoint_edges(record))
            except SageMapError as exc:
                status = "semantic-rejected"
                rejection = " ".join(str(exc).split())

        chunk["probeStatuses"].add(status)
        if rejection:
            chunk["probeRejections"][rejection] = (
                int(chunk["probeRejections"].get(rejection, 0)) + 1
            )

    cursor.finish()
    chunk_rows: list[dict[str, Any]] = []
    for chunk in chunks.values():
        statuses = sorted(chunk.pop("probeStatuses"))
        rejections = chunk.pop("probeRejections")
        chunk["probeStatus"] = statuses[0] if len(statuses) == 1 else "mixed"
        chunk["probeRejections"] = [
            {"reason": reason, "occurrences": count}
            for reason, count in sorted(
                rejections.items(), key=lambda item: item[0].casefold()
            )
        ]
        chunk_rows.append(chunk)
    chunk_rows.sort(key=lambda item: (item["name"].casefold(), item["version"]))

    strict_accepted = True
    strict_reason: str | None = None
    try:
        parse_sage_map_bytes(source, profile=resolved_profile.map_kind)
    except SageMapError as exc:
        strict_accepted = False
        strict_reason = " ".join(str(exc).split())

    return {
        "sourceBytes": len(source),
        "sourceSha256": hashlib.sha256(source).hexdigest(),
        "bodySha256": hashlib.sha256(body).hexdigest(),
        "envelopeKind": str(envelope["kind"]),
        "chunks": chunk_rows,
        "features": features,
        "strictCook": {
            **_profile_evidence(resolved_profile),
            "accepted": strict_accepted,
            "reason": strict_reason,
        },
    }


def census_sage_map_file(
    path: Path | str, *, profile: str = "multiplayer"
) -> dict[str, Any]:
    source = Path(path)
    size = source.stat().st_size
    if size > MAX_SOURCE_BYTES:
        raise SageMapError(f"map source exceeds limit: {size}")
    return census_sage_map_bytes(source.read_bytes(), profile=profile)


def _height_metadata(heightmap: _HeightMap) -> dict[str, Any]:
    raw_min = min(heightmap.elevations)
    raw_max = max(heightmap.elevations)
    return {
        "version": heightmap.version,
        "width": heightmap.width,
        "height": heightmap.height,
        "area": heightmap.area,
        "borderWidth": heightmap.border_width,
        "borders": heightmap.borders,
        "renderedQuads": [heightmap.width - 1, heightmap.height - 1],
        "playableWorldExtent": [
            (heightmap.width - 2 * heightmap.border_width) * 10,
            (heightmap.height - 2 * heightmap.border_width) * 10,
        ],
        "horizontalScale": 10.0,
        "verticalScale": heightmap.vertical_scale,
        "rawElevationMin": raw_min,
        "rawElevationMax": raw_max,
        "worldElevationMin": raw_min * heightmap.vertical_scale,
        "worldElevationMax": raw_max * heightmap.vertical_scale,
        "heightmap": {
            "path": "heightmap.r16",
            "encoding": "uint16",
            "endianness": "little",
            "order": "row-major-y-then-x",
        },
    }


def _source_grid_descriptor(
    path: str,
    payload: bytes,
    *,
    cell_count: int,
    cell_size: int,
    encoding: str,
) -> dict[str, Any]:
    expected_bytes = cell_count * cell_size
    if len(payload) != expected_bytes:
        raise SageMapError(
            f"terrain source grid {path} has {len(payload)} bytes; expected {expected_bytes}"
        )
    return {
        "path": path,
        "encoding": encoding,
        "endianness": "little",
        "order": "row-major-y-then-x",
        "cellCount": cell_count,
        "cellSizeBytes": cell_size,
        "byteLength": len(payload),
        "sha256": hashlib.sha256(payload).hexdigest(),
        "sourceExact": True,
    }


def _source_packed_bit_grid_descriptor(
    path: str,
    payload: bytes,
    *,
    width: int,
    height: int,
) -> dict[str, Any]:
    row_stride = (width + 7) // 8
    expected_bytes = row_stride * height
    if len(payload) != expected_bytes:
        raise SageMapError(
            f"terrain packed source grid {path} has {len(payload)} bytes; "
            f"expected {expected_bytes}"
        )
    return {
        "path": path,
        "encoding": "packed-single-bit",
        "bitOrder": "least-significant-bit-first",
        "order": "row-major-y-then-x",
        "gridWidth": width,
        "gridHeight": height,
        "rowStrideBytes": row_stride,
        "rowPadding": True,
        "byteLength": len(payload),
        "sha256": hashlib.sha256(payload).hexdigest(),
        "sourceExact": True,
    }


def _source_description_descriptor(
    path: str,
    payload: bytes,
    *,
    source_count: int,
    record_count: int,
    record_size: int,
) -> dict[str, Any]:
    expected_count = max(source_count - 1, 0)
    if record_count != expected_count:
        raise SageMapError(
            f"terrain source description count for {path} is {record_count}; "
            f"expected {expected_count} from source count {source_count}"
        )
    expected_bytes = record_count * record_size
    if len(payload) != expected_bytes:
        raise SageMapError(
            f"terrain source descriptions {path} have {len(payload)} bytes; "
            f"expected {expected_bytes}"
        )
    return {
        "path": path,
        "encoding": "opaque-fixed-records",
        "sourceCount": source_count,
        "reservedZeroEntryExcluded": source_count > 0,
        "recordCount": record_count,
        "recordSizeBytes": record_size,
        "byteLength": len(payload),
        "sha256": hashlib.sha256(payload).hexdigest(),
        "sourceExact": True,
    }


def _contained_output_path(output: Path, relative_path: str) -> Path:
    if Path(relative_path).is_absolute():
        raise SageMapError(
            f"terrain output path must be pack-relative: {relative_path}"
        )
    target = output / relative_path
    try:
        target.resolve().relative_to(output.resolve())
    except ValueError as exc:
        raise SageMapError(
            f"terrain output path escapes map directory: {relative_path}"
        ) from exc
    return target


def _bounded_binding_text(value: Any, label: str) -> str:
    if not isinstance(value, str) or not value or len(value) > MAX_OBJECT_BINDING_TEXT:
        raise SageMapError(
            f"{label} must be a nonempty string of at most "
            f"{MAX_OBJECT_BINDING_TEXT} characters"
        )
    if value != value.strip() or any(ord(character) < 32 for character in value):
        raise SageMapError(f"unsafe {label}: {value!r}")
    return value


def _binding_path(value: Any, label: str, suffix: str) -> str:
    raw = _bounded_binding_text(value, label)
    try:
        parts = safe_relative_parts(raw)
    except ValueError as exc:
        raise SageMapError(f"unsafe {label}: {raw!r}") from exc
    normalized = "/".join(parts)
    if Path(normalized).suffix.casefold() != suffix:
        raise SageMapError(f"{label} must name a {suffix} file: {raw!r}")
    return normalized


def _binding_entry(
    value: Any,
    *,
    kind: str,
    required_fields: frozenset[str],
    optional_fields: frozenset[str] = frozenset(),
) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise SageMapError(f"sage-map objectBindings.{kind} entries must be objects")
    fields = set(value)
    missing = sorted(required_fields - fields)
    unsupported = sorted(fields - required_fields - optional_fields)
    if missing:
        raise SageMapError(
            f"sage-map objectBindings.{kind} entry is missing field(s): "
            + ", ".join(missing)
        )
    if unsupported:
        raise SageMapError(
            f"sage-map objectBindings.{kind} entry has unsupported field(s): "
            + ", ".join(unsupported)
        )
    return dict(value)


def _object_binding_inventory(
    objects: list[dict[str, Any]],
    object_bindings: Any,
) -> dict[str, Any]:
    type_counts: dict[str, int] = {}
    cooked_case: dict[str, str] = {}
    for item in objects:
        type_name = str(item["typeName"])
        folded = type_name.casefold()
        prior = cooked_case.get(folded)
        if prior is not None and prior != type_name:
            raise SageMapError(
                f"case-colliding cooked object type names: {prior!r} and {type_name!r}"
            )
        cooked_case[folded] = type_name
        type_counts[type_name] = type_counts.get(type_name, 0) + 1

    logical_entries: list[Any] = []
    model_entries: list[Any] = []
    structure_entries: list[Any] = []
    if object_bindings is not None:
        if not isinstance(object_bindings, dict):
            raise SageMapError("sage-map options.objectBindings must be an object")
        fields = set(object_bindings)
        required = {"logical", "models"}
        allowed = required | {"structures"}
        missing = sorted(required - fields)
        unsupported = sorted(fields - allowed)
        if missing:
            raise SageMapError(
                "sage-map options.objectBindings is missing field(s): "
                + ", ".join(missing)
            )
        if unsupported:
            raise SageMapError(
                "sage-map options.objectBindings has unsupported field(s): "
                + ", ".join(unsupported)
            )
        logical_entries = object_bindings["logical"]
        model_entries = object_bindings["models"]
        structure_entries = object_bindings.get("structures", [])
        if (
            not isinstance(logical_entries, list)
            or not isinstance(model_entries, list)
            or not isinstance(structure_entries, list)
        ):
            raise SageMapError(
                "sage-map options.objectBindings.logical, .models, and optional "
                ".structures must be arrays"
            )
        if (
            len(logical_entries) + len(model_entries) + len(structure_entries)
            > MAX_OBJECT_BINDING_TYPES
        ):
            raise SageMapError(
                "sage-map object binding count exceeds limit: "
                f"{MAX_OBJECT_BINDING_TYPES}"
            )

    declarations: dict[str, dict[str, Any]] = {}
    declared_case: dict[str, tuple[str, str]] = {}

    def register(type_name: str, kind: str, record: dict[str, Any]) -> None:
        folded = type_name.casefold()
        prior = declared_case.get(folded)
        if prior is not None:
            prior_name, prior_kind = prior
            if prior_kind != kind:
                label = (
                    "conflicting logical/model object binding entries for "
                    if {prior_kind, kind} == {"logical", "models"}
                    else "conflicting object binding entries for "
                )
                raise SageMapError(label + f"{prior_name!r} and {type_name!r}")
            if prior_name == type_name:
                raise SageMapError(f"duplicate {kind} object binding: {type_name!r}")
            raise SageMapError(
                f"case-colliding {kind} object bindings: {prior_name!r} and {type_name!r}"
            )
        exact_cooked = cooked_case.get(folded)
        if exact_cooked is None:
            raise SageMapError(f"unknown declared object typeName: {type_name!r}")
        if exact_cooked != type_name:
            raise SageMapError(
                f"declared object typeName must match cooked case exactly: "
                f"{type_name!r} != {exact_cooked!r}"
            )
        declared_case[folded] = (type_name, kind)
        declarations[type_name] = record

    logical_fields = frozenset({"typeName", "classification"})
    for raw in logical_entries:
        entry = _binding_entry(raw, kind="logical", required_fields=logical_fields)
        type_name = _bounded_binding_text(
            entry["typeName"], "objectBindings.logical.typeName"
        )
        classification = _bounded_binding_text(
            entry["classification"], "objectBindings.logical.classification"
        )
        if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,127}", classification):
            raise SageMapError(
                f"unsafe objectBindings.logical.classification: {classification!r}"
            )
        register(
            type_name,
            "logical",
            {
                "classification": classification,
                "status": "logical",
                "matchMethod": OBJECT_BINDING_MATCH_METHOD,
                "sourceVirtualModel": None,
                "glb": None,
            },
        )

    def walk_surface_sources(value: object, label: str) -> dict[str, dict[str, str]]:
        if value is None:
            return {}
        if not isinstance(value, dict) or not value:
            raise SageMapError(f"{label} must be a non-empty object")
        result: dict[str, dict[str, str]] = {}
        for mesh_name, raw_source in value.items():
            if (
                not isinstance(mesh_name, str)
                or not mesh_name
                or not isinstance(raw_source, dict)
                or set(raw_source) != {"sourceVirtualModel", "glb"}
            ):
                raise SageMapError(f"{label} has an invalid source row")
            result[mesh_name] = {
                "sourceVirtualModel": _binding_path(
                    raw_source["sourceVirtualModel"],
                    f"{label}.{mesh_name}.sourceVirtualModel",
                    ".w3d",
                ),
                "glb": _binding_path(
                    raw_source["glb"], f"{label}.{mesh_name}.glb", ".glb"
                ),
            }
        return result

    model_fields = frozenset(
        {"typeName", "sourceVirtualModel", "glb", "matchMethod"}
    )
    for raw in model_entries:
        entry = _binding_entry(
            raw,
            kind="models",
            required_fields=model_fields,
            optional_fields=frozenset({"walkSurfaceSources"}),
        )
        type_name = _bounded_binding_text(
            entry["typeName"], "objectBindings.models.typeName"
        )
        match_method = _bounded_binding_text(
            entry["matchMethod"], "objectBindings.models.matchMethod"
        )
        if match_method != OBJECT_BINDING_MATCH_METHOD:
            raise SageMapError(
                "sage-map object model bindings require "
                f"matchMethod={OBJECT_BINDING_MATCH_METHOD!r}"
            )
        register(
            type_name,
            "models",
            {
                "classification": "renderable",
                "status": "bound",
                "matchMethod": match_method,
                "sourceVirtualModel": _binding_path(
                    entry["sourceVirtualModel"],
                    "objectBindings.models.sourceVirtualModel",
                    ".w3d",
                ),
                "glb": _binding_path(entry["glb"], "objectBindings.models.glb", ".glb"),
                **(
                    {
                        "walkSurfaceSources": walk_surface_sources(
                            entry.get("walkSurfaceSources"),
                            "objectBindings.models.walkSurfaceSources",
                        )
                    }
                    if entry.get("walkSurfaceSources") is not None
                    else {}
                ),
            },
        )

    structure_fields = frozenset(
        {"typeName", "sourceVirtualModel", "glb", "objectId", "matchMethod"}
    )
    for raw in structure_entries:
        entry = _binding_entry(
            raw,
            kind="structures",
            required_fields=structure_fields,
            optional_fields=frozenset({"walkSurfaceSources"}),
        )
        type_name = _bounded_binding_text(
            entry["typeName"], "objectBindings.structures.typeName"
        )
        match_method = _bounded_binding_text(
            entry["matchMethod"], "objectBindings.structures.matchMethod"
        )
        if match_method != OBJECT_BINDING_MATCH_METHOD:
            raise SageMapError(
                "sage-map object structure bindings require "
                f"matchMethod={OBJECT_BINDING_MATCH_METHOD!r}"
            )
        object_id = _bounded_binding_text(
            entry["objectId"], "objectBindings.structures.objectId"
        )
        if not re.fullmatch(r"[a-z0-9][a-z0-9._-]{0,127}", object_id):
            raise SageMapError(
                f"unsafe objectBindings.structures.objectId: {object_id!r}"
            )
        register(
            type_name,
            "structures",
            {
                "classification": "lifecycle-structure",
                "status": "bound",
                "matchMethod": match_method,
                "sourceVirtualModel": _binding_path(
                    entry["sourceVirtualModel"],
                    "objectBindings.structures.sourceVirtualModel",
                    ".w3d",
                ),
                "glb": _binding_path(
                    entry["glb"], "objectBindings.structures.glb", ".glb"
                ),
                "objectId": object_id,
                **(
                    {
                        "walkSurfaceSources": walk_surface_sources(
                            entry.get("walkSurfaceSources"),
                            "objectBindings.structures.walkSurfaceSources",
                        )
                    }
                    if entry.get("walkSurfaceSources") is not None
                    else {}
                ),
            },
        )

    records: list[dict[str, Any]] = []
    for type_name in sorted(type_counts, key=lambda value: (value.casefold(), value)):
        binding = declarations.get(
            type_name,
            {
                "classification": "unknown",
                "status": "unresolved",
                "matchMethod": "none",
                "sourceVirtualModel": None,
                "glb": None,
            },
        )
        records.append(
            {
                "typeName": type_name,
                "placementCount": type_counts[type_name],
                **binding,
            }
        )

    type_summary = {
        status: sum(record["status"] == status for record in records)
        for status in ("bound", "logical", "unresolved")
    }
    placement_summary = {
        status: sum(
            int(record["placementCount"])
            for record in records
            if record["status"] == status
        )
        for status in ("bound", "logical", "unresolved")
    }
    unresolved_type_count = type_summary["unresolved"]
    resolution_status = "complete" if unresolved_type_count == 0 else "partial"
    summary = {
        "resolutionStatus": resolution_status,
        "typeCount": len(records),
        "placementCount": len(objects),
        "resolvedTypeCount": type_summary["bound"] + type_summary["logical"],
        "resolvedPlacementCount": (
            placement_summary["bound"] + placement_summary["logical"]
        ),
        "boundTypeCount": type_summary["bound"],
        "boundPlacementCount": placement_summary["bound"],
        "logicalTypeCount": type_summary["logical"],
        "logicalPlacementCount": placement_summary["logical"],
        "unresolvedTypeCount": unresolved_type_count,
        "unresolvedPlacementCount": placement_summary["unresolved"],
    }
    if sum(record["placementCount"] for record in records) != len(objects):
        raise SageMapError("object binding inventory placement total mismatch")
    return {
        "schema": "openbfme.sage-object-bindings",
        "schemaVersion": 0,
        "matchPolicy": "explicit-exact-type-name-only",
        "summary": summary,
        "records": records,
    }


def _road_inventory(objects: list[dict[str, Any]]) -> dict[str, Any]:
    """Preserve SAGE road control points without guessing spline semantics."""

    source_points = [item for item in objects if int(item["roadType"]) != 0]
    control_points: list[dict[str, Any]] = []
    for item in source_points:
        wire_type = int(item["roadType"])
        control_points.append(
            {
                "sequence": len(control_points),
                "sourceIndex": int(item["index"]),
                "roadId": str(item["typeName"]),
                "wireType": wire_type,
                "role": (
                    "segment-start"
                    if wire_type == 2
                    else "segment-end"
                    if wire_type == 4
                    else "unresolved"
                ),
                "status": "unresolved",
                "segmentIndex": None,
                "sagePosition": list(item["sagePosition"]),
                "godotPosition": list(item["godotPosition"]),
            }
        )

    segments: list[dict[str, Any]] = []
    diagnostics: list[dict[str, Any]] = []
    cursor = 0
    while cursor < len(control_points):
        start = control_points[cursor]
        following = (
            control_points[cursor + 1] if cursor + 1 < len(control_points) else None
        )
        if (
            start["wireType"] == 2
            and following is not None
            and following["wireType"] == 4
            and following["roadId"] == start["roadId"]
        ):
            segment_index = len(segments)
            start["status"] = "paired"
            start["segmentIndex"] = segment_index
            following["status"] = "paired"
            following["segmentIndex"] = segment_index
            segments.append(
                {
                    "index": segment_index,
                    "roadId": start["roadId"],
                    "startSourceIndex": start["sourceIndex"],
                    "endSourceIndex": following["sourceIndex"],
                    "sageStart": list(start["sagePosition"]),
                    "sageEnd": list(following["sagePosition"]),
                    "godotStart": list(start["godotPosition"]),
                    "godotEnd": list(following["godotPosition"]),
                }
            )
            cursor += 2
            continue

        wire_type = int(start["wireType"])
        if wire_type not in (2, 4):
            reason = "unsupported-road-control-wire-type"
        elif wire_type == 4:
            reason = "unpaired-segment-end"
        elif following is None:
            reason = "unpaired-segment-start"
        elif following["wireType"] != 4:
            reason = "segment-start-not-followed-by-wire-type-4"
        else:
            reason = "segment-road-id-mismatch"
        diagnostics.append(
            {
                "sourceIndex": start["sourceIndex"],
                "roadId": start["roadId"],
                "wireType": wire_type,
                "reason": reason,
            }
        )
        cursor += 1

    road_ids = sorted(
        {str(point["roadId"]) for point in control_points},
        key=lambda value: (value.casefold(), value),
    )
    paired_count = sum(point["status"] == "paired" for point in control_points)
    unresolved_count = len(control_points) - paired_count
    status = (
        "empty"
        if not control_points
        else "exact-paired-control-points"
        if unresolved_count == 0
        else "unresolved-control-points"
    )
    return {
        "schema": "openbfme.sage-roads",
        "schemaVersion": 0,
        "coordinateTransform": "godot=(sage.x,sage.z,-sage.y)",
        "pairingPolicy": "source-order-exact-wire-2-then-4-same-road-id",
        "curveReconstruction": "not-attempted",
        "roadIds": road_ids,
        "summary": {
            "status": status,
            "roadIdCount": len(road_ids),
            "controlPointCount": len(control_points),
            "pairedControlPointCount": paired_count,
            "unresolvedControlPointCount": unresolved_count,
            "segmentCount": len(segments),
            "unresolvedDiagnosticCount": len(diagnostics),
        },
        "controlPoints": control_points,
        "segments": segments,
        "unresolvedDiagnostics": diagnostics,
    }


def _canonical_json_sha256(value: Any) -> str:
    payload = json.dumps(
        value,
        ensure_ascii=False,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def _waypoint_runtime_semantics(
    waypoints: list[dict[str, Any]],
    edges: list[dict[str, int]],
) -> tuple[list[dict[str, Any]], dict[str, Any] | None]:
    """Build a lossless raw view plus runtime-faithful derived indexes.

    The extension is emitted only when authored edges or identity quirks need
    attestation. Keeping the no-edge, unique-name v18 fixture on its established
    byte contract makes unrelated map and BlendTileData work independently stable.
    """

    names: dict[str, list[dict[str, int]]] = {}
    folded_names: dict[str, list[dict[str, Any]]] = {}
    empty_names: list[dict[str, int]] = []
    mismatches: list[dict[str, Any]] = []
    lookup_by_name: dict[str, dict[str, Any]] = {}
    waypoint_ids = {int(item["id"]) for item in waypoints}
    for source_index, item in enumerate(waypoints):
        waypoint_id = int(item["id"])
        name = str(item["name"])
        reference = {"sourceIndex": source_index, "waypointId": waypoint_id}
        names.setdefault(name, []).append(reference)
        folded_names.setdefault(name.casefold(), []).append({**reference, "name": name})
        if name == "":
            empty_names.append(reference)
        if "authoredUniqueId" in item:
            mismatches.append(
                {
                    **reference,
                    "waypointName": name,
                    "authoredUniqueId": str(item["authoredUniqueId"]),
                }
            )
        lookup_by_name[name] = {**reference, "name": name}

    duplicate_groups = [
        {"name": name, "records": records}
        for name, records in names.items()
        if len(records) > 1
    ]
    casefold_collision_groups = [
        {"records": records}
        for records in folded_names.values()
        if len({str(item["name"]) for item in records}) > 1
    ]
    name_lookup = sorted(
        lookup_by_name.values(), key=lambda item: int(item["sourceIndex"])
    )

    raw_edges: list[dict[str, Any]] = []
    resolved_edges: list[dict[str, Any]] = []
    unresolved_edges: list[dict[str, Any]] = []
    adjacency_by_start: dict[int, list[int]] = {}
    for source_index, edge in enumerate(edges):
        start_id = int(edge["startId"])
        end_id = int(edge["endId"])
        resolved = start_id in waypoint_ids and end_id in waypoint_ids
        raw = {
            "sourceIndex": source_index,
            "startId": start_id,
            "endId": end_id,
            "resolved": resolved,
        }
        raw_edges.append(raw)
        if resolved:
            resolved_edges.append(raw)
            adjacency_by_start.setdefault(start_id, []).append(end_id)
        else:
            unresolved_edges.append(raw)
    runtime_adjacency = [
        {"startId": start_id, "endIds": end_ids}
        for start_id, end_ids in adjacency_by_start.items()
    ]

    needs_extension = bool(
        raw_edges
        or duplicate_groups
        or casefold_collision_groups
        or empty_names
        or mismatches
    )
    if not needs_extension:
        return raw_edges, None

    evidence = {
        "waypointRecordCount": len(waypoints),
        "orderedWaypointRecordsSha256": _canonical_json_sha256(waypoints),
        "nameLookupCount": len(name_lookup),
        "nameLookupSha256": _canonical_json_sha256(name_lookup),
        "rawEdgeCount": len(raw_edges),
        "rawEdgesSha256": _canonical_json_sha256(raw_edges),
        "resolvedEdgeCount": len(resolved_edges),
        "resolvedEdgesSha256": _canonical_json_sha256(resolved_edges),
        "unresolvedEdgeCount": len(unresolved_edges),
        "unresolvedEdgesSha256": _canonical_json_sha256(unresolved_edges),
        "runtimeAdjacencyStartCount": len(runtime_adjacency),
        "runtimeAdjacencyEdgeCount": len(resolved_edges),
        "runtimeAdjacencySha256": _canonical_json_sha256(runtime_adjacency),
        "duplicateNameGroupCount": len(duplicate_groups),
        "duplicateNameRecordCount": sum(
            len(item["records"]) for item in duplicate_groups
        ),
        "duplicateNamesSha256": _canonical_json_sha256(duplicate_groups),
        "caseFoldCollisionGroupCount": len(casefold_collision_groups),
        "caseFoldCollisionRecordCount": sum(
            len(item["records"]) for item in casefold_collision_groups
        ),
        "caseFoldCollisionsSha256": _canonical_json_sha256(casefold_collision_groups),
        "emptyNameCount": len(empty_names),
        "emptyNamesSha256": _canonical_json_sha256(empty_names),
        "authoredIdentityMismatchCount": len(mismatches),
        "authoredIdentityMismatchesSha256": _canonical_json_sha256(mismatches),
    }
    return raw_edges, {
        "schema": "openbfme.sage-waypoint-runtime-semantics",
        "schemaVersion": 0,
        "rawWaypointPolicy": "source-order-preserved-no-synthesis-rename-or-merge",
        "nameLookupPolicy": "exact-case-sensitive-last-source-wins",
        "unresolvedEdgePolicy": (
            "preserved-raw-omitted-from-derived-runtime-adjacency"
        ),
        "nameLookup": name_lookup,
        "runtimeAdjacency": runtime_adjacency,
        "evidence": evidence,
    }


def _valid_castle_siege_contract(castle_siege: object) -> bool:
    """Accept the legacy v1 seal or the v2 per-map capability contract.

    v1 is the admission lane's exact-order, exact-length five-blocker shape;
    the mounted pack predates per-map derivation, so it must keep loading.
    v2 carries the derived per-map ``required`` capability set; blockers are
    computed by the runtime as ``required - implemented`` and are therefore
    never authored.
    """

    if not isinstance(castle_siege, dict):
        return False
    shared = (
        castle_siege.get("family") == "retail-castle-siege-skirmish"
        and castle_siege.get("gameplayStatus") == "blocked-named-gaps"
        and castle_siege.get("admissionPolicy")
        == "document-loadable-lobby-visible-gameplay-fails-closed"
    )
    if not shared:
        return False
    if set(castle_siege) == {"family", "gameplayStatus", "blockers", "admissionPolicy"}:
        return castle_siege.get("blockers") == [
            "walkable-walls",
            "defendable-gates",
            "wall-garrisons",
            "wall-mounted-defenses",
            "skirmish-ai-libraries",
        ]
    if set(castle_siege) == {
        "version",
        "family",
        "gameplayStatus",
        "admissionPolicy",
        "required",
    }:
        version = castle_siege.get("version")
        if isinstance(version, bool) or not isinstance(version, int) or version != 2:
            return False
        try:
            validate_capability_subset(castle_siege.get("required"))
        except CastleCapabilityError:
            return False
        return True
    return False


def convert_sage_map(
    source: Path | str,
    output_directory: Path | str,
    metadata: dict[str, Any] | None = None,
    expected: dict[str, Any] | None = None,
    object_bindings: Any = None,
    fixtures: dict[str, Any] | None = None,
    ai_bases: dict[str, Any] | None = None,
    *,
    profile: str = "multiplayer",
) -> list[Path]:
    """Cook a SAGE map into deterministic, redistributor-safe external pack data."""

    resolved_profile = _resolve_map_profile(profile)
    metadata = dict(metadata or {})
    expected = dict(expected or {})
    unsupported_metadata = sorted(set(metadata) - ALLOWED_MAP_METADATA)
    if unsupported_metadata:
        raise SageMapError(
            "sage-map metadata may contain presentation fields only; "
            f"unsupported or protected field(s): {', '.join(unsupported_metadata)}"
        )
    for field in (
        "id",
        "displayName",
        "preview",
        "art",
        "terrainMaterials",
        "roadMaterials",
    ):
        if field in metadata and not isinstance(metadata[field], str):
            raise SageMapError(f"sage-map metadata.{field} must be a string")
    if "knownEnvironment" in metadata and not isinstance(
        metadata["knownEnvironment"], dict
    ):
        raise SageMapError("sage-map metadata.knownEnvironment must be an object")
    if "castleSiege" in metadata:
        if not _valid_castle_siege_contract(metadata["castleSiege"]):
            raise SageMapError("sage-map metadata.castleSiege contract is invalid")
    if fixtures is not None:
        # The fixtures document is the gameplay counterpart to
        # object-bindings.json (design 4.1); it is sealed before any cooking so
        # a malformed document never reaches a pack.  The import is deferred:
        # castle_fixtures reaches sage_map transitively via
        # playable_structure_compiler -> castle_behavior.
        from .castle_fixtures import CastleFixturesError, validate_map_fixtures

        try:
            validate_map_fixtures(fixtures)
        except CastleFixturesError as exc:
            raise SageMapError(f"sage-map fixtures document is invalid: {exc}") from exc
    if ai_bases is not None:
        from .castle_ai_bases import AI_BASES_SCHEMA

        if (
            not isinstance(ai_bases, dict)
            or ai_bases.get("schema") != AI_BASES_SCHEMA
            or ai_bases.get("schemaVersion") != 0
            or not isinstance(ai_bases.get("layouts"), list)
            or ai_bases.get("layoutCount") != len(ai_bases["layouts"])
            or ai_bases.get("fallback") not in {"authored-map-specific", "generic-any"}
        ):
            raise SageMapError("sage-map aiBases document is invalid")
    if resolved_profile.map_kind != "multiplayer":
        missing_names = [
            field for field in ("id", "displayName") if field not in metadata
        ]
        if missing_names:
            raise SageMapError(
                f"{resolved_profile.map_kind} map conversion requires explicit metadata field(s): "
                + ", ".join(missing_names)
            )

    parsed = parse_sage_map_file(source, profile=resolved_profile.map_kind)
    actual_invariants: dict[str, Any] = {
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
    unsupported_expected = sorted(set(expected) - set(actual_invariants))
    if unsupported_expected:
        raise SageMapError(
            "unknown sage-map expected invariant(s): " + ", ".join(unsupported_expected)
        )
    for key, expected_value in expected.items():
        actual_value = actual_invariants[key]
        if expected_value != actual_value:
            raise SageMapError(
                f"sage-map invariant {key} mismatch: expected {expected_value!r}, got {actual_value!r}"
            )
    nonroad_objects = [item for item in parsed.objects if int(item["roadType"]) == 0]
    binding_inventory = _object_binding_inventory(nonroad_objects, object_bindings)
    binding_summary = binding_inventory["summary"]
    road_inventory = _road_inventory(parsed.objects)
    road_summary = road_inventory["summary"]
    output = Path(output_directory)
    output.mkdir(parents=True, exist_ok=True)

    height_path = output / "heightmap.r16"
    height_path.write_bytes(parsed.heightmap.encoded)
    passability_path = output / "impassability.bit"
    passability_path.write_bytes(parsed.impassability)

    source_layer_payloads = parsed.terrain_source_layers
    blend_version = int(parsed.blend["version"])
    blend_cell_word_bits = _BLEND_CELL_WORD_BITS_BY_VERSION[blend_version]
    blend_cell_size = blend_cell_word_bits // 8
    blend_cell_suffix = f"u{blend_cell_word_bits}"
    blend_cell_encoding = f"opaque-uint{blend_cell_word_bits}"
    source_layer_descriptors = {
        "tileIndices": _source_grid_descriptor(
            "terrain-tile-indices.u16",
            source_layer_payloads["tileIndices"],
            cell_count=parsed.heightmap.area,
            cell_size=2,
            encoding="uint16",
        ),
        "blendCells": _source_grid_descriptor(
            f"terrain-blend-cells.{blend_cell_suffix}",
            source_layer_payloads["blendCells"],
            cell_count=parsed.heightmap.area,
            cell_size=blend_cell_size,
            encoding=blend_cell_encoding,
        ),
        "threeWayBlendCells": _source_grid_descriptor(
            f"terrain-three-way-blend-cells.{blend_cell_suffix}",
            source_layer_payloads["threeWayBlendCells"],
            cell_count=parsed.heightmap.area,
            cell_size=blend_cell_size,
            encoding=blend_cell_encoding,
        ),
        "cliffCells": _source_grid_descriptor(
            f"terrain-cliff-cells.{blend_cell_suffix}",
            source_layer_payloads["cliffCells"],
            cell_count=parsed.heightmap.area,
            cell_size=blend_cell_size,
            encoding=blend_cell_encoding,
        ),
    }
    source_description_descriptors = {
        "blendDescriptions": _source_description_descriptor(
            "terrain-blend-descriptions.bin",
            source_layer_payloads["blendDescriptions"],
            source_count=int(parsed.blend["rawBlendCount"]),
            record_count=int(parsed.blend["blendDescriptionCount"]),
            record_size=18,
        ),
        "cliffMappings": _source_description_descriptor(
            "terrain-cliff-mappings.bin",
            source_layer_payloads["cliffMappings"],
            source_count=int(parsed.blend["rawCliffCount"]),
            record_count=int(parsed.blend["cliffMappingCount"]),
            record_size=38,
        ),
    }
    versioned_blend_layers: dict[str, Any] | None = None
    if blend_version in _LOSSLESS_LEGACY_BLEND_VERSIONS:
        layer_presence = _blend_source_layer_presence(blend_version)
        versioned_layer_descriptors: dict[str, dict[str, Any]] = {}
        for name in _BLEND_VERSIONED_LAYER_ORDER:
            if not layer_presence[name]:
                if name in source_layer_payloads:
                    raise SageMapError(
                        f"absent BlendTileData v{blend_version} layer was synthesized: {name}"
                    )
                versioned_layer_descriptors[name] = {
                    "present": False,
                    "absence": _BLEND_ABSENCE_REASON,
                }
                continue
            try:
                payload = source_layer_payloads[name]
            except KeyError as exc:
                raise SageMapError(
                    f"missing exact BlendTileData v{blend_version} source layer: {name}"
                ) from exc
            path = _BLEND_VERSIONED_LAYER_PATHS[name]
            if name == "flammability":
                descriptor = _source_grid_descriptor(
                    path,
                    payload,
                    cell_count=parsed.heightmap.area,
                    cell_size=1,
                    encoding="uint8",
                )
            else:
                descriptor = _source_packed_bit_grid_descriptor(
                    path,
                    payload,
                    width=parsed.heightmap.width,
                    height=parsed.heightmap.height,
                )
            versioned_layer_descriptors[name] = {
                "present": True,
                **descriptor,
            }
        versioned_blend_layers = {
            "schema": "openbfme.sage-blend-versioned-source-layers",
            "schemaVersion": 0,
            "sourceVersion": blend_version,
            "blendCellWordBits": blend_cell_word_bits,
            "structuralConversion": _BLEND_STRUCTURAL_CONVERSION,
            "runtimeDefaultParity": _BLEND_RUNTIME_DEFAULT_PARITY,
            "layers": versioned_layer_descriptors,
        }
    source_binary_paths: list[Path] = []
    for key, descriptor in (
        *source_layer_descriptors.items(),
        *source_description_descriptors.items(),
    ):
        target = _contained_output_path(output, str(descriptor["path"]))
        written = target.write_bytes(source_layer_payloads[key])
        if written != descriptor["byteLength"]:
            raise SageMapError(
                f"short write for terrain source layer {descriptor['path']}: "
                f"wrote {written} of {descriptor['byteLength']} bytes"
            )
        source_binary_paths.append(target)

    if versioned_blend_layers is not None:
        for name, descriptor in versioned_blend_layers["layers"].items():
            if descriptor["present"] is not True or name == "impassability":
                continue
            target = _contained_output_path(output, str(descriptor["path"]))
            payload = source_layer_payloads[name]
            written = target.write_bytes(payload)
            if written != descriptor["byteLength"]:
                raise SageMapError(
                    f"short write for BlendTileData source layer {descriptor['path']}: "
                    f"wrote {written} of {descriptor['byteLength']} bytes"
                )
            source_binary_paths.append(target)

    terrain_source_layers = {
        "schema": "openbfme.sage-terrain-source-layers",
        "schemaVersion": 0,
        "gridWidth": parsed.heightmap.width,
        "gridHeight": parsed.heightmap.height,
        "cellCount": parsed.heightmap.area,
        "layers": source_layer_descriptors,
        "descriptionTables": source_description_descriptors,
    }
    if versioned_blend_layers is not None:
        terrain_source_layers["versionedBlendLayers"] = versioned_blend_layers

    terrain = {
        "schema": "openbfme.sage-terrain",
        "schemaVersion": 0,
        "height": _height_metadata(parsed.heightmap),
        "passability": {
            "path": "impassability.bit",
            "meaning": "one-is-impassable",
            "bitOrder": "least-significant-bit-first",
            "rowStrideBytes": (parsed.heightmap.width + 7) // 8,
            "rowPadding": True,
            "sourceExact": True,
        },
        "blend": parsed.blend,
        "sourceLayers": terrain_source_layers,
        "buildability": {
            "sourceGridPresent": False,
            "status": "pending-derived-mask",
        },
    }
    terrain_path = output / "terrain.json"
    write_json_atomic(terrain_path, terrain)

    water = {
        "schema": "openbfme.sage-water",
        "schemaVersion": 0,
        "waterHeightUnits": "absolute-sage-world-z",
        "standingAreas": parsed.standing_water,
        "rivers": parsed.rivers,
        "standingWaves": parsed.standing_waves,
        "standingWaveStatus": (
            "empty"
            if not parsed.standing_waves
            else "source-records-imported-runtime-pending"
        ),
    }
    water_path = output / "water.json"
    write_json_atomic(water_path, water)

    trigger_data = {
        "schema": "openbfme.sage-triggers",
        "schemaVersion": 0,
        "coordinateTransform": "godotXZ=(sage.x,-sage.y)",
        "count": len(parsed.triggers),
        "status": (
            "empty"
            if not parsed.triggers
            else "source-records-imported-runtime-pending"
        ),
        "areas": parsed.triggers,
    }
    triggers_path = output / "triggers.json"
    write_json_atomic(triggers_path, trigger_data)

    object_data = {
        "schema": "openbfme.sage-map-objects",
        "schemaVersion": 0,
        "positionZMeaning": "offset-added-to-sampled-terrain-height",
        "coordinateTransform": "godot=(sage.x,sage.z,-sage.y)",
        "count": len(parsed.objects),
        "objects": parsed.objects,
    }
    objects_path = output / "objects.json"
    write_json_atomic(objects_path, object_data)

    roads_path = output / "roads.json"
    write_json_atomic(roads_path, road_inventory)

    object_bindings_path = output / "object-bindings.json"
    write_json_atomic(object_bindings_path, binding_inventory)

    fixtures_path = None
    if fixtures is not None:
        fixtures_path = output / "fixtures.json"
        write_json_atomic(fixtures_path, fixtures)

    ai_bases_path = None
    if ai_bases is not None:
        ai_bases_path = output / "ai_bases.json"
        write_json_atomic(ai_bases_path, ai_bases)

    player_start_bindings = [
        {
            "playerIndex": int(item["playerIndex"]),
            "waypointId": int(item["id"]),
            "waypointName": str(item["name"]),
        }
        for item in sorted(
            parsed.player_starts.values(), key=lambda value: int(value["playerIndex"])
        )
    ]
    raw_waypoint_edges, waypoint_runtime_semantics = _waypoint_runtime_semantics(
        parsed.waypoints,
        parsed.waypoint_edges,
    )
    waypoint_data = {
        "schema": "openbfme.sage-waypoints",
        "schemaVersion": 0,
        "count": len(parsed.waypoints),
        "waypoints": parsed.waypoints,
        "playerStarts": parsed.player_starts,
        "playerStartBindings": player_start_bindings,
        "topLevelWaypointPathCount": parsed.waypoint_path_count,
        "edges": (
            raw_waypoint_edges
            if waypoint_runtime_semantics is not None
            else parsed.waypoint_edges
        ),
        "routingGraphStatus": (
            "source-edges-preserved-unresolved-omitted-from-runtime-adjacency"
            if any(edge["resolved"] is False for edge in raw_waypoint_edges)
            else "source-edges-imported-runtime-pending"
            if raw_waypoint_edges
            else "empty-no-authored-navmesh"
        ),
    }
    if waypoint_runtime_semantics is not None:
        waypoint_data["runtimeSemantics"] = waypoint_runtime_semantics
    waypoints_path = output / "waypoints.json"
    write_json_atomic(waypoints_path, waypoint_data)

    setup_path = output / "setup.json"
    write_json_atomic(setup_path, parsed.setup)

    summaries = {
        "setup": {
            "declaredPlayerCount": int(parsed.setup["declaredPlayerCount"]),
            "lobbySlotCount": int(parsed.setup["lobbySlotCount"]),
            "scenarioPlayerCount": int(parsed.setup["scenarioPlayerCount"]),
            "teamCount": int(parsed.setup["teamCount"]),
            "libraryListCount": len(parsed.setup["libraryMapLists"]),
            "status": str(parsed.setup["runtimeStatus"]),
        },
        "scripts": {
            "listCount": int(parsed.script_summary["listCount"]),
            "nonemptyListCount": int(parsed.script_summary["nonemptyListCount"]),
            "status": "empty"
            if parsed.script_summary["nonemptyListCount"] == 0
            else "not-converted",
        },
        "triggers": {
            "count": parsed.trigger_count,
            "status": (
                "empty"
                if parsed.trigger_count == 0
                else "source-records-imported-runtime-pending"
            ),
        },
        "standingWaves": {
            "count": parsed.standing_wave_count,
            "status": (
                "empty"
                if parsed.standing_wave_count == 0
                else "source-records-imported-runtime-pending"
            ),
        },
    }
    conversion_evidence = dict(parsed.profile)
    if parsed.source_chunk_layouts:
        conversion_evidence["sourceChunkLayouts"] = parsed.source_chunk_layouts

    chunks_path = output / "chunks.json"
    write_json_atomic(
        chunks_path,
        {
            "schema": "openbfme.sage-map-inventory",
            "schemaVersion": 0,
            "conversionEvidence": conversion_evidence,
            "source": {
                "sha256": parsed.source_sha256,
                "packaged": False,
                "bodySha256": parsed.body_sha256,
                "envelope": parsed.envelope,
            },
            "chunks": parsed.chunks,
            "summaries": summaries,
        },
    )

    map_id = str(metadata.pop("id", "bfme2.map.fords-of-isen-ii"))
    display_name = str(metadata.pop("displayName", "Fords of Isen II"))
    # The menu's map list needs the authored lobby capacity in the map document
    # itself: a catalog row is optional, the map document is not. Both facts are
    # already validated against one another by ``_validate_multiplayer_setup``;
    # publishing them here keeps the runtime from re-deriving player capacity.
    player_start_count = len(parsed.player_starts)
    player_capacity = {
        "playerStartCount": player_start_count,
        "declaredPlayerCount": int(parsed.setup["declaredPlayerCount"]),
        "lobbySlotCount": int(parsed.setup["lobbySlotCount"]),
        "scenarioPlayerCount": int(parsed.setup["scenarioPlayerCount"]),
        "source": "authored-player-start-waypoints",
        "startBindings": player_start_bindings,
    }
    map_data: dict[str, Any] = {
        "schema": "openbfme.map",
        "schemaVersion": 0,
        "id": map_id,
        "displayName": display_name,
        "playerCount": player_start_count,
        "playerCapacity": player_capacity,
        **parsed.profile,
        "conversionEvidence": conversion_evidence,
        "sourceFormat": "sage-map-binary",
        "sourceBinaryImported": True,
        "sourceBinaryPackaged": False,
        "coordinateTransform": "godot=(sage.x,sage.z,-sage.y)",
        "terrain": "terrain.json",
        "water": "water.json",
        "objects": "objects.json",
        "roads": "roads.json",
        "roadSummary": dict(road_summary),
        "objectBindings": "object-bindings.json",
        "objectResolution": dict(binding_summary),
        "waypoints": "waypoints.json",
        "setup": "setup.json",
        "triggers": "triggers.json",
        "inventory": "chunks.json",
        "conversionStatus": {
            "terrain": "heightmap-imported-material-rendering-pending",
            "water": "source-geometry-imported-runtime-rendering-pending",
            "objects": (
                "placements-imported-object-resolution-"
                + str(binding_summary["resolutionStatus"])
            ),
            "roads": (
                "empty"
                if road_summary["controlPointCount"] == 0
                else "source-control-point-pairs-imported-rendering-pending"
                if road_summary["unresolvedControlPointCount"] == 0
                else "source-control-points-preserved-unresolved"
            ),
            "passability": "source-grid-imported",
            "buildability": "pending-derived-mask",
            "setup": str(parsed.setup["runtimeStatus"]),
            "scripts": summaries["scripts"]["status"],
            "triggers": summaries["triggers"]["status"],
            "standingWaves": summaries["standingWaves"]["status"],
        },
        "source": {
            "sha256": parsed.source_sha256,
            "packaged": False,
        },
    }
    for field in (
        "preview",
        "art",
        "terrainMaterials",
        "roadMaterials",
        "knownEnvironment",
        "castleSiege",
    ):
        if field in metadata:
            map_data[field] = metadata[field]
    if fixtures is not None:
        map_data["fixtures"] = "fixtures.json"
        map_data["conversionStatus"]["fixtures"] = (
            "map-object-fixtures-imported-sim-routing-pending"
        )
    if ai_bases is not None:
        map_data["aiBases"] = "ai_bases.json"
        map_data["conversionStatus"]["aiBases"] = (
            "map-specific-retail-layouts-compiled"
            if ai_bases.get("layouts")
            else "generic-any-fallback"
        )
    map_path = output / "map.json"
    write_json_atomic(map_path, map_data)

    return [
        height_path,
        passability_path,
        *source_binary_paths,
        terrain_path,
        water_path,
        triggers_path,
        objects_path,
        roads_path,
        object_bindings_path,
        *([fixtures_path] if fixtures_path is not None else []),
        *([ai_bases_path] if ai_bases_path is not None else []),
        waypoints_path,
        setup_path,
        chunks_path,
        map_path,
    ]
