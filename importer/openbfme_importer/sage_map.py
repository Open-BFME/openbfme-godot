"""Bounded, independent reader for the verified BFME II multiplayer map facts.

The importer deliberately supports only versions proven by the frozen retail
multiplayer corpus. It rejects unsupported variants instead of guessing, and
emits cooked data rather than carrying retail map binaries into a pack.
"""

from __future__ import annotations

from array import array
from dataclasses import dataclass
import hashlib
import math
from pathlib import Path
import re
import struct
import sys
from typing import Any, Iterator

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
MAX_BACK_REFERENCE = 131_072
MAX_OBJECT_BINDING_TYPES = 4_096
MAX_OBJECT_BINDING_TEXT = 1_024
OBJECT_BINDING_MATCH_METHOD = "exact-type-name"

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
    {"id", "displayName", "preview", "art", "terrainMaterials", "knownEnvironment"}
)


class SageMapError(ValueError):
    """Raised when an input is unsupported, malformed, or exceeds a bound."""


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
            raise SageMapError(f"oversized Unicode string in {self.label}: {characters}")
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


def _append_literals(cursor: _Cursor, output: bytearray, count: int, expected: int) -> None:
    if len(output) + count > expected:
        raise SageMapError("RefPack literal exceeds declared output size")
    output.extend(cursor.bytes(count))


def _append_reference(output: bytearray, length: int, distance: int, expected: int) -> None:
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


def _records(cursor: _Cursor, names: dict[int, str], *, cap: int, label: str) -> Iterator[_Record]:
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


def _parse_height(record: _Record) -> _HeightMap:
    if record.version != 5:
        raise SageMapError(f"unsupported HeightMapData version: {record.version}")
    cursor = record.payload
    width = cursor.u32()
    height = cursor.u32()
    border_width = cursor.u32()
    if width < 2 or height < 2 or width * height > MAX_GRID_CELLS:
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
    return _HeightMap(record.version, width, height, border_width, borders, elevations, encoded)


