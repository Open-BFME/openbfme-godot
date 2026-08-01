from pathlib import Path

import pytest

from openbfme_importer.profile import ImportProfile
from openbfme_importer.retail_unit_rules import (
    OUTPUT_PATH,
    RANGER_UNIT_SPEC,
    extract_retail_unit_rules,
    retail_unit_rule_source_paths,
)


ROOT = Path(__file__).resolve().parents[2]
EFFECTIVE = ROOT / ".private" / "retail-work" / "cache" / "effective-assets"
SOURCE_PATHS = (
    "data/ini/gamedata.ini",
    "data/ini/locomotor.ini",
    "data/ini/weapon.ini",
    "data/ini/attributemodifier.ini",
    "data/ini/commandbutton.ini",
    "data/ini/object/goodfaction/hordes/men/menhordes.ini",
    "data/ini/object/goodfaction/units/men/gondorfighter.ini",
    "data/ini/object/goodfaction/units/men/gondorarcher.ini",
    "data/ini/object/goodfaction/units/men/gondortowershieldguard.ini",
    "data/ini/object/goodfaction/units/men/gondorcavalry.ini",
)


def test_base_profile_declares_one_retail_unit_rule_bundle_and_source_closure() -> None:
    profile = ImportProfile.load(ROOT / "importer" / "profiles" / "men-fords-v0.json")
    resources = [item for item in profile.resources if item.converter == "retail-unit-rules"]
    assert len(resources) == 1
    assert resources[0].output == OUTPUT_PATH
    declared_patterns = {
        pattern
        for resource in profile.resources
        for pattern in resource.patterns
    }
    assert set(SOURCE_PATHS).issubset(declared_patterns)
    assert profile.pack_metadata["files"]["unitRules"] == OUTPUT_PATH


def test_effective_bfme2_106_rules_are_exact_and_provenanced() -> None:
    if not all((EFFECTIVE / path).is_file() for path in SOURCE_PATHS):
        pytest.skip("private effective BFME II 1.06 corpus is not present")
    document = extract_retail_unit_rules(
        {path: EFFECTIVE / path for path in SOURCE_PATHS}
    )
    expected = {
        "bfme2.object.gondor-fighter": (50, 11.5, 1000, 500, 1000, 40, 15),
        "bfme2.object.gondor-archer": (47, 300, 0, 200, 0, 25, 15),
        "bfme2.object.gondor-tower-guard": (37, 35, 1000, 500, 1000, 50, 15),
        "bfme2.object.gondor-knight": (80, 11.5, 1000, 500, 1000, 35, 10),
    }
    assert document["schema"] == "openbfme.retail-unit-rules"
    assert len(document["sources"]) == 10
    for unit in document["units"]:
        weapon = unit["member"]["weapon"]
        actual = (
            unit["horde"]["locomotorSet"]["speed"]["value"],
            weapon["attackRange"]["value"],
            weapon["delayBetweenShotsMs"]["value"],
            weapon["preAttackDelayMs"]["value"],
            weapon["firingDurationMs"]["value"],
            weapon["damage"]["value"],
            unit["horde"]["formation"]["memberCount"],
        )
        assert actual == expected[unit["id"]]
        for field in (
            unit["horde"]["locomotorSet"]["speed"],
            weapon["attackRange"],
            weapon["delayBetweenShotsMs"],
            weapon["preAttackDelayMs"],
            weapon["firingDurationMs"],
            weapon["damage"],
        ):
            assert field["source"]["ini"].startswith("data/ini/")
            assert field["source"]["scopeName"]
            assert field["source"]["line"] > 0

    archer = next(
        unit
        for unit in document["units"]
        if unit["id"] == "bfme2.object.gondor-archer"
    )
    weapon_sets = archer["member"]["weaponSets"]
    assert len(weapon_sets) == 2
    assert weapon_sets[0]["conditions"] == ["None"]
    assert set(weapon_sets[0]["slots"]) == {"primary", "tertiary"}
    assert weapon_sets[1]["conditions"] == ["CLOSE_RANGE", "CONTESTING_BUILDING"]
    assert set(weapon_sets[1]["slots"]) == {"primary", "secondary", "tertiary"}
    ranged = weapon_sets[0]["slots"]["primary"]
    melee = weapon_sets[1]["slots"]["secondary"]
    assert (
        ranged["name"],
        ranged["attackRange"]["value"],
        ranged["damage"]["value"],
        ranged["preAttackDelayMs"]["value"],
    ) == ("GondorArcherBow", 300, 25, 200)
    assert (
        melee["name"],
        melee["attackRange"]["value"],
        melee["damage"]["value"],
        melee["delayBetweenShotsMs"]["value"],
        melee["preAttackDelayMs"]["value"],
        melee["firingDurationMs"]["value"],
    ) == ("GondorArcherBowMelee", 11.5, 5, 1700, 666, 1000)
    switch = archer["member"]["dualWeaponSwitchDistance"]
    assert switch["value"] == 40
    assert switch["source"]["field"] == "SwitchWeaponOnCloseRangeDistance"

    command = document["stanceCommand"]
    assert command["fields"]["Stances"]["tokens"] == [
        "HoldGround",
        "Battle",
        "Aggressive",
    ]
    units_by_id = {unit["id"]: unit for unit in document["units"]}
    fighter_stances = units_by_id["bfme2.object.gondor-fighter"]["horde"]["stances"]
    assert fighter_stances["template"] == "FighterHorde"
    assert fighter_stances["states"]["Aggressive"]["damageMultiplier"] == 1.25
    assert fighter_stances["states"]["Aggressive"]["incomingDamageMultiplier"] == 1.1
    assert fighter_stances["states"]["HoldGround"]["damageMultiplier"] == 0.85
    assert fighter_stances["states"]["HoldGround"]["incomingDamageMultiplier"] == 0.75
    archer_stances = units_by_id["bfme2.object.gondor-archer"]["horde"]["stances"]
    assert archer_stances["template"] == "ArcherHorde"
    assert archer_stances["states"]["Aggressive"]["damageMultiplier"] == 1.1
    assert archer_stances["states"]["HoldGround"]["damageMultiplier"] == 0.7
    assert archer_stances["states"]["HoldGround"]["speedMultiplier"] == 0.4
    assert all(unit["member"]["health"]["value"] > 0 for unit in document["units"])


