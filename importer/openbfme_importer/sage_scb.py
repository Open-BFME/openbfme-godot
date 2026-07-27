"""Fail-closed BFME script-container conversion and exact native backtesting.

The supported wire layouts are the layouts independently observed in the
standalone Rise of the Witch-king retail corpus, plus the older record
revisions that retail's shipped script ``libraries`` are authored in (see
``_NESTED_VERSIONS`` and the layout tables beside it).  Conversion preserves
source order, repeated records, repeated player/team names, every script
argument, and every property occurrence.  Unsupported non-empty map/camera
layouts are rejected instead of being silently discarded.

The native representation is JSON-safe.  Its independent writer reconstructs
the decoded ``CkMp`` body byte-for-byte; :func:`backtest_sage_scb_native` then
compares that reconstruction with the verified source body.  Retail strings
are therefore present only in caller-owned private native data.  Corpus
diagnostics deliberately retain aggregate counts and canonical hashes only.
"""

from __future__ import annotations

from collections import Counter
from collections.abc import Iterable, Mapping, Sequence
from dataclasses import dataclass
import hashlib
import json
import math
from pathlib import Path
import struct
from typing import Any

from .sage_map import (
    MAX_PROPERTIES_PER_OBJECT,
    MAX_SOURCE_BYTES,
    MAX_TOP_LEVEL_RECORDS,
    SageMapError,
    _Cursor,
    _Record,
    _parse_name_table,
    _records,
    decode_sage_map_blob,
)
from .util import write_json_atomic


SCB_NATIVE_SCHEMA = "openbfme.sage-scb-native"
SCB_NATIVE_SCHEMA_VERSION = 0
SCB_BACKTEST_SCHEMA = "openbfme.sage-scb-backtest"
SCB_BACKTEST_SCHEMA_VERSION = 0
SCB_DIAGNOSTIC_SCHEMA = "openbfme.sage-scb-native-diagnostic"
SCB_DIAGNOSTIC_SCHEMA_VERSION = 0

MAX_SCB_SOURCES = 1_024
MAX_NESTED_RECORDS = 100_000
MAX_SCRIPT_DEPTH = 64
MAX_SCRIPT_ARGUMENTS = 100_000
MAX_SCRIPT_PLAYERS = 4_096
MAX_SCRIPT_TEAMS = 65_536
MAX_NAMED_CAMERAS = 4_096

_TOP_LEVEL_VERSIONS = {
    "ScriptImportSize": frozenset({1}),
    "PlayerScriptsList": frozenset({1}),
    "NamedCameras": frozenset({2}),
    "CameraAnimationList": frozenset({3}),
    "ScriptsPlayers": frozenset({2}),
    "ObjectsList": frozenset({3}),
    "TriggerAreas": frozenset({1}),
    "ScriptTeams": frozenset({1}),
    "WaypointsList": frozenset({1}),
}
_REQUIRED_TOP_LEVEL_CHUNKS = frozenset(_TOP_LEVEL_VERSIONS)
# Record revisions this converter admits.  ``ScriptGroup`` v2, ``Script`` v2,
# ``Condition`` v4/v5 and ``ScriptAction``/``ScriptActionFalse`` v2 are the
# revisions retail's shipped script ``libraries`` are authored in; the higher
# revisions are what retail's own .map/.scb files use.  Every admitted layout
# below was measured on the retail corpus by exact payload consumption, never
# inferred: the record framing (asset-name index, version, payload length) is
# version-independent, so each record's byte boundaries are known before any
# field is interpreted, and a candidate layout is accepted only when it
# consumes its declared payload precisely.
_NESTED_VERSIONS = {
    "ScriptList": frozenset({1}),
    "ScriptGroup": frozenset({2, 3}),
    "Script": frozenset({2, 4}),
    "OrCondition": frozenset({1}),
    "Condition": frozenset({4, 5, 6}),
    "ScriptAction": frozenset({2, 3}),
    "ScriptActionFalse": frozenset({2, 3}),
}

# Trailing 32-bit flags carried by ``Condition``/``ScriptAction``/
# ``ScriptActionFalse``, in wire order, keyed by record version.  Measured as
# ``payload length - bytes consumed by the version-independent prefix``
# (contentType, internal-name key, argument vector); every trailing dword in
# the corpus is 0 or 1, which ``_bool32`` re-checks per record.
_CONDITION_TRAILING_FLAGS: dict[int, tuple[str, ...]] = {
    4: (),
    5: ("enabled", "inverted"),
    6: ("enabled", "inverted"),
}
_ACTION_TRAILING_FLAGS: dict[int, tuple[str, ...]] = {
    2: (),
    3: ("enabled",),
}

# A flag the older wire does not carry is fixed *by construction*, not by
# assumption: the record contains no byte that could express the other value.
# A Condition v4 has no ``inverted`` dword, so it cannot be negated; a
# Condition v4 / ScriptAction v2 has no ``enabled`` dword, so it cannot be
# switched off.  The native record still carries the keys, so downstream
# consumers see one stable shape across versions, and reconstruction refuses
# any value other than the default for a version that cannot encode it.
_SCRIPT_CONTENT_FLAG_DEFAULTS: dict[str, bool] = {"enabled": True, "inverted": False}

# ``Script`` v2 ends at ``evaluationInterval``.  ``Script`` v4 continues with
# the sequential/loop/scope tail.  ``ScriptGroup`` v2 and v3 share one field
# set (ascii16 name, two bool8, then child records), so ScriptGroup needs no
# per-version table.
_SCRIPT_TAIL_VERSIONS = frozenset({4})

# Defaults for the v4-only ``Script`` tail, again by construction: a v2 script
# has no bytes for sequential firing, action looping, a sequential target or a
# scope, so it fires all actions once, unbound to any target.  ``scope``
# defaults to ``"ALL"`` -- the unrestricted scope, and the only value the
# retail corpus ever writes in a v4 script.
_SCRIPT_TAIL_DEFAULTS: dict[str, Any] = {
    "actionsFireSequentially": False,
    "loopActions": False,
    "loopCount": 0,
    "sequentialTargetType": 0,
    "sequentialTargetName": "",
    "scope": "ALL",
}

_PROPERTY_TYPES = frozenset(range(6))
_SCRIPT_SCOPES = frozenset({"ALL", "Planning", "X"})
_SEQUENTIAL_TARGET_TYPES = frozenset({0, 1})
_POSITION_ARGUMENT_TYPE = 16


class SageScbError(ValueError):
    """Raised with a stable structural category for unsafe SCB input."""

    def __init__(self, code: str, detail: str) -> None:
        self.code = code
        super().__init__(f"{code}: {detail}")


@dataclass(frozen=True, slots=True)
class SageScbBacktestReport:
    """Identifier-free evidence for one exact native round trip."""

    source_bytes: int
    decoded_body_bytes: int
    chunk_count: int
    source_sha256: str
    decoded_body_sha256: str
    reconstructed_body_sha256: str
    semantic_sha256: str
    evidence_sha256: str

    def to_dict(self) -> dict[str, Any]:
        return {
            "schema": SCB_BACKTEST_SCHEMA,
            "schemaVersion": SCB_BACKTEST_SCHEMA_VERSION,
            "accepted": True,
            "exactWireMatch": True,
            "sourceBytes": self.source_bytes,
            "decodedBodyBytes": self.decoded_body_bytes,
            "chunkCount": self.chunk_count,
            "sourceSha256": self.source_sha256,
            "decodedBodySha256": self.decoded_body_sha256,
            "reconstructedBodySha256": self.reconstructed_body_sha256,
            "semanticSha256": self.semantic_sha256,
            "evidenceSha256": self.evidence_sha256,
        }


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _canonical_bytes(value: object) -> bytes:
    try:
        return json.dumps(
            value,
            ensure_ascii=False,
            allow_nan=False,
            sort_keys=True,
            separators=(",", ":"),
        ).encode("utf-8")
    except (TypeError, ValueError) as exc:
        raise SageScbError(
            "invalid-native-json", "value is not canonical JSON"
        ) from exc


def _canonical_sha256(value: object) -> str:
    return _sha256(_canonical_bytes(value))


def _bool32(cursor: _Cursor, label: str) -> bool:
    value = cursor.u32()
    if value not in (0, 1):
        raise SageScbError("invalid-boolean", f"{label} has 32-bit value {value}")
    return bool(value)


