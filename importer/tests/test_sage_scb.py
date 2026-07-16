from __future__ import annotations

from copy import deepcopy
import hashlib
import json
import struct

import pytest

from openbfme_importer.sage_scb import (
    SCB_BACKTEST_SCHEMA,
    SCB_DIAGNOSTIC_SCHEMA,
    SCB_NATIVE_SCHEMA,
    SageScbError,
    backtest_sage_scb_native,
    canonical_sage_scb_semantic_sha256,
    convert_sage_scb_bytes,
    diagnose_sage_scb_sources,
    reconstruct_sage_scb_body,
)


_NAMES = (
    "ScriptImportSize",
    "PlayerScriptsList",
    "NamedCameras",
    "CameraAnimationList",
    "ScriptsPlayers",
    "ObjectsList",
    "TriggerAreas",
    "ScriptTeams",
    "WaypointsList",
    "ScriptList",
    "ScriptGroup",
    "Script",
    "OrCondition",
    "Condition",
    "ScriptAction",
    "ScriptActionFalse",
    "internalKey",
    "teamName",
)


def _u16(value: int) -> bytes:
    return struct.pack("<H", value)


def _u32(value: int) -> bytes:
    return struct.pack("<I", value)


def _i32(value: int) -> bytes:
    return struct.pack("<i", value)


def _f32(value: float) -> bytes:
    return struct.pack("<f", value)


def _ascii(value: str) -> bytes:
    payload = value.encode("cp1252")
    return _u16(len(payload)) + payload


def _dotnet(value: str) -> bytes:
    payload = value.encode("utf-8")
    length = len(payload)
    prefix = bytearray()
    while length >= 0x80:
        prefix.append((length & 0x7F) | 0x80)
        length >>= 7
    prefix.append(length)
    return bytes(prefix) + payload


def _indices() -> dict[str, int]:
    count = len(_NAMES)
    return {name: count - index for index, name in enumerate(_NAMES)}


def _u24(value: int) -> bytes:
    return value.to_bytes(3, "little")


def _record(
    name: str,
    version: int,
    payload: bytes,
    indices: dict[str, int],
) -> bytes:
    return _u32(indices[name]) + _u16(version) + _u32(len(payload)) + payload


def _property_key(name: str, indices: dict[str, int]) -> bytes:
    return b"\x03" + _u24(indices[name])


def _argument(text: str) -> bytes:
    return _u32(10) + _i32(7) + _f32(1.25) + _ascii(text)


def _condition(indices: dict[str, int]) -> bytes:
    payload = b"".join(
        (
            _u32(101),
            _property_key("internalKey", indices),
            _u32(1),
            _argument("condition-argument"),
            _u32(1),
            _u32(0),
        )
    )
    return _record("Condition", 6, payload, indices)


def _action(name: str, content_type: int, text: str, indices: dict[str, int]) -> bytes:
    payload = b"".join(
        (
            _u32(content_type),
            _property_key("internalKey", indices),
            _u32(1),
            _argument(text),
            _u32(1),
        )
    )
    return _record(name, 3, payload, indices)


def _script(name: str, indices: dict[str, int]) -> bytes:
    nested = b"".join(
        (
            _record("OrCondition", 1, _condition(indices), indices),
            _action("ScriptAction", 201, "true-argument", indices),
            _action("ScriptActionFalse", 202, "false-argument", indices),
        )
    )
    payload = b"".join(
        (
            _ascii(name),
            _ascii("fixture comment"),
            _ascii("fixture conditions"),
            _ascii("fixture actions"),
            bytes((1, 0, 1, 1, 1, 0)),
            _u32(3),
            bytes((1, 0)),
            _i32(2),
            b"\x00",
            _ascii("fixture-team"),
            _ascii("ALL"),
            nested,
        )
    )
    return _record("Script", 4, payload, indices)


def _script_list(indices: dict[str, int]) -> bytes:
    group_payload = (
        _ascii("fixture-group") + b"\x01\x00" + _script("duplicate-script", indices)
    )
    payload = b"".join(
        (
            _record("ScriptGroup", 3, group_payload, indices),
            _script("duplicate-script", indices),
        )
    )
    return _record("ScriptList", 1, payload, indices)


