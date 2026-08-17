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
EFFECTIVE = ROOT / "workspace" / "retail-work" / "cache" / "effective-assets"
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


def _shroud_corpora() -> list[tuple[str, Path]]:
    """Every retail corpus on this machine, newest edition last.

    Two reasons this does not just reuse the module-level ``EFFECTIVE``:

    1. ``ROOT`` is ``parents[2]`` of this file, which inside a git WORKTREE is
       the worktree root - and a worktree has no ``workspace``. The corpus is
       only ever in the main checkout, so the root is resolved by walking up
       until one is found. Without this the test SKIPS in every worktree, which
       is exactly how it silently failed to gate anything.
    2. The content baseline is RotWK, not BFME2 1.06, so a BFME2-only fixture
       gates nothing on a RotWK box.

    The asserted values are identical in both editions - only the line numbers
    drift (``SHROUD_CLEAR_STANDARD`` is gamedata.ini:36 in BFME2 1.06 and :38 in
    RotWK) - so the same assertions run against whichever corpora exist.
    """
    root = Path(__file__).resolve()
    for candidate in root.parents:
        if (candidate / "workspace" / "retail-work").is_dir():
            root = candidate
            break
    else:
        return []
    private = root / "workspace" / "retail-work"
    candidates = [
        ("bfme2-1.06", private / "cache" / "effective-assets"),
        ("rotwk", private / "editions" / "rotwk" / "cache" / "effective-assets"),
    ]
    paths = retail_unit_rule_source_paths((RANGER_UNIT_SPEC,))
    return [
        (name, base)
        for name, base in candidates
        if all((base / path).is_file() for path in paths)
    ]


@pytest.mark.parametrize("edition,corpus", _shroud_corpora() or [("none", None)])
def test_shroud_clearing_range_is_compiled_separately_from_vision(
    edition: str, corpus: Path | None
) -> None:
    """ShroudClearingRange is its own range and the HORDE carries the real one.

    Retail keeps two independent macro families (``gamedata.ini``
    ``SHROUD_CLEAR_*`` versus ``VISION_*``). The values below are read straight
    out of the effective corpus and are identical in BFME2 1.06 and RotWK::

      SHROUD_CLEAR_STANDARD             = 25   (gamedata.ini:36 / :38)
      GONDOR_RANGER_VISION_RANGE        = 480  (:1182 / :1942)
      GONDOR_RANGER_HORDE_VISION_RANGE  = 470  (:1183 / :1943)
      GONDOR_RANGER_HORDE_SHROUD_RANGE  = 500  (:1184 / :1944)

    The member value of 25 is deliberate: horde members must not each deshroud.
    Anything that reads the member and ignores the horde deshrouds a bubble 16x
    too small, and the two ranges must never be derived from one another - the
    ranger horde is VisionRange 470 / ShroudClearingRange 500.
    """
    if corpus is None:
        pytest.skip("no retail effective-assets corpus is present on this machine")
    paths = retail_unit_rule_source_paths((RANGER_UNIT_SPEC,))
    document = extract_retail_unit_rules(
        {path: corpus / path for path in paths},
        unit_specs=(RANGER_UNIT_SPEC,),
    )
    ranger = document["units"][0]
    assert ranger["horde"]["shroudClearingRange"]["value"] == 500, edition
    assert ranger["horde"]["visionRange"]["value"] == 470, edition
    assert ranger["member"]["shroudClearingRange"]["value"] == 25, edition
    assert ranger["member"]["visionRange"]["value"] == 480, edition
    # Provenance rides the value, like every other compiled number here: the
    # authoring line, and the macro it resolved through.
    provenance = ranger["horde"]["shroudClearingRange"]
    assert provenance["raw"] == "GONDOR_RANGER_HORDE_SHROUD_RANGE"
    assert provenance["source"]["field"] == "ShroudClearingRange"
    assert provenance["source"]["scopeName"] == "GondorRangerHorde"
    assert provenance["resolvedDefines"][0]["name"] == "GONDOR_RANGER_HORDE_SHROUD_RANGE"


def test_at_least_one_retail_corpus_is_available_for_shroud_gating() -> None:
    """A guard on the guard: if no corpus resolves, the test above is a no-op.

    Without this, the parametrized case degrades to a single skip and the
    importer half of the fog lane gates nothing - which is precisely the state
    this change was asked to fix.
    """
    if not (Path(__file__).resolve().parents[3] / "workspace").exists() and not _shroud_corpora():
        pytest.skip("no private retail corpus on this machine at all")
    assert _shroud_corpora(), (
        "no retail effective-assets corpus resolved; the ShroudClearingRange "
        "compile test cannot gate anything here"
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