def _require_version(record: _Record, versions: frozenset[int]) -> None:
    if record.version not in versions:
        raise SageScbError(
            "unsupported-version",
            f"unsupported {record.name} version {record.version}",
        )


def _claim_node(budget: dict[str, int], *, depth: int) -> None:
    if depth > MAX_SCRIPT_DEPTH:
        raise SageScbError(
            "script-depth-limit", f"script nesting exceeds {MAX_SCRIPT_DEPTH}"
        )
    budget["nodes"] += 1
    if budget["nodes"] > MAX_NESTED_RECORDS:
        raise SageScbError(
            "script-record-limit",
            f"nested record count exceeds {MAX_NESTED_RECORDS}",
        )


def _property_key(
    cursor: _Cursor, names: Mapping[int, str], *, label: str
) -> dict[str, Any]:
    wire_type = cursor.u8()
    if wire_type not in _PROPERTY_TYPES:
        raise SageScbError(
            "unsupported-property-type", f"{label} has wire type {wire_type}"
        )
    name_index = cursor.u24()
    try:
        name = names[name_index]
    except KeyError as exc:
        raise SageScbError(
            "unknown-property-name-index", f"{label} has an unknown name index"
        ) from exc
    return {"name": name, "wireTypeCode": wire_type}


def _property_value(cursor: _Cursor, wire_type: int) -> Any:
    if wire_type == 0:
        return cursor.bool8()
    if wire_type == 1:
        return cursor.i32()
    if wire_type == 2:
        return cursor.f32()
    if wire_type in (3, 5):
        return cursor.ascii16()
    if wire_type == 4:
        return cursor.unicode16()
    raise SageScbError(
        "unsupported-property-type", f"property has wire type {wire_type}"
    )


def _parse_properties(
    cursor: _Cursor, names: Mapping[int, str], *, label: str
) -> list[dict[str, Any]]:
    count = cursor.u16()
    if count > MAX_PROPERTIES_PER_OBJECT:
        raise SageScbError(
            "property-count-limit",
            f"{label} property count exceeds {MAX_PROPERTIES_PER_OBJECT}",
        )
    result: list[dict[str, Any]] = []
    for _ in range(count):
        key = _property_key(cursor, names, label=label)
        result.append(
            {
                **key,
                "value": _property_value(cursor, int(key["wireTypeCode"])),
            }
        )
    return result


def _parse_argument(cursor: _Cursor) -> dict[str, Any]:
    argument_type = cursor.u32()
    if argument_type == _POSITION_ARGUMENT_TYPE:
        return {
            "argumentType": argument_type,
            "position": [cursor.f32(), cursor.f32(), cursor.f32()],
        }
    return {
        "argumentType": argument_type,
        "integer": cursor.i32(),
        "real": cursor.f32(),
        "text": cursor.ascii16(),
    }


def _content_flag_names(kind: str, version: int) -> tuple[str, ...]:
    """Trailing flags this record version carries, in wire order.

    Fails closed on an unknown version so that widening ``_NESTED_VERSIONS``
    without also measuring a layout can never silently mis-parse.
    """

    table = _CONDITION_TRAILING_FLAGS if kind == "condition" else _ACTION_TRAILING_FLAGS
    try:
        return table[version]
    except KeyError as exc:
        raise SageScbError(
            "unsupported-version", f"unsupported {kind} version {version}"
        ) from exc


def _content_flag_keys(kind: str) -> tuple[str, ...]:
    """Flag keys every native record of this kind carries, in wire order."""

    return ("enabled", "inverted") if kind == "condition" else ("enabled",)


def _script_has_tail(version: int) -> bool:
    """Whether this ``Script`` version carries the sequential/loop/scope tail."""

    if version not in _NESTED_VERSIONS["Script"]:
        raise SageScbError(
            "unsupported-version", f"unsupported Script version {version}"
        )
    return version in _SCRIPT_TAIL_VERSIONS


def _read_nested(
    cursor: _Cursor,
    names: Mapping[int, str],
    *,
    label: str,
) -> list[_Record]:
    return list(_records(cursor, dict(names), cap=MAX_NESTED_RECORDS, label=label))


def _parse_script_content(
    record: _Record,
    names: Mapping[int, str],
    *,
    kind: str,
    budget: dict[str, int],
    depth: int,
) -> dict[str, Any]:
    _claim_node(budget, depth=depth)
    _require_version(record, _NESTED_VERSIONS[record.name])
    cursor = record.payload
    content_type = cursor.u32()
    internal_name = _property_key(cursor, names, label=f"{kind} internal name")
    argument_count = cursor.u32()
    if argument_count > MAX_SCRIPT_ARGUMENTS:
        raise SageScbError(
            "script-argument-limit",
            f"{kind} argument count exceeds {MAX_SCRIPT_ARGUMENTS}",
        )
    arguments = [_parse_argument(cursor) for _ in range(argument_count)]
    present = _content_flag_names(kind, record.version)
    read = {name: _bool32(cursor, f"{kind} {name}") for name in present}
    result: dict[str, Any] = {
        "contentType": content_type,
        "internalName": internal_name,
        "arguments": arguments,
    }
    for name in _content_flag_keys(kind):
        # Absent on the older wire: the documented, by-construction default.
        result[name] = read.get(name, _SCRIPT_CONTENT_FLAG_DEFAULTS[name])
    cursor.finish()
    return result


def _parse_or_condition(
    record: _Record,
    names: Mapping[int, str],
    *,
    budget: dict[str, int],
    depth: int,
) -> dict[str, Any]:
    _claim_node(budget, depth=depth)
    _require_version(record, _NESTED_VERSIONS[record.name])
    records: list[dict[str, Any]] = []
    for child in _read_nested(record.payload, names, label="OrCondition"):
        if child.name != "Condition":
            raise SageScbError(
                "unsupported-nested-record",
                f"OrCondition contains {child.name}",
            )
        records.append(
            {
                "name": child.name,
                "version": child.version,
                "value": _parse_script_content(
                    child,
                    names,
                    kind="condition",
                    budget=budget,
                    depth=depth + 1,
                ),
            }
        )
    record.payload.finish()
    return {"records": records}


def _parse_script(
    record: _Record,
    names: Mapping[int, str],
    *,
    budget: dict[str, int],
    depth: int,
) -> dict[str, Any]:
    _claim_node(budget, depth=depth)
    _require_version(record, _NESTED_VERSIONS[record.name])
    cursor = record.payload
    result: dict[str, Any] = {
        "name": cursor.ascii16(),
        "comment": cursor.ascii16(),
        "conditionsComment": cursor.ascii16(),
        "actionsComment": cursor.ascii16(),
        "isActive": cursor.bool8(),
        "deactivateUponSuccess": cursor.bool8(),
        "activeInEasy": cursor.bool8(),
        "activeInMedium": cursor.bool8(),
        "activeInHard": cursor.bool8(),
        "isSubroutine": cursor.bool8(),
        "evaluationInterval": cursor.u32(),
    }
    if _script_has_tail(record.version):
        result["actionsFireSequentially"] = cursor.bool8()
        result["loopActions"] = cursor.bool8()
        result["loopCount"] = cursor.i32()
        sequential_target_type = cursor.u8()
        if sequential_target_type not in _SEQUENTIAL_TARGET_TYPES:
            raise SageScbError(
                "unsupported-sequential-target",
                f"script target type is {sequential_target_type}",
            )
        result["sequentialTargetType"] = sequential_target_type
        result["sequentialTargetName"] = cursor.ascii16()
        scope = cursor.ascii16()
        if scope not in _SCRIPT_SCOPES:
            raise SageScbError(
                "unsupported-script-scope", "script scope is unrecognized"
            )
        result["scope"] = scope
    else:
        # Absent on the older wire: the documented, by-construction defaults.
        result.update(_SCRIPT_TAIL_DEFAULTS)

    records: list[dict[str, Any]] = []
    for child in _read_nested(cursor, names, label="Script"):
        if child.name == "OrCondition":
            value = _parse_or_condition(child, names, budget=budget, depth=depth + 1)
        elif child.name in ("ScriptAction", "ScriptActionFalse"):
            value = _parse_script_content(
                child,
                names,
                kind="action",
                budget=budget,
                depth=depth + 1,
            )
        else:
            raise SageScbError(
                "unsupported-nested-record", f"Script contains {child.name}"
            )
        records.append({"name": child.name, "version": child.version, "value": value})
    cursor.finish()
    result["records"] = records
    return result


