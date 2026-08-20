"""Retail FORMATION ModifierLists must reach their toggle horde descriptors.

Oracle: the pure RotWK 2.01 effective-assets tree.  A toggle link is the
authored graph ``CommandSet -> HORDE_TOGGLE_FORMATION`` plus the base horde's
``HordeContain.AlternateFormation`` and that alternate ChildObject's
``HordeContain.AttributeModifiers``.  Commented rows are intentionally absent.
"""

from __future__ import annotations

from pathlib import Path

import pytest

from openbfme_importer.playable_unit_compiler import (
    compile_playable_unit_descriptor,
    prepare_playable_unit_compiler,
)
from openbfme_importer.playable_unit_import import _source_documents


REPO_ROOT = Path(__file__).resolve().parents[2]
PRIVATE_ROOT = REPO_ROOT / "workspace" / "retail-work"
if not (PRIVATE_ROOT / "editions/rotwk/cache/effective-assets").is_dir():
    # Claude/Codex worktrees sit below <main>/.claude/worktrees/<id>.
    PRIVATE_ROOT = REPO_ROOT.parents[2] / "workspace" / "retail-work"
EFFECTIVE_ASSETS = PRIVATE_ROOT / "editions/rotwk/cache/effective-assets"


# Every active Category=FORMATION list reachable from an effective *playable*
# horde HORDE_TOGGLE_FORMATION route. The obsolete-only MordorHaradrimHorde
# has no UNIT_BUILD route and therefore no playable-unit document to compile.
# Values use the compiler's percent/100 scalar.
# Source rows: attributemodifier.ini:615-620, 628-650, 686-699, 756-823.
EXPECTED_TOGGLE_MODIFIERS = {
    "GondorTowerShieldGuardHorde": {
        "GondorTowerShieldGuardHordePorcupine": [("CRUSHED_DECELERATE", 10.0)],
    },
    "RohanSpearmenHorde": {
        "GondorTowerShieldGuardHordePorcupine": [("CRUSHED_DECELERATE", 10.0)],
    },
    "IsengardPikemanHorde": {
        "IsengardPikemanHordePorcupine": [("CRUSHED_DECELERATE", 10.0)],
    },
    "ElvenMithlondSentryHorde": {
        "ElvenMithlondSentryHordePorcupine": [("CRUSHED_DECELERATE", 10.0)],
    },
    "DwarvenPhalanxHorde": {
        "DwarvenPhalanxHordePorcupine": [("CRUSHED_DECELERATE", 10.0)],
    },
    "WildMarauderHorde": {
        "WildMarauderHordePorcupine": [("CRUSHED_DECELERATE", 10.0)],
    },
    "AngmarHillTrollHorde": {
        "WildMarauderHordePorcupine": [("CRUSHED_DECELERATE", 10.0)],
    },
    "MordorEasterlingHorde": {
        "MordorEasterlingHordePorcupine": [("CRUSHED_DECELERATE", 10.0)],
    },
    "GondorFighterHorde": {
        "GondorFighterBlock": [
            ("ARMOR", 0.25),
            ("DAMAGE_MULT", 0.8),
            ("SPEED", 0.6),
        ],
    },
    "IsengardFighterHorde": {
        "IsengardFighterHordeBlockBonus": [
            ("ARMOR", 0.25),
            ("DAMAGE_MULT", 0.8),
            ("SPEED", 0.6),
        ],
    },
    "RohanArcherHorde": {
        "RohanArcherSkirmish": [("ARMOR", -0.5), ("DAMAGE_MULT", 1.25)],
    },
    "RohanRohirrimArcherHorde": {
        "RohanHorseWegde": [
            ("ARMOR", -0.25),
            ("DAMAGE_MULT", 1.25),
            ("MINIMUM_CRUSH_VELOCITY", 0.5),
        ],
    },
}


@pytest.mark.skipif(
    not EFFECTIVE_ASSETS.is_dir(),
    reason="pure RotWK effective-assets oracle is not present",
)
def test_all_toggle_reachable_formation_rows_compile_onto_horde_documents() -> None:
    documents = _source_documents(EFFECTIVE_ASSETS)
    prepared = prepare_playable_unit_compiler(documents)
    for horde_id, expected_lists in EXPECTED_TOGGLE_MODIFIERS.items():
        descriptor = compile_playable_unit_descriptor(
            horde_id, documents, prepared=prepared, game="rotwk"
        )
        simulation = descriptor["gameplay"]["simulation"]
        formation = simulation["resolved"].get("formation", {})
        toggle = formation.get("toggle", simulation.get("formationToggle"))
        assert toggle is not None, horde_id
        compiled = {
            row["id"]: [(modifier["kind"], modifier["value"]) for modifier in row["modifiers"]]
            for row in toggle["modifierLists"]
        }
        assert compiled == expected_lists, horde_id

        # Every row stays present even when runtime support is receipt-only;
        # the sim, not the importer, owns the support decision.
        assert all(
            set(modifier) >= {
                "kind",
                "value",
                "authoredValue",
                "application",
                "runtimeSupport",
                "sourceIni",
                "line",
            }
            for row in toggle["modifierLists"]
            for modifier in row["modifiers"]
        )

    tower = compile_playable_unit_descriptor(
        "GondorTowerShieldGuardHorde",
        documents,
        prepared=prepared,
        game="rotwk",
    )
    tower_rows = tower["gameplay"]["simulation"]["resolved"]["formation"][
        "toggle"
    ]["modifierLists"][0]["modifiers"]
    assert tower_rows == [
        {
            "kind": "CRUSHED_DECELERATE",
            "value": 10.0,
            "authoredValue": "1000%",
            "application": "multiplicative",
            "runtimeSupport": "supported",
            "sourceIni": "data/ini/attributemodifier.ini",
            "line": 762,
        }
    ]
