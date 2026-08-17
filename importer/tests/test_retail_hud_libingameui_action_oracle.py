from __future__ import annotations

import json
from pathlib import Path

import pytest

from openbfme_importer.retail_hud_libingameui_action_oracle import (
    HudLibInGameUiActionOracleError,
    build_contract,
    build_contract_from_payloads,
    write_contract,
)
from tests.retail_inputs import retail_file


ROOT = Path(__file__).resolve().parents[2]
ASSETS = ROOT / "workspace" / "retail-work" / "cache" / "effective-assets"
GAME_DAT = retail_file("game.dat")
OUTPUT = ROOT / "workspace" / "scratch" / "hud-libingameui-action-oracle"
SOURCE_NAMES = (
    "libInGameUI.apt",
    "libInGameUI.const",
    "libInGameUI.dat",
    "InGameSideCommandBar.apt",
    "InGameSideCommandBar.const",
    "InGameSideCommandBar.dat",
    "Palantir.apt",
    "Palantir.const",
    "Palantir.dat",
)

pytestmark = pytest.mark.skipif(
    not all((ASSETS / name).is_file() for name in SOURCE_NAMES)
    or not GAME_DAT.is_file(),
    reason="private retail HUD sources or BFME2 game.dat are absent",
)


def _payloads() -> dict[str, bytes]:
    return {name.casefold(): (ASSETS / name).read_bytes() for name in SOURCE_NAMES}


def test_contract_is_deterministic_and_classifies_the_program_exactly() -> None:
    first = build_contract(ASSETS, GAME_DAT)
    second = build_contract_from_payloads(_payloads(), GAME_DAT.read_bytes())
    assert first == second
    assert first["summary"] == {
        "targetProgramCount": 1,
        "localFunctionCount": 3,
        "parentContextCount": 2,
        "nativeRangeCount": 12,
        "declarationOnly": False,
        "staticTypedAdapterImplementationSafe": True,
        "runtimeSupportIncluded": False,
        "genericActionScriptVmRequired": False,
        "remainingConcreteContentGateCount": 1,
    }
    assert first["program"] == {
        "scriptId": "libingameui:37332",
        "owner": "sprite:6",
        "frameIndex": 0,
        "sourceOffset": 37332,
        "instructionOffset": 53656,
        "byteLength": 294,
        "sha256": "81298e35028262cc75249d9ee6057fc1c105369e81aeec3bb485d09a2f59cd70",
        "recordSha256": "32d5cbdb23a531eb389742291823b5884d511518f7ebbe6141e7804c091f85de",
    }


def test_local_function_bodies_branches_and_order_are_exact() -> None:
    contract = build_contract(ASSETS, GAME_DAT)
    assert [row["name"] for row in contract["functionBodies"]] == [
        "CreateContent",
        "DeleteContent",
        "",
    ]
    assert contract["functionBodies"][0]["typedEffect"] == [
        "attachMovie(contentType, contentName, 0)",
        "contentClip=this[contentName]",
        "if contentClip is defined copy placeholder _x,_y,_width,_height in order",
        "if contentClip is defined set extern[String(this)+'_ContentName']=String(contentClip)",
    ]
    assert contract["functionBodies"][1]["typedEffect"] == (
        "if contentClip is defined: contentClip.removeMovieClip()"
    )
    assert contract["functionBodies"][2]["installedAs"] == "onUnload"
    assert [row["offset"] for row in contract["branchInputs"]] == [
        53760,
        53858,
        53873,
    ]
    assert contract["firstInitializationOrder"] == [
        "install CreateContent",
        "install DeleteContent",
        "test initialized",
        "install onUnload",
        "call _parent.OnMovieClipFrameLoaded(this)",
        "set initialized=true",
    ]