def _parse_script_group(
    record: _Record,
    names: Mapping[int, str],
    *,
    budget: dict[str, int],
    depth: int,
) -> dict[str, Any]:
    _claim_node(budget, depth=depth)
    _require_version(record, _NESTED_VERSIONS[record.name])
    cursor = record.payload
    result: dict[str, Any] = {
        "name": cursor.ascii16(),
        "isActive": cursor.bool8(),
        "isSubroutine": cursor.bool8(),
    }
    records: list[dict[str, Any]] = []
    for child in _read_nested(cursor, names, label="ScriptGroup"):
        if child.name == "Script":
            value = _parse_script(child, names, budget=budget, depth=depth + 1)
        elif child.name == "ScriptGroup":
            value = _parse_script_group(child, names, budget=budget, depth=depth + 1)
        else:
            raise SageScbError(
                "unsupported-nested-record", f"ScriptGroup contains {child.name}"
            )
        records.append({"name": child.name, "version": child.version, "value": value})
    cursor.finish()
    result["records"] = records
    return result


def _parse_script_list(
    record: _Record,
    names: Mapping[int, str],
    *,
    budget: dict[str, int],
    depth: int,
) -> dict[str, Any]:
    _claim_node(budget, depth=depth)
    _require_version(record, _NESTED_VERSIONS[record.name])
    records: list[dict[str, Any]] = []
    for child in _read_nested(record.payload, names, label="ScriptList"):
        if child.name == "Script":
            value = _parse_script(child, names, budget=budget, depth=depth + 1)
        elif child.name == "ScriptGroup":
            value = _parse_script_group(child, names, budget=budget, depth=depth + 1)
        else:
            raise SageScbError(
                "unsupported-nested-record", f"ScriptList contains {child.name}"
            )
        records.append({"name": child.name, "version": child.version, "value": value})
    record.payload.finish()
    return {"records": records}


def _parse_player_scripts(
    record: _Record,
    names: Mapping[int, str],
    *,
    budget: dict[str, int],
) -> dict[str, Any]:
    records: list[dict[str, Any]] = []
    for child in _read_nested(record.payload, names, label="PlayerScriptsList"):
        if child.name != "ScriptList":
            raise SageScbError(
                "unsupported-nested-record",
                f"PlayerScriptsList contains {child.name}",
            )
        records.append(
            {
                "name": child.name,
                "version": child.version,
                "value": _parse_script_list(child, names, budget=budget, depth=1),
            }
        )
    record.payload.finish()
    return {"records": records}


def _parse_named_cameras(record: _Record) -> dict[str, Any]:
    cursor = record.payload
    count = cursor.u32()
    if count > MAX_NAMED_CAMERAS:
        raise SageScbError(
            "named-camera-limit", f"named-camera count exceeds {MAX_NAMED_CAMERAS}"
        )
    cameras: list[dict[str, Any]] = []
    for _ in range(count):
        cameras.append(
            {
                "lookAtPoint": [cursor.f32(), cursor.f32(), cursor.f32()],
                "name": cursor.ascii16(),
                "pitch": cursor.f32(),
                "roll": cursor.f32(),
                "yaw": cursor.f32(),
                "zoom": cursor.f32(),
                "fieldOfView": cursor.f32(),
                "unknown": cursor.f32(),
            }
        )
    cursor.finish()
    return {"cameras": cameras}


def _parse_count_only_empty(record: _Record, *, code: str) -> dict[str, Any]:
    count = record.payload.u32()
    if count:
        raise SageScbError(code, f"{record.name} has {count} unsupported entries")
    record.payload.finish()
    return {"count": 0, "status": "source-authored-empty"}


def _parse_scripts_players(record: _Record, names: Mapping[int, str]) -> dict[str, Any]:
    cursor = record.payload
    has_properties = _bool32(cursor, "ScriptsPlayers hasPlayerProperties")
    count = cursor.u32()
    if count > MAX_SCRIPT_PLAYERS:
        raise SageScbError(
            "script-player-limit",
            f"script-player count exceeds {MAX_SCRIPT_PLAYERS}",
        )
    players: list[dict[str, Any]] = []
    for _ in range(count):
        player: dict[str, Any] = {"name": cursor.ascii16()}
        if has_properties:
            player["properties"] = _parse_properties(
                cursor, names, label="script player"
            )
        players.append(player)
    cursor.finish()
    return {"hasPlayerProperties": has_properties, "players": players}


def _parse_script_teams(record: _Record, names: Mapping[int, str]) -> dict[str, Any]:
    teams: list[dict[str, Any]] = []
    while record.payload.remaining:
        if len(teams) >= MAX_SCRIPT_TEAMS:
            raise SageScbError(
                "script-team-limit",
                f"script-team count exceeds {MAX_SCRIPT_TEAMS}",
            )
        teams.append(
            {
                "properties": _parse_properties(
                    record.payload, names, label="script team"
                )
            }
        )
    record.payload.finish()
    return {"teams": teams}


def _parse_top_level_record(
    record: _Record,
    names: Mapping[int, str],
    *,
    budget: dict[str, int],
) -> dict[str, Any]:
    try:
        versions = _TOP_LEVEL_VERSIONS[record.name]
    except KeyError as exc:
        raise SageScbError(
            "unsupported-top-level-chunk", f"unsupported chunk {record.name}"
        ) from exc
    _require_version(record, versions)

    if record.name == "ScriptImportSize":
        value = {
            "unknown1": record.payload.u32(),
            "unknown2": record.payload.u32(),
        }
        record.payload.finish()
    elif record.name == "PlayerScriptsList":
        value = _parse_player_scripts(record, names, budget=budget)
    elif record.name == "NamedCameras":
        value = _parse_named_cameras(record)
    elif record.name == "CameraAnimationList":
        value = _parse_count_only_empty(
            record, code="unsupported-camera-animation-layout"
        )
    elif record.name == "ScriptsPlayers":
        value = _parse_scripts_players(record, names)
    elif record.name == "ObjectsList":
        if record.payload.remaining:
            raise SageScbError(
                "unsupported-objects-layout",
                "SCB ObjectsList is non-empty without terrain height context",
            )
        record.payload.finish()
        value = {"count": 0, "status": "source-authored-empty"}
    elif record.name == "TriggerAreas":
        value = _parse_count_only_empty(record, code="unsupported-trigger-area-layout")
    elif record.name == "ScriptTeams":
        value = _parse_script_teams(record, names)
    elif record.name == "WaypointsList":
        value = _parse_count_only_empty(record, code="unsupported-waypoint-layout")
    else:  # pragma: no cover - exhaustive dispatch guarded above
        raise AssertionError(record.name)
    return {"name": record.name, "version": record.version, "value": value}


def _map_error_code(exc: SageMapError) -> str:
    message = str(exc).casefold()
    if "truncated" in message or "need " in message and "have " in message:
        return "truncated-wire"
    if "refpack" in message or "ear " in message:
        return "malformed-envelope"
    if "asset-name" in message or "utf-8 asset name" in message:
        return "malformed-name-table"
    if "unsupported sage map envelope" in message:
        return "unsupported-envelope"
    return "malformed-wire"


def canonical_sage_scb_semantic_sha256(native: Mapping[str, Any]) -> str:
    """Hash only the ordered semantic/name-table portion of a native SCB."""

    try:
        value = {
            "assetNames": native["assetNames"],
            "chunks": native["chunks"],
        }
    except (KeyError, TypeError) as exc:
        raise SageScbError(
            "invalid-native-schema", "native value lacks semantic fields"
        ) from exc
    return _canonical_sha256(value)


