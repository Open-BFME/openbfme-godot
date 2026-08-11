from __future__ import annotations

from pathlib import Path

import pytest

from openbfme_importer.faction_profile import (
    _eva_event_side_sounds,
    _eva_side_map_document,
)


ROOT = Path(__file__).resolve().parents[2]
PRIVATE_ROOT = ROOT / ".private" / "retail-work"
if not (PRIVATE_ROOT / "editions/rotwk/cache/effective-assets").is_dir() and ROOT.parent.name == "worktrees":
    PRIVATE_ROOT = ROOT.parents[2] / ".private" / "retail-work"
EVA_INI = (
    PRIVATE_ROOT
    / "editions/rotwk/cache/effective-assets/data/ini/eva.ini"
)
SIDES = {"Angmar", "Mordor", "Isengard", "Wild", "Men", "Elves", "Dwarves"}


def _document() -> dict[str, object]:
    if not EVA_INI.is_file():
        pytest.fail("pure RotWK 2.01 eva.ini oracle is not present")
    source = EVA_INI.read_bytes()
    return _eva_side_map_document(_eva_event_side_sounds(source), source)


@pytest.mark.parametrize(
    "event_id",
    [
        "CannotBuildDueToCPLimit",
        "CannotBuildDueToFunds",
        "UnitUnderAttack",
        "StructureUnderAttack",
        "CampDestroyed",
        "DiscoveredRing",
        "LocalPlayerGainsRing",
        "AlliedPlayerGainsRing",
        "EnemyPlayerGainsRing",
        "LocalPlayerLosesRing",
        "RingPickedUpLocal",
        "RingPickedUpEnemy",
        "UpgradeBannerCarrierTechnologyReady",
        "UpgradeFlameArrowsReady",
        "UpgradeForgedBladesReady",
    ],
)
def test_high_traffic_eva_event_has_all_seven_faction_sounds(event_id: str) -> None:
    document = _document()
    assert document["schemaVersion"] == 1
    assert set(document["events"][event_id]) == SIDES
    semantics = document["semantics"][event_id]
    assert semantics["priority"] > 0
    assert semantics["cooldownMs"] >= 0


def test_pure_rotwk_has_no_generic_unit_lost_announcer_event() -> None:
    events = _document()["events"]
    assert "UnitLost" not in events
    assert "BattalionLost" not in events


def test_economic_plot_loss_is_authored_silent_not_substituted() -> None:
    document = _document()
    assert "EconPlotDestroyed" not in document["events"]
    assert document["semantics"]["EconPlotDestroyed"] == {
        "priority": 4,
        "cooldownMs": 15_000,
        "expirationMs": 3_000,
    }


def test_compiles_exact_retail_cooldown_and_priority_values() -> None:
    semantics = _document()["semantics"]
    assert semantics["CannotBuildDueToCPLimit"]["priority"] == 7
    assert semantics["CannotBuildDueToCPLimit"]["cooldownMs"] == 60_000
    assert semantics["StructureUnderAttack"]["cooldownMs"] == 30_000
    assert semantics["UpgradeForgedBladesReady"] == {
        "priority": 6,
        "cooldownMs": 1_000,
        "expirationMs": 10_000,
    }