def _count_nonzero_uint32(values: memoryview) -> int:
    return sum(value[0] != 0 for value in struct.iter_unpack("<I", values))


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
    if record.version != 18:
        raise SageMapError(f"unsupported BlendTileData version: {record.version}")
    cursor = record.payload
    area = heightmap.area
    num_tiles = cursor.u32()
    if num_tiles != area:
        raise SageMapError(f"BlendTileData tile count {num_tiles} does not match {area}")
    tiles = cursor.bytes(area * 2)
    max_tile = max((item[0] for item in struct.iter_unpack("<H", tiles)), default=0)
    blends = cursor.bytes(area * 4)
    three_way = cursor.bytes(area * 4)
    cliffs = cursor.bytes(area * 4)

    impassability, impassable_count = _packed_grid(cursor, heightmap.width, heightmap.height)
    _, player_impassable_count = _packed_grid(cursor, heightmap.width, heightmap.height)
    _, passage_width_count = _packed_grid(cursor, heightmap.width, heightmap.height)
    _, taintable_count = _packed_grid(cursor, heightmap.width, heightmap.height)
    _, extra_passability_count = _packed_grid(cursor, heightmap.width, heightmap.height)
    flammability = cursor.bytes(area)
    _, visible_count = _packed_grid(cursor, heightmap.width, heightmap.height)

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
    for value in flammability:
        key = str(value)
        flammability_counts[key] = flammability_counts.get(key, 0) + 1
    summary = {
        "version": record.version,
        "numTiles": num_tiles,
        "maxTileValue": max_tile,
        "nonzeroBlendCells": _count_nonzero_uint32(blends),
        "nonzeroThreeWayBlendCells": _count_nonzero_uint32(three_way),
        "nonzeroCliffCells": _count_nonzero_uint32(cliffs),
        "gridStats": {
            "impassable": impassable_count,
            "impassableToPlayers": player_impassable_count,
            "passageWidth": passage_width_count,
            "taintable": taintable_count,
            "extraPassability": extra_passability_count,
            "visible": visible_count,
        },
        "flammabilityCounts": flammability_counts,
        "textureCellCount": texture_cell_count,
        "rawBlendCount": raw_blend_count,
        "rawCliffCount": raw_cliff_count,
        "blendDescriptionCount": blend_description_count,
        "cliffMappingCount": cliff_mapping_count,
        "magicValue": magic1,
        "textures": textures,
        "sourceBuildabilityPresent": False,
    }
    source_layers = {
        "tileIndices": tiles,
        "blendCells": blends,
        "threeWayBlendCells": three_way,
        "cliffCells": cliffs,
        "blendDescriptions": blend_descriptions,
        "cliffMappings": cliff_mappings,
    }
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
    if record.version != 6:
        raise SageMapError(f"unsupported SidesList version: {record.version}")
    cursor = record.payload
    unknown_boolean = cursor.bool8()
    player_count = cursor.i32()
    if player_count < 0 or player_count > MAX_SCENARIO_PLAYERS:
        raise SageMapError(f"SidesList player count exceeds limit: {player_count}")
    players: list[dict[str, Any]] = []
    player_names: set[str] = set()
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
        player_key = player_name.casefold()
        if player_key in player_names:
            raise SageMapError(f"duplicate SidesList player name: {player_name!r}")
        player_names.add(player_key)
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
    waypoint_names: set[str] = set()
    for child in _records(record.payload, names, cap=MAX_OBJECTS, label="ObjectsList"):
        if child.name != "Object" or child.version != 3:
            raise SageMapError(f"unsupported ObjectsList child: {child.name} v{child.version}")
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
            if not isinstance(waypoint_name, str) or not waypoint_name:
                raise SageMapError("waypoint is missing a nonempty waypointName")
            if not isinstance(unique_id, str) or unique_id != waypoint_name:
                raise SageMapError("waypoint uniqueID does not match waypointName")
            if waypoint_id in waypoint_ids:
                raise SageMapError(f"duplicate waypointID: {waypoint_id}")
            name_key = waypoint_name.casefold()
            if name_key in waypoint_names:
                raise SageMapError(f"duplicate waypointName: {waypoint_name!r}")
            waypoint_ids.add(waypoint_id)
            waypoint_names.add(name_key)
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
            waypoints.append(waypoint)
            if waypoint_name.startswith("Player_") and waypoint_name.endswith("_Start"):
                start_match = re.fullmatch(r"Player_([1-9][0-9]*)_Start", waypoint_name)
                if start_match is None:
                    raise SageMapError(
                        f"noncanonical player-start waypoint name: {waypoint_name!r}"
                    )
                waypoint["playerIndex"] = int(start_match.group(1))
                if waypoint_name in player_starts:
                    raise SageMapError(f"duplicate player start: {waypoint_name!r}")
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
                "godotPoints": [_godot_water_point(point, water_height) for point in points],
            }
        )
    cursor.finish()
    return areas


def _parse_rivers(record: _Record) -> list[dict[str, Any]]:
    if record.version != 2:
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
    if record.version != 2:
        raise SageMapError(
            f"unsupported StandingWaveAreas version: {record.version}"
        )
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
        pca_raw = cursor.u32()
        if pca_raw not in (0, 1):
            raise SageMapError(
                f"invalid 32-bit boolean {pca_raw} in StandingWaveAreas"
            )
        areas.append(
            {
                "id": unique_id,
                "name": name,
                "layer": layer,
                "uvScrollSpeed": uv_speed,
                "additive": additive,
                "texture": texture,
                "pcaWave": bool(pca_raw),
                "settings": settings,
                "sagePoints": points,
                "godotXZPoints": [_godot_planar_point(point) for point in points],
            }
        )
    cursor.finish()
    return areas