def convert_sage_scb_bytes(source: bytes) -> dict[str, Any]:
    """Convert one supported SCB source into ordered, JSON-safe semantics."""

    if not isinstance(source, bytes):
        raise SageScbError("invalid-source-type", "SCB source must be bytes")
    if len(source) > MAX_SOURCE_BYTES:
        raise SageScbError(
            "source-size-limit", f"SCB source exceeds {MAX_SOURCE_BYTES} bytes"
        )
    try:
        body, envelope = decode_sage_map_blob(source)
        cursor = _Cursor(body, label="SCB body")
        if cursor.bytes(4) != b"CkMp":
            raise SageScbError("invalid-magic", "decoded SCB lacks CkMp magic")
        names = _parse_name_table(cursor)
        budget = {"nodes": 0}
        chunks = [
            _parse_top_level_record(record, names, budget=budget)
            for record in _records(
                cursor,
                names,
                cap=MAX_TOP_LEVEL_RECORDS,
                label="ScbFile",
            )
        ]
        cursor.finish()
    except SageScbError:
        raise
    except SageMapError as exc:
        raise SageScbError(_map_error_code(exc), "SCB wire parsing failed") from exc

    present = {str(chunk["name"]) for chunk in chunks}
    missing = sorted(_REQUIRED_TOP_LEVEL_CHUNKS - present)
    if missing:
        raise SageScbError(
            "missing-required-chunk",
            f"SCB is missing {len(missing)} required chunk type(s)",
        )

    native: dict[str, Any] = {
        "schema": SCB_NATIVE_SCHEMA,
        "schemaVersion": SCB_NATIVE_SCHEMA_VERSION,
        "source": {
            "sourceBytes": len(source),
            "sourceSha256": _sha256(source),
            "decodedBodyBytes": len(body),
            "decodedBodySha256": _sha256(body),
            "envelopeKind": envelope["kind"],
        },
        "assetNames": [{"index": index, "name": name} for index, name in names.items()],
        "chunks": chunks,
    }
    native["semanticSha256"] = canonical_sage_scb_semantic_sha256(native)

    reconstructed = reconstruct_sage_scb_body(native)
    if reconstructed != body:
        raise SageScbError(
            "converter-roundtrip-mismatch",
            "native semantics do not reconstruct the decoded source",
        )
    return native


class _DuplicateJsonKey(ValueError):
    pass


def _object_without_duplicate_keys(
    pairs: list[tuple[str, object]],
) -> dict[str, object]:
    result: dict[str, object] = {}
    for key, value in pairs:
        if key in result:
            raise _DuplicateJsonKey(key)
        result[key] = value
    return result


def _reject_json_constant(value: str) -> object:
    raise ValueError(value)


def _coerce_native(value: Mapping[str, Any] | str | bytes) -> Mapping[str, Any]:
    if isinstance(value, Mapping):
        return value
    if isinstance(value, bytes):
        try:
            value = value.decode("utf-8")
        except UnicodeDecodeError as exc:
            raise SageScbError(
                "invalid-native-json", "native JSON is not UTF-8"
            ) from exc
    if not isinstance(value, str):
        raise SageScbError(
            "invalid-native-type", "native SCB must be a mapping or JSON text"
        )
    try:
        parsed = json.loads(
            value,
            object_pairs_hook=_object_without_duplicate_keys,
            parse_constant=_reject_json_constant,
        )
    except (json.JSONDecodeError, UnicodeError, ValueError) as exc:
        raise SageScbError("invalid-native-json", "native JSON is malformed") from exc
    if not isinstance(parsed, Mapping):
        raise SageScbError("invalid-native-schema", "native JSON root is not an object")
    return parsed


def _mapping(value: object, *, label: str) -> Mapping[str, Any]:
    if not isinstance(value, Mapping):
        raise SageScbError("invalid-native-schema", f"{label} is not an object")
    if not all(isinstance(key, str) for key in value):
        raise SageScbError("invalid-native-schema", f"{label} has a non-string key")
    return value


def _sequence(value: object, *, label: str) -> Sequence[Any]:
    if not isinstance(value, Sequence) or isinstance(value, (str, bytes, bytearray)):
        raise SageScbError("invalid-native-schema", f"{label} is not an array")
    return value


def _keys(value: Mapping[str, Any], expected: frozenset[str], *, label: str) -> None:
    actual = frozenset(value)
    if actual != expected:
        raise SageScbError(
            "invalid-native-schema", f"{label} fields do not match the schema"
        )


def _integer(value: object, *, label: str, minimum: int, maximum: int) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise SageScbError("invalid-native-schema", f"{label} is not an integer")
    if value < minimum or value > maximum:
        raise SageScbError("invalid-native-schema", f"{label} is out of range")
    return value


def _boolean(value: object, *, label: str) -> bool:
    if not isinstance(value, bool):
        raise SageScbError("invalid-native-schema", f"{label} is not a boolean")
    return value


