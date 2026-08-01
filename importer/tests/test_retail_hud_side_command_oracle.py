from __future__ import annotations

from pathlib import Path

import pytest

from openbfme_importer.retail_hud_side_command_oracle import (
    HudSideCommandOracleError,
    build_contract,
    build_contract_from_payloads,
    frame_label_for_neighbors,
    show_target_names,
)
from tests.retail_inputs import retail_file


ROOT = Path(__file__).resolve().parents[2]
ASSETS = ROOT / ".private" / "retail-work" / "cache" / "effective-assets"
GAME_DAT = retail_file("game.dat")
SOURCE_NAMES = (
    "InGameSideCommandBar.apt",
    "InGameSideCommandBar.const",
    "InGameSideCommandBar.dat",
)

pytestmark = pytest.mark.skipif(
    not all((ASSETS / name).is_file() for name in SOURCE_NAMES)
    or not GAME_DAT.is_file(),
    reason="private retail InGameSideCommandBar source or BFME2 game.dat is absent",
)


def _payloads() -> dict[str, bytes]:
    return {name: (ASSETS / name).read_bytes() for name in SOURCE_NAMES}


def _game_dat() -> bytes:
    return GAME_DAT.read_bytes()


def test_exact_side_command_contract_is_deterministic_and_complete() -> None:
    first = build_contract(ASSETS, GAME_DAT)
    second = build_contract_from_payloads(_payloads(), _game_dat())
    assert first == second
    assert first["summary"] == {
        "targetScriptCount": 3,
        "exactFunctionBodyCount": 6,
        "localButtonCount": 12,
        "authoredShowTargetCount": 15,
        "staticallyAbsentShowTargetCount": 4,
        "unresolvedRuntimeTraceCount": 0,
        "genericActionScriptVmRequired": False,
        "implementationIncluded": False,
    }
    assert [row["scriptId"] for row in first["scripts"]] == [
        "ingamesidecommandbar:6272",
        "ingamesidecommandbar:6368",
        "ingamesidecommandbar:7296",
    ]
    assert [row["sha256"] for row in first["scripts"]] == [
        "ee3f7f3c582961473ffbbebe851f0086820fd9fa57c62f3573a021d2c5917557",
        "268aab1f60a086e5bf869d83da0aabdfe2f383d688bf98ce6a6a59fab274f040",
        "56466dca85c04dd52fd50a5cb02ea625cdad4b776c7ef14a6854f6412caca675",
    ]
    assert {row["name"] for row in first["functionLibrary"]["functions"]} == {
        "GetNextButton",
        "IsNextButtonFrameVisible",
        "GetPriorButton",
        "IsPriorButtonFrameVisible",
        "UpdateFrameState",
        "UpdateNeighborFrameStates",
    }


def test_typed_topology_and_authored_call_order_are_exact() -> None:
    contract = build_contract(ASSETS, GAME_DAT)
    assert show_target_names() == [f"Button{index}" for index in range(1, 16)]
    assert contract["scripts"][0]["typedEffect"] == [
        "UpdateNeighborFrameStates()"
    ]
    assert contract["scripts"][1]["typedEffect"] == [
        "UpdateFrameState()",
        "UpdateNeighborFrameStates()",
    ]
    assert contract["functionLibrary"]["updateNeighborFrameStates"]["order"] == [
        "next",
        "prior",
    ]
    assert frame_label_for_neighbors(
        frame_exists=False,
        prior_frame_visible=True,
        next_frame_visible=True,
    ) is None
    assert {
        (prior, next_): frame_label_for_neighbors(
            frame_exists=True,
            prior_frame_visible=prior,
            next_frame_visible=next_,
        )
        for prior in (False, True)
        for next_ in (False, True)
    } == {
        (False, False): "_topbottom",
        (False, True): "_top",
        (True, False): "_bottom",
        (True, True): "_middle",
    }


def test_native_evidence_closes_the_two_runtime_questions() -> None:
    contract = build_contract(ASSETS, GAME_DAT)
    reachability = contract["timelineReachability"]
    assert reachability["root"] == {
        "frameIndex": 0,
        "placementSourceOffset": 3400,
        "name": "ButtonSet",
        "characterId": 21,
        "depth": 1,
        "translation": [1048.300048828125, 361.29998779296875],
    }
    assert reachability["buttonSet"]["localButtonNames"] == [
        f"Button{index}" for index in range(12)
    ]
    assert reachability["buttonSet"]["staticallyAbsentShowTargets"] == [
        "Button12",
        "Button13",
        "Button14",
        "Button15",
    ]
    assert reachability["buttonFrame"]["hideFrameActionOrder"] == [6264, 6272]
    assert reachability["buttonFrame"]["showFrameActionOrder"] == [6368]
    assert [row["name"] for row in reachability["buttonFrame"]["showFramePlacements"]] == [
        "Frame",
        "ButtonGlass",
    ]
    assert [
        row["rawRecordType"]
        for row in reachability["buttonFrame"]["showFramePlacements"]
    ] == [3, 3]
    assert contract["unresolvedRuntimeTraces"] == []
    assert contract["resolvedNativeQuestions"] == {
        "staticallyAbsentShowTargets": {
            "targets": ["Button12", "Button13", "Button14", "Button15"],
            "effect": "ordered call is consumed as a no-op",
            "evidenceRangeIds": [
                "opcode-b2-table-entry",
                "opcode-b2-handler",
                "named-call-undefined-check",
                "named-call-undefined-branch",
            ],
        },
        "showFrameScheduling": {
            "frameIndex": 10,
            "actionSourceOffset": 6368,
            "placementsVisibleAtAction": ["Frame", "ButtonGlass"],
            "frameLabelResult": "use sealed UpdateFrameState truth table",
            "evidenceRangeIds": [
                "frame-record-processing",
                "frame-action-selection",
                "frame-action-queue-add",
                "seek-frame-order",
                "advance-frame-order",
                "tick-order",
                "frame-action-queue-run",
            ],
        },
    }
    assert contract["nativeRuntimeEvidence"]["callNamedMethodPop"] == {
        "opcode": "0xB2",
        "dispatchTableVirtualAddress": "0x00DDCF10",
        "handlerVirtualAddress": "0x00B09200",
        "callCoreVirtualAddress": "0x00B08070",
        "undefinedReceiverCheckVirtualAddress": "0x00B081AC",
        "undefinedReceiverBranchVirtualAddress": "0x00B087A5",
        "undefinedReceiverEffect": (
            "consume argument-count, receiver, method, and arguments; "
            "push undefined; pop-form handler discards undefined"
        ),
        "raisesActionScriptError": False,
    }
    assert contract["nativeRuntimeEvidence"]["frameScheduling"][
        "sameFramePlacementVisibleToAction"
    ] is True


def test_changed_retail_identity_fails_closed() -> None:
    payloads = _payloads()
    changed = bytearray(payloads["InGameSideCommandBar.apt"])
    changed[6272] ^= 1
    payloads["InGameSideCommandBar.apt"] = bytes(changed)
    with pytest.raises(HudSideCommandOracleError, match="source identity changed"):
        build_contract_from_payloads(payloads, _game_dat())


def test_changed_game_dat_identity_fails_closed() -> None:
    changed = bytearray(_game_dat())
    changed[0x6FD810] ^= 1
    with pytest.raises(HudSideCommandOracleError, match="game.dat identity changed"):
        build_contract_from_payloads(_payloads(), bytes(changed))