def _parse_player_scripts(record: _Record, names: dict[int, str]) -> dict[str, int]:
    if record.version != 1:
        raise SageMapError(f"unsupported PlayerScriptsList version: {record.version}")
    lists = 0
    nonempty = 0
    for child in _records(record.payload, names, cap=1_024, label="PlayerScriptsList"):
        if child.name != "ScriptList" or child.version != 1:
            raise SageMapError(f"unsupported player script child: {child.name} v{child.version}")
        lists += 1
        if child.size:
            nonempty += 1
        child.payload.skip(child.payload.remaining)
    record.payload.finish()
    return {"listCount": lists, "nonemptyListCount": nonempty}


def _validate_multiplayer_setup(
    *,
    mp_positions: dict[str, Any] | None,
    sides: dict[str, Any] | None,
    teams: dict[str, Any] | None,
    library_map_lists: dict[str, Any] | None,
    player_scripts_present: bool,
    script_summary: dict[str, int],
    waypoints_present: bool,
    waypoint_edges: list[dict[str, int]],
    waypoints: list[dict[str, Any]],
    player_starts: dict[str, dict[str, Any]],
) -> dict[str, Any]:
    missing: list[str] = []
    if mp_positions is None:
        missing.append("MPPositionList")
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
            "multiplayer map is missing required setup chunk(s): " + ", ".join(missing)
        )
    assert mp_positions is not None
    assert sides is not None
    assert teams is not None
    assert library_map_lists is not None

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

    player_names = {str(item["name"]).casefold(): str(item["name"]) for item in players}
    default_team_names: set[str] = set()
    for team in team_items:
        owner = str(team["owner"])
        owner_key = owner.casefold()
        if owner_key not in player_names:
            raise SageMapError(
                f"Teams owner does not resolve to a SidesList player: {owner!r}"
            )
        expected_default = ("team" + player_names[owner_key]).casefold()
        if str(team["name"]).casefold() == expected_default:
            if owner_key in default_team_names:
                raise SageMapError(f"duplicate default team for player: {owner!r}")
            default_team_names.add(owner_key)
    missing_defaults = sorted(set(player_names) - default_team_names)
    if missing_defaults:
        names = ", ".join(repr(player_names[key]) for key in missing_defaults)
        raise SageMapError(f"SidesList player is missing a default team: {names}")

    start_indices = sorted(
        int(item["playerIndex"])
        for item in player_starts.values()
        if "playerIndex" in item
    )
    if not start_indices:
        raise SageMapError("multiplayer map has no player-start waypoints")
    expected_indices = list(range(1, len(start_indices) + 1))
    if start_indices != expected_indices:
        raise SageMapError(
            f"player-start waypoints are noncontiguous: {start_indices!r}"
        )
    if len(start_indices) > len(mp_positions["slots"]):
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
    for edge in waypoint_edges:
        start_id = int(edge["startId"])
        end_id = int(edge["endId"])
        if start_id not in waypoint_ids or end_id not in waypoint_ids:
            raise SageMapError(
                f"WaypointsList edge has a missing endpoint: {start_id}->{end_id}"
            )

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

    return {
        "schema": "openbfme.sage-multiplayer-setup",
        "schemaVersion": 0,
        "sourceVersions": {
            "MPPositionList": int(mp_positions["version"]),
            "MPPositionInfo": 1,
            "SidesList": int(sides["version"]),
            "Teams": int(teams["version"]),
            "LibraryMapLists": int(library_map_lists["version"]),
            "LibraryMaps": 1,
            "PlayerScriptsList": 1,
            "WaypointsList": 1,
        },
        "declaredPlayerCount": len(start_indices),
        "lobbySlotCount": len(mp_positions["slots"]),
        "lobbySlots": mp_positions["slots"],
        "scenarioPlayerCount": scenario_player_count,
        "scenarioPlayers": players,
        "teamCount": len(team_items),
        "extraTeamCount": len(team_items) - scenario_player_count,
        "teams": team_items,
        "duplicateTeamNames": duplicate_team_names,
        "sidesUnknownBoolean": bool(sides["unknownBoolean"]),
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
            "playerStartsContiguous": True,
            "startCountWithinLobbySlots": True,
            "startCountWithinScenarioPlayers": True,
            "waypointEdgeEndpointsResolved": True,
        },
        "runtimeStatus": "source-records-imported-runtime-pending",
    }


