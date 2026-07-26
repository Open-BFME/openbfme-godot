from __future__ import annotations

from pathlib import Path

import pytest

from openbfme_importer.retail_hud_men_fords_reachability_oracle import (
    _validate_native,
    build_contract,
)
from tests.retail_inputs import retail_file


ROOT = Path(__file__).resolve().parents[2]
FRAME = ROOT / ".private" / "scratch" / "hud-frame-selection" / "contract-a.json"
MAP = (
    ROOT
    / ".private"
    / "content-packs"
    / "bfme2-five-maps-106-private"
    / "maps"
    / "fords-of-isen-ii"
    / "map.json"
)
SETUP = MAP.with_name("setup.json")
GAME_DAT = retail_file("game.dat")
SIDE_APT = (
    ROOT
    / ".private"
    / "retail-work"
    / "cache"
    / "effective-assets"
    / "InGameSideCommandBar.apt"
)

pytestmark = pytest.mark.skipif(
    not all(path.is_file() for path in (FRAME, MAP, SETUP, GAME_DAT, SIDE_APT)),
    reason="private BFME2 1.06 Men/Fords reachability inputs are absent",
)


def _build() -> dict:
    return build_contract(FRAME, MAP, SETUP, GAME_DAT, SIDE_APT)


def test_oracle_is_deterministic_and_deletes_only_the_closed_research_blocker() -> None:
    first = _build()
    second = _build()
    assert first == second
    assert first["summary"] == {
        "broadResearchBlockersDeleted": 1,
        "blockersRetained": 1,
        "narrowNativeAliasGatesRetained": 1,
        "overbroadRequirementsDeleted": 1,
        "deletedRequirement": "unconditional side-command FadeIn at match load/start",
        "retainedBlockers": [
            "palantir-nondefault-selection-not-bound",
        ],
        "implementationReadyBlocker": "side-command-bar-fade-in-not-bound",
        "runtimeBindingImplementedByThisOracle": False,
        "retailPayloadEmitted": False,
        "genericDispatchAllowed": False,
    }
    assert len(first["aggregateSha256"]) == 64


def test_palantir_routes_preserve_exact_priority_and_all_four_variants() -> None:
    palantir = _build()["palantir"]
    routes = palantir["orderedRoutes"]
    assert routes[0]["zeroResult"] == {"index": 2, "state": "_goodSingle"}
    assert routes[0]["nonzeroResult"] == {"index": 4, "state": "_evilSingle"}
    assert routes[1]["zeroResult"] == {"index": 1, "state": "_good"}
    assert routes[1]["nonzeroResult"] == {"index": 3, "state": "_evil"}
    assert routes[2]["result"] == {"index": 1, "state": "_good"}
    assert all(not route.get("semanticNameProved", False) for route in routes[:2])
    assert palantir["blockerDecision"] == "retain"


def test_side_fade_in_is_selection_reachable_but_not_load_mandatory() -> None:
    side = _build()["sideCommandBar"]
    start = side["normalMatchStartDecision"]
    assert start == {
        "unconditionalAtMovieLoad": False,
        "mandatoryWithoutSelection": False,
        "reachableWithEligibleSelection": True,
        "firstTickAutoSelectionProved": False,
        "finding": (
            "Delete the overbroad requirement that FadeIn must run at movie load or "
            "unconditionally on the first match tick. Keep a narrow first-selection "
            "trace if exact initial auto-selection timing is later claimed."
        ),
    }
    assert side["nativeStateMachine"]["exactOrder"] == [
        "load root -> state 1",
        "update resolves and validates selected local object",
        "state not 2/3 -> dispatch root.FadeIn()",
        "native writes state 2",
        "APT FadeIn calls root.gotoAndPlay(target)",
        "OnAptInGameSideCommandBarFadeInComplete -> state 3",
    ]
    assert side["blockerDecision"] == "delete-broad-research-blocker-after-binding"


def test_side_fade_in_math_and_completion_are_exact() -> None:
    timeline = _build()["sideCommandBar"]["aptTimeline"]
    assert timeline["fadeInTargetExamples"] == {
        "currentframe31": 12,
        "currentframe32": 22,
        "currentframe37": 17,
        "currentframe41": 13,
        "currentframe42": 12,
    }
    completion = timeline["fadeInCompletion"]
    assert completion["triggerFrameOneBased"] == 22
    assert completion["nativeCondition"] == "callback changes state 2 to state 3 only"
    assert completion["settledStopFrameOneBased"] == 31


def test_declared_roster_and_typed_selection_predicate_are_closed() -> None:
    side = _build()["sideCommandBar"]
    roster = side["declaredRosterCommandSets"]
    assert len(roster) == 9
    assert {row["selectionKind"] for row in roster} == {"battalion", "structure"}
    assert all(row["inPalantirYesCount"] > 0 for row in roster)
    common = set(roster[0]["multiSelectCommands"])
    for row in roster[1:4]:
        common.intersection_update(row["multiSelectCommands"])
    assert {
        "Command_ToggleStance",
        "Command_AttackMove",
        "Command_Stop",
    }.issubset(common)
    eligibility = side["nativeStateMachine"]["eligibilityLoop"]
    assert eligibility["candidateCapacity"] == 32
    assert eligibility["acceptedRowCapacity"] == 15
    assert eligibility["returnCondition"] == "accepted-row count > 0"
    typed = side["godotTypedInputContract"]
    assert typed["allNineRosterSelectionsResult"] is True
    assert typed["mixedBattalionSelectionResult"] is True
    assert typed["enemyDeadOrPostMatchSelectionResult"] is False
    assert side["narrowNativeAliasGate"]["blocksGodotTypedImplementation"] is False
    assert side["narrowNativeAliasGate"]["blocksClaimOfExactNativeAliasParity"] is True


def test_native_validation_fails_closed_on_in_memory_mutation() -> None:
    game_dat = bytearray(GAME_DAT.read_bytes())
    game_dat[0] ^= 0xFF
    with pytest.raises(ValueError, match="pinned BFME2 1.06"):
        _validate_native(bytes(game_dat))
