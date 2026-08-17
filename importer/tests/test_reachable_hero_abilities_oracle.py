"""Retail regressions for the five reachable P0 hero abilities.

The private BFME2 1.06 and RotWK 2.01 effective INI views are the authority.
No retail bytes or generated pack documents are stored in this test.
"""

from __future__ import annotations

from pathlib import Path

import pytest

from openbfme_importer.playable_unit_compiler import (
    _canonical_bytes,
    _digest,
    _ini_block_semantic,
    _named_blocks,
    compile_playable_unit_descriptor,
    prepare_playable_unit_compiler,
    validate_playable_unit_descriptor,
)


REPO = Path(__file__).resolve().parents[2]
EDITIONS = {
    "bfme2": REPO / ".private/retail-work/cache/effective-assets",
    "rotwk": REPO / ".private/retail-work/editions/rotwk/cache/effective-assets",
}
OWNERS = {
    "ElvenGaladriel_RingHero": "Command_SpecialAbilityTerribleFury",
    "GondorAragornMP": "Command_SpawnOathbreakers",
    "GondorBoromir": "Command_SpecialAbilityCaptainOfGondorBoromir",
    "GondorFaramir": "Command_SpecialAbilityCaptainOfGondor",
    "RohanTheoden": "Command_SpecialAbilityKingsFavor",
}


def _documents(root: Path) -> dict[str, bytes]:
    return {
        path.relative_to(root).as_posix().lower(): path.read_bytes()
        for path in root.rglob("*")
        if path.is_file() and path.suffix.lower() in {".ini", ".inc"}
    }


@pytest.mark.parametrize("game", ["bfme2", "rotwk"])
def test_five_reachable_hero_abilities_compile_from_retail(game: str) -> None:
    root = EDITIONS[game]
    if not root.is_dir():
        pytest.skip(f"private {game} effective-assets oracle is not present")
    documents = _documents(root)
    prepared = prepare_playable_unit_compiler(documents)

    rows: dict[str, dict[str, object]] = {}
    for object_id, command_id in OWNERS.items():
        descriptor = compile_playable_unit_descriptor(
            object_id,
            documents,
            prepared=prepared,
            game=game,
            scenario_admission={
                "role": "scenario-only",
                "surfaces": ["script-spawn"],
            },
        )
        validate_playable_unit_descriptor(descriptor)
        rows[object_id] = next(
            row for row in descriptor["abilities"] if row["id"] == command_id
        )

    galadriel = rows["ElvenGaladriel_RingHero"]
    assert galadriel["implementation"]["status"] == "implemented"
    assert galadriel["cooldownMs"] == 180000
    assert galadriel["effect"]["kind"] == "terror"
    assert galadriel["effect"]["radius"] == 200
    assert galadriel["effect"]["durationMs"] == 10000
    assert galadriel["effect"]["emotionNuggetId"] == "Terror_Base"
    assert galadriel["effect"]["engineEnum"] == "SPECIAL_SCREECH"
    assert galadriel["effect"]["sourceIni"].endswith("elven/galadriel.ini")
    assert galadriel["targeting"] == "self"
    assert any(
        "engine SPECIAL_SCREECH" in limitation
        for limitation in galadriel["implementation"]["limitations"]
    )

    aragorn = rows["GondorAragornMP"]
    assert aragorn["implementation"]["status"] == "implemented"
    assert aragorn["cooldownMs"] == (300000 if game == "bfme2" else 180000)
    assert aragorn["effect"]["kind"] == "summon"
    assert aragorn["effect"]["oclId"] == (
        "OCL_AragornArmyofTheDeadEggSmall"
        if game == "bfme2"
        else "OCL_GondorArmyofTheDeadEggSmall"
    )
    assert aragorn["targeting"] == "point"
    assert aragorn["effect"]["objects"][0]["id"] in {
        "AragornArmyofTheDeadSmallEgg",
        "GondorArmyofTheDeadSmallEgg",
    }
    leaves = aragorn["effect"]["leaves"]
    assert len(leaves["objectCreationLists"]) >= 2
    assert len(leaves["objects"]) >= 6
    assert len(leaves["weapons"]) >= 1
    assert aragorn["specialPowerContract"] == {
        "flags": ["LIMIT_DISTANCE", "NO_FORBIDDEN_OBJECTS"],
        "forbiddenObjectFilter": ["NO_SUMMON_NEAR_OBJECT_FILTER"],
        "forbiddenObjectRange": 60.0,
        "maxCastRange": 200,
        "sourceIni": "data/ini/specialpower.ini",
    }
    assert aragorn["levelGate"]["requiredLevel"] == 10

    expected_levels = {
        "GondorBoromir": 7,
        "GondorFaramir": 7,
        "RohanTheoden": 4,
    }
    for object_id, required_level in expected_levels.items():
        favor = rows[object_id]
        assert favor["implementation"]["status"] == "implemented"
        assert favor["cooldownMs"] == 180000
        assert favor["effect"]["kind"] == "experience-grant"
        assert favor["effect"]["experience"] == 50
        assert favor["effect"]["radiusEffect"] == 150
        assert favor["effect"]["startAbilityRange"] == 200.0
        assert favor["effect"]["affects"] == (
            "ANY +CAVALRY +INFANTRY -STRUCTURE -CASTLE_KEEP "
            "-BASE_FOUNDATION -HERO -MOVE_ONLY -DOZER ALLIES"
        )
        assert favor["targeting"] == "point"
        assert favor["specialPowerContract"] == {
            "flags": ["NEEDS_OBJECT_FILTER"],
            "objectFilter": ["KINGSFAVOR_OBJECTFILTER"],
            "sourceIni": "data/ini/specialpower.ini",
        }
        assert favor["levelGate"]["requiredLevel"] == required_level


