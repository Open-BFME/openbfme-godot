from __future__ import annotations

from pathlib import Path
import struct

import pytest

from openbfme_importer.castle_behavior import (
    CastleBehaviorCompilerError,
    compile_castle_behavior_contract,
)


def _dotnet_string(value: str) -> bytes:
    encoded = value.encode("utf-8")
    return bytes([len(encoded)]) + encoded


def _u16_string(value: str) -> bytes:
    encoded = value.encode("cp1252")
    return struct.pack("<H", len(encoded)) + encoded


def _record(index: int, version: int, payload: bytes) -> bytes:
    return struct.pack("<IHI", index, version, len(payload)) + payload


def _bse(token: str, citadel: str, pad: str) -> bytes:
    names = ["HeightMapData", "ObjectsList", "Object", "CastleTemplates", token]
    indexes = {name: index + 1 for index, name in enumerate(names)}
    table = struct.pack("<I", len(names))
    for index in range(len(names), 0, -1):
        table += _dotnet_string(names[index - 1]) + struct.pack("<I", index)
    height = struct.pack("<IIIII4H", 2, 2, 0, 0, 4, 1, 1, 1, 1)
    objects = b""
    for object_id in (pad, citadel):
        payload = struct.pack("<ffffI", 0.0, 0.0, 0.0, 0.0, 0)
        payload += _u16_string(object_id) + struct.pack("<H", 0)
        objects += _record(indexes["Object"], 3, payload)
    castle = bytes([3]) + indexes[token].to_bytes(3, "little")
    castle += struct.pack("<I", 2)
    castle += _u16_string("") + _u16_string(pad)
    castle += struct.pack("<ffffII", 94.800048828125, -61.75, 0.5, 0.7853981852531433, 40, 1)
    castle += _u16_string("") + _u16_string(citadel)
    castle += struct.pack("<ffffII", 0.0, 0.0, 0.0, 0.0, 40, 1)
    castle += struct.pack("<I", 0)
    return b"CkMp" + table + b"".join(
        (
            _record(indexes["HeightMapData"], 5, height),
            _record(indexes["ObjectsList"], 3, objects),
            _record(indexes["CastleTemplates"], 5, castle),
        )
    )


def _evidence(assignments: list[dict[str, object]]) -> dict[str, object]:
    return {
        "runtimeModules": [
            {
                "moduleKind": "CastleBehavior",
                "assignments": assignments,
            }
        ]
    }


@pytest.mark.parametrize(
    ("faction", "token"),
    [
        ("Men", "Fortress_Men"),
        ("Elves", "Fortress_Elves"),
        ("Dwarves", "Fortress_Dwarven"),
        ("Isengard", "Fortress_Isengard"),
        ("Mordor", "Fortress_Mordor"),
        ("Wild", "Fortress_Wild"),
        ("Angmar", "Fortress_Angmar"),
    ],
)
def test_compiles_all_seven_faction_assignments_with_exact_bse_values(
    tmp_path: Path, faction: str, token: str
) -> None:
    virtual = Path("bases") / token.casefold() / f"{token.casefold()}.bse"
    physical = tmp_path / virtual
    physical.parent.mkdir(parents=True)
    physical.write_bytes(_bse(token, f"{faction}Citadel", f"{faction}Pad"))
    assignments = [
        {"key": "CastleToUnpackForFaction", "rawValue": f"{name} {value}"}
        for name, value in (("Men", "Unused_Men"), (faction, token))
        if name != faction or value == token
    ]

    contract = compile_castle_behavior_contract(
        _evidence(assignments), faction, tmp_path
    )

    assert contract is not None
    assert contract["castleTemplateToken"] == token
    assert contract["source"]["virtualPath"] == virtual.as_posix()
    assert contract["pieces"] == [
        {
            "index": 0,
            "objectId": f"{faction}Pad",
            "offset": [94.800048828125, -61.75, 0.5],
            "offsetRawQ32": [407163109376, -265214230528, 2147483648],
            "angleRadians": 0.7853981852531433,
            "angleRadiansRawQ32": 3373259520,
            "priority": 40,
            "phase": 1,
        },
        {
            "index": 1,
            "objectId": f"{faction}Citadel",
            "offset": [0.0, 0.0, 0.0],
            "offsetRawQ32": [0, 0, 0],
            "angleRadians": 0.0,
            "angleRadiansRawQ32": 0,
            "priority": 40,
            "phase": 1,
        },
    ]


def test_authored_castle_missing_faction_assignment_fails_closed(tmp_path: Path) -> None:
    with pytest.raises(CastleBehaviorCompilerError, match="exactly one"):
        compile_castle_behavior_contract(
            _evidence(
                [{"key": "CastleToUnpackForFaction", "rawValue": "Elves Fortress_Elves"}]
            ),
            "Men",
            tmp_path,
        )


def test_no_castle_module_is_honestly_absent(tmp_path: Path) -> None:
    assert compile_castle_behavior_contract({"runtimeModules": []}, "Men", tmp_path) is None
