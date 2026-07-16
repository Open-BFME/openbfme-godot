from __future__ import annotations

import json
from pathlib import Path

import pytest

from openbfme_importer.retail_hud_wnd_callback_oracle import build_contract


ROOT = Path(__file__).resolve().parents[2]
ASSETS = ROOT / ".private" / "retail-work" / "cache" / "effective-assets"
MANIFEST = ASSETS / ".openbfme" / "manifest.json"
GAME_DAT = Path("F:/BFME2/game.dat")

pytestmark = pytest.mark.skipif(
    not all(path.is_file() for path in (MANIFEST, GAME_DAT)),
    reason="private retail WND callback inputs are absent",
)


def _build() -> dict:
    return build_contract(
        ASSETS, json.loads(MANIFEST.read_text(encoding="utf-8")), GAME_DAT
    )


def test_callback_contract_is_exact_and_deterministic() -> None:
    first = _build()
    assert first == _build()
    assert first["summary"] == {
        "callbackIdentityCount": 21,
        "exactRetailHandlerCount": 17,
        "runtimeBuiltInHandlerCount": 4,
        "baselineSliceRequiredCount": 19,
        "eventDormantCount": 1,
        "outsideDeclaredPlayerSliceCount": 1,
        "genericDispatchAllowed": False,
    }


def test_exact_handlers_and_unresolved_builtins_are_separated() -> None:
    contract = _build()
    resolved = [
        row
        for row in contract["callbacks"]
        if row["retailHandler"]["status"] == "exact-retail-handler"
    ]
    unresolved = [
        row
        for row in contract["callbacks"]
        if row["retailHandler"]["handlerVa"] is None
    ]
    assert len(resolved) == 17
    assert {row["name"] for row in unresolved} == {
        "GameWinDefaultInput",
        "GameWinDefaultSystem",
        "GameWinDefaultTooltip",
        "W3DGameWinDefaultDraw",
    }
    assert all(row["retailHandler"]["descriptorSha256"] for row in resolved)
    assert all(row["retailHandler"]["entryPrefixSha256"] for row in resolved)


def test_every_callback_retains_controls_messages_interface_and_trace() -> None:
    contract = _build()
    assert len(contract["baselineSliceRequired"]) == 19
    assert contract["eventDormant"] == ["BeaconWindowInput"]
    assert contract["outsideDeclaredPlayerSlice"] == ["ControlBarObserverSystem"]
    for row in contract["callbacks"]:
        assert (
            row["controls"] and row["messages"] and row["effect"] and row["interface"]
        )
        assert row["retailHandler"]["trace"]
        assert not row["genericDispatchAllowed"]