def _team_properties(indices: dict[str, int], *, duplicate_property: bool) -> bytes:
    values = (
        ["duplicate-team", "duplicate-team"]
        if duplicate_property
        else ["duplicate-team"]
    )
    return _u16(len(values)) + b"".join(
        _property_key("teamName", indices) + _ascii(value) for value in values
    )


def _fixture(
    *,
    version_overrides: dict[str, int] | None = None,
    camera_animation_count: int = 0,
) -> bytes:
    indices = _indices()
    overrides = version_overrides or {}
    name_table = _u32(len(_NAMES)) + b"".join(
        _dotnet(name) + _u32(indices[name]) for name in _NAMES
    )
    named_camera = b"".join(
        (
            _u32(1),
            _f32(1.0),
            _f32(2.0),
            _f32(3.0),
            _ascii("fixture-camera"),
            _f32(4.0),
            _f32(5.0),
            _f32(6.0),
            _f32(7.0),
            _f32(8.0),
            _f32(9.0),
        )
    )
    chunks = (
        ("ScriptImportSize", _u32(11) + _u32(22)),
        ("PlayerScriptsList", _script_list(indices)),
        ("NamedCameras", named_camera),
        ("CameraAnimationList", _u32(camera_animation_count)),
        (
            "ScriptsPlayers",
            _u32(0) + _u32(2) + _ascii("duplicate-player") + _ascii("duplicate-player"),
        ),
        ("ObjectsList", b""),
        ("TriggerAreas", _u32(0)),
        (
            "ScriptTeams",
            _team_properties(indices, duplicate_property=True)
            + _team_properties(indices, duplicate_property=False),
        ),
        ("WaypointsList", _u32(0)),
    )
    versions = {
        "ScriptImportSize": 1,
        "PlayerScriptsList": 1,
        "NamedCameras": 2,
        "CameraAnimationList": 3,
        "ScriptsPlayers": 2,
        "ObjectsList": 3,
        "TriggerAreas": 1,
        "ScriptTeams": 1,
        "WaypointsList": 1,
    }
    return (
        b"CkMp"
        + name_table
        + b"".join(
            _record(
                name,
                overrides.get(name, versions[name]),
                payload,
                indices,
            )
            for name, payload in chunks
        )
    )


def _chunk(native: dict[str, object], name: str) -> dict[str, object]:
    chunks = native["chunks"]
    assert isinstance(chunks, list)
    return next(item for item in chunks if item["name"] == name)


def test_converts_ordered_duplicates_and_exactly_backtests() -> None:
    source = _fixture()

    native = convert_sage_scb_bytes(source)
    backtest = backtest_sage_scb_native(source, native)

    assert native["schema"] == SCB_NATIVE_SCHEMA
    assert reconstruct_sage_scb_body(native) == source
    assert backtest.to_dict()["schema"] == SCB_BACKTEST_SCHEMA
    assert backtest.to_dict()["exactWireMatch"] is True

    scripts_chunk = _chunk(native, "PlayerScriptsList")
    script_list = scripts_chunk["value"]["records"][0]["value"]
    assert [record["name"] for record in script_list["records"]] == [
        "ScriptGroup",
        "Script",
    ]
    grouped_script = script_list["records"][0]["value"]["records"][0]["value"]
    direct_script = script_list["records"][1]["value"]
    assert grouped_script["name"] == direct_script["name"] == "duplicate-script"
    assert [record["name"] for record in direct_script["records"]] == [
        "OrCondition",
        "ScriptAction",
        "ScriptActionFalse",
    ]

    players = _chunk(native, "ScriptsPlayers")["value"]["players"]
    assert [player["name"] for player in players] == [
        "duplicate-player",
        "duplicate-player",
    ]
    teams = _chunk(native, "ScriptTeams")["value"]["teams"]
    assert len(teams) == 2
    assert len(teams[0]["properties"]) == 2
    assert [prop["name"] for prop in teams[0]["properties"]] == [
        "teamName",
        "teamName",
    ]