def test_rotwk_carnage_uses_split_retail_hero_mode_contract() -> None:
    """Lurtz/Gothmog author duration and modifier on separate modules."""

    root = EDITIONS["rotwk"]
    if not root.is_dir():
        pytest.skip("private rotwk effective-assets oracle is not present")
    documents = _documents(root)
    prepared = prepare_playable_unit_compiler(documents)

    expected = {
        "IsengardLurtz": ("Command_SpecialAbilityLurtzCarnage", 20000),
        "MordorGothmog": ("Command_SpecialAbilityGothmogCarnage", 10000),
    }
    for object_id, (command_id, duration_ms) in expected.items():
        descriptor = compile_playable_unit_descriptor(
            object_id,
            documents,
            prepared=prepared,
            game="rotwk",
            scenario_admission={
                "role": "scenario-only",
                "surfaces": ["script-spawn"],
            },
        )
        validate_playable_unit_descriptor(descriptor)
        row = next(
            ability for ability in descriptor["abilities"]
            if ability["id"] == command_id
        )
        assert row["implementation"]["status"] == "implemented"
        assert row["effect"]["kind"] == "attribute-modifier"
        assert row["effect"]["modifierId"] == "LurtzCarnage"
        assert row["effect"]["durationMs"] == duration_ms
        assert row["effect"]["affectsSelf"] is True
        assert row["effect"]["modifiers"]


def test_rotwk_azog_fury_resolves_its_cross_file_retail_contract() -> None:
    """Azog explicitly reuses one CaH SpecialPower, not the CaH power catalog."""

    root = EDITIONS["rotwk"]
    if not root.is_dir():
        pytest.skip("private rotwk effective-assets oracle is not present")
    documents = _documents(root)
    prepared = prepare_playable_unit_compiler(documents)

    descriptor = compile_playable_unit_descriptor(
        "WildAzog",
        documents,
        prepared=prepared,
        game="rotwk",
        scenario_admission={
            "role": "scenario-only",
            "surfaces": ["script-spawn"],
        },
    )
    validate_playable_unit_descriptor(descriptor)
    fury = next(
        row for row in descriptor["abilities"] if row["id"] == "Command_AzogFury"
    )

    assert fury["slot"] == 3
    assert fury["specialPowerId"] == "SpecialAbilityCreateAHeroBattleRage_Level3"
    assert fury["cooldownMs"] == 120000
    assert fury["levelGate"] == {
        "upgradeIds": ["Upgrade_AzogFury"],
        "requiredLevel": 4,
        "sourceIni": "data/ini/experiencelevels.ini",
    }
    assert fury["specialPowerContract"] == {
        "publicTimer": False,
        "sourceIni": "data/ini/createaherospecialpowers.ini",
    }
    assert fury["implementation"]["status"] == "implemented"
    assert fury["effect"]["kind"] == "attribute-modifier"
    assert fury["effect"]["modifierId"] == "CreateAHeroBattleRageBonus_Level3"
    assert fury["effect"]["durationMs"] == 60000
    assert fury["effect"]["range"] == 1
    assert fury["effect"]["affectsSelf"] is True
    assert fury["effect"]["affectsFilter"] == "ANY +CAVALRY +INFANTRY"
    assert fury["effect"]["sourceIni"] == (
        "data/ini/attributemodifier.ini"
    )
    assert fury["effect"]["modifiers"] == [
        {"kind": "ARMOR", "value": 0.3, "application": "additive"},
        {"kind": "DAMAGE_MULT", "value": 2.0, "application": "multiplicative"},
    ]
    assert fury["effect"]["category"] == "BUFF"
    assert fury["effect"]["fxIds"] == ["FX_CAHBattleRage60"]
    assert {module["sourceIni"] for module in fury["modules"]} == {
        "data/ini/object/evilfaction/units/wild/azog.ini"
    }

    source_rows = {
        row["virtualPath"]: row for row in descriptor["sourceDocuments"]
    }
    resolved_abilities = {
        "kind": "ResolvedHeroAbilities",
        "abilities": descriptor["abilities"],
    }
    cah_blocks = _named_blocks(
        documents["data/ini/createaherospecialpowers.ini"], "SpecialPower"
    )
    cah_semantic = [
        resolved_abilities,
        _ini_block_semantic(
            "SpecialPower",
            cah_blocks["specialabilitycreateaherobattlerage_level3"],
        ),
    ]
    cah_semantic.sort(key=_canonical_bytes)
    assert source_rows["data/ini/createaherospecialpowers.ini"][
        "semanticSha256"
    ] == _digest(cah_semantic)

    primary_blocks = _named_blocks(
        documents["data/ini/specialpower.ini"], "SpecialPower"
    )
    primary_semantic = [
        resolved_abilities,
        *(
            _ini_block_semantic("SpecialPower", primary_blocks[power_id])
            for power_id in (
                "specialabilityfakeleadership2",
                "specialabilitykingsfavor",
                "specialabilitycapturebuilding",
            )
        ),
    ]
    primary_semantic.sort(key=_canonical_bytes)
    assert source_rows["data/ini/specialpower.ini"]["semanticSha256"] == _digest(
        primary_semantic
    )
