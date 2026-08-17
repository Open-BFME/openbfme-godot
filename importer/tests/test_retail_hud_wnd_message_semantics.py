from __future__ import annotations

import json
from pathlib import Path

import pytest

from openbfme_importer.retail_hud_wnd_message_semantics import build_contract
from tests.retail_inputs import retail_file

ROOT = Path(__file__).resolve().parents[2]
ASSETS = ROOT / "workspace" / "retail-work" / "cache" / "effective-assets"
MANIFEST = ASSETS / ".openbfme" / "manifest.json"
GAME_DAT = retail_file("game.dat")
pytestmark = pytest.mark.skipif(
    not MANIFEST.is_file() or not GAME_DAT.is_file(), reason="private inputs absent"
)


def _build() -> dict:
    return build_contract(
        ASSETS, json.loads(MANIFEST.read_text(encoding="utf-8")), GAME_DAT
    )


def test_contract_is_exact_and_deterministic() -> None:
    first = _build()
    assert first == _build()
    assert first["summary"] == {
        "prioritizedHandlerCount": 5,
        "fullyExactHandlerCount": 3,
        "boundedOpaqueBranchHandlerCount": 2,
        "implemented": False,
        "genericDispatchAllowed": False,
    }


def test_exact_return_contracts_are_retained() -> None:
    by_name = {row["name"]: row for row in _build()["handlers"]}
    assert by_name["ControlBarInput"]["returnContract"] == "always 0 (unhandled)"
    assert "0 only for messages" in by_name["GameWinBlockInput"]["returnContract"]
    assert (
        "exact parent callback return"
        in by_name["PassSelectedButtonsToParentSystem"]["returnContract"]
    )
    assert all(
        row["controls"] and row["states"] and row["interface"]
        for row in by_name.values()
    )


def test_large_handlers_fail_closed_on_opaque_aliases() -> None:
    by_name = {row["name"]: row for row in _build()["handlers"]}
    assert len(by_name["LeftHUDInput"]["unresolved"]) == 3
    assert len(by_name["ControlBarSystem"]["unresolved"]) == 3
    assert _build()["retainedNotImplemented"] == {
        "BeaconWindowInput": "event-dormant",
        "ControlBarObserverSystem": "outside declared player-v-player slice",
    }
