from __future__ import annotations

from pathlib import Path

import pytest

from openbfme_importer.retail_hud_palantir_command_oracle import (
    HudPalantirCommandOracleError,
    build_contract,
    build_contract_from_payloads,
    write_contract,
)
from tests.retail_inputs import retail_file


ROOT = Path(__file__).resolve().parents[2]
ASSETS = ROOT / "workspace" / "retail-work" / "cache" / "effective-assets"
GAME_DAT = retail_file("game.dat")
SOURCE_NAMES = (
    "Palantir.apt",
    "Palantir.const",
    "Palantir.dat",
    "libInGameUI.apt",
    "libInGameUI.const",
    "libInGameUI.dat",
)

pytestmark = pytest.mark.skipif(
    not all((ASSETS / name).is_file() for name in SOURCE_NAMES)
    or not GAME_DAT.is_file(),
    reason="private retail Palantir command source or BFME2 game.dat is absent",
)


def _payloads() -> dict[str, bytes]:
    return {name: (ASSETS / name).read_bytes() for name in SOURCE_NAMES}


def test_command_oracle_is_deterministic_and_classifies_registration_only() -> None:
    first = build_contract(ASSETS, GAME_DAT)
    second = build_contract_from_payloads(_payloads(), GAME_DAT.read_bytes())
    assert first == second
    assert first["summary"] == {
        "targetScriptCount": 3,
        "declarationOnlyProgramCount": 1,
        "implementationSafeProgramCount": 2,
        "hostIntentOnlyProgramCount": 1,
        "lifecycleFunctionCount": 6,
        "buttonMethodCount": 3,
        "numericButtonFrameCount": 6,
        "nativeHostHandlerCount": 6,
        "remainingTraceGateCount": 2,
        "genericActionScriptVmRequired": False,
        "implementationIncluded": False,
    }
    assert first["implementationOpportunity"]["implementationSafeProgramIds"] == [
        "palantir:169224",
        "palantir:169256",
    ]
    assert first["implementationOpportunity"]["hostIntentOnlyProgramIds"] == [
        "palantir:167296"
    ]
    assert first["scripts"][1]["typedEffect"] == {
        "classification": "declaration-only-typed-registration",
        "functions": [
            "OnMovieClipFrameLoaded",
            "OnMovieClipFrameUnloaded",
            "OnCommandButtonSubMenuLoaded",
            "OnCommandButtonSubMenuUnloaded",
            "OnCommandButtonToggleFlashLoaded",
            "OnCommandButtonToggleFlashUnloaded",
        ],
        "invocationDuringDeclaration": False,
    }


def test_skill_upgrade_and_button_method_semantics_are_exact() -> None:
    contract = build_contract(ASSETS, GAME_DAT)
    assert contract["scripts"][0]["typedEffect"] == {
        "authoredOrder": [
            {"call": "_root.UpdateSkillUpgradeButton", "arguments": []},
            {
                "when": "Boolean(_global.InGame)",
                "call": "_root.SetCommandButtonState",
                "argumentsInOrder": [
                    [1, "_up"],
                    [2, "_disabled"],
                    [4, "_up"],
                    [5, "_disabled"],
                ],
            },
        ],
        "classification": "typed-host-call-intent",
    }
    assert [row["name"] for row in contract["buttonMethodRegistrations"]] == [
        "SetAutoAbilityOverlayState",
        "SetFlashEffectState",
        "SetGlassState",
    ]
    assert [row["target"] for row in contract["buttonMethodRegistrations"]] == [
        "this._parent._parent.AutoAbilityOverlays[this._name]",
        "this._parent.FlashEffects[this._name]",
        "this._parent['glass' + this._name]",
    ]
    assert contract["scripts"][2]["typedEffect"]["buttonOrder"] == [
        "0",
        "1",
        "2",
        "3",
        "4",
        "5",
    ]


def test_lifecycle_reachability_order_and_native_registry_are_sealed() -> None:
    contract = build_contract(ASSETS, GAME_DAT)
    reachability = contract["timelineReachability"]
    assert [
        (row["name"], row["characterId"], row["depth"]) for row in reachability["root"]
    ] == [
        ("CommandUI", 86, 17),
        ("CommandButtons", 114, 44),
        ("AutoAbilityOverlays", 122, 90),
    ]
    command = reachability["commandButtons"]
    assert command["numericButtonFrames"] == ["1", "2", "3", "4", "5", "0"]
    assert command["glassTargets"] == [f"glass{index}" for index in range(6)]
    assert command["toggleFlashTargets"] == [
        "toggleFlash0",
        "toggleFlash1",
        "toggleFlash2",
        "toggleFlash3",
    ]
    assert command["subMenuTargets"] == [
        "subMenu0",
        "subMenu1",
        "subMenu2",
        "subMenu3",
    ]
    assert command["showSourceOrder"][0] == {
        "kind": "action-script",
        "sourceOffset": 169256,
        "rawRecordType": 1,
    }
    assert all(row["rawRecordType"] == 3 for row in command["showSourceOrder"][2:])
    assert [row["symbol"] for row in contract["lifecycleCallers"]] == [
        "CommandButtonToggleFlash",
        "MovieClipFrame",
        "CommandButtonSubMenu",
    ]
    registry = contract["nativeRuntimeEvidence"]["hostRegistry"]
    assert [row["handlerVirtualAddress"] for row in registry] == [
        "0x00929698",
        "0x009297A0",
        "0x009297DD",
        "0x009298E0",
        "0x0092991E",
        "0x00929A21",
    ]
    assert contract["nativeRuntimeEvidence"]["indexPolicy"] == (
        "parse index and accept only 0 through 5"
    )
    assert (
        contract["nativeRuntimeEvidence"]["frameScheduling"][
            "sameFramePlacementsVisibleToAction"
        ]
        is True
    )


def test_remaining_gates_are_minimal_and_specific() -> None:
    contract = build_contract(ASSETS, GAME_DAT)
    assert [row["id"] for row in contract["remainingTraceGates"]] == [
        "skill-upgrade-root-method-effects",
        "command-child-lifecycle-host-result",
    ]
    assert contract["remainingTraceGates"][0]["scenario"] == (
        "enter CommandUI _show once while InGame is true"
    )
    assert contract["remainingTraceGates"][1]["scenario"] == (
        "one CommandButtons show-hide cycle"
    )


def test_oracle_output_is_byte_deterministic(tmp_path: Path) -> None:
    contract = build_contract(ASSETS, GAME_DAT)
    first = tmp_path / "first.json"
    second = tmp_path / "second.json"
    write_contract(contract, first)
    write_contract(
        build_contract_from_payloads(_payloads(), GAME_DAT.read_bytes()), second
    )
    assert first.read_bytes() == second.read_bytes()


def test_changed_source_and_native_identity_fail_closed() -> None:
    payloads = _payloads()
    changed_source = bytearray(payloads["Palantir.apt"])
    changed_source[169256] ^= 1
    payloads["Palantir.apt"] = bytes(changed_source)
    with pytest.raises(HudPalantirCommandOracleError, match="source identity changed"):
        build_contract_from_payloads(payloads, GAME_DAT.read_bytes())

    changed_game = bytearray(GAME_DAT.read_bytes())
    changed_game[0x529687] ^= 1
    with pytest.raises(
        HudPalantirCommandOracleError, match="game.dat identity changed"
    ):
        build_contract_from_payloads(_payloads(), bytes(changed_game))