def _floating(value: object, *, label: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise SageScbError("invalid-native-schema", f"{label} is not numeric")
    result = float(value)
    if not math.isfinite(result):
        raise SageScbError("invalid-native-schema", f"{label} is non-finite")
    try:
        packed = struct.pack("<f", result)
    except OverflowError as exc:
        raise SageScbError("invalid-native-schema", f"{label} exceeds f32") from exc
    restored = struct.unpack("<f", packed)[0]
    if restored != result and not (restored == 0.0 and result == 0.0):
        raise SageScbError(
            "invalid-native-schema", f"{label} is not an exact source f32 value"
        )
    return result


def _string(value: object, *, label: str) -> str:
    if not isinstance(value, str):
        raise SageScbError("invalid-native-schema", f"{label} is not a string")
    return value


def _hash(value: object, *, label: str) -> str:
    result = _string(value, label=label)
    if len(result) != 64:
        raise SageScbError("invalid-native-schema", f"{label} is not SHA-256")
    try:
        bytes.fromhex(result)
    except ValueError as exc:
        raise SageScbError("invalid-native-schema", f"{label} is not SHA-256") from exc
    if result != result.lower():
        raise SageScbError("invalid-native-schema", f"{label} is not canonical")
    return result


def _u16(value: int) -> bytes:
    return struct.pack("<H", value)


def _u32(value: int) -> bytes:
    return struct.pack("<I", value)


def _i32(value: int) -> bytes:
    return struct.pack("<i", value)


def _f32(value: float) -> bytes:
    return struct.pack("<f", value)


def _ascii16(value: object, *, label: str) -> bytes:
    text = _string(value, label=label)
    try:
        payload = text.encode("cp1252")
    except UnicodeEncodeError as exc:
        raise SageScbError("invalid-native-schema", f"{label} is not CP1252") from exc
    if len(payload) > 0xFFFF:
        raise SageScbError("invalid-native-schema", f"{label} exceeds uint16")
    return _u16(len(payload)) + payload


def _unicode16(value: object, *, label: str) -> bytes:
    text = _string(value, label=label)
    try:
        payload = text.encode("utf-16-le")
    except UnicodeEncodeError as exc:
        raise SageScbError("invalid-native-schema", f"{label} is not UTF-16") from exc
    units = len(payload) // 2
    if units > 0xFFFF:
        raise SageScbError("invalid-native-schema", f"{label} exceeds uint16")
    return _u16(units) + payload


def _dotnet_string(value: object, *, label: str) -> bytes:
    text = _string(value, label=label)
    try:
        payload = text.encode("utf-8")
    except UnicodeEncodeError as exc:
        raise SageScbError("invalid-native-schema", f"{label} is not UTF-8") from exc
    if len(payload) > 0x0FFFFFFF:
        raise SageScbError("invalid-native-schema", f"{label} is oversized")
    length = len(payload)
    prefix = bytearray()
    while length >= 0x80:
        prefix.append((length & 0x7F) | 0x80)
        length >>= 7
    prefix.append(length)
    return bytes(prefix) + payload


def _pack_property_key(
    value: object, name_to_index: Mapping[str, int], *, label: str
) -> bytes:
    item = _mapping(value, label=label)
    _keys(item, frozenset({"name", "wireTypeCode"}), label=label)
    name = _string(item["name"], label=f"{label}.name")
    wire_type = _integer(
        item["wireTypeCode"], label=f"{label}.wireTypeCode", minimum=0, maximum=5
    )
    try:
        index = name_to_index[name]
    except KeyError as exc:
        raise SageScbError(
            "invalid-native-schema", f"{label} name is absent from assetNames"
        ) from exc
    if index > 0xFFFFFF:
        raise SageScbError(
            "invalid-native-schema", f"{label} asset-name index exceeds uint24"
        )
    return bytes((wire_type,)) + index.to_bytes(3, "little")


def _pack_property(
    value: object, name_to_index: Mapping[str, int], *, label: str
) -> bytes:
    item = _mapping(value, label=label)
    _keys(item, frozenset({"name", "wireTypeCode", "value"}), label=label)
    key = _pack_property_key(
        {"name": item["name"], "wireTypeCode": item["wireTypeCode"]},
        name_to_index,
        label=label,
    )
    wire_type = int(item["wireTypeCode"])
    raw_value = item["value"]
    if wire_type == 0:
        payload = bytes((_boolean(raw_value, label=f"{label}.value"),))
    elif wire_type == 1:
        payload = _i32(
            _integer(
                raw_value,
                label=f"{label}.value",
                minimum=-(2**31),
                maximum=2**31 - 1,
            )
        )
    elif wire_type == 2:
        payload = _f32(_floating(raw_value, label=f"{label}.value"))
    elif wire_type in (3, 5):
        payload = _ascii16(raw_value, label=f"{label}.value")
    elif wire_type == 4:
        payload = _unicode16(raw_value, label=f"{label}.value")
    else:  # pragma: no cover - guarded above
        raise AssertionError(wire_type)
    return key + payload


def _pack_properties(
    value: object, name_to_index: Mapping[str, int], *, label: str
) -> bytes:
    properties = _sequence(value, label=label)
    if len(properties) > MAX_PROPERTIES_PER_OBJECT:
        raise SageScbError("invalid-native-schema", f"{label} exceeds limit")
    return _u16(len(properties)) + b"".join(
        _pack_property(item, name_to_index, label=f"{label}[{index}]")
        for index, item in enumerate(properties)
    )


def _pack_argument(value: object, *, label: str) -> bytes:
    item = _mapping(value, label=label)
    argument_type = _integer(
        item.get("argumentType"),
        label=f"{label}.argumentType",
        minimum=0,
        maximum=0xFFFFFFFF,
    )
    if argument_type == _POSITION_ARGUMENT_TYPE:
        _keys(item, frozenset({"argumentType", "position"}), label=label)
        position = _sequence(item["position"], label=f"{label}.position")
        if len(position) != 3:
            raise SageScbError(
                "invalid-native-schema", f"{label}.position must have three values"
            )
        return _u32(argument_type) + b"".join(
            _f32(_floating(component, label=f"{label}.position[{index}]"))
            for index, component in enumerate(position)
        )
    _keys(
        item,
        frozenset({"argumentType", "integer", "real", "text"}),
        label=label,
    )
    return b"".join(
        (
            _u32(argument_type),
            _i32(
                _integer(
                    item["integer"],
                    label=f"{label}.integer",
                    minimum=-(2**31),
                    maximum=2**31 - 1,
                )
            ),
            _f32(_floating(item["real"], label=f"{label}.real")),
            _ascii16(item["text"], label=f"{label}.text"),
        )
    )


def _pack_script_content(
    value: object,
    name_to_index: Mapping[str, int],
    *,
    kind: str,
    version: int,
    label: str,
) -> bytes:
    item = _mapping(value, label=label)
    expected = {"contentType", "internalName", "arguments"}
    expected.update(_content_flag_keys(kind))
    _keys(item, frozenset(expected), label=label)
    arguments = _sequence(item["arguments"], label=f"{label}.arguments")
    if len(arguments) > MAX_SCRIPT_ARGUMENTS:
        raise SageScbError("invalid-native-schema", f"{label}.arguments exceeds limit")
    payload = bytearray(
        _u32(
            _integer(
                item["contentType"],
                label=f"{label}.contentType",
                minimum=0,
                maximum=0xFFFFFFFF,
            )
        )
    )
    payload.extend(
        _pack_property_key(
            item["internalName"], name_to_index, label=f"{label}.internalName"
        )
    )
    payload.extend(_u32(len(arguments)))
    for index, argument in enumerate(arguments):
        payload.extend(_pack_argument(argument, label=f"{label}.arguments[{index}]"))
    present = _content_flag_names(kind, version)
    for name in _content_flag_keys(kind):
        flag = _boolean(item[name], label=f"{label}.{name}")
        if name in present:
            payload.extend(_u32(flag))
        elif flag != _SCRIPT_CONTENT_FLAG_DEFAULTS[name]:
            # This version has no byte for the flag, so a non-default value
            # could not be written back; refuse rather than silently drop it.
            raise SageScbError(
                "invalid-native-schema",
                f"{label}.{name} must be "
                f"{_SCRIPT_CONTENT_FLAG_DEFAULTS[name]} at {kind} version {version}",
            )
    return bytes(payload)


def _pack_nested_record(
    value: object,
    name_to_index: Mapping[str, int],
    *,
    label: str,
    depth: int,
) -> bytes:
    if depth > MAX_SCRIPT_DEPTH:
        raise SageScbError("invalid-native-schema", "script nesting exceeds limit")
    item = _mapping(value, label=label)
    _keys(item, frozenset({"name", "version", "value"}), label=label)
    name = _string(item["name"], label=f"{label}.name")
    version = _integer(
        item["version"], label=f"{label}.version", minimum=0, maximum=0xFFFF
    )
    if name not in _NESTED_VERSIONS or version not in _NESTED_VERSIONS[name]:
        raise SageScbError(
            "unsupported-version", f"unsupported native {name} version {version}"
        )
    data = item["value"]
    if name == "ScriptList":
        payload = _pack_script_list(
            data, name_to_index, label=f"{label}.value", depth=depth
        )
    elif name == "ScriptGroup":
        payload = _pack_script_group(
            data, name_to_index, label=f"{label}.value", depth=depth
        )
    elif name == "Script":
        payload = _pack_script(
            data, name_to_index, version=version, label=f"{label}.value", depth=depth
        )
    elif name == "OrCondition":
        payload = _pack_or_condition(
            data, name_to_index, label=f"{label}.value", depth=depth
        )
    elif name == "Condition":
        payload = _pack_script_content(
            data,
            name_to_index,
            kind="condition",
            version=version,
            label=f"{label}.value",
        )
    elif name in ("ScriptAction", "ScriptActionFalse"):
        payload = _pack_script_content(
            data,
            name_to_index,
            kind="action",
            version=version,
            label=f"{label}.value",
        )
    else:  # pragma: no cover - exhaustive
        raise AssertionError(name)
    return _pack_record(name, version, payload, name_to_index, label=label)


def _pack_records(
    value: object,
    name_to_index: Mapping[str, int],
    *,
    label: str,
    allowed: frozenset[str],
    depth: int,
) -> bytes:
    records = _sequence(value, label=label)
    if len(records) > MAX_NESTED_RECORDS:
        raise SageScbError("invalid-native-schema", f"{label} exceeds limit")
    result = bytearray()
    for index, record in enumerate(records):
        item = _mapping(record, label=f"{label}[{index}]")
        name = item.get("name")
        if name not in allowed:
            raise SageScbError(
                "invalid-native-schema", f"{label}[{index}] has invalid record kind"
            )
        result.extend(
            _pack_nested_record(
                item,
                name_to_index,
                label=f"{label}[{index}]",
                depth=depth + 1,
            )
        )
    return bytes(result)


def _pack_script_list(
    value: object,
    name_to_index: Mapping[str, int],
    *,
    label: str,
    depth: int,
) -> bytes:
    item = _mapping(value, label=label)
    _keys(item, frozenset({"records"}), label=label)
    return _pack_records(
        item["records"],
        name_to_index,
        label=f"{label}.records",
        allowed=frozenset({"Script", "ScriptGroup"}),
        depth=depth,
    )


def _pack_script_group(
    value: object,
    name_to_index: Mapping[str, int],
    *,
    label: str,
    depth: int,
) -> bytes:
    item = _mapping(value, label=label)
    _keys(
        item,
        frozenset({"name", "isActive", "isSubroutine", "records"}),
        label=label,
    )
    return b"".join(
        (
            _ascii16(item["name"], label=f"{label}.name"),
            bytes((_boolean(item["isActive"], label=f"{label}.isActive"),)),
            bytes((_boolean(item["isSubroutine"], label=f"{label}.isSubroutine"),)),
            _pack_records(
                item["records"],
                name_to_index,
                label=f"{label}.records",
                allowed=frozenset({"Script", "ScriptGroup"}),
                depth=depth,
            ),
        )
    )


def _pack_script(
    value: object,
    name_to_index: Mapping[str, int],
    *,
    version: int,
    label: str,
    depth: int,
) -> bytes:
    item = _mapping(value, label=label)
    expected = frozenset(
        {
            "name",
            "comment",
            "conditionsComment",
            "actionsComment",
            "isActive",
            "deactivateUponSuccess",
            "activeInEasy",
            "activeInMedium",
            "activeInHard",
            "isSubroutine",
            "evaluationInterval",
            "actionsFireSequentially",
            "loopActions",
            "loopCount",
            "sequentialTargetType",
            "sequentialTargetName",
            "scope",
            "records",
        }
    )
    _keys(item, expected, label=label)
    target_type = _integer(
        item["sequentialTargetType"],
        label=f"{label}.sequentialTargetType",
        minimum=0,
        maximum=0xFF,
    )
    if target_type not in _SEQUENTIAL_TARGET_TYPES:
        raise SageScbError(
            "invalid-native-schema", f"{label}.sequentialTargetType is unsupported"
        )
    scope = _string(item["scope"], label=f"{label}.scope")
    if scope not in _SCRIPT_SCOPES:
        raise SageScbError("invalid-native-schema", f"{label}.scope is unsupported")
    has_tail = _script_has_tail(version)
    if not has_tail:
        # This version has no bytes for the tail, so a non-default value could
        # not be written back; refuse rather than silently drop it.
        for key, default in _SCRIPT_TAIL_DEFAULTS.items():
            actual = item[key]
            if type(actual) is not type(default) or actual != default:
                raise SageScbError(
                    "invalid-native-schema",
                    f"{label}.{key} must be {default!r} at Script version {version}",
                )
    result = bytearray()
    for key in ("name", "comment", "conditionsComment", "actionsComment"):
        result.extend(_ascii16(item[key], label=f"{label}.{key}"))
    for key in (
        "isActive",
        "deactivateUponSuccess",
        "activeInEasy",
        "activeInMedium",
        "activeInHard",
        "isSubroutine",
    ):
        result.append(_boolean(item[key], label=f"{label}.{key}"))
    result.extend(
        _u32(
            _integer(
                item["evaluationInterval"],
                label=f"{label}.evaluationInterval",
                minimum=0,
                maximum=0xFFFFFFFF,
            )
        )
    )
    if has_tail:
        result.append(
            _boolean(
                item["actionsFireSequentially"],
                label=f"{label}.actionsFireSequentially",
            )
        )
        result.append(_boolean(item["loopActions"], label=f"{label}.loopActions"))
        result.extend(
            _i32(
                _integer(
                    item["loopCount"],
                    label=f"{label}.loopCount",
                    minimum=-(2**31),
                    maximum=2**31 - 1,
                )
            )
        )
        result.append(target_type)
        result.extend(
            _ascii16(
                item["sequentialTargetName"], label=f"{label}.sequentialTargetName"
            )
        )
        result.extend(_ascii16(scope, label=f"{label}.scope"))
    result.extend(
        _pack_records(
            item["records"],
            name_to_index,
            label=f"{label}.records",
            allowed=frozenset({"OrCondition", "ScriptAction", "ScriptActionFalse"}),
            depth=depth,
        )
    )
    return bytes(result)


def _pack_or_condition(
    value: object,
    name_to_index: Mapping[str, int],
    *,
    label: str,
    depth: int,
) -> bytes:
    item = _mapping(value, label=label)
    _keys(item, frozenset({"records"}), label=label)
    return _pack_records(
        item["records"],
        name_to_index,
        label=f"{label}.records",
        allowed=frozenset({"Condition"}),
        depth=depth,
    )


def _pack_record(
    name: str,
    version: int,
    payload: bytes,
    name_to_index: Mapping[str, int],
    *,
    label: str,
) -> bytes:
    try:
        index = name_to_index[name]
    except KeyError as exc:
        raise SageScbError(
            "invalid-native-schema", f"{label} name is absent from assetNames"
        ) from exc
    if len(payload) > 0xFFFFFFFF:
        raise SageScbError("invalid-native-schema", f"{label} payload is oversized")
    return _u32(index) + _u16(version) + _u32(len(payload)) + payload


def _pack_count_only_empty(value: object, *, label: str) -> bytes:
    item = _mapping(value, label=label)
    _keys(item, frozenset({"count", "status"}), label=label)
    if _integer(item["count"], label=f"{label}.count", minimum=0, maximum=0) != 0:
        raise SageScbError("invalid-native-schema", f"{label}.count is nonzero")
    if item["status"] != "source-authored-empty":
        raise SageScbError("invalid-native-schema", f"{label}.status is invalid")
    return _u32(0)


def _pack_named_cameras(value: object, *, label: str) -> bytes:
    item = _mapping(value, label=label)
    _keys(item, frozenset({"cameras"}), label=label)
    cameras = _sequence(item["cameras"], label=f"{label}.cameras")
    if len(cameras) > MAX_NAMED_CAMERAS:
        raise SageScbError("invalid-native-schema", f"{label}.cameras exceeds limit")
    result = bytearray(_u32(len(cameras)))
    expected = frozenset(
        {
            "lookAtPoint",
            "name",
            "pitch",
            "roll",
            "yaw",
            "zoom",
            "fieldOfView",
            "unknown",
        }
    )
    for index, camera in enumerate(cameras):
        camera_label = f"{label}.cameras[{index}]"
        record = _mapping(camera, label=camera_label)
        _keys(record, expected, label=camera_label)
        point = _sequence(record["lookAtPoint"], label=f"{camera_label}.lookAtPoint")
        if len(point) != 3:
            raise SageScbError(
                "invalid-native-schema", f"{camera_label}.lookAtPoint is not a vector3"
            )
        for component_index, component in enumerate(point):
            result.extend(
                _f32(
                    _floating(
                        component,
                        label=f"{camera_label}.lookAtPoint[{component_index}]",
                    )
                )
            )
        result.extend(_ascii16(record["name"], label=f"{camera_label}.name"))
        for key in ("pitch", "roll", "yaw", "zoom", "fieldOfView", "unknown"):
            result.extend(_f32(_floating(record[key], label=f"{camera_label}.{key}")))
    return bytes(result)


def _pack_scripts_players(
    value: object, name_to_index: Mapping[str, int], *, label: str
) -> bytes:
    item = _mapping(value, label=label)
    _keys(item, frozenset({"hasPlayerProperties", "players"}), label=label)
    has_properties = _boolean(
        item["hasPlayerProperties"], label=f"{label}.hasPlayerProperties"
    )
    players = _sequence(item["players"], label=f"{label}.players")
    if len(players) > MAX_SCRIPT_PLAYERS:
        raise SageScbError("invalid-native-schema", f"{label}.players exceeds limit")
    result = bytearray(_u32(has_properties) + _u32(len(players)))
    for index, player in enumerate(players):
        player_label = f"{label}.players[{index}]"
        record = _mapping(player, label=player_label)
        expected = {"name", "properties"} if has_properties else {"name"}
        _keys(record, frozenset(expected), label=player_label)
        result.extend(_ascii16(record["name"], label=f"{player_label}.name"))
        if has_properties:
            result.extend(
                _pack_properties(
                    record["properties"],
                    name_to_index,
                    label=f"{player_label}.properties",
                )
            )
    return bytes(result)


def _pack_script_teams(
    value: object, name_to_index: Mapping[str, int], *, label: str
) -> bytes:
    item = _mapping(value, label=label)
    _keys(item, frozenset({"teams"}), label=label)
    teams = _sequence(item["teams"], label=f"{label}.teams")
    if len(teams) > MAX_SCRIPT_TEAMS:
        raise SageScbError("invalid-native-schema", f"{label}.teams exceeds limit")
    result = bytearray()
    for index, team in enumerate(teams):
        team_label = f"{label}.teams[{index}]"
        record = _mapping(team, label=team_label)
        _keys(record, frozenset({"properties"}), label=team_label)
        result.extend(
            _pack_properties(
                record["properties"],
                name_to_index,
                label=f"{team_label}.properties",
            )
        )
    return bytes(result)


def _pack_top_level_record(
    value: object, name_to_index: Mapping[str, int], *, label: str
) -> bytes:
    item = _mapping(value, label=label)
    _keys(item, frozenset({"name", "version", "value"}), label=label)
    name = _string(item["name"], label=f"{label}.name")
    version = _integer(
        item["version"], label=f"{label}.version", minimum=0, maximum=0xFFFF
    )
    if name not in _TOP_LEVEL_VERSIONS or version not in _TOP_LEVEL_VERSIONS[name]:
        raise SageScbError(
            "unsupported-version", f"unsupported native {name} version {version}"
        )
    data = item["value"]
    if name == "ScriptImportSize":
        record = _mapping(data, label=f"{label}.value")
        _keys(record, frozenset({"unknown1", "unknown2"}), label=f"{label}.value")
        payload = _u32(
            _integer(
                record["unknown1"],
                label=f"{label}.value.unknown1",
                minimum=0,
                maximum=0xFFFFFFFF,
            )
        ) + _u32(
            _integer(
                record["unknown2"],
                label=f"{label}.value.unknown2",
                minimum=0,
                maximum=0xFFFFFFFF,
            )
        )
    elif name == "PlayerScriptsList":
        record = _mapping(data, label=f"{label}.value")
        _keys(record, frozenset({"records"}), label=f"{label}.value")
        payload = _pack_records(
            record["records"],
            name_to_index,
            label=f"{label}.value.records",
            allowed=frozenset({"ScriptList"}),
            depth=0,
        )
    elif name == "NamedCameras":
        payload = _pack_named_cameras(data, label=f"{label}.value")
    elif name in ("CameraAnimationList", "TriggerAreas", "WaypointsList"):
        payload = _pack_count_only_empty(data, label=f"{label}.value")
    elif name == "ScriptsPlayers":
        payload = _pack_scripts_players(data, name_to_index, label=f"{label}.value")
    elif name == "ObjectsList":
        record = _mapping(data, label=f"{label}.value")
        _keys(record, frozenset({"count", "status"}), label=f"{label}.value")
        if record["count"] != 0 or record["status"] != "source-authored-empty":
            raise SageScbError("invalid-native-schema", f"{label}.value is not empty")
        payload = b""
    elif name == "ScriptTeams":
        payload = _pack_script_teams(data, name_to_index, label=f"{label}.value")
    else:  # pragma: no cover - exhaustive
        raise AssertionError(name)
    return _pack_record(name, version, payload, name_to_index, label=label)


def reconstruct_sage_scb_body(
    native: Mapping[str, Any] | str | bytes,
) -> bytes:
    """Strictly validate native JSON and reconstruct its decoded CkMp body."""

    root = _coerce_native(native)
    _keys(
        root,
        frozenset(
            {
                "schema",
                "schemaVersion",
                "source",
                "assetNames",
                "chunks",
                "semanticSha256",
            }
        ),
        label="native",
    )
    if root["schema"] != SCB_NATIVE_SCHEMA:
        raise SageScbError("invalid-native-schema", "native schema is unsupported")
    if root["schemaVersion"] != SCB_NATIVE_SCHEMA_VERSION:
        raise SageScbError(
            "invalid-native-schema", "native schema version is unsupported"
        )
    source = _mapping(root["source"], label="native.source")
    _keys(
        source,
        frozenset(
            {
                "sourceBytes",
                "sourceSha256",
                "decodedBodyBytes",
                "decodedBodySha256",
                "envelopeKind",
            }
        ),
        label="native.source",
    )
    _integer(
        source["sourceBytes"],
        label="native.source.sourceBytes",
        minimum=0,
        maximum=MAX_SOURCE_BYTES,
    )
    _hash(source["sourceSha256"], label="native.source.sourceSha256")
    decoded_size = _integer(
        source["decodedBodyBytes"],
        label="native.source.decodedBodyBytes",
        minimum=0,
        maximum=MAX_SOURCE_BYTES * 4,
    )
    decoded_sha = _hash(
        source["decodedBodySha256"], label="native.source.decodedBodySha256"
    )
    if source["envelopeKind"] not in ("uncompressed", "ear-refpack"):
        raise SageScbError(
            "invalid-native-schema", "native.source.envelopeKind is unsupported"
        )
    semantic_sha = _hash(root["semanticSha256"], label="native.semanticSha256")
    actual_semantic_sha = canonical_sage_scb_semantic_sha256(root)
    if semantic_sha != actual_semantic_sha:
        raise SageScbError(
            "semantic-hash-mismatch", "native semantic hash does not match its data"
        )

    asset_names = _sequence(root["assetNames"], label="native.assetNames")
    if len(asset_names) > 0xFFFFFF:
        raise SageScbError("invalid-native-schema", "assetNames exceeds uint24")
    body = bytearray(b"CkMp" + _u32(len(asset_names)))
    name_to_index: dict[str, int] = {}
    expected_index = len(asset_names)
    for ordinal, raw_entry in enumerate(asset_names):
        label = f"native.assetNames[{ordinal}]"
        entry = _mapping(raw_entry, label=label)
        _keys(entry, frozenset({"index", "name"}), label=label)
        index = _integer(
            entry["index"], label=f"{label}.index", minimum=1, maximum=0xFFFFFF
        )
        if index != expected_index:
            raise SageScbError(
                "invalid-native-schema", "assetNames indices are not descending"
            )
        expected_index -= 1
        name = _string(entry["name"], label=f"{label}.name")
        if name in name_to_index:
            raise SageScbError(
                "invalid-native-schema", "assetNames contains a duplicate name"
            )
        name_to_index[name] = index
        body.extend(_dotnet_string(name, label=f"{label}.name"))
        body.extend(_u32(index))

    chunks = _sequence(root["chunks"], label="native.chunks")
    if len(chunks) > MAX_TOP_LEVEL_RECORDS:
        raise SageScbError("invalid-native-schema", "native.chunks exceeds limit")
    present: set[str] = set()
    for index, chunk in enumerate(chunks):
        mapped = _mapping(chunk, label=f"native.chunks[{index}]")
        name = mapped.get("name")
        if isinstance(name, str):
            present.add(name)
        body.extend(
            _pack_top_level_record(
                mapped, name_to_index, label=f"native.chunks[{index}]"
            )
        )
    if _REQUIRED_TOP_LEVEL_CHUNKS - present:
        raise SageScbError(
            "invalid-native-schema", "native is missing a required top-level chunk"
        )
    result = bytes(body)
    if len(result) != decoded_size or _sha256(result) != decoded_sha:
        raise SageScbError(
            "native-body-hash-mismatch",
            "reconstructed body disagrees with native source evidence",
        )
    return result


def backtest_sage_scb_native(
    source: bytes, native: Mapping[str, Any] | str | bytes
) -> SageScbBacktestReport:
    """Independently reconstruct and byte-compare a native SCB conversion."""

    if not isinstance(source, bytes):
        raise SageScbError("invalid-source-type", "SCB source must be bytes")
    root = _coerce_native(native)
    source_evidence = _mapping(root.get("source"), label="native.source")
    try:
        body, envelope = decode_sage_map_blob(source)
    except SageMapError as exc:
        raise SageScbError(
            _map_error_code(exc), "source backtest decode failed"
        ) from exc
    source_sha = _sha256(source)
    body_sha = _sha256(body)
    expected_source = {
        "sourceBytes": len(source),
        "sourceSha256": source_sha,
        "decodedBodyBytes": len(body),
        "decodedBodySha256": body_sha,
        "envelopeKind": envelope["kind"],
    }
    if dict(source_evidence) != expected_source:
        raise SageScbError(
            "source-evidence-mismatch", "native source evidence is not exact"
        )
    reconstructed = reconstruct_sage_scb_body(root)
    if reconstructed != body:
        raise SageScbError(
            "wire-mismatch", "native reconstruction differs from decoded source"
        )
    semantic_sha = _hash(root["semanticSha256"], label="native.semanticSha256")
    chunks = _sequence(root["chunks"], label="native.chunks")
    evidence = {
        "schema": SCB_BACKTEST_SCHEMA,
        "schemaVersion": SCB_BACKTEST_SCHEMA_VERSION,
        "sourceBytes": len(source),
        "decodedBodyBytes": len(body),
        "chunkCount": len(chunks),
        "sourceSha256": source_sha,
        "decodedBodySha256": body_sha,
        "reconstructedBodySha256": _sha256(reconstructed),
        "semanticSha256": semantic_sha,
        "exactWireMatch": True,
    }
    return SageScbBacktestReport(
        source_bytes=len(source),
        decoded_body_bytes=len(body),
        chunk_count=len(chunks),
        source_sha256=source_sha,
        decoded_body_sha256=body_sha,
        reconstructed_body_sha256=_sha256(reconstructed),
        semantic_sha256=semantic_sha,
        evidence_sha256=_canonical_sha256(evidence),
    )


def _child_records(record: Mapping[str, Any]) -> Sequence[Any]:
    name = record.get("name")
    value = record.get("value")
    if name in (
        "PlayerScriptsList",
        "ScriptList",
        "ScriptGroup",
        "Script",
        "OrCondition",
    ):
        mapped = _mapping(value, label="diagnostic record value")
        return _sequence(mapped["records"], label="diagnostic records")
    return ()


def _record_layout(record: Mapping[str, Any]) -> dict[str, Any]:
    return {
        "name": record["name"],
        "version": record["version"],
        "records": [
            _record_layout(_mapping(child, label="diagnostic child"))
            for child in _child_records(record)
        ],
    }


def _accumulate_native_facts(
    native: Mapping[str, Any],
    *,
    versions: Counter[tuple[str, int]],
    semantics: Counter[str],
    layouts: Counter[str],
) -> None:
    chunks = _sequence(native["chunks"], label="diagnostic chunks")
    layouts[
        _canonical_sha256([_record_layout(_mapping(x, label="chunk")) for x in chunks])
    ] += 1

    def visit(raw_record: object) -> None:
        record = _mapping(raw_record, label="diagnostic record")
        name = str(record["name"])
        version = int(record["version"])
        versions[(name, version)] += 1
        semantics["records"] += 1
        value = _mapping(record["value"], label="diagnostic value")
        if name == "ScriptList":
            semantics["scriptLists"] += 1
        elif name == "ScriptGroup":
            semantics["scriptGroups"] += 1
        elif name == "Script":
            semantics["scripts"] += 1
        elif name == "OrCondition":
            semantics["orConditions"] += 1
        elif name == "Condition":
            semantics["conditions"] += 1
            semantics["arguments"] += len(
                _sequence(value["arguments"], label="condition arguments")
            )
        elif name == "ScriptAction":
            semantics["trueActions"] += 1
            semantics["arguments"] += len(
                _sequence(value["arguments"], label="action arguments")
            )
        elif name == "ScriptActionFalse":
            semantics["falseActions"] += 1
            semantics["arguments"] += len(
                _sequence(value["arguments"], label="action arguments")
            )
        elif name == "NamedCameras":
            semantics["namedCameras"] += len(
                _sequence(value["cameras"], label="named cameras")
            )
        elif name == "ScriptsPlayers":
            players = _sequence(value["players"], label="script players")
            semantics["scriptPlayers"] += len(players)
            for player in players:
                mapped = _mapping(player, label="script player")
                if "properties" in mapped:
                    semantics["properties"] += len(
                        _sequence(mapped["properties"], label="player properties")
                    )
        elif name == "ScriptTeams":
            teams = _sequence(value["teams"], label="script teams")
            semantics["scriptTeams"] += len(teams)
            for team in teams:
                mapped = _mapping(team, label="script team")
                semantics["properties"] += len(
                    _sequence(mapped["properties"], label="team properties")
                )
        for child in _child_records(record):
            visit(child)

    for chunk in chunks:
        visit(chunk)


def diagnose_sage_scb_sources(sources: Iterable[bytes]) -> dict[str, Any]:
    """Return path-, identifier-, and payload-free aggregate SCB evidence."""

    source_list = list(sources)
    if len(source_list) > MAX_SCB_SOURCES:
        raise SageScbError(
            "source-count-limit", f"SCB source count exceeds {MAX_SCB_SOURCES}"
        )
    source_members: list[dict[str, Any]] = []
    native_members: list[str] = []
    backtest_members: list[str] = []
    rejected_members: list[dict[str, Any]] = []
    rejection_reasons: Counter[str] = Counter()
    envelope_kinds: Counter[str] = Counter()
    versions: Counter[tuple[str, int]] = Counter()
    semantics: Counter[str] = Counter()
    layouts: Counter[str] = Counter()
    total_bytes = 0
    accepted = 0

    for source in source_list:
        if not isinstance(source, bytes):
            raise SageScbError("invalid-source-type", "SCB source must be bytes")
        source_sha = _sha256(source)
        total_bytes += len(source)
        source_members.append({"bytes": len(source), "sha256": source_sha})
        try:
            native = convert_sage_scb_bytes(source)
            backtest = backtest_sage_scb_native(source, native)
        except SageScbError as exc:
            rejection_reasons[exc.code] += 1
            rejected_members.append(
                {"bytes": len(source), "sha256": source_sha, "reason": exc.code}
            )
            continue
        accepted += 1
        native_members.append(_canonical_sha256(native))
        backtest_members.append(backtest.evidence_sha256)
        envelope_kinds[str(native["source"]["envelopeKind"])] += 1
        _accumulate_native_facts(
            native, versions=versions, semantics=semantics, layouts=layouts
        )

    report: dict[str, Any] = {
        "schema": SCB_DIAGNOSTIC_SCHEMA,
        "schemaVersion": SCB_DIAGNOSTIC_SCHEMA_VERSION,
        "sourceCount": len(source_list),
        "sourceBytes": total_bytes,
        "acceptedCount": accepted,
        "rejectedCount": len(source_list) - accepted,
        "rejectionReasons": [
            {"reason": reason, "sourceCount": count}
            for reason, count in sorted(rejection_reasons.items())
        ],
        "envelopeKinds": [
            {"kind": kind, "sourceCount": count}
            for kind, count in sorted(envelope_kinds.items())
        ],
        "recordVersions": [
            {"name": name, "version": version, "occurrences": count}
            for (name, version), count in sorted(versions.items())
        ],
        "layoutCounts": [
            {"layoutSha256": digest, "sourceCount": count}
            for digest, count in sorted(layouts.items())
        ],
        "semanticCounts": {
            key: semantics[key]
            for key in (
                "records",
                "scriptLists",
                "scriptGroups",
                "scripts",
                "orConditions",
                "conditions",
                "trueActions",
                "falseActions",
                "arguments",
                "scriptPlayers",
                "scriptTeams",
                "properties",
                "namedCameras",
            )
        },
        "canonicalHashes": {
            "sourceSetSha256": _canonical_sha256(
                {
                    "schema": "openbfme.sage-scb-source-set",
                    "members": sorted(
                        source_members, key=lambda item: (item["sha256"], item["bytes"])
                    ),
                }
            ),
            "acceptedNativeSetSha256": _canonical_sha256(
                {
                    "schema": "openbfme.sage-scb-native-set",
                    "members": sorted(native_members),
                }
            ),
            "backtestSetSha256": _canonical_sha256(
                {
                    "schema": "openbfme.sage-scb-backtest-set",
                    "members": sorted(backtest_members),
                }
            ),
            "rejectedSetSha256": _canonical_sha256(
                {
                    "schema": "openbfme.sage-scb-rejected-set",
                    "members": sorted(
                        rejected_members,
                        key=lambda item: (
                            item["sha256"],
                            item["bytes"],
                            item["reason"],
                        ),
                    ),
                }
            ),
        },
    }
    report["reportIdentitySha256"] = _canonical_sha256(report)
    return report


def write_sage_scb_diagnostic(
    source_paths: Iterable[Path | str], output_path: Path | str
) -> dict[str, Any]:
    """Read SCBs and atomically write only aggregate diagnostic evidence."""

    paths = sorted(
        (Path(path) for path in source_paths), key=lambda path: str(path).casefold()
    )
    sources: list[bytes] = []
    for path in paths:
        if path.is_symlink() or not path.is_file():
            raise SageScbError(
                "unsafe-source-path", "SCB diagnostic source is not a regular file"
            )
        try:
            sources.append(path.read_bytes())
        except OSError as exc:
            raise SageScbError(
                "source-read-failed", "SCB diagnostic source could not be read"
            ) from exc
    report = diagnose_sage_scb_sources(sources)
    write_json_atomic(Path(output_path), report)
    return report
