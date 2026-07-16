from __future__ import annotations

import copy
import json
from pathlib import Path

import pytest

from openbfme_importer.retail_hud_resource_flash_oracle import build_contract


ROOT = Path(__file__).resolve().parents[2]
SCENE = (
    ROOT
    / ".private"
    / "scratch"
    / "hud-apt-clip-actions"
    / "bundle-a"
    / "data"
    / "ui"
    / "palantir"
    / "scene-contract.json"
)
ASSETS = ROOT / ".private" / "retail-work" / "cache" / "effective-assets"
MANIFEST = ASSETS / ".openbfme" / "manifest.json"
GAME_DAT = Path("F:/BFME2/game.dat")

pytestmark = pytest.mark.skipif(
    not SCENE.is_file() or not MANIFEST.is_file() or not GAME_DAT.is_file(),
    reason="private BFME2 resource-flash inputs are absent",
)


def _inputs() -> tuple[dict, dict]:
    return (
        json.loads(SCENE.read_text(encoding="utf-8")),
        json.loads(MANIFEST.read_text(encoding="utf-8")),
    )


def _build(scene: dict | None = None) -> dict:
    source, manifest = _inputs()
    return build_contract(scene if scene is not None else source, ASSETS, manifest, GAME_DAT)


def test_contract_is_payload_free_deterministic_and_exact() -> None:
    first = _build()
    assert first == _build()
    assert first["summary"] == {
        "typedInputCount": 1,
        "visualTargetCount": 1,
        "audioEventCount": 1,
        "audioLeafCount": 1,
        "nativeHandlerExact": True,
        "visualReplayExact": True,
        "mixerOverlapTraceRequired": True,
        "implementationIncluded": False,
        "genericDispatchAllowed": False,
        "fallbackAllowed": False,
    }
    serialized = json.dumps(first, sort_keys=True)
    assert "RIFF" not in serialized
    assert "AudioEvent Gui_PalantirResourceBarFlash\r\n" not in serialized


def test_exact_typed_visual_and_audio_path_is_retained() -> None:
    contract = _build()
    assert contract["typedInput"]["method"] == "PlayCommandPointEffect"
    assert contract["typedInput"]["arguments"] == []
    assert contract["typedInput"]["effect"] == {
        "target": "CommandPointsFlash",
        "method": "gotoAndPlay",
        "arguments": ["_go"],
    }
    assert contract["script"]["effectsInAuthoredOrder"][0]["kind"] == (
        "play-current-timeline"
    )
    assert contract["audio"]["event"]["eventId"] == (
        "Gui_PalantirResourceBarFlash"
    )
    assert contract["audio"]["event"]["sounds"] == ["UCommandPoints"]
    assert len(contract["audio"]["leaves"]) == 1
    assert contract["audio"]["leaves"][0]["sha256"] == (
        "f2d3aff531ecfd3616069d53551823f92aee92f009382d3bf39d4ec8e2eca350"
    )


def test_replay_is_exact_and_only_mixer_overlap_remains_dynamic() -> None:
    contract = _build()
    assert contract["visual"]["entryFrame"]["index"] == 8
    assert contract["visual"]["returnFrame"]["index"] == 57
    assert contract["visual"]["authoredFrameIntervalSpanMilliseconds"] == 1617
    assert "rewinds the one placed" in contract["replayAndOverlap"]["visual"]
    assert contract["hostPath"]["native"]["existingVoiceSuppressionInHandler"] is False
    assert len(contract["unresolved"]) == 2
    assert "mixer" in contract["unresolved"][0]


def test_changed_script_operand_fails_closed() -> None:
    scene, _manifest = _inputs()
    changed = copy.deepcopy(scene)
    row = next(
        item
        for item in changed["actionScripts"]
        if item["scriptId"] == "palantir:332504"
    )
    row["instructions"][1]["operand"] = "InventedSound"
    with pytest.raises(ValueError, match="script operands changed"):
        _build(changed)
