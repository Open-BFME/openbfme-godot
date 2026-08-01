from __future__ import annotations

import json
from pathlib import Path

import pytest

from openbfme_importer.retail_hud_wnd_activation_oracle import build_contract
from tests.retail_inputs import retail_file


ROOT = Path(__file__).resolve().parents[2]
ASSETS = ROOT / ".private" / "retail-work" / "cache" / "effective-assets"
MANIFEST = ASSETS / ".openbfme" / "manifest.json"
GAME_DAT = retail_file("game.dat")
OPENSAGE = ROOT / ".private" / "scratch" / "opensage-source"

pytestmark = pytest.mark.skipif(
    not all(path.is_file() for path in (MANIFEST, GAME_DAT)),
    reason="private retail HUD activation inputs are absent",
)


def _build() -> dict:
    return build_contract(
        ASSETS,
        json.loads(MANIFEST.read_text(encoding="utf-8")),
        GAME_DAT,
        opensage_root=OPENSAGE,
    )


def test_activation_contract_is_exact_and_deterministic() -> None:
    first = _build()
    assert first == _build()
    assert first["summary"] == {
        "decision": "active-companion-not-candidate-dead",
        "wndLayoutLoaded": True,
        "aptPalantirConstructed": True,
        "wndCallbackIdentityCount": 21,
        "runtimeTraceRequiredForActivation": False,
        "wndSemanticBlockerRetained": True,
    }


def test_same_retail_init_routes_to_wnd_and_apt() -> None:
    contract = _build()
    route = contract["staticActivationRoute"]
    assert route["wndLoadCallVa"] == "0x0069e5ad"
    assert route["wndLoaderVa"] == "0x0069c23e"
    assert route["aptFactoryDispatchVa"] == "0x0069e5f5"
    assert route["aptFactorySlot"] == {"slotOffset": "0x1dc", "targetVa": "0x0048f32c"}
    assert len(contract["retailCode"]) == 6


def test_wnd_callbacks_and_resizer_controls_remain_live_dependencies() -> None:
    contract = _build()
    assert contract["wnd"]["windowCount"] == 87
    assert len(contract["wnd"]["callbacks"]) == 21
    assert contract["resizer"]["exactControlReferenceCount"] == 84
    assert contract["recommendation"]["retainWndPayload"]
    assert contract["recommendation"]["retainAll21CallbackIdentities"]
