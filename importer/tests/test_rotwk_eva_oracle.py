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


def test_heavy_armor_ready_has_only_the_five_authored_faction_sounds() -> None:
    sides = set(_document()["events"]["UpgradeHeavyArmorReady"])
    assert sides == {"Angmar", "Isengard", "Men", "Mordor", "Wild"}
    assert {"Dwarves", "Elves"}.isdisjoint(sides)


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
        "jumpToLocation": False,
    }


def test_compiles_other_eva_events_to_block_from_oracle_bytes() -> None:
    """Retail's mutual-suppression field compiles to per-event block lists.

    All eight ``OtherEvaEventsToBlock`` uses in pure RotWK 2.01 eva.ini,
    transcribed from the oracle bytes: the UnitUnderAttack /
    UnitUnderAttackFromShroudedUnit mutual suppression, UnitAmbushed blocking
    both, and the five ring-event suppressors of DiscoveredRing.
    """

    semantics = _document()["semantics"]
    blockers = {
        event_id: fields.get("blockEvents")
        for event_id, fields in semantics.items()
        if "blockEvents" in fields
    }
    assert blockers == {
        "GaladrielDie": ["DiscoveredRing"],
        "SauronDie": ["DiscoveredRing"],
        "UnitUnderAttack": ["UnitUnderAttackFromShroudedUnit"],
        "UnitUnderAttackFromShroudedUnit": ["UnitUnderAttack"],
        "UnitAmbushed": ["UnitUnderAttack", "UnitUnderAttackFromShroudedUnit"],
        "LocalPlayerLosesRing": ["DiscoveredRing"],
        "RingPickedUpLocal": ["DiscoveredRing"],
        "RingPickedUpEnemy": ["DiscoveredRing"],
    }


def test_compiles_home_base_jump_and_delay_fields_from_oracle_bytes() -> None:
    semantics = _document()["semantics"]
    # eva.ini:260-262 (BuildQueuePausedDueToCPLimit) - "Global event ...
    # basically a global event"; the two QuietTimeMS uses sit here and on
    # BuildQueuePausedDueToFunds.
    cp_limit = semantics["BuildQueuePausedDueToCPLimit"]
    assert cp_limit["playFromHomeBase"] is True
    assert cp_limit["jumpToLocation"] is False
    assert cp_limit["quietTimeMs"] == 10_000
    # eva.ini:215 - the CannotBuild UI-feedback events stay UI feedback.
    assert semantics["CannotBuildDueToCPLimit"]["jumpToLocation"] is False
    # eva.ini:3560-3561 - "Wait until really ready" / "Don't jump to
    # unit-creations".
    troll = semantics["MountainTrollCreated"]
    assert troll == {
        "priority": 5,
        "cooldownMs": 10_000,
        "expirationMs": 10_000,
        "delayMs": 3_000,
        "jumpToLocation": False,
    }
    # eva.ini:3007 - "Don't want voice coming from dead body".
    assert semantics["GaladrielDie"]["playFromHomeBase"] is True


def test_suppression_field_counts_match_a_from_bytes_census() -> None:
    """The compiled semantics must carry every authored use, counted from the
    oracle bytes here - never from the compiler's own tables."""

    if not EVA_INI.is_file():
        pytest.fail("pure RotWK 2.01 eva.ini oracle is not present")
    import re

    header = re.compile(
        rb"^(?:NewEvaEvent|PredefinedEvaEvent)\s+([A-Za-z0-9_+.-]+)\s*$", re.IGNORECASE
    )
    census: dict[str, int] = {}
    current = False
    depth = 0
    for raw in EVA_INI.read_bytes().splitlines():
        line = raw.split(b";", 1)[0].decode("latin-1").strip()
        if not line:
            continue
        if not current:
            current = header.fullmatch(line.encode("latin-1")) is not None
            depth = 0
            continue
        if line.casefold() == "end":
            if depth == 0:
                current = False
            else:
                depth -= 1
            continue
        if "=" not in line:
            depth += 1
            continue
        name = line.split("=", 1)[0].strip().casefold()
        census[name] = census.get(name, 0) + 1

    semantics = _document()["semantics"]
    compiled = {
        "otherevaeventstoblock": sum(1 for f in semantics.values() if "blockEvents" in f),
        "alwaysplayfromhomebase": sum(
            1 for f in semantics.values() if "playFromHomeBase" in f
        ),
        "countasjumptolocation": sum(1 for f in semantics.values() if "jumpToLocation" in f),
        "millisecondstowaitbeforeplaying": sum(1 for f in semantics.values() if "delayMs" in f),
    }
    assert compiled == {
        "otherevaeventstoblock": census["otherevaeventstoblock"],
        "alwaysplayfromhomebase": census["alwaysplayfromhomebase"],
        "countasjumptolocation": census["countasjumptolocation"],
        "millisecondstowaitbeforeplaying": census["millisecondstowaitbeforeplaying"],
    }
    # The census itself is pinned so a retail-byte change cannot silently move
    # both sides of the comparison above.
    assert compiled == {
        "otherevaeventstoblock": 8,
        "alwaysplayfromhomebase": 48,
        "countasjumptolocation": 97,
        "millisecondstowaitbeforeplaying": 20,
    }