@dataclass(slots=True)
class ParsedSageMap:
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


def parse_sage_map_bytes(source: bytes) -> ParsedSageMap:
    body, envelope = decode_sage_map_blob(source)
    cursor = _Cursor(body, label="CkMp map")
    if cursor.bytes(4) != b"CkMp":
        raise SageMapError("missing CkMp magic")
    names = _parse_name_table(cursor)

    chunks: list[dict[str, Any]] = []
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
    triggers: list[dict[str, Any]] = []
    standing_waves: list[dict[str, Any]] = []
    waypoint_edges: list[dict[str, int]] = []
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
                raise SageMapError("duplicate HeightMapData")
            heightmap = _parse_height(record)
        elif record.name == "BlendTileData":
            if heightmap is None:
                raise SageMapError("BlendTileData appears before HeightMapData")
            if blend is not None:
                raise SageMapError("duplicate BlendTileData")
            impassability, blend, terrain_source_layers = _parse_blend(record, heightmap)
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
        elif record.name == "WaypointsList":
            if waypoints_list_seen:
                raise SageMapError("duplicate WaypointsList")
            waypoints_list_seen = True
            waypoint_edges = _parse_waypoint_edges(record)
        elif record.name == "PlayerScriptsList":
            if player_scripts_seen:
                raise SageMapError("duplicate PlayerScriptsList")
            player_scripts_seen = True
            script_summary = _parse_player_scripts(record, names)
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
        raise SageMapError("multiplayer map is missing required ObjectsList")
    setup = _validate_multiplayer_setup(
        mp_positions=mp_positions,
        sides=sides,
        teams=teams,
        library_map_lists=library_map_lists,
        player_scripts_present=player_scripts_seen,
        script_summary=script_summary,
        waypoints_present=waypoints_list_seen,
        waypoint_edges=waypoint_edges,
        waypoints=waypoints,
        player_starts=player_starts,
    )
    return ParsedSageMap(
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


def parse_sage_map_file(path: Path | str) -> ParsedSageMap:
    source = Path(path)
    size = source.stat().st_size
    if size > MAX_SOURCE_BYTES:
        raise SageMapError(f"map source exceeds limit: {size}")
    return parse_sage_map_bytes(source.read_bytes())


_CENSUS_SUPPORTED_SIGNATURES = {
    "HeightMapData": 5,
    "BlendTileData": 18,
    "MPPositionList": 0,
    "SidesList": 6,
    "LibraryMapLists": 1,
    "Teams": 1,
    "ObjectsList": 3,
    "StandingWaterAreas": 2,
    "RiverAreas": 2,
    "TriggerAreas": 1,
    "StandingWaveAreas": 2,
    "WaypointsList": 1,
    "PlayerScriptsList": 1,
}


def _census_set_sha256(domain: str, values: list[str]) -> str:
    digest = hashlib.sha256()
    digest.update(domain.encode("ascii") + b"\0")
    for value in sorted(set(values), key=lambda item: (item.casefold(), item)):
        encoded = value.encode("utf-8")
        digest.update(len(encoded).to_bytes(4, "little"))
        digest.update(encoded)
    return digest.hexdigest()


def census_sage_map_bytes(source: bytes) -> dict[str, Any]:
    """Return bounded payload-free facts without weakening the strict map cook."""

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

        expected_version = _CENSUS_SUPPORTED_SIGNATURES.get(record.name)
        if expected_version is None:
            status = "unclassified"
            rejection = None
        elif record.version != expected_version:
            status = "unsupported-version"
            rejection = None
        else:
            status = "parsed"
            rejection = None
            try:
                if record.name == "HeightMapData":
                    heightmap = _parse_height(record)
                    features["height"] = {
                        "width": heightmap.width,
                        "height": heightmap.height,
                        "borderWidth": heightmap.border_width,
                    }
                elif record.name == "BlendTileData":
                    if heightmap is None:
                        raise SageMapError("BlendTileData census requires earlier HeightMapData")
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
                        raise SageMapError("ObjectsList census requires earlier HeightMapData")
                    objects, waypoints, starts = _parse_objects(record, names, heightmap)
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
                    features["nonemptyScriptListCount"] = int(summary["nonemptyListCount"])
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
            for reason, count in sorted(rejections.items(), key=lambda item: item[0].casefold())
        ]
        chunk_rows.append(chunk)
    chunk_rows.sort(key=lambda item: (item["name"].casefold(), item["version"]))

    strict_accepted = True
    strict_reason: str | None = None
    try:
        parse_sage_map_bytes(source)
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
            "accepted": strict_accepted,
            "reason": strict_reason,
        },
    }


