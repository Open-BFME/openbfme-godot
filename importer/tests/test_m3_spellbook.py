from __future__ import annotations

import copy

import pytest

from openbfme_importer.m3_pack_expansion import extract_command_points, extract_spellbook
from importer.tests.test_m3_pack_expansion import fixture_report


def spell_fixture() -> tuple[dict, bytes, bytes]:
    report = fixture_report()
    sciences = [f"SCIENCE_Men{i:02d}" for i in range(12)]
    powers = [f"SpellBookMen{i:02d}" for i in range(12)]
    report["dependencies"] = {"spellbookSciences": sciences, "spellbookSpecialPowers": powers}
    for index, (science, power) in enumerate(zip(sciences, powers, strict=True)):
        report["definitions"]["commandButtons"].extend([
            {"id": f"Purchase_{index}", "fields": {"Science": [science], "ButtonImage": [f"SpellIcon_{index}"], "TextLabel": [f"TEXT:Spell{index}"], "DescriptLabel": [f"TEXT:SpellTip{index}"]}},
            {"id": f"Cast_{index}", "fields": {"SpecialPower": [power], "ButtonImage": [f"SpellIcon_{index}"], "TextLabel": [f"TEXT:Spell{index}"], "DescriptLabel": [f"TEXT:SpellTip{index}"]}},
        ])
    science_source = "\n".join(
        f"Science {science}\n  SciencePurchasePointCost = GOOD_RANK_{1 + index // 3}_COST\n  SciencePurchasePointCostMP = {1 + index // 3}\n" + (f"  PrerequisiteSciences = {sciences[index - 1]}\n" if index else "") + "End"
        for index, science in enumerate(sciences)
    ).encode("ascii")
    power_source = "\n".join(f"SpecialPower {power}\n  ReloadTime = {30000 + index * 1000}\nEnd" for index, power in enumerate(powers)).encode("ascii")
    return report, science_source, power_source


def test_spellbook_shape_preserves_costs_tree_recharge_icons_and_tooltips() -> None:
    report, sciences, powers = spell_fixture()
    result = extract_spellbook(report, sciences, powers)
    assert result["scienceCount"] == result["powerCount"] == 12
    assert result["sciences"][0]["pointCost"] == 1
    assert result["sciences"][1]["prerequisites"] == ["SCIENCE_Men00"]
    assert result["powers"][-1]["reloadTimeMs"] == 41000
    assert all(row["icons"] and len(row["textIds"]) == 2 for row in result["sciences"] + result["powers"])


def test_spellbook_and_command_point_extractors_fail_closed_and_keep_all_modes() -> None:
    report, sciences, powers = spell_fixture()
    broken = copy.deepcopy(report)
    broken["dependencies"]["spellbookSciences"].pop()
    with pytest.raises(ValueError, match="12 sciences"):
        extract_spellbook(broken, sciences, powers)

    gamedata = b"""
GameData
 GoodCommandPointLimit = 300
 GoodCommandPoints = 100 150
 GoodCommandPointsBonus = 20
 GoodCommandPointsAI = 600 650
 GoodCommandPointsMP2 = 100 1000
 GoodCommandPointsMP3 = 100 875
 GoodCommandPointsMP4 = 100 750
 GoodCommandPointsMP5 = 100 675
 GoodCommandPointsMP6 = 100 625
 GoodCommandPointsMP7 = 100 575
 GoodCommandPointsMP8 = 100 500
End
"""
    result = extract_command_points(gamedata)
    assert result["commandPoints"]["GoodCommandPointLimit"] == [300]
    assert result["commandPoints"]["GoodCommandPointsMP8"] == [100, 500]
    with pytest.raises(ValueError, match="missing command-point fields"):
        extract_command_points(gamedata.replace(b" GoodCommandPointsMP8 = 100 500\n", b""))
