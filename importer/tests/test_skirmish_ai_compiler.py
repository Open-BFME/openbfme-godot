from __future__ import annotations

import hashlib
import json
from pathlib import Path

import pytest

from openbfme_importer.faction_slice_profile import compose_faction_profile
from openbfme_importer.skirmish_ai_compiler import (
    SKIRMISH_AI_PACK_KEY,
    SKIRMISH_AI_RUNTIME_PATH,
    compile_skirmish_ai,
)


REPO_ROOT = Path(__file__).resolve().parents[2]
BFME2_ROOT = REPO_ROOT / "workspace" / "retail-extract"
ROTWK_ROOT = (
    REPO_ROOT
    / "workspace"
    / "retail-work"
    / "editions"
    / "rotwk"
    / "cache"
    / "effective-assets"
)


def _oracle(root: Path) -> Path:
    if not (root / "data" / "ini" / "default" / "skirmishaidata.ini").is_file():
        pytest.skip(f"retail AI oracle is unavailable: {root}")
    return root


@pytest.fixture(scope="module")
def bfme2() -> dict[str, object]:
    return compile_skirmish_ai(_oracle(BFME2_ROOT), game="bfme2")


@pytest.fixture(scope="module")
def rotwk() -> dict[str, object]:
    return compile_skirmish_ai(_oracle(ROTWK_ROOT), game="rotwk")


def test_men_army_round_trips_authored_rows_verbatim(rotwk):
    army = rotwk["armies"]["MenOfTheWestArmy"]
    members = army["armyMembers"]
    assert len(members) == 14  # live pure-2.01 oracle; old brief said 11
    assert [row["name"]["value"] for row in members[:3]] == [
        "GondorFighterHorde_Member",
        "GondorArcherHorde_Member",
        "GondorKnightHorde_Member",
    ]
    assert members[0]["fields"]["PercentageOfArmyPhase1"]["value"] == "40.0"
    assert members[-2]["fields"]["PercentageOfArmyPhase3"]["value"] == "0.02"
    assert members[-1]["fields"]["PercentageOfArmyPhase3"]["value"] == "0.0"
    assert army["fields"]["MustUseCommandPointPercentage_Phase1"]["value"] == "90%"
    assert army["heroBuildOrder"]["value"] == [
        "ElvenGaladriel_RingHero",
        "RohanFrodo",
        "RohanEowyn",
        "RohanEomer",
        "GondorBoromir",
        "RohanTheoden",
        "GondorFaramir",
        "GondorAragornMP",
        "GondorGandalf",
    ]
    assert army["offensiveBuildings"]["value"] == [
        "GondorBattleTower",
        "GondorStatue",
        "GondorWell",
    ]


def test_all_combat_chain_matrices_preserve_authored_sentinel(rotwk):
    chains = rotwk["combatChains"]
    assert len(chains) == 12
    assert any("-1.0" in row["targetPriorityModifiers"]["value"] for row in chains)
    for row in chains:
        assert len(row["targetTypes"]["value"]) == len(
            row["targetPriorityModifiers"]["value"]
        )


def test_duplicate_dozer_name_is_preserved_and_rows_are_keyed_by_side(bfme2, rotwk):
    assert len(bfme2["dozerAssignments"]) == 6
    assert len(rotwk["dozerAssignments"]) == 8
    dwarf = bfme2["dozerAssignments"]["Dwarves"]
    mordor = bfme2["dozerAssignments"]["Mordor"]
    assert dwarf["name"]["value"] == mordor["name"]["value"] == "MordorDefaultDozer"
    assert dwarf["fields"]["Unit"]["value"] == "DwarvenPorter"


def test_ai_bases_cover_generic_override_and_casefolded_bse_join(bfme2, rotwk):
    assert len(bfme2["aiBases"]) == 60
    assert len(rotwk["aiBases"]) == 104
    assert any(row["gameMapToUseOn"]["value"] == "<ANY>" for row in bfme2["aiBases"])
    assert any(row["gameMapToUseOn"]["value"] != "<ANY>" for row in bfme2["aiBases"])
    assert any(row["bseVirtualPath"]["value"] != "unresolved" for row in rotwk["aiBases"])


def test_bfme2_and_rotwk_army_census(bfme2, rotwk):
    assert bfme2["census"]["armyDefinitionCount"] == 6
    assert rotwk["census"]["armyDefinitionCount"] == 8
    assert {"ArnorArmy", "AngmarArmy"} <= set(rotwk["armies"])
    assert {"ArnorArmy", "AngmarArmy"}.isdisjoint(bfme2["armies"])


def test_every_authored_fact_carries_source_and_line(rotwk):
    def visit(value):
        if isinstance(value, dict):
            if "value" in value:
                assert value["sourceIni"] in {
                    "data/ini/default/aidata.ini",
                    "data/ini/default/skirmishaidata.ini",
                }
                assert isinstance(value["line"], int) and value["line"] > 0
            for nested in value.values():
                visit(nested)
        elif isinstance(value, list):
            for nested in value:
                visit(nested)

    visit(rotwk)


def test_deprecated_retail_blocks_carry_comment_provenance(rotwk):
    deprecated = rotwk["deprecated"]
    assert deprecated["deprecatedByRetail"] is True
    assert deprecated["skirmishBuildLists"]
    assert deprecated["attackPriorities"]
    comment = deprecated["retailDeprecationComment"]
    assert "no longer in use" in comment["value"]
    assert comment["sourceIni"] == "data/ini/default/aidata.ini"
    assert comment["line"] == 207


def test_composed_fixture_pack_registers_skirmish_document(tmp_path, bfme2):
    base = {
        "format": 1,
        "id": "base",
        "title": "base",
        "pack": {"id": "bfme2-men-vslice", "version": "test", "files": {}},
        "resources": [],
        "runtime_data": {},
    }
    coverage = {
        "schema": "openbfme.faction-import-coverage",
        "schemaVersion": 0,
        "objects": [],
        "summary": {
            "convertedCount": 0,
            "converterGapCount": 0,
            "conversionComplete": True,
        },
    }
    payload = json.dumps(coverage, sort_keys=True, separators=(",", ":")).encode()
    coverage["aggregateSha256"] = hashlib.sha256(payload).hexdigest()
    (tmp_path / "men-coverage.json").write_text(json.dumps(coverage), encoding="utf-8")
    profile, receipt = compose_faction_profile(
        base,
        tmp_path,
        ["men"],
        game="bfme2",
        skirmish_ai_runtime=bfme2,
    )
    assert profile["runtime_data"][SKIRMISH_AI_RUNTIME_PATH] == bfme2
    assert profile["pack"]["files"][SKIRMISH_AI_PACK_KEY] == SKIRMISH_AI_RUNTIME_PATH
    assert receipt["skirmishAi"]["runtimePath"] == SKIRMISH_AI_RUNTIME_PATH
