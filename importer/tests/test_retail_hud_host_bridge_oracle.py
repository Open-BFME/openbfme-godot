from __future__ import annotations

import copy
import json
from pathlib import Path

import pytest

from openbfme_importer.retail_hud_host_bridge_oracle import (
    RENDER_CALLBACKS,
    SCRIPT_MAP,
    build_contract,
)


ROOT = Path(__file__).resolve().parents[2]
SCENE = (
    ROOT
    / "workspace"
    / "scratch"
    / "hud-apt-clip-actions"
    / "bundle-a"
    / "data"
    / "ui"
    / "palantir"
    / "scene-contract.json"
)
WND = (
    ROOT
    / "workspace"
    / "retail-work"
    / "cache"
    / "effective-assets"
    / "window"
    / "controlbar.wnd"
)
OPENSAGE = (
    ROOT / "workspace" / "scratch" / "hud-apt-text-buttons" / "opensage-src"
)


pytestmark = pytest.mark.skipif(
    not SCENE.is_file() or not WND.is_file(),
    reason="private retail HUD oracle inputs are absent",
)


def _inputs() -> tuple[dict, bytes]:
    return json.loads(SCENE.read_text(encoding="utf-8")), WND.read_bytes()


def test_exact_bridge_contract_is_deterministic_and_complete() -> None:
    scene, wnd = _inputs()
    first = build_contract(scene, wnd, opensage_root=OPENSAGE)
    second = build_contract(scene, wnd, opensage_root=OPENSAGE)
    assert first == second
    assert first["summary"] == {
        "blockedActionScriptCount": 17,
        "clipEventCount": 28,
        "clipProgramCount": 6,
        "initializeEventCount": 27,
        "unloadEventCount": 1,
        "wndWindowCount": 87,
        "wndCallbackIdentityCount": 21,
        "genericCallbackDispatchAllowed": False,
        "implementationIncluded": False,
    }
    assert {row["scriptId"] for row in first["actionScripts"]} == set(SCRIPT_MAP)
    assert len(first["clipEvents"]) == 28
    assert len(first["hostCalls"]) == 20
    assert all(row["name"] and row["argument"] for row in first["hostCalls"])
    assert all(row["mapping"] for row in first["actionScripts"])
    assert all("unresolved" in row for row in first["actionScripts"])
    assert {row["name"] for row in first["rendererCallbacks"]} == set(
        RENDER_CALLBACKS
    )
    assert all(
        not row["genericDispatchAllowed"] for row in first["rendererCallbacks"]
    )


def test_clip_events_and_wnd_callbacks_are_individually_accounted() -> None:
    scene, wnd = _inputs()
    contract = build_contract(scene, wnd)
    assert all(row["targetPath"] and row["programId"] for row in contract["clipEvents"])
    assert all(row["effect"] for row in contract["clipEvents"])
    callbacks = contract["wnd"]["callbackControls"]
    assert len(callbacks) == 21
    assert len(contract["wnd"]["controls"]) == 87
    assert all("controlId" in row for row in contract["wnd"]["controls"])
    assert all(row["controls"] for row in callbacks)
    assert all(row["status"] and row["proposal"] and row["risk"] for row in callbacks)
    assert sum(row["status"] == "deterministic-proposal" for row in callbacks) == 4
    assert {
        "ControlBarSystem",
        "ControlBarInput",
        "LeftHUDInput",
        "W3DNoDraw",
        "W3DLeftHUDDraw",
    }.issubset({row["callback"] for row in callbacks})
    assert contract["wnd"]["numericCommandIds"].endswith("do not invent")
    assert contract["wnd"]["activationGate"]["status"] == (
        "candidate-dead-for-vertical-slice-not-retail-proven"
    )


def test_changed_actionscript_identity_fails_closed() -> None:
    scene, wnd = _inputs()
    changed = copy.deepcopy(scene)
    changed["actionScripts"] = [
        row
        for row in changed["actionScripts"]
        if row.get("scriptId") != "palantir:333872"
    ]
    with pytest.raises(ValueError, match="identity set changed"):
        build_contract(changed, wnd)
