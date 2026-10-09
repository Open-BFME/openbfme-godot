"""Lane WEAPON-1: the committed golden tables equal a fresh dump of the binary (skipped loudly without the image), and the
independent retail line scan gives the counts the engine tests hard-code (skipped loudly without the installs)."""
from __future__ import annotations

import os
import sys
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
# qualified imports: tools/horde_oracle has a module of the same name (dump_tables, gen_names) and `pytest tools` runs both suites in one process
sys.path.insert(0, str(ROOT))

from tools.weapon import dump_tables  # noqa: E402

DATA = ROOT / "engine" / "tests" / "data" / "weapon1"


def _image():
    try:
        return dump_tables.game_dat()
    except SystemExit:
        pytest.skip("SKIPPED LOUDLY: RW_GAME_DAT / ROTWK_INSTALL is not set (the golden tables cannot be re-dumped)")


def test_committed_tables_match_the_binary():
    fresh = dump_tables.build(_image())
    assert fresh, "no tables dumped"
    for fname, text in fresh.items():
        assert (DATA / fname).read_text() == text, fname


def test_row_counts():
    assert len((DATA / "table_weapon_template.tsv").read_text().splitlines()) == 124
    assert len((DATA / "table_weapon_template_set.tsv").read_text().splitlines()) == 10
    assert len((DATA / "list_damage_type_names.tsv").read_text().splitlines()) == 28


def test_generated_name_registry_is_current():
    from tools.weapon import gen_names

    assert gen_names.OUT.exists() and gen_names.OUT.read_text() == gen_names.render(), "run tools/weapon/gen_names.py"


def test_retail_block_counts():
    from tools.weapon import scan_retail_blocks

    inst = scan_retail_blocks.installs()
    if inst is None:
        pytest.skip("SKIPPED LOUDLY: ROTWK_INSTALL / BFME2_INSTALL are not set (the retail line scan cannot run)")
    counts = scan_retail_blocks.scan(*inst)
    # the numbers engine/tests/test_weapon_data.cpp asserts against the parsed stores
    assert counts == EXPECTED_COUNTS


EXPECTED_COUNTS = {
    "Weapon": 747, "WeaponUnique": 747, "Armor": 221, "ArmorUnique": 221, "DamageFX": 12,
    "ObjectWeaponSet": 792, "ObjectArmorSet": 1903,
    "Nuggets": {
        "DamageNugget": 653, "ProjectileNugget": 269, "MetaImpactNugget": 171, "FireLogicNugget": 65, "HordeAttackNugget": 37,
        "DOTNugget": 27, "AttributeModifierNugget": 23, "WeaponOCLNugget": 13, "ParalyzeNugget": 8, "SlaveAttackNugget": 8,
        "LuaEventNugget": 5, "GrabNugget": 4, "SpecialModelConditionNugget": 4, "EmotionWeaponNugget": 3, "StealMoneyNugget": 3,
        "OpenGateNugget": 3, "SpawnAndFadeNugget": 2, "DamageFieldNugget": 1, "DamageContainedNugget": 1,
    },
}
