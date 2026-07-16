from __future__ import annotations
import json
from pathlib import Path
import pytest
from openbfme_importer.retail_hud_wnd_draw_semantics import build_contract

ROOT = Path(__file__).resolve().parents[2]
ASSETS = ROOT / ".private" / "retail-work" / "cache" / "effective-assets"
MANIFEST = ASSETS / ".openbfme" / "manifest.json"
GAME_DAT = Path("F:/BFME2/game.dat")
pytestmark = pytest.mark.skipif(
    not MANIFEST.is_file() or not GAME_DAT.is_file(), reason="private inputs absent"
)


def _build() -> dict:
    return build_contract(
        ASSETS, json.loads(MANIFEST.read_text(encoding="utf-8")), GAME_DAT
    )


def test_draw_contract_is_exact_and_deterministic() -> None:
    first = _build()
    assert first == _build()
    assert first["summary"] == {
        "drawCallbackCount": 10,
        "exactFullBodyCount": 10,
        "provenNoOpCount": 1,
        "callbacksImplemented": False,
        "inventedVisualsAllowed": False,
        "genericDispatchAllowed": False,
    }


def test_no_draw_is_exact_single_byte_noop() -> None:
    row = next(row for row in _build()["callbacks"] if row["name"] == "W3DNoDraw")
    assert row["byteLength"] == 1
    assert (
        row["sha256"]
        == "ae3f4619b0413d70d3004b9131c3752153074e45725be13b9a148978895e359e"
    )
    assert row["inputs"] == row["order"] == row["unresolved"] == []


def test_every_draw_has_controls_typed_order_and_fail_closed_policy() -> None:
    for row in _build()["callbacks"]:
        assert row["controls"] and row["interface"] and row["state"]
        assert not row["genericDispatchAllowed"]
    by_name = {row["name"]: row for row in _build()["callbacks"]}
    assert by_name["W3DPowerDraw"]["assets"] == [
        "PowerPointY",
        "PowerPointG",
        "PowerBarSlider",
    ]
    assert by_name["W3DCommandBarGenExpDraw"]["assets"] == [
        "GenExpBarTop1",
        "GenExpBarBottom1",
        "GenExpBar1",
    ]
