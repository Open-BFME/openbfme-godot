from __future__ import annotations

import copy
import json
from pathlib import Path

import pytest

from openbfme_importer.retail_hud_external_attachment_oracle import build_contract
from tests.retail_inputs import retail_file


ROOT = Path(__file__).resolve().parents[2]
ASSETS = ROOT / ".private" / "retail-work" / "cache" / "effective-assets"
PLAN = ROOT / ".private" / "scratch" / "hud-261-source-conversion" / "plan-a.json"
EXTERNAL = ROOT / ".private" / "scratch" / "hud-external-movies" / "contract-a.json"
WND = ROOT / ".private" / "scratch" / "hud-wnd-activation-oracle" / "contract-a.json"
GAME_DAT = retail_file("game.dat")

pytestmark = pytest.mark.skipif(
    not all(path.is_file() for path in (PLAN, EXTERNAL, WND, GAME_DAT)),
    reason="private retail HUD attachment inputs are absent",
)


def _read(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def _build() -> dict:
    return build_contract(ASSETS, _read(PLAN), _read(EXTERNAL), _read(WND), GAME_DAT)


def test_attachment_scope_order_and_placement_are_exact_and_deterministic() -> None:
    first = _build()
    second = _build()
    assert first == second
    assert first["summary"] == {
        "blockedTargetCount": 4,
        "exactAttachmentCount": 4,
        "genericVmRequiredCount": 0,
        "independentRootAllowedCount": 0,
        "sourceDefaultHiddenOrClosedCount": 4,
        "normalMenVsMenStaticallyDormantCount": 3,
        "normalMenVsMenVisibilityTraceCount": 1,
        "implementationIncluded": False,
    }
    assert first["attachmentOrder"]["fullInitialSetupOrder"] == [
        "InGameSpellBook",
        "InGameSideCommandBar",
        "InGameHelpBox",
        "InGameHeroSelect",
        "InGamePlanningMode",
    ]
    assert [row["targetScope"] for row in first["targets"]] == [
        "Palantir.root.frame0/SpellBookUI",
        "Palantir.root.frame0/helpBox",
        "Palantir.root.frame0/HeroSelectUI",
        "Palantir.root.frame0/planningModeUI",
    ]
    assert [row["placeholder"]["depth"] for row in first["targets"]] == [
        3,
        176,
        174,
        180,
    ]
    assert all(row["placeholder"]["characterId"] == 41 for row in first["targets"])
    assert all(not row["genericMovieRootAllowed"] for row in first["targets"])


def test_source_defaults_lifecycle_and_flagged_null_remain_typed() -> None:
    contract = _build()
    by_target = {
        row["targetScope"].rsplit("/", 1)[-1]: row for row in contract["targets"]
    }
    assert by_target["SpellBookUI"]["sourceRoot"]["labels"] == {"_hide": 0, "_show": 9}
    assert by_target["HeroSelectUI"]["sourceRoot"]["labels"] == {
        "_hide": 0,
        "_fadein": 9,
        "_show": 19,
    }
    assert by_target["planningModeUI"]["sourceRoot"]["labels"] == {
        "_init": 0,
        "_open": 9,
        "_close": 19,
    }
    assert [row["sourceRoot"]["initialStopFrame"] for row in contract["targets"]] == [
        8,
        0,
        8,
        8,
    ]
    assert all(row["godotInterfaceProposal"]["attach"] for row in contract["targets"])
    flagged = contract["heroSelectFlaggedNull"]
    assert flagged["sourceOffset"] == 166756
    assert flagged["flags"] == "0xb6"
    assert flagged["pointerState"] == "source-flagged-null"
    assert (
        flagged["recordSha256"]
        == "7cf6432cbd91629acd5252c69aa957a08cadffd61214ae49ed0e078dec99a135"
    )


def test_wnd_companion_and_native_lifecycle_are_accounted_without_guessing() -> None:
    contract = _build()
    assert contract["wndCompanion"]["attachmentAuthority"].startswith("none")
    assert contract["nativeLifecycle"]["retainedSlots"] == {
        "HeroSelectUI": "+0xc4",
        "helpBox": "+0xc8",
        "planningModeUI": "+0xcc",
    }
    assert contract["nativeLifecycle"]["resetClearOrder"] == [
        "HeroSelectUI",
        "helpBox",
        "planningModeUI",
    ]
    assert len(contract["nativeLifecycle"]["code"]) == 10
    assert all(
        row["sha256"] and row["byteLength"] > 0
        for row in contract["nativeLifecycle"]["code"]
    )
    assert {row["id"] for row in contract["unresolvedRuntimeTraces"]} == {
        "apt-load-completion-order",
        "hero-select-initial-visibility",
        "palantir-target-removal-order",
        "help-box-alt-anchor-runtime-value",
    }


def test_changed_authority_fails_closed() -> None:
    plan = _read(PLAN)
    plan["aggregateSha256"] = "0" * 64
    with pytest.raises(ValueError, match="261-source HUD plan changed"):
        build_contract(ASSETS, plan, _read(EXTERNAL), _read(WND), GAME_DAT)

    external = copy.deepcopy(_read(EXTERNAL))
    external["aggregateSha256"] = "0" * 64
    with pytest.raises(ValueError, match="external-movies-oracle"):
        build_contract(ASSETS, _read(PLAN), external, _read(WND), GAME_DAT)