def census_sage_map_file(path: Path | str) -> dict[str, Any]:
    source = Path(path)
    size = source.stat().st_size
    if size > MAX_SOURCE_BYTES:
        raise SageMapError(f"map source exceeds limit: {size}")
    return census_sage_map_bytes(source.read_bytes())


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
        raise SageMapError(f"terrain output path must be pack-relative: {relative_path}")
    target = output / relative_path
    try:
        target.resolve().relative_to(output.resolve())
    except ValueError as exc:
        raise SageMapError(f"terrain output path escapes map directory: {relative_path}") from exc
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
) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise SageMapError(f"sage-map objectBindings.{kind} entries must be objects")
    fields = set(value)
    missing = sorted(required_fields - fields)
    unsupported = sorted(fields - required_fields)
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
    if object_bindings is not None:
        if not isinstance(object_bindings, dict):
            raise SageMapError("sage-map options.objectBindings must be an object")
        fields = set(object_bindings)
        required = {"logical", "models"}
        missing = sorted(required - fields)
        unsupported = sorted(fields - required)
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
        if not isinstance(logical_entries, list) or not isinstance(model_entries, list):
            raise SageMapError(
                "sage-map options.objectBindings.logical and .models must be arrays"
            )
        if len(logical_entries) + len(model_entries) > MAX_OBJECT_BINDING_TYPES:
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
                raise SageMapError(
                    "conflicting logical/model object binding entries for "
                    f"{prior_name!r} and {type_name!r}"
                )
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

    model_fields = frozenset(
        {"typeName", "sourceVirtualModel", "glb", "matchMethod"}
    )
    for raw in model_entries:
        entry = _binding_entry(raw, kind="models", required_fields=model_fields)
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
                "glb": _binding_path(
                    entry["glb"], "objectBindings.models.glb", ".glb"
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


def convert_sage_map(
    source: Path | str,
    output_directory: Path | str,
    metadata: dict[str, Any] | None = None,
    expected: dict[str, Any] | None = None,
    object_bindings: Any = None,
) -> list[Path]:
    """Cook a SAGE map into deterministic, redistributor-safe external pack data."""

    metadata = dict(metadata or {})
    expected = dict(expected or {})
    unsupported_metadata = sorted(set(metadata) - ALLOWED_MAP_METADATA)
    if unsupported_metadata:
        raise SageMapError(
            "sage-map metadata may contain presentation fields only; "
            f"unsupported or protected field(s): {', '.join(unsupported_metadata)}"
        )
    for field in ("id", "displayName", "preview", "art", "terrainMaterials"):
        if field in metadata and not isinstance(metadata[field], str):
            raise SageMapError(f"sage-map metadata.{field} must be a string")
    if "knownEnvironment" in metadata and not isinstance(metadata["knownEnvironment"], dict):
        raise SageMapError("sage-map metadata.knownEnvironment must be an object")

    parsed = parse_sage_map_file(source)
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
    binding_inventory = _object_binding_inventory(parsed.objects, object_bindings)
    binding_summary = binding_inventory["summary"]
    output = Path(output_directory)
    output.mkdir(parents=True, exist_ok=True)

    height_path = output / "heightmap.r16"
    height_path.write_bytes(parsed.heightmap.encoded)
    passability_path = output / "impassability.bit"
    passability_path.write_bytes(parsed.impassability)

    source_layer_payloads = parsed.terrain_source_layers
    source_layer_descriptors = {
        "tileIndices": _source_grid_descriptor(
            "terrain-tile-indices.u16",
            source_layer_payloads["tileIndices"],
            cell_count=parsed.heightmap.area,
            cell_size=2,
            encoding="uint16",
        ),
        "blendCells": _source_grid_descriptor(
            "terrain-blend-cells.u32",
            source_layer_payloads["blendCells"],
            cell_count=parsed.heightmap.area,
            cell_size=4,
            encoding="opaque-uint32",
        ),
        "threeWayBlendCells": _source_grid_descriptor(
            "terrain-three-way-blend-cells.u32",
            source_layer_payloads["threeWayBlendCells"],
            cell_count=parsed.heightmap.area,
            cell_size=4,
            encoding="opaque-uint32",
        ),
        "cliffCells": _source_grid_descriptor(
            "terrain-cliff-cells.u32",
            source_layer_payloads["cliffCells"],
            cell_count=parsed.heightmap.area,
            cell_size=4,
            encoding="opaque-uint32",
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
        "sourceLayers": {
            "schema": "openbfme.sage-terrain-source-layers",
            "schemaVersion": 0,
            "gridWidth": parsed.heightmap.width,
            "gridHeight": parsed.heightmap.height,
            "cellCount": parsed.heightmap.area,
            "layers": source_layer_descriptors,
            "descriptionTables": source_description_descriptors,
        },
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

    object_bindings_path = output / "object-bindings.json"
    write_json_atomic(object_bindings_path, binding_inventory)

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
    waypoint_data = {
        "schema": "openbfme.sage-waypoints",
        "schemaVersion": 0,
        "count": len(parsed.waypoints),
        "waypoints": parsed.waypoints,
        "playerStarts": parsed.player_starts,
        "playerStartBindings": player_start_bindings,
        "topLevelWaypointPathCount": parsed.waypoint_path_count,
        "edges": parsed.waypoint_edges,
        "routingGraphStatus": (
            "source-edges-imported-runtime-pending"
            if parsed.waypoint_edges
            else "empty-no-authored-navmesh"
        ),
    }
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
            **parsed.script_summary,
            "status": "empty" if parsed.script_summary["nonemptyListCount"] == 0 else "not-converted",
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
    chunks_path = output / "chunks.json"
    write_json_atomic(
        chunks_path,
        {
            "schema": "openbfme.sage-map-inventory",
            "schemaVersion": 0,
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
    map_data: dict[str, Any] = {
        "schema": "openbfme.map",
        "schemaVersion": 0,
        "id": map_id,
        "displayName": display_name,
        "sourceFormat": "sage-map-binary",
        "sourceBinaryImported": True,
        "sourceBinaryPackaged": False,
        "coordinateTransform": "godot=(sage.x,sage.z,-sage.y)",
        "terrain": "terrain.json",
        "water": "water.json",
        "objects": "objects.json",
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
            "passability": "source-grid-imported",
            "buildability": "pending-derived-mask",
            "setup": "source-records-imported-runtime-pending",
            "scripts": summaries["scripts"]["status"],
            "triggers": summaries["triggers"]["status"],
            "standingWaves": summaries["standingWaves"]["status"],
        },
        "source": {
            "sha256": parsed.source_sha256,
            "packaged": False,
        },
    }
    for field in ("preview", "art", "terrainMaterials", "knownEnvironment"):
        if field in metadata:
            map_data[field] = metadata[field]
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
        object_bindings_path,
        waypoints_path,
        setup_path,
        chunks_path,
        map_path,
    ]
