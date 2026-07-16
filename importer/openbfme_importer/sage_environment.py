"""Deterministic BFME II Fords of Isen II environment evidence extractor.

This module is an oracle/reporting tool, not a converter.  It decodes only the
BFME II chunk versions and flat INI sections whose layouts are evidenced by the
retail Fords map.  Unknown bytes and unsupported semantics remain explicit in
the report instead of being replaced with renderer guesses.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
import math
from pathlib import Path
import re
from typing import Any, Iterable

from .sage_cst import strip_sage_comments
from .sage_ini import MAX_ASSIGNMENTS_PER_BLOCK, MAX_BLOCKS, MAX_INI_BYTES
from .sage_map import (
    MAX_SOURCE_BYTES,
    MAX_TOP_LEVEL_RECORDS,
    SageMapError,
    _Cursor,
    _parse_name_table,
    _parse_typed_properties,
    _records,
    decode_sage_map_blob,
    parse_sage_map_bytes,
)
from .util import write_json_atomic


SCHEMA = "openbfme.sage-fords-environment-evidence"
SCHEMA_VERSION = 0

FORDS_MAP_PATH = "maps/map mp fords of isen ii/map mp fords of isen ii.map"
FORDS_MAP_INI_PATH = "maps/map mp fords of isen ii/map.ini"
WEATHER_INI_PATH = "data/ini/weather.ini"
WATER_INI_PATH = "data/ini/water.ini"
WATER_TEXTURES_INI_PATH = "data/ini/watertextures.ini"
GAME_DATA_INI_PATH = "data/ini/gamedata.ini"

_SOURCE_PATHS = (
    FORDS_MAP_PATH,
    FORDS_MAP_INI_PATH,
    WEATHER_INI_PATH,
    WATER_INI_PATH,
    WATER_TEXTURES_INI_PATH,
    GAME_DATA_INI_PATH,
)

_TIME_OF_DAY = {1: "MORNING", 2: "AFTERNOON", 3: "EVENING", 4: "NIGHT"}
_WEATHER = {0: "NORMAL", 1: "SNOWY"}
_COMPRESSION = {0: "NONE", 1: "REFPACK"}

_WORLD_INFO_TYPES = {
    "cameraMaxHeight": 2,
    "cameraPitchAngle": 2,
    "cameraYawAngle": 2,
    "cameraScrollSpeedScalar": 2,
    "weather": 1,
    "compression": 1,
    "cameraGroundMinHeight": 2,
    "cameraGroundMaxHeight": 2,
    "mapName": 3,
    "mapDescription": 3,
    "isScenarioMultiplayer": 0,
}

_LIGHT_NAMES = (
    "terrainSun",
    "objectSun",
    "infantrySun",
    "terrainAccent1",
    "objectAccent1",
    "infantryAccent1",
    "terrainAccent2",
    "objectAccent2",
    "infantryAccent2",
)

_TARGET_CHUNK_VERSIONS = {
    "WorldInfo": 1,
    "GlobalLighting": 8,
    "PostEffectsChunk": 1,
    "EnvironmentData": 3,
    "NamedCameras": 2,
    "CameraAnimationList": 3,
}

_GAME_DATA_EXACT_KEYS = {
    "UseCloudMap",
    "UseCloudPlane",
    "DrawSkyBox",
    "UseShadowVolumes",
    "UseShadowDecals",
    "UseShadowMapping",
    "DefaultCameraMinHeight",
    "DefaultCameraMaxHeight",
    "DefaultCameraPitchAngle",
    "DefaultCameraYawAngle",
    "DefaultCameraScrollSpeedScalar",
    "CameraLockHeightDelta",
    "CameraTerrainSampleRadiusForHeight",
    "UseCameraInReplay",
    "CameraAdjustSpeed",
    "ScrollAmountCutoff",
    "EnforceMaxCameraHeight",
    "TerrainHeightAtEdgeOfMap",
    "TimeOfDay",
    "Weather",
    "ForceModelsToFollowTimeOfDay",
    "ForceModelsToFollowWeather",
}
_GAME_DATA_PREFIXES = ("TerrainLighting", "TerrainObjectsLighting")

_FLOAT_COMPONENT = re.compile(
    r"([A-Za-z]+):([-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[Ee][-+]?\d+)?)"
)
_INTEGER = re.compile(r"[-+]?\d+")
_REAL = re.compile(r"[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[Ee][-+]?\d+)")
_ASSET_REFERENCE = re.compile(r"^[^\s]+\.(?:tga|dds|w3d)$", re.IGNORECASE)


class SageEnvironmentError(ValueError):
    """Raised when environment evidence is malformed, ambiguous, or unsupported."""


@dataclass(frozen=True, slots=True)
class _Assignment:
    key: str
    value: str
    line: int


@dataclass(frozen=True, slots=True)
class _IniBlock:
    kind: str
    name: str | None
    header_line: int
    end_line: int
    assignments: tuple[_Assignment, ...]

    @property
    def identity(self) -> str:
        return self.kind if self.name is None else f"{self.kind} {self.name}"


def _sha256(source: bytes) -> str:
    return hashlib.sha256(source).hexdigest()


def _canonical_sha256(value: object) -> str:
    payload = json.dumps(
        value,
        ensure_ascii=False,
        allow_nan=False,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")
    return _sha256(payload)


def _read_source(path: Path, *, maximum: int, label: str) -> bytes:
    if path.is_symlink():
        raise SageEnvironmentError(f"{label} cannot be a symbolic link")
    if not path.is_file():
        raise SageEnvironmentError(f"{label} is not a regular file: {path}")
    size = path.stat().st_size
    if size > maximum:
        raise SageEnvironmentError(f"{label} exceeds {maximum} byte limit: {size}")
    source = path.read_bytes()
    if len(source) != size:
        raise SageEnvironmentError(f"{label} changed during read")
    return source


def _source_path(root: Path, virtual_path: str) -> Path:
    candidate = root.joinpath(*virtual_path.split("/"))
    try:
        candidate.resolve().relative_to(root.resolve())
    except ValueError as exc:
        raise SageEnvironmentError(
            f"source escapes effective root: {virtual_path}"
        ) from exc
    return candidate


def _source_evidence(virtual_path: str, source: bytes) -> dict[str, Any]:
    return {
        "virtualPath": virtual_path,
        "byteCount": len(source),
        "sha256": _sha256(source),
    }


def _decode_ini(source: bytes, label: str) -> str:
    if len(source) > MAX_INI_BYTES:
        raise SageEnvironmentError(f"{label} exceeds {MAX_INI_BYTES} byte limit")
    if b"\0" in source:
        raise SageEnvironmentError(f"{label} contains a NUL byte")
    try:
        return source.decode("cp1252")
    except UnicodeDecodeError as exc:
        raise SageEnvironmentError(f"{label} has unsupported encoding") from exc


def _parse_ini_blocks(
    source: bytes,
    *,
    label: str,
    block_shapes: dict[str, bool],
) -> tuple[_IniBlock, ...]:
    """Parse selected flat blocks with line-level evidence.

    ``block_shapes`` maps block kind to whether it requires a one-token name.
    Other top-level sections are ignored. Selected sections fail closed on an
    invalid header, duplicate identity, missing ``End``, or excessive size.
    """

    canonical = {key.casefold(): (key, named) for key, named in block_shapes.items()}
    blocks: list[_IniBlock] = []
    current: tuple[str, str | None, int] | None = None
    assignments: list[_Assignment] = []

    def finish(end_line: int) -> None:
        nonlocal current, assignments
        assert current is not None
        blocks.append(
            _IniBlock(current[0], current[1], current[2], end_line, tuple(assignments))
        )
        if len(blocks) > MAX_BLOCKS:
            raise SageEnvironmentError(f"{label} selected block count exceeds limit")
        current = None
        assignments = []

    for line_number, raw in enumerate(_decode_ini(source, label).splitlines(), 1):
        line = strip_sage_comments(raw).strip()
        if not line:
            continue
        if current is not None:
            if line.casefold() == "end":
                finish(line_number)
                continue
            tokens = line.split()
            nested = canonical.get(tokens[0].casefold())
            if nested is not None and "=" not in line:
                raise SageEnvironmentError(
                    f"{label} has unterminated {current[0]} block before line {line_number}"
                )
            if "=" not in line:
                continue
            key, value = (part.strip() for part in line.split("=", 1))
            if not key:
                raise SageEnvironmentError(
                    f"{label} has empty key at line {line_number}"
                )
            assignments.append(_Assignment(key, value, line_number))
            if len(assignments) > MAX_ASSIGNMENTS_PER_BLOCK:
                raise SageEnvironmentError(
                    f"{label} assignment count exceeds limit in {current[0]}"
                )
            continue

        if "=" in line or line.casefold() == "end":
            continue
        tokens = line.split()
        selected = canonical.get(tokens[0].casefold())
        if selected is None:
            continue
        kind, named = selected
        if named and len(tokens) != 2:
            raise SageEnvironmentError(
                f"{label} has invalid named {kind} header at line {line_number}"
            )
        if not named and len(tokens) != 1:
            raise SageEnvironmentError(
                f"{label} has invalid singleton {kind} header at line {line_number}"
            )
        current = (kind, tokens[1] if named else None, line_number)
        assignments = []

    if current is not None:
        raise SageEnvironmentError(
            f"{label} has unterminated {current[0]} block from line {current[2]}"
        )

    identities: set[str] = set()
    for block in blocks:
        identity = block.identity.casefold()
        if identity in identities:
            raise SageEnvironmentError(f"{label} has duplicate block: {block.identity}")
        identities.add(identity)
    return tuple(blocks)


def _decode_ini_value(value: str) -> object:
    value = value.strip()
    folded = value.casefold()
    if folded in {"yes", "true"}:
        return True
    if folded in {"no", "false"}:
        return False
    if _INTEGER.fullmatch(value):
        return int(value)
    if _REAL.fullmatch(value):
        result = float(value)
        if not math.isfinite(result):
            raise SageEnvironmentError(f"non-finite INI scalar: {value!r}")
        return result
    matches = list(_FLOAT_COMPONENT.finditer(value))
    if matches and " ".join(match.group(0) for match in matches) == " ".join(
        value.split()
    ):
        result: dict[str, float] = {}
        for match in matches:
            key = match.group(1).upper()
            if key in result:
                raise SageEnvironmentError(f"duplicate INI vector component: {key}")
            number = float(match.group(2))
            if not math.isfinite(number):
                raise SageEnvironmentError(
                    f"non-finite INI vector component: {value!r}"
                )
            result[key] = number
        return result
    number_tokens = value.split()
    if len(number_tokens) > 1 and all(_REAL.fullmatch(item) for item in number_tokens):
        numbers = [float(item) for item in number_tokens]
        if not all(math.isfinite(item) for item in numbers):
            raise SageEnvironmentError(f"non-finite INI number list: {value!r}")
        return numbers
    return value


def _assignment_report(item: _Assignment) -> dict[str, Any]:
    return {
        "key": item.key,
        "rawValue": item.value,
        "value": _decode_ini_value(item.value),
        "line": item.line,
    }


def _block_report(block: _IniBlock, virtual_path: str) -> dict[str, Any]:
    assignments = [_assignment_report(item) for item in block.assignments]
    evidence = {
        "virtualPath": virtual_path,
        "section": block.identity,
        "headerLine": block.header_line,
        "endLine": block.end_line,
    }
    return {
        **evidence,
        "assignments": assignments,
        "aggregateSha256": _canonical_sha256(
            {"evidence": evidence, "assignments": assignments}
        ),
    }


def _blocks_by_kind(blocks: Iterable[_IniBlock], kind: str) -> list[_IniBlock]:
    folded = kind.casefold()
    return [block for block in blocks if block.kind.casefold() == folded]


def _singleton(blocks: Iterable[_IniBlock], kind: str) -> _IniBlock | None:
    selected = _blocks_by_kind(blocks, kind)
    if len(selected) > 1:
        raise SageEnvironmentError(f"ambiguous singleton block: {kind}")
    return selected[0] if selected else None


def _unique_assignments(block: _IniBlock) -> dict[str, _Assignment]:
    result: dict[str, _Assignment] = {}
    spellings: dict[str, str] = {}
    for item in block.assignments:
        key = item.key.casefold()
        if key in result:
            raise SageEnvironmentError(
                f"duplicate assignment {item.key!r} in {block.identity}; first spelling "
                f"was {spellings[key]!r}"
            )
        result[key] = item
        spellings[key] = item.key
    return result


def _overlay_report(
    global_block: _IniBlock | None,
    map_block: _IniBlock | None,
    *,
    global_path: str,
    map_path: str,
) -> dict[str, Any]:
    global_items = _unique_assignments(global_block) if global_block else {}
    map_items = _unique_assignments(map_block) if map_block else {}
    keys = sorted(set(global_items) | set(map_items))
    effective: list[dict[str, Any]] = []
    for key in keys:
        selected = map_items.get(key, global_items.get(key))
        assert selected is not None
        source_path = map_path if key in map_items else global_path
        effective.append(
            {**_assignment_report(selected), "sourceVirtualPath": source_path}
        )
    return {
        "mapDefinedKeys": sorted(item.key for item in map_items.values()),
        "overriddenGlobalKeys": sorted(
            map_items[key].key for key in set(map_items) & set(global_items)
        ),
        "inheritedGlobalKeys": sorted(
            global_items[key].key for key in set(global_items) - set(map_items)
        ),
        "effectiveAssignments": effective,
    }


def _finite_f32(cursor: _Cursor, label: str) -> float:
    value = cursor.f32()
    if not math.isfinite(value):
        raise SageEnvironmentError(f"{label} is not finite")
    return value


def _vector3(cursor: _Cursor, label: str) -> list[float]:
    return [_finite_f32(cursor, f"{label}.{axis}") for axis in ("x", "y", "z")]


def _argb(cursor: _Cursor) -> dict[str, int]:
    value = cursor.u32()
    return {
        "a": (value >> 24) & 0xFF,
        "r": (value >> 16) & 0xFF,
        "g": (value >> 8) & 0xFF,
        "b": value & 0xFF,
        "packedArgb": value,
    }


def _chunk_evidence(record: object, payload: bytes) -> dict[str, Any]:
    return {
        "name": record.name,
        "version": record.version,
        "recordOffsetInDecodedBody": record.offset,
        "payloadByteCount": record.size,
        "payloadSha256": _sha256(payload),
    }


def _parse_world_info(
    payload: bytes, names: dict[int, str], evidence: dict[str, Any]
) -> dict[str, Any]:
    cursor = _Cursor(payload, label="WorldInfo v1")
    properties = _parse_typed_properties(
        cursor,
        names,
        label="WorldInfo",
        expected_types=_WORLD_INFO_TYPES,
    )
    cursor.finish()
    by_name = {item["name"]: item for item in properties}
    unknown = [item for item in properties if item["name"] not in _WORLD_INFO_TYPES]

    def enum_value(name: str, values: dict[int, str]) -> dict[str, Any] | None:
        item = by_name.get(name)
        if item is None:
            return None
        raw = int(item["value"])
        if raw not in values:
            raise SageEnvironmentError(f"WorldInfo {name} has unsupported value: {raw}")
        return {"wireValue": raw, "name": values[raw]}

    camera_names = (
        "cameraMaxHeight",
        "cameraPitchAngle",
        "cameraYawAngle",
        "cameraScrollSpeedScalar",
        "cameraGroundMinHeight",
        "cameraGroundMaxHeight",
    )
    return {
        "chunk": evidence,
        "properties": properties,
        "weather": enum_value("weather", _WEATHER),
        "compression": enum_value("compression", _COMPRESSION),
        "mapCameraConstraints": {
            name: by_name[name]["value"] for name in camera_names if name in by_name
        },
        "unresolvedProperties": unknown,
    }


def _parse_global_lighting(payload: bytes, evidence: dict[str, Any]) -> dict[str, Any]:
    cursor = _Cursor(payload, label="GlobalLighting v8")
    time_value = cursor.u32()
    if time_value not in _TIME_OF_DAY:
        raise SageEnvironmentError(
            f"GlobalLighting has unsupported time-of-day value: {time_value}"
        )
    configurations: dict[str, Any] = {}
    for time_name in _TIME_OF_DAY.values():
        lights: dict[str, Any] = {}
        for light_name in _LIGHT_NAMES:
            lights[light_name] = {
                "ambient": _vector3(cursor, f"{time_name}.{light_name}.ambient"),
                "color": _vector3(cursor, f"{time_name}.{light_name}.color"),
                "direction": _vector3(cursor, f"{time_name}.{light_name}.direction"),
            }
        configurations[time_name] = lights
    shadow_color = _argb(cursor)
    unknown = cursor.bytes(44)
    no_cloud_factor = _vector3(cursor, "GlobalLighting.noCloudFactor")
    cursor.finish()
    return {
        "chunk": evidence,
        "activeTimeOfDay": {"wireValue": time_value, "name": _TIME_OF_DAY[time_value]},
        "configurations": configurations,
        "shadowColor": shadow_color,
        "noCloudFactorRgb": no_cloud_factor,
        "unresolvedVersion8Field": {
            "byteCount": len(unknown),
            "sha256": _sha256(unknown),
            "status": "semantics-unresolved-no-value-inferred",
        },
    }


def _parse_environment_data(payload: bytes, evidence: dict[str, Any]) -> dict[str, Any]:
    cursor = _Cursor(payload, label="EnvironmentData v3")
    result = {
        "chunk": evidence,
        "waterMaxAlphaDepth": _finite_f32(cursor, "waterMaxAlphaDepth"),
        "deepWaterAlpha": _finite_f32(cursor, "deepWaterAlpha"),
        "isMacroTextureStretched": cursor.bool8(),
        "macroTexture": cursor.ascii16(),
        "cloudTexture": cursor.ascii16(),
    }
    cursor.finish()
    return result


def _parse_named_cameras(payload: bytes, evidence: dict[str, Any]) -> dict[str, Any]:
    cursor = _Cursor(payload, label="NamedCameras v2")
    count = cursor.u32()
    if count > 4_096:
        raise SageEnvironmentError(f"named-camera count exceeds limit: {count}")
    cameras: list[dict[str, Any]] = []
    for index in range(count):
        cameras.append(
            {
                "index": index,
                "lookAtPoint": _vector3(cursor, f"namedCamera[{index}].lookAtPoint"),
                "name": cursor.ascii16(),
                "pitch": _finite_f32(cursor, f"namedCamera[{index}].pitch"),
                "roll": _finite_f32(cursor, f"namedCamera[{index}].roll"),
                "yaw": _finite_f32(cursor, f"namedCamera[{index}].yaw"),
                "zoom": _finite_f32(cursor, f"namedCamera[{index}].zoom"),
                "fieldOfView": _finite_f32(cursor, f"namedCamera[{index}].fieldOfView"),
                "unknown": _finite_f32(cursor, f"namedCamera[{index}].unknown"),
            }
        )
    cursor.finish()
    return {"chunk": evidence, "count": count, "cameras": cameras}


def _parse_count_only_chunk(
    payload: bytes,
    evidence: dict[str, Any],
    *,
    byte_count: bool,
    label: str,
) -> dict[str, Any]:
    cursor = _Cursor(payload, label=label)
    count = cursor.u8() if byte_count else cursor.u32()
    if count == 0:
        cursor.finish()
        return {"chunk": evidence, "count": 0, "status": "source-authored-empty"}
    remainder = cursor.bytes(cursor.remaining)
    return {
        "chunk": evidence,
        "count": count,
        "status": "entries-present-payload-semantics-unsupported",
        "unparsedPayloadByteCount": len(remainder),
        "unparsedPayloadSha256": _sha256(remainder),
    }


def _parse_environment_chunks(
    source: bytes,
) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    try:
        strict = parse_sage_map_bytes(source)
        body, envelope = decode_sage_map_blob(source)
        cursor = _Cursor(body, label="Fords environment map")
        if cursor.bytes(4) != b"CkMp":
            raise SageEnvironmentError("map is missing CkMp magic")
        names = _parse_name_table(cursor)
    except SageMapError as exc:
        raise SageEnvironmentError(str(exc)) from exc

    parsed: dict[str, Any] = {}
    unresolved: list[dict[str, Any]] = []
    seen: set[str] = set()
    for record in _records(
        cursor,
        names,
        cap=MAX_TOP_LEVEL_RECORDS,
        label="MapFile/environment",
    ):
        if record.name not in _TARGET_CHUNK_VERSIONS:
            record.payload.skip(record.payload.remaining)
            continue
        if record.name in seen:
            raise SageEnvironmentError(f"duplicate environment chunk: {record.name}")
        seen.add(record.name)
        expected_version = _TARGET_CHUNK_VERSIONS[record.name]
        if record.version != expected_version:
            raise SageEnvironmentError(
                f"unsupported {record.name} version: {record.version}; expected {expected_version}"
            )
        payload = record.payload.bytes(record.payload.remaining)
        evidence = _chunk_evidence(record, payload)
        try:
            if record.name == "WorldInfo":
                parsed[record.name] = _parse_world_info(payload, names, evidence)
            elif record.name == "GlobalLighting":
                parsed[record.name] = _parse_global_lighting(payload, evidence)
            elif record.name == "EnvironmentData":
                parsed[record.name] = _parse_environment_data(payload, evidence)
            elif record.name == "NamedCameras":
                parsed[record.name] = _parse_named_cameras(payload, evidence)
            elif record.name == "PostEffectsChunk":
                parsed[record.name] = _parse_count_only_chunk(
                    payload,
                    evidence,
                    byte_count=True,
                    label="PostEffectsChunk v1",
                )
            elif record.name == "CameraAnimationList":
                parsed[record.name] = _parse_count_only_chunk(
                    payload,
                    evidence,
                    byte_count=False,
                    label="CameraAnimationList v3",
                )
        except SageMapError as exc:
            raise SageEnvironmentError(str(exc)) from exc
    cursor.finish()

    for name in _TARGET_CHUNK_VERSIONS:
        if name not in seen:
            unresolved.append(
                {
                    "scope": "map-binary",
                    "field": name,
                    "evidence": {
                        "chunk": name,
                        "status": "absent",
                        "decodedBodySha256": strict.body_sha256,
                    },
                    "reason": "chunk-not-encoded-no-value-inferred",
                }
            )
    if "GlobalLighting" in parsed:
        unresolved.append(
            {
                "scope": "map-binary",
                "field": "GlobalLighting.version8Unknown44Bytes",
                "evidence": parsed["GlobalLighting"]["chunk"],
                "reason": "field-present-semantics-unresolved-no-value-inferred",
            }
        )
    world_info = parsed.get("WorldInfo")
    if world_info and world_info["unresolvedProperties"]:
        unresolved.append(
            {
                "scope": "map-binary",
                "field": "WorldInfo.unrecognizedProperties",
                "evidence": {
                    "chunk": world_info["chunk"],
                    "properties": world_info["unresolvedProperties"],
                },
                "reason": "property-wire-values-preserved-semantics-unresolved",
            }
        )
    named_cameras = parsed.get("NamedCameras")
    if named_cameras and named_cameras["count"]:
        unresolved.append(
            {
                "scope": "map-binary",
                "field": "NamedCameras.camera.unknown",
                "evidence": {
                    "chunk": named_cameras["chunk"],
                    "occurrenceCount": named_cameras["count"],
                },
                "reason": "float-field-present-semantics-unresolved-no-use-inferred",
            }
        )
    for name in ("PostEffectsChunk", "CameraAnimationList"):
        item = parsed.get(name)
        if item and item["status"].startswith("entries-present"):
            unresolved.append(
                {
                    "scope": "map-binary",
                    "field": name,
                    "evidence": item["chunk"],
                    "reason": "entries-present-but-entry-layout-not-decoded-by-this-oracle",
                }
            )

    return (
        {
            "sourceSha256": strict.source_sha256,
            "decodedBodySha256": strict.body_sha256,
            "envelope": envelope,
            "relevantChunkSignatures": [
                item for item in strict.chunks if item["name"] in _TARGET_CHUNK_VERSIONS
            ],
            "decoded": parsed,
        },
        unresolved,
    )


def _filter_game_data(block: _IniBlock) -> dict[str, Any]:
    selected = tuple(
        item
        for item in block.assignments
        if item.key in _GAME_DATA_EXACT_KEYS or item.key.startswith(_GAME_DATA_PREFIXES)
    )
    return _block_report(
        _IniBlock(
            block.kind,
            block.name,
            block.header_line,
            block.end_line,
            selected,
        ),
        GAME_DATA_INI_PATH,
    )


def _water_material_rows(map_data: dict[str, Any]) -> dict[str, Any]:
    standing: list[dict[str, Any]] = []
    for area in map_data["standing_water"]:
        standing.append(
            {
                key: area[key]
                for key in (
                    "id",
                    "name",
                    "layer",
                    "uvScrollSpeed",
                    "additive",
                    "bumpMap",
                    "skyTexture",
                    "waterHeight",
                    "shader",
                    "depthColors",
                )
            }
            | {"pointCount": len(area["sagePoints"])}
        )
    rivers: list[dict[str, Any]] = []
    for area in map_data["rivers"]:
        rivers.append(
            {
                key: area[key]
                for key in (
                    "id",
                    "name",
                    "layer",
                    "uvScrollSpeed",
                    "additive",
                    "textures",
                    "colorRgb",
                    "alpha",
                    "waterHeight",
                    "minimumWaterLod",
                )
            }
            | {"crossSectionCount": len(area["crossSections"])}
        )
    return {
        "standingWaterAreaCount": len(standing),
        "standingWaterMaterials": standing,
        "riverAreaCount": len(rivers),
        "riverMaterials": rivers,
        "geometryHandoff": {
            "standingWaterPointCount": sum(item["pointCount"] for item in standing),
            "riverCrossSectionCount": sum(item["crossSectionCount"] for item in rivers),
            "source": "existing strict sage_map standing-water and river geometry",
        },
    }


def _reference_add(
    references: dict[str, dict[str, Any]],
    name: str,
    *,
    scope: str,
    field: str,
    role: str,
) -> None:
    if not name or not _ASSET_REFERENCE.fullmatch(name):
        return
    key = name.casefold()
    row = references.setdefault(key, {"requestedName": name, "uses": []})
    row["uses"].append({"scope": scope, "field": field, "role": role})


def _collect_references(
    chunks: dict[str, Any],
    active_water_set: _IniBlock | None,
    weather: _IniBlock | None,
    water_transparency: dict[str, Any],
    water_texture_lists: list[_IniBlock],
    water_materials: dict[str, Any],
) -> dict[str, dict[str, Any]]:
    references: dict[str, dict[str, Any]] = {}
    environment = chunks.get("EnvironmentData")
    if environment:
        for field in ("macroTexture", "cloudTexture"):
            _reference_add(
                references,
                environment[field],
                scope="map-binary/EnvironmentData",
                field=field,
                role="active",
            )
    if active_water_set:
        for item in active_water_set.assignments:
            if "texture" in item.key.casefold():
                _reference_add(
                    references,
                    item.value,
                    scope=f"global-ini/{active_water_set.identity}",
                    field=item.key,
                    role="active",
                )
    if weather:
        assignments = _unique_assignments(weather)
        snow = assignments.get("snowtexture")
        if snow:
            _reference_add(
                references,
                snow.value,
                scope="global-ini/Weather",
                field=snow.key,
                role="conditional",
            )
    for item in water_transparency["effectiveAssignments"]:
        if "texture" in item["key"].casefold():
            _reference_add(
                references,
                str(item["rawValue"]),
                scope="effective-ini/WaterTransparency",
                field=str(item["key"]),
                role="active",
            )
    for block in water_texture_lists:
        for item in block.assignments:
            if item.key.casefold() == "texture":
                _reference_add(
                    references,
                    item.value,
                    scope=f"global-ini/{block.identity}",
                    field=item.key,
                    role="catalog",
                )
    for index, area in enumerate(water_materials["standingWaterMaterials"]):
        for field in ("bumpMap", "skyTexture", "shader", "depthColors"):
            _reference_add(
                references,
                str(area[field]),
                scope=f"map-binary/StandingWaterAreas[{index}]",
                field=field,
                role="active",
            )
    for index, area in enumerate(water_materials["riverMaterials"]):
        for field, value in area["textures"].items():
            _reference_add(
                references,
                str(value),
                scope=f"map-binary/RiverAreas[{index}]",
                field=field,
                role="active",
            )
    return references


def _asset_index(root: Path) -> dict[str, list[Path]]:
    index: dict[str, list[Path]] = {}
    for path in root.rglob("*"):
        if path.is_symlink():
            raise SageEnvironmentError(f"effective asset tree contains symlink: {path}")
        if path.is_file():
            index.setdefault(path.name.casefold(), []).append(path)
    for paths in index.values():
        paths.sort(key=lambda item: item.relative_to(root).as_posix().casefold())
    return index


def _candidate_report(path: Path, root: Path) -> dict[str, Any]:
    source = _read_source(path, maximum=MAX_SOURCE_BYTES, label="referenced asset")
    return {
        "virtualPath": path.relative_to(root).as_posix(),
        "byteCount": len(source),
        "sha256": _sha256(source),
    }


def _resolve_references(
    root: Path, references: dict[str, dict[str, Any]]
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    index = _asset_index(root)
    rows: list[dict[str, Any]] = []
    unresolved: list[dict[str, Any]] = []
    for row in sorted(
        references.values(), key=lambda item: item["requestedName"].casefold()
    ):
        requested = str(row["requestedName"])
        requested_path = Path(requested)
        exact = index.get(requested_path.name.casefold(), [])
        compiled: list[Path] = []
        if requested_path.suffix.casefold() == ".tga":
            compiled = index.get((requested_path.stem + ".dds").casefold(), [])
        exact_rows = [_candidate_report(path, root) for path in exact]
        compiled_rows = [_candidate_report(path, root) for path in compiled]
        if len(exact_rows) == 1:
            status = "one-exact-filename-candidate"
        elif len(exact_rows) > 1:
            status = "ambiguous-exact-filename-candidates"
        elif len(compiled_rows) == 1:
            status = "one-retail-compiled-dds-stem-counterpart"
        elif len(compiled_rows) > 1:
            status = "ambiguous-retail-compiled-dds-stem-counterparts"
        else:
            status = "unresolved-in-effective-tree"
        uses = sorted(
            row["uses"],
            key=lambda item: (item["role"], item["scope"], item["field"]),
        )
        result = {
            "requestedName": requested,
            "uses": uses,
            "status": status,
            "exactFilenameCandidates": exact_rows,
            "compiledDdsStemCounterparts": compiled_rows,
        }
        rows.append(result)
        if any(item["role"] == "active" for item in uses) and status in {
            "unresolved-in-effective-tree",
            "ambiguous-exact-filename-candidates",
            "ambiguous-retail-compiled-dds-stem-counterparts",
        }:
            unresolved.append(
                {
                    "scope": "effective-asset-tree",
                    "field": requested,
                    "evidence": {"uses": uses, "resolutionStatus": status},
                    "reason": "active-reference-does-not-have-one-evidence-candidate",
                }
            )
    return rows, unresolved


def _source_aggregate(sources: list[dict[str, Any]]) -> str:
    digest = hashlib.sha256()
    digest.update(b"openbfme.sage-fords-environment-sources\0")
    for item in sorted(sources, key=lambda row: row["virtualPath"].casefold()):
        digest.update(item["virtualPath"].encode("utf-8") + b"\0")
        digest.update(str(item["byteCount"]).encode("ascii") + b"\0")
        digest.update(item["sha256"].encode("ascii") + b"\0")
    return digest.hexdigest()


def build_fords_environment_report(effective_root: Path | str) -> dict[str, Any]:
    """Build a payload-free environment evidence report from the effective tree."""

    root = Path(effective_root)
    if root.is_symlink() or not root.is_dir():
        raise SageEnvironmentError(
            f"effective asset root is not a regular directory: {root}"
        )
    sources: dict[str, bytes] = {}
    source_rows: list[dict[str, Any]] = []
    for virtual_path in _SOURCE_PATHS:
        maximum = MAX_SOURCE_BYTES if virtual_path.endswith(".map") else MAX_INI_BYTES
        source = _read_source(
            _source_path(root, virtual_path),
            maximum=maximum,
            label=virtual_path,
        )
        sources[virtual_path] = source
        source_rows.append(_source_evidence(virtual_path, source))

    chunk_report, unresolved = _parse_environment_chunks(sources[FORDS_MAP_PATH])
    strict_map = parse_sage_map_bytes(sources[FORDS_MAP_PATH])

    map_blocks = _parse_ini_blocks(
        sources[FORDS_MAP_INI_PATH],
        label=FORDS_MAP_INI_PATH,
        block_shapes={"Weather": False, "WaterTransparency": False},
    )
    weather_blocks = _parse_ini_blocks(
        sources[WEATHER_INI_PATH],
        label=WEATHER_INI_PATH,
        block_shapes={"Weather": False, "WeatherData": True},
    )
    water_blocks = _parse_ini_blocks(
        sources[WATER_INI_PATH],
        label=WATER_INI_PATH,
        block_shapes={"WaterSet": True, "WaterTransparency": False},
    )
    water_texture_blocks = _parse_ini_blocks(
        sources[WATER_TEXTURES_INI_PATH],
        label=WATER_TEXTURES_INI_PATH,
        block_shapes={"WaterTextureList": True},
    )
    game_blocks = _parse_ini_blocks(
        sources[GAME_DATA_INI_PATH],
        label=GAME_DATA_INI_PATH,
        block_shapes={"GameData": False},
    )

    map_weather = _singleton(map_blocks, "Weather")
    global_weather = _singleton(weather_blocks, "Weather")
    map_water = _singleton(map_blocks, "WaterTransparency")
    global_water = _singleton(water_blocks, "WaterTransparency")
    game_data = _singleton(game_blocks, "GameData")
    if global_weather is None or global_water is None or game_data is None:
        missing = [
            name
            for name, value in (
                ("Weather", global_weather),
                ("WaterTransparency", global_water),
                ("GameData", game_data),
            )
            if value is None
        ]
        raise SageEnvironmentError(
            "missing required global INI block(s): " + ", ".join(missing)
        )

    water_sets = _blocks_by_kind(water_blocks, "WaterSet")
    water_texture_lists = _blocks_by_kind(water_texture_blocks, "WaterTextureList")
    weather_data = _blocks_by_kind(weather_blocks, "WeatherData")
    active_time = (
        chunk_report["decoded"]
        .get("GlobalLighting", {})
        .get("activeTimeOfDay", {})
        .get("name")
    )
    active_water_sets = [
        block
        for block in water_sets
        if block.name is not None
        and block.name.casefold() == str(active_time).casefold()
    ]
    if len(active_water_sets) != 1:
        raise SageEnvironmentError(
            f"active time {active_time!r} resolves to {len(active_water_sets)} WaterSet blocks"
        )
    active_water_set = active_water_sets[0]

    fog_overlay = _overlay_report(
        global_weather,
        map_weather,
        global_path=WEATHER_INI_PATH,
        map_path=FORDS_MAP_INI_PATH,
    )
    water_overlay = _overlay_report(
        global_water,
        map_water,
        global_path=WATER_INI_PATH,
        map_path=FORDS_MAP_INI_PATH,
    )
    water_materials = _water_material_rows(
        {"standing_water": strict_map.standing_water, "rivers": strict_map.rivers}
    )
    references = _collect_references(
        chunk_report["decoded"],
        active_water_set,
        global_weather,
        water_overlay,
        water_texture_lists,
        water_materials,
    )
    reference_rows, unresolved_references = _resolve_references(root, references)
    unresolved.extend(unresolved_references)

    if map_weather is None:
        unresolved.append(
            {
                "scope": "map-ini",
                "field": "Weather",
                "evidence": {"virtualPath": FORDS_MAP_INI_PATH, "status": "absent"},
                "reason": "no-map-local-weather-fields-encoded",
            }
        )
    if map_water is None:
        unresolved.append(
            {
                "scope": "map-ini",
                "field": "WaterTransparency",
                "evidence": {"virtualPath": FORDS_MAP_INI_PATH, "status": "absent"},
                "reason": "no-map-local-water-fields-encoded",
            }
        )
    unresolved.append(
        {
            "scope": "map-binary",
            "field": "FogSettings",
            "evidence": {
                "chunk": "FogSettings",
                "status": "absent",
                "decodedBodySha256": chunk_report["decodedBodySha256"],
            },
            "reason": "no-fog-chunk-encoded-map-ini-weather-is-the-exact-fog-source",
        }
    )
    unresolved.append(
        {
            "scope": "renderer-contract",
            "field": "sage-light-and-camera-coordinate-transform",
            "evidence": {
                "lightingChunk": chunk_report["decoded"]
                .get("GlobalLighting", {})
                .get("chunk"),
                "namedCamerasChunk": chunk_report["decoded"]
                .get("NamedCameras", {})
                .get("chunk"),
            },
            "reason": "source-vectors-are-exact-but-godot-basis-transform-is-not-encoded-here",
        }
    )
    unresolved.append(
        {
            "scope": "renderer-contract",
            "field": "shadow-volume-decal-godot-equivalence",
            "evidence": {
                "globalDefaults": GAME_DATA_INI_PATH,
                "mapShadowColorChunk": chunk_report["decoded"]
                .get("GlobalLighting", {})
                .get("chunk"),
            },
            "reason": "retail-mode-flags-and-color-are-exact-but-renderer-equivalence-needs-oracle-validation",
        }
    )

    result: dict[str, Any] = {
        "schema": SCHEMA,
        "schemaVersion": SCHEMA_VERSION,
        "mapId": "fords-of-isen-ii",
        "sourceProvenance": {
            "effectiveRootRole": "retail-effective-winner-tree",
            "files": source_rows,
            "aggregateSha256": _source_aggregate(source_rows),
        },
        "mapBinary": chunk_report,
        "timeAndWeather": {
            "activeTimeOfDay": chunk_report["decoded"]
            .get("GlobalLighting", {})
            .get("activeTimeOfDay"),
            "mapWeather": chunk_report["decoded"].get("WorldInfo", {}).get("weather"),
            "globalGameDataDefaults": _filter_game_data(game_data),
        },
        "fog": {
            "mapLocal": _block_report(map_weather, FORDS_MAP_INI_PATH)
            if map_weather
            else None,
            "globalDefault": _block_report(global_weather, WEATHER_INI_PATH),
            "overlayEvidence": fog_overlay,
        },
        "lightingAndShadows": {
            "mapGlobalLighting": chunk_report["decoded"].get("GlobalLighting"),
            "globalGameDataDefaults": _filter_game_data(game_data),
        },
        "skyAndClouds": {
            "mapEnvironmentData": chunk_report["decoded"].get("EnvironmentData"),
            "globalWeather": _block_report(global_weather, WEATHER_INI_PATH),
            "globalGameDataDefaults": _filter_game_data(game_data),
        },
        "water": {
            "mapEnvironmentData": chunk_report["decoded"].get("EnvironmentData"),
            "activeWaterSet": _block_report(active_water_set, WATER_INI_PATH),
            "allWaterSets": [
                _block_report(item, WATER_INI_PATH) for item in water_sets
            ],
            "mapLocalTransparency": _block_report(map_water, FORDS_MAP_INI_PATH)
            if map_water
            else None,
            "globalTransparency": _block_report(global_water, WATER_INI_PATH),
            "transparencyOverlayEvidence": water_overlay,
            "textureLists": [
                _block_report(item, WATER_TEXTURES_INI_PATH)
                for item in water_texture_lists
            ],
            "mapMaterialRows": water_materials,
        },
        "cameras": {
            "mapWorldInfo": chunk_report["decoded"].get("WorldInfo"),
            "mapNamedCameras": chunk_report["decoded"].get("NamedCameras"),
            "mapCameraAnimations": chunk_report["decoded"].get("CameraAnimationList"),
            "globalGameDataDefaults": _filter_game_data(game_data),
        },
        "postEffects": chunk_report["decoded"].get("PostEffectsChunk"),
        "weatherDefinitions": [
            _block_report(item, WEATHER_INI_PATH) for item in weather_data
        ],
        "referencedAssets": reference_rows,
        "unresolved": sorted(
            unresolved,
            key=lambda item: (item["scope"], item["field"], item["reason"]),
        ),
        "runtimeHandoff": [
            {
                "target": "active-time-and-weather",
                "source": "GlobalLighting.activeTimeOfDay + WorldInfo.weather",
                "status": "exact-source-values-ready",
            },
            {
                "target": "terrain-object-infantry-light-rigs",
                "source": "GlobalLighting.configurations[activeTimeOfDay]",
                "status": "values-ready-godot-basis-transform-unresolved",
            },
            {
                "target": "fog",
                "source": "map.ini Weather overlay with weather.ini Weather defaults",
                "status": "exact-source-values-ready",
            },
            {
                "target": "sky-cloud-macro-textures",
                "source": "EnvironmentData + Weather cloud controls + GameData sky flags",
                "status": "references-evidenced-renderer-binding-pending",
            },
            {
                "target": "water-materials",
                "source": "EnvironmentData + active WaterSet + WaterTransparency + map water rows",
                "status": "parameters-ready-one-candidate-per-active-reference-required",
            },
            {
                "target": "camera-limits-and-anchors",
                "source": "WorldInfo + NamedCameras + GameData defaults",
                "status": "values-ready-godot-basis-transform-unresolved",
            },
            {
                "target": "shadow-mode-and-color",
                "source": "GameData shadow flags + GlobalLighting.shadowColor",
                "status": "retail-facts-ready-renderer-equivalence-unresolved",
            },
        ],
    }
    result["aggregateSha256"] = _canonical_sha256(result)
    return result


def write_fords_environment_report(
    effective_root: Path | str, output: Path | str
) -> dict[str, Any]:
    report = build_fords_environment_report(effective_root)
    write_json_atomic(Path(output), report)
    return report


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Extract deterministic BFME II Fords environment evidence"
    )
    parser.add_argument("--effective-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    report = write_fords_environment_report(args.effective_root, args.output)
    print(report["aggregateSha256"])
    return 0


if __name__ == "__main__":  # pragma: no cover
    raise SystemExit(main())
