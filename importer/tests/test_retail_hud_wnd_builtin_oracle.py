from __future__ import annotations

import json
from pathlib import Path

import pytest

from openbfme_importer.retail_hud_wnd_builtin_oracle import (
    BUILT_INS,
    build_contract,
)
from tests.retail_inputs import retail_file


ROOT = Path(__file__).resolve().parents[2]
ASSETS = ROOT / ".private" / "retail-work" / "cache" / "effective-assets"
MANIFEST = ASSETS / ".openbfme" / "manifest.json"
GAME_DAT = retail_file("game.dat")

pytestmark = pytest.mark.skipif(
    not all(path.is_file() for path in (MANIFEST, GAME_DAT)),
    reason="private retail WND built-in inputs are absent",
)


def _build() -> dict:
    return build_contract(
        ASSETS, json.loads(MANIFEST.read_text(encoding="utf-8")), GAME_DAT
    )


def test_builtin_contract_is_exact_deterministic_and_stays_blocked() -> None:
    first = _build()
    assert first == _build()
    assert first["summary"] == {
        "builtInCount": 4,
        "implementationSafeCount": 0,
        "unresolvedDynamicTargetCount": 4,
        "noOpProvenCount": 0,
        "delegationProvenCount": 0,
        "genericDispatchAllowed": False,
        "runtimeCaptureRequired": True,
    }
    assert first["recommendation"]["implementationSafeCallbacks"] == []
    assert first["recommendation"]["retainBlockedCallbacks"] == list(BUILT_INS)


def test_exact_slots_bindings_and_runtime_recipes_are_sealed() -> None:
    callbacks = {row["name"]: row for row in _build()["callbacks"]}
    assert set(callbacks) == set(BUILT_INS)
    assert {
        name: row["callbackSlot"]["offset"] for name, row in callbacks.items()
    } == {
        "GameWinDefaultInput": "0x1e0",
        "GameWinDefaultSystem": "0x1e4",
        "GameWinDefaultTooltip": "0x1ec",
        "W3DGameWinDefaultDraw": "0x1e8",
    }
    assert [row["controls"][0]["controlId"] for row in callbacks.values()] == [
        "ControlBar.wnd:ProductionQueueWindow",
        "ControlBar.wnd:LeftHUD",
        "ControlBar.wnd:ProductionQueueWindow",
        "ControlBar.wnd:ControlBarParent",
    ]
    for row in callbacks.values():
        assert row["staticDecision"] == {
            "descriptorFound": False,
            "handlerVa": None,
            "noOpProven": False,
            "delegationProven": False,
            "implementationSafe": False,
            "classification": "dynamic-target-unresolved",
        }
        assert row["dynamicGates"]
        assert row["runtimeCapture"]["loadReturnBreakpointVa"] == "0x0069c25f"
        assert len(row["runtimeCapture"]["procedure"]) == 5
        assert not row["genericDispatchAllowed"]


def test_registry_absence_and_default_draw_candidate_are_not_overclaimed() -> None:
    contract = _build()
    assert [(row["name"], row["entryCount"]) for row in contract["registryTables"]] == [
        ("draw", 35),
        ("system", 21),
        ("input", 18),
        ("tooltip-token", 1),
    ]
    assert contract["fullStructuralDescriptorScan"] == {
        "descriptorCount": 473,
        "rowsSha256": "75a223fadaa4305dd99582332ad8fd119fc265ca57844fb6438110f44917006f",
        "builtInDescriptorCount": 0,
        "asciiOrUtf16BuiltInNameCount": 0,
    }
    candidate = contract["defaultDrawCandidate"]
    assert candidate["candidateVa"] == "0x0049dc32"
    assert candidate["directCallers"] == [
        "0x0049ddfb",
        "0x0049de40",
        "0x0049de81",
        "0x004a33a2",
        "0x004a3a57",
    ]
    assert candidate["identityTieToAuthoredBuiltIn"] is False
    assert candidate["implementationSafe"] is False
