from __future__ import annotations

import json
from pathlib import Path

import pytest

from openbfme_importer.cook.maps import convert_cooked_map, convert_from_pack


def _write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, sort_keys=True), encoding="utf-8")


def _synthetic_cooked_map(root: Path) -> None:
    root.mkdir()
    _write_json(
        root / "map.json",
        {
            "schema": "openbfme.map",
            "id": "test.synthetic",
            "source": {"sha256": "2" * 64},
            "terrain": "terrain.json",
            "objects": "objects.json",
            "waypoints": "waypoints.json",
            "water": "water.json",
        },
    )
    _write_json(
        root / "terrain.json",
        {
            "height": {
                "width": 4,
                "height": 3,
                "horizontalScale": 10,
                "heightmap": {"path": "heightmap.r16"},
            },
            "passability": {"path": "impassability.bit", "rowStrideBytes": 1},
        },
    )
    (root / "heightmap.r16").write_bytes(bytes(range(24)))
    (root / "impassability.bit").write_bytes(bytes((0, 2, 0)))
    _write_json(
        root / "waypoints.json",
        {
            "waypoints": [
                {"name": "Player_1_Start", "playerIndex": 1, "sagePosition": [5.25, 10, 0]},
                {"name": "Center", "sagePosition": [20, 15, 0]},
            ],
            "playerStarts": {"Player_1_Start": [5.25, 10, 0]},
        },
    )
    _write_json(
        root / "objects.json",
        {
            "objects": [
                {
                    "typeName": "KnownTree",
                    "sagePosition": [12.5, 7.25, 1],
                    "worldZ": 3.75,
                    "sageAngleRadians": 0.125,
                    "properties": {
                        "originalOwner": "PlyrCivilian/teamPlyrCivilian",
                        "enabled": True,
                    },
                }
            ]
        },
    )
    _write_json(root / "water.json", {"standingAreas": []})


def test_clean_room_cooked_map_is_complete_and_byte_deterministic(tmp_path: Path) -> None:
    cooked = tmp_path / "cooked"
    _synthetic_cooked_map(cooked)
    first = tmp_path / "first.json"
    second = tmp_path / "second.json"

    document = convert_cooked_map(cooked, first)
    convert_cooked_map(cooked, second)

    assert first.read_bytes() == second.read_bytes()
    assert document["schema"] == "openbfme.map.v1"
    assert document["world"] == {"width": 40, "height": 30, "cell_size": 10}
    assert document["start_positions"]["0"] == {"x": 5.25, "y": 10, "facing": 0}
    assert len(document["plots"]) == 6
    assert document["objects"][0]["owner"] == "PlyrCivilian"
    assert document["height_grid"]["data_base64"]
    assert document["passability_grid"]["data_base64"] == "AAIA"


def test_real_fords_pack_converts_when_available(tmp_path: Path) -> None:
    roots = list(
        Path("workspace/content-packs/bfme2-skirmish-maps-private").glob("*/data/maps.json")
    )
    if not roots:
        pytest.skip("private BFME2 skirmish map pack is absent")
    output = tmp_path / "fords-map-v1.json"

    document = convert_from_pack(roots[0].parents[1], "Fords of Isen II", output)

    assert document["source"]["path"].casefold().endswith("map mp fords of isen ii.map")
    assert len(document["start_positions"]) == 2
    assert len(document["plots"]) == 12
    assert len(document["objects"]) > 1_000
    assert output.stat().st_size > 100_000