def test_effective_ranger_rules_preserve_core_combat_and_defer_long_shot() -> None:
    paths = retail_unit_rule_source_paths((RANGER_UNIT_SPEC,))
    if not all((EFFECTIVE / path).is_file() for path in paths):
        pytest.skip("private effective BFME II 1.06 Ranger corpus is not present")
    document = extract_retail_unit_rules(
        {path: EFFECTIVE / path for path in paths},
        unit_specs=(RANGER_UNIT_SPEC,),
    )
    assert len(document["sources"]) == 7
    assert len(document["units"]) == 1
    ranger = document["units"][0]
    assert ranger["id"] == "bfme2.object.gondor-ranger"
    assert ranger["hordeId"] == "bfme2.object.gondor-ranger-horde"
    assert ranger["member"]["health"]["value"] == 300
    assert ranger["horde"]["locomotorSet"]["speed"]["value"] == 50
    assert ranger["horde"]["visionRange"]["value"] == 470
    assert ranger["horde"]["formation"]["memberCount"] == 10
    assert ranger["member"]["dualWeaponSwitchDistance"]["value"] == 24
    base, close = ranger["member"]["weaponSets"]
    assert base["conditions"] == ["None"]
    assert set(base["slots"]) == {"primary", "tertiary", "quinary"}
    assert close["conditions"] == ["CLOSE_RANGE", "CONTESTING_BUILDING"]
    assert set(close["slots"]) == {"primary", "secondary", "tertiary", "quinary"}
    bow = base["slots"]["primary"]
    sword = close["slots"]["secondary"]
    assert (
        bow["name"],
        bow["attackRange"]["value"],
        bow["damage"]["value"],
        bow["preAttackDelayMs"]["value"],
    ) == ("GondorRangerBow", 400, 65, 400)
    assert (
        sword["name"],
        sword["attackRange"]["value"],
        sword["damage"]["value"],
        sword["delayBetweenShotsMs"]["value"],
        sword["preAttackDelayMs"]["value"],
        sword["firingDurationMs"]["value"],
    ) == ("GondorRangerSword", 11.5, 20, 800, 700, 800)
    for weapon_set in (base, close):
        assert weapon_set["slots"]["quinary"] == {
            "name": "MenLongShotFakeWeapon",
            "deferredSpecialAbility": True,
            "source": weapon_set["slots"]["quinary"]["source"],
        }