def test_native_json_round_trip_is_exact() -> None:
    source = _fixture()
    native = convert_sage_scb_bytes(source)
    serialized = json.dumps(native, sort_keys=True, separators=(",", ":"))

    report = backtest_sage_scb_native(source, serialized)

    assert report.decoded_body_sha256 == hashlib.sha256(source).hexdigest()
    assert reconstruct_sage_scb_body(serialized) == source


def test_backtest_rejects_semantic_tamper() -> None:
    source = _fixture()
    native = convert_sage_scb_bytes(source)
    tampered = deepcopy(native)
    players = _chunk(tampered, "ScriptsPlayers")["value"]["players"]
    players[0]["name"] = "tampered-player"

    with pytest.raises(SageScbError, match="semantic-hash-mismatch") as exc_info:
        backtest_sage_scb_native(source, tampered)

    assert exc_info.value.code == "semantic-hash-mismatch"


def test_backtest_rejects_reordered_records_even_with_new_semantic_hash() -> None:
    source = _fixture()
    native = convert_sage_scb_bytes(source)
    reordered = deepcopy(native)
    script_list = _chunk(reordered, "PlayerScriptsList")["value"]["records"][0]["value"]
    script_list["records"].reverse()
    reordered["semanticSha256"] = canonical_sage_scb_semantic_sha256(reordered)

    with pytest.raises(SageScbError, match="native-body-hash-mismatch") as exc_info:
        backtest_sage_scb_native(source, reordered)

    assert exc_info.value.code == "native-body-hash-mismatch"


def test_backtest_rejects_a_different_source() -> None:
    source = _fixture()
    native = convert_sage_scb_bytes(source)
    different = bytearray(source)
    different[-1] ^= 1

    with pytest.raises(SageScbError, match="source-evidence-mismatch") as exc_info:
        backtest_sage_scb_native(bytes(different), native)

    assert exc_info.value.code == "source-evidence-mismatch"


def test_truncated_source_is_structurally_rejected() -> None:
    with pytest.raises(SageScbError) as exc_info:
        convert_sage_scb_bytes(_fixture()[:-1])

    assert exc_info.value.code == "truncated-wire"


def test_unknown_top_level_version_is_rejected() -> None:
    source = _fixture(version_overrides={"NamedCameras": 99})

    with pytest.raises(SageScbError, match="unsupported-version") as exc_info:
        convert_sage_scb_bytes(source)

    assert exc_info.value.code == "unsupported-version"


def test_nonempty_unproven_camera_animation_layout_is_rejected() -> None:
    source = _fixture(camera_animation_count=1)

    with pytest.raises(
        SageScbError, match="unsupported-camera-animation-layout"
    ) as exc_info:
        convert_sage_scb_bytes(source)

    assert exc_info.value.code == "unsupported-camera-animation-layout"


def test_duplicate_native_json_key_is_rejected() -> None:
    with pytest.raises(SageScbError, match="invalid-native-json") as exc_info:
        reconstruct_sage_scb_body('{"schema":"a","schema":"b"}')

    assert exc_info.value.code == "invalid-native-json"


def test_diagnostic_is_deterministic_and_identifier_free() -> None:
    first = _fixture()
    second = _fixture(version_overrides={"NamedCameras": 99})

    report = diagnose_sage_scb_sources([first, second])
    reversed_report = diagnose_sage_scb_sources([second, first])
    payload = json.dumps(report, sort_keys=True)

    assert report["schema"] == SCB_DIAGNOSTIC_SCHEMA
    assert report["sourceCount"] == 2
    assert report["acceptedCount"] == 1
    assert report["rejectedCount"] == 1
    assert report["rejectionReasons"] == [
        {"reason": "unsupported-version", "sourceCount": 1}
    ]
    assert report["reportIdentitySha256"] == reversed_report["reportIdentitySha256"]
    assert "duplicate-script" not in payload
    assert "duplicate-player" not in payload
    assert "fixture-camera" not in payload
    assert "internalKey" not in payload


def test_native_schema_rejects_extra_fields() -> None:
    native = convert_sage_scb_bytes(_fixture())
    native["unexpected"] = True

    with pytest.raises(SageScbError, match="invalid-native-schema") as exc_info:
        reconstruct_sage_scb_body(native)

    assert exc_info.value.code == "invalid-native-schema"