def test_parent_callbacks_placement_and_native_identities_are_closed() -> None:
    contract = build_contract(ASSETS, GAME_DAT)
    contexts = contract["parentHostMethods"]
    assert [row["context"] for row in contexts] == [
        "ingamesidecommandbar",
        "palantir",
    ]
    assert [[callback["host"] for callback in row["callbacks"]] for row in contexts] == [
        [
            "OnAptInGameSideCommandBarButtonFrameLoaded",
            "OnAptInGameSideCommandBarButtonFrameUnloaded",
        ],
        [
            "PalantirCommandUI::OnButtonFrameLoaded",
            "PalantirCommandUI::OnButtonFrameUnloaded",
        ],
    ]
    reachability = contract["timelineAndPlacementReachability"]
    assert reachability["export"]["sourceOrder"] == [37332, 37340]
    assert reachability["sideCommand"]["buttonNames"] == [
        f"Button{index}" for index in range(12)
    ]
    assert reachability["palantir"]["instanceNamesInSourceOrder"] == [
        "1",
        "2",
        "3",
        "4",
        "5",
        "0",
    ]
    native = contract["nativeRuntimeEvidence"]
    assert native["hostRegistrations"] == {
        "OnAptInGameSideCommandBarButtonFrameLoaded": "0x009283F8",
        "OnAptInGameSideCommandBarButtonFrameUnloaded": "0x00928738",
        "PalantirCommandUI::OnButtonFrameLoaded": "0x00929698",
        "PalantirCommandUI::OnButtonFrameUnloaded": "0x009297A0",
    }
    assert native["contentAdapterOrder"][0] == (
        "read _level%u.%s_ContentName from extern"
    )


def test_decision_is_safe_static_metadata_not_runtime_support() -> None:
    decision = build_contract(ASSETS, GAME_DAT)["implementationDecision"]
    assert decision["declarationOnly"] is False
    assert decision["staticTypedAdapterImplementationSafe"] is True
    assert decision["runtimeSupportIncluded"] is False
    assert decision["profileBlockerRemovalAuthorized"] is False
    assert decision["runtimeTraceRequired"] is False
    assert decision["remainingGate"] == (
        "the requested contentType must exist in the converted retail movie/export "
        "closure before that concrete child can render"
    )


def test_output_is_byte_deterministic_and_payload_free() -> None:
    OUTPUT.mkdir(parents=True, exist_ok=True)
    first = OUTPUT / "pytest-a.json"
    second = OUTPUT / "pytest-b.json"
    write_contract(build_contract(ASSETS, GAME_DAT), first)
    write_contract(
        build_contract_from_payloads(_payloads(), GAME_DAT.read_bytes()), second
    )
    assert first.read_bytes() == second.read_bytes()
    decoded = json.loads(first.read_text(encoding="utf-8"))
    assert decoded["schema"] == "openbfme.private-hud-libingameui-action-oracle"
    assert "instructions" not in decoded["program"]
    assert all(not Path(row["virtualPath"]).is_absolute() for row in decoded["sources"])


def test_changed_retail_source_and_game_dat_fail_closed() -> None:
    payloads = _payloads()
    changed_apt = bytearray(payloads["libingameui.apt"])
    changed_apt[37332] ^= 1
    payloads["libingameui.apt"] = bytes(changed_apt)
    with pytest.raises(
        HudLibInGameUiActionOracleError, match="source identity changed"
    ):
        build_contract_from_payloads(payloads, GAME_DAT.read_bytes())

    changed_game = bytearray(GAME_DAT.read_bytes())
    changed_game[0x5C2943] ^= 1
    with pytest.raises(
        HudLibInGameUiActionOracleError, match="pinned BFME2 1.06 executable"
    ):
        build_contract_from_payloads(_payloads(), bytes(changed_game))


def test_output_cannot_escape_private_.private(tmp_path: Path) -> None:
    with pytest.raises(HudLibInGameUiActionOracleError, match="under workspace"):
        write_contract(build_contract(ASSETS, GAME_DAT), tmp_path / "contract.json")
