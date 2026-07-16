from __future__ import annotations

import json
from pathlib import Path
import struct

import pytest

from openbfme_importer.retail_hud_frame_selection import (
    OUTPUT_SCHEMA,
    HudFrameSelectionError,
    _Reader,
    _frame_table,
    _require_script_range,
    build_hud_frame_selection,
)


def test_bounded_frame_table_decodes_label_place_and_action() -> None:
    data = bytearray(256)
    struct.pack_into("<iI", data, 0, 3, 16)
    struct.pack_into("<III", data, 16, 32, 64, 144)

    struct.pack_into("<IIII", data, 32, 2, 200, 0x70000, 0)
    data[200:206] = b"_good\0"

    struct.pack_into("<IIII", data, 64, 3, 0x26, 7, 102)
    struct.pack_into("<ffffff", data, 80, 1.0, 0.0, 0.0, 1.0, 12.5, 9.0)
    struct.pack_into("<fIi", data, 112, 0.0, 208, -1)
    data[208:222] = b"PalantirFrame\0"

    struct.pack_into("<II", data, 144, 1, 232)

    frames = _frame_table(_Reader(bytes(data), "fixture.apt"), 0, 1)

    assert frames[0][0] == {
        "kind": "label",
        "sourceOffset": 32,
        "name": "_good",
        "flags": 0x70000,
        "frameId": 0,
    }
    assert frames[0][1]["name"] == "PalantirFrame"
    assert frames[0][1]["characterId"] == 102
    assert frames[0][1]["translation"] == [12.5, 9.0]
    assert frames[0][2]["instructionsOffset"] == 232


def test_bounded_frame_table_rejects_unknown_item_kind() -> None:
    data = bytearray(32)
    struct.pack_into("<iI", data, 0, 1, 8)
    struct.pack_into("<I", data, 8, 16)
    struct.pack_into("<I", data, 16, 99)

    with pytest.raises(HudFrameSelectionError, match="unknown frame-item kind 99"):
        _frame_table(_Reader(bytes(data), "fixture.apt"), 0, 1)


def test_script_range_requires_the_exact_digest() -> None:
    data = b"bounded-script"
    row = _require_script_range(
        data,
        0,
        len(data),
        "6100f2c69f47aaa8ff44f2770fdc0bac0d1d05e010db751367db72f4ba155755",
        "fixture",
    )
    assert row["byteRange"] == [0, len(data)]

    with pytest.raises(HudFrameSelectionError, match="byte range changed"):
        _require_script_range(data, 0, len(data), "0" * 64, "fixture")


def test_private_retail_selection_is_deterministic_and_fail_closed(
    tmp_path: Path,
) -> None:
    repo = Path(__file__).resolve().parents[2]
    plan = repo / ".private/scratch/hud-apt-profile/plan.json"
    assets = repo / ".private/retail-work/cache/effective-assets"
    if not plan.is_file() or not assets.is_dir():
        pytest.skip("private BFME2 retail HUD closure is unavailable")
    first = tmp_path / "first.json"
    second = tmp_path / "second.json"

    contract_a = build_hud_frame_selection(plan, assets, first)
    contract_b = build_hud_frame_selection(plan, assets, second)

    assert first.read_bytes() == second.read_bytes()
    assert contract_a == contract_b
    assert contract_a["schema"] == OUTPUT_SCHEMA
    assert contract_a["summary"] == {
        "selectionContractReady": True,
        "initialPalantirVariant": "good-double",
        "initialSideCommandBarVisible": False,
        "parityReady": False,
        "blockerCount": 2,
    }
    selection = contract_a["palantir"]["initialSelection"]
    assert selection["state"] == "_good"
    assert selection["importSymbol"] == "PalantirFrame_GoodDouble"
    assert selection["localImportCharacterId"] == 102
    assert selection["exportCharacterId"] == 19
    states = {
        row["state"]: row["importSymbol"]
        for row in contract_a["palantir"]["palantirFrame"]["states"]
    }
    assert states["_goodSingle"] == "PalantirFrame_GoodSingle"
    assert states["_good"] == "PalantirFrame_GoodDouble"
    side = contract_a["inGameSideCommandBar"]
    assert side["initialSelection"]["state"] == "hidden-offscreen"
    assert side["authoredPlayback"]["settledFrame"] == 10
    assert side["labels"]["_fadeIn"] == 11
    assert json.loads(first.read_text(encoding="utf-8"))["aggregateSha256"] == (
        contract_a["aggregateSha256"]
    )

    changed = json.loads(plan.read_text(encoding="utf-8"))
    changed["summary"]["movieCount"] = 6
    changed_plan = tmp_path / "changed-plan.json"
    changed_plan.write_text(json.dumps(changed), encoding="utf-8")
    with pytest.raises(HudFrameSelectionError, match="plan aggregate changed"):
        build_hud_frame_selection(changed_plan, assets, tmp_path / "rejected.json")
