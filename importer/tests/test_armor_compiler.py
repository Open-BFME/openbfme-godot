from __future__ import annotations

import pytest

from openbfme_importer.armor_compiler import (
    ArmorCompilerError,
    base_weapon_targets,
    compile_armor_contract,
    compile_armor_table,
    compile_weapon_upgrades,
)
from openbfme_importer.playable_unit_compiler import (
    _ancestry,
    prepare_playable_unit_compiler,
)


ARMOR_INI = b"""
Armor TestArmor
  Armor = DEFAULT        100%
  Armor = SLASH          50%
  Armor = PIERCE         25%
  Armor = SPECIALIST     200%
  Armor = CAVALRY        150%
  Armor = SIEGE          100%
  Armor = FLAME          33%
  Armor = LOGICAL_FIRE   0%
  Armor = LOGICAL_FIRE   0%
  FlankedPenalty = 50%
End

Armor TestHeavyArmor
  Armor = DEFAULT        50%
  Armor = SLASH          25%
  Armor = PIERCE         20%
  DamageScalar = 120%
End

Armor TestConflictArmor
  Armor = DEFAULT        100%
  Armor = SLASH          50%
  Armor = SLASH          75%
End

Armor TestNoDefaultArmor
  Armor = SLASH          50%
End

Armor TestUnknownTypeArmor
  Armor = DEFAULT        100%
  Armor = INVERTED       50%
End
"""

WEAPON_INI = b"""
Weapon TestSword
  AttackRange = 10
  Damage = 40
  DamageType = SLASH
End

Weapon TestSwordUpgraded
  AttackRange = 10
  DamageNugget
    Damage = TEST_UPGRADED_SWORD_DAMAGE
    DamageType = SLASH
    DamageScalar = 200% ANY +INFANTRY -HERO
    DamageScalar = 150% ANY +HERO
  End
End

Weapon TestBow
  AttackRange = 300
  ProjectileNugget
    ProjectileTemplateName = TestArrow
    WarheadTemplateName = TestBowWarhead
    ForbiddenUpgradeNames = Upgrade_TestFireArrows
  End
  ProjectileNugget
    ProjectileTemplateName = TestFireArrow
    WarheadTemplateName = TestBowFireWarhead
    RequiredUpgradeNames = Upgrade_TestFireArrows
  End
End

Weapon TestBowWarhead
  DamageNugget
    Damage = 25
    DamageType = PIERCE
  End
End

Weapon TestBowFireWarhead
  DamageNugget
    Damage = 1
    DamageType = FLAME
    DamageScalar = 50000% NONE +MINE
  End
  DamageNugget
    Damage = TEST_FIRE_BONUS_DAMAGE
    DamageType = FLAME
    DamageScalar = 25% ALL -STRUCTURE
  End
  DamageNugget
    Damage = 25
    DamageType = PIERCE
  End
End

Weapon TestPike
  AttackRange = 10
  DamageNugget
    Damage = 60
    DamageType = SPECIALIST
    ForbiddenUpgradeNames = Upgrade_TestForgedBlades
  End
  DamageNugget
    Damage = TEST_PIKE_UPGRADED_DAMAGE
    DamageType = SPECIALIST
    DamageScalar = 200% ANY +INFANTRY -HERO
    RequiredUpgradeNames = Upgrade_TestForgedBlades
  End
End

Weapon TestBrokenUpgradedWeapon
  AttackRange = 10
End
"""

GAMEDATA_INI = b"""
#define TEST_UPGRADED_SWORD_DAMAGE 90
#define TEST_FIRE_BONUS_DAMAGE 32
#define TEST_PIKE_UPGRADED_DAMAGE 115
"""


def _object_document(body: str) -> bytes:
    return ("Object TestUnit\n" + body + "End\n").encode("cp1252")


def _documents(object_payload: bytes) -> dict[str, bytes]:
    return {
        "data/ini/object/test.ini": object_payload,
        "data/ini/armor.ini": ARMOR_INI,
        "data/ini/weapon.ini": WEAPON_INI,
        "data/ini/gamedata.ini": GAMEDATA_INI,
        "data/ini/commandset.ini": b"",
        "data/ini/commandbutton.ini": b"",
        "data/ini/playertemplate.ini": b"",
    }


def _lineage(documents: dict[str, bytes], name: str = "TestUnit"):
    prepared = prepare_playable_unit_compiler(documents)
    obj = prepared.objects[name.casefold()]
    return _ancestry(prepared.objects, obj), prepared


def test_armor_table_resolves_scalars_and_provenance() -> None:
    table = compile_armor_table(_documents(b""), "TestArmor")
    assert table["setId"] == "TestArmor"
    assert table["default"]["percent"] == 100.0
    assert table["scalars"]["slash"]["percent"] == 50.0
    assert table["scalars"]["pierce"]["percent"] == 25.0
    assert table["scalars"]["specialist"]["percent"] == 200.0
    # Duplicate rows with identical values are accepted (retail FortressArmor
    # authors LOGICAL_FIRE twice); one provenance line is kept.
    assert table["scalars"]["logical_fire"]["percent"] == 0.0
    for entry in (table["default"], *table["scalars"].values()):
        assert entry["sourceIni"] == "data/ini/armor.ini"
        assert entry["line"] > 0
    assert table["damageScalar"]["percent"] == 100.0
    assert table["flankedPenalty"]["percent"] == 50.0
    assert "no flanking model" in table["flankedPenalty"]["semantic"]


def test_armor_table_resolves_authored_damage_scalar() -> None:
    table = compile_armor_table(_documents(b""), "TestHeavyArmor")
    assert table["damageScalar"]["percent"] == 120.0
    assert table["damageScalar"]["line"] > 0
    assert "flankedPenalty" not in table


def test_armor_table_fails_closed_on_unknown_set() -> None:
    with pytest.raises(ArmorCompilerError, match="no unique authored definition"):
        compile_armor_table(_documents(b""), "MissingArmor")


def test_armor_table_fails_closed_on_conflicting_rows() -> None:
    with pytest.raises(ArmorCompilerError, match="conflicting SLASH rows"):
        compile_armor_table(_documents(b""), "TestConflictArmor")


def test_armor_table_fails_closed_on_missing_default() -> None:
    with pytest.raises(ArmorCompilerError, match="no DEFAULT row"):
        compile_armor_table(_documents(b""), "TestNoDefaultArmor")


def test_armor_table_fails_closed_on_unknown_damage_type() -> None:
    with pytest.raises(ArmorCompilerError, match="unknown damage type"):
        compile_armor_table(_documents(b""), "TestUnknownTypeArmor")


def test_armor_contract_without_armor_set_records_engine_passthrough() -> None:
    lineage, _ = _lineage(_documents(_object_document("  KindOf = INFANTRY\n")))
    contract = compile_armor_contract(_documents(b""), lineage)
    assert contract["setId"] is None
    assert "SAGE engine" in contract["semantic"]
    assert contract["upgrades"] == []


def test_armor_contract_resolves_base_and_upgrade_sets() -> None:
    payload = _object_document(
        "  KindOf = INFANTRY\n"
        "  ArmorSet\n"
        "    Conditions = None\n"
        "    Armor = TestArmor\n"
        "  End\n"
        "  ArmorSet\n"
        "    Conditions = PLAYER_UPGRADE\n"
        "    Armor = TestHeavyArmor\n"
        "  End\n"
        "  Behavior = ArmorUpgrade ArmorUpgradeModuleTag\n"
        "    TriggeredBy = Upgrade_TestHeavyArmor\n"
        "    ArmorSetFlag = PLAYER_UPGRADE\n"
        "  End\n"
    )
    documents = _documents(payload)
    lineage, _ = _lineage(documents)
    contract = compile_armor_contract(documents, lineage)
    assert contract["setId"] == "TestArmor"
    assert contract["table"]["scalars"]["slash"]["percent"] == 50.0
    assert contract["sourceIni"] == "data/ini/object/test.ini"
    assert contract["line"] > 0
    assert len(contract["upgrades"]) == 1
    upgrade = contract["upgrades"][0]
    assert upgrade["upgradeId"] == "Upgrade_TestHeavyArmor"
    assert upgrade["armorSetFlag"] == "PLAYER_UPGRADE"
    assert upgrade["setId"] == "TestHeavyArmor"
    assert upgrade["table"]["damageScalar"]["percent"] == 120.0
    assert upgrade["behavior"]["kind"] == "ArmorUpgrade"
    assert contract["excludedUpgradeSets"] == []


def test_armor_contract_defaults_armor_set_flag() -> None:
    payload = _object_document(
        "  KindOf = INFANTRY\n"
        "  ArmorSet\n"
        "    Conditions = None\n"
        "    Armor = TestArmor\n"
        "  End\n"
        "  ArmorSet\n"
        "    Conditions = PLAYER_UPGRADE\n"
        "    Armor = TestHeavyArmor\n"
        "  End\n"
        "  Behavior = ArmorUpgrade ArmorUpgradeModuleTag\n"
        "    TriggeredBy = Upgrade_TestHeavyArmor\n"
        "  End\n"
    )
    documents = _documents(payload)
    lineage, _ = _lineage(documents)
    contract = compile_armor_contract(documents, lineage)
    upgrade = contract["upgrades"][0]
    assert upgrade["armorSetFlag"] == "PLAYER_UPGRADE"
    assert "default is PLAYER_UPGRADE" in upgrade["armorSetFlagSemantic"]


def test_armor_contract_fails_closed_on_missing_referenced_set() -> None:
    payload = _object_document(
        "  KindOf = INFANTRY\n"
        "  ArmorSet\n"
        "    Conditions = None\n"
        "    Armor = MissingArmor\n"
        "  End\n"
    )
    documents = _documents(payload)
    lineage, _ = _lineage(documents)
    with pytest.raises(ArmorCompilerError, match="MissingArmor"):
        compile_armor_contract(documents, lineage)


def test_armor_contract_fails_closed_on_upgrade_without_set() -> None:
    payload = _object_document(
        "  KindOf = INFANTRY\n"
        "  ArmorSet\n"
        "    Conditions = None\n"
        "    Armor = TestArmor\n"
        "  End\n"
        "  Behavior = ArmorUpgrade ArmorUpgradeModuleTag\n"
        "    TriggeredBy = Upgrade_TestHeavyArmor\n"
        "  End\n"
    )
    documents = _documents(payload)
    lineage, _ = _lineage(documents)
    with pytest.raises(ArmorCompilerError, match="no ArmorSet gated"):
        compile_armor_contract(documents, lineage)


def test_armor_contract_records_unmatched_upgrade_sets() -> None:
    payload = _object_document(
        "  KindOf = INFANTRY\n"
        "  ArmorSet\n"
        "    Conditions = None\n"
        "    Armor = TestArmor\n"
        "  End\n"
        "  ArmorSet\n"
        "    Conditions = PLAYER_UPGRADE\n"
        "    Armor = TestHeavyArmor\n"
        "  End\n"
    )
    documents = _documents(payload)
    lineage, _ = _lineage(documents)
    contract = compile_armor_contract(documents, lineage)
    assert contract["upgrades"] == []
    assert len(contract["excludedUpgradeSets"]) == 1
    assert contract["excludedUpgradeSets"][0]["setId"] == "TestHeavyArmor"


def _weapon_object(weapon_block: str, behavior: str) -> bytes:
    return _object_document(
        "  KindOf = INFANTRY\n"
        "  ArmorSet\n"
        "    Conditions = None\n"
        "    Armor = TestArmor\n"
        "  End\n"
        f"{weapon_block}"
        f"{behavior}"
    )


def test_weapon_upgrade_compiles_weapon_swap() -> None:
    payload = _weapon_object(
        "  WeaponSet\n"
        "    Conditions = None\n"
        "    Weapon = PRIMARY TestSword\n"
        "  End\n"
        "  WeaponSet\n"
        "    Conditions = PLAYER_UPGRADE\n"
        "    Weapon = PRIMARY TestSwordUpgraded\n"
        "  End\n",
        "  Behavior = WeaponSetUpgrade WeaponSetUpgradeModuleTag\n"
        "    TriggeredBy = Upgrade_TestForgedBlades\n"
        "  End\n",
    )
    documents = _documents(payload)
    lineage, prepared = _lineage(documents)
    upgrades = compile_weapon_upgrades(
        documents,
        [lineage],
        base_weapon_targets(lineage),
        prepared.numeric_defines,
    )
    assert len(upgrades) == 1
    upgrade = upgrades[0]
    assert upgrade["kind"] == "weapon-swap"
    assert upgrade["upgradeId"] == "Upgrade_TestForgedBlades"
    assert upgrade["weaponId"] == "TestSwordUpgraded"
    assert upgrade["damage"]["value"] == 90
    assert upgrade["damage"]["expression"] == "TEST_UPGRADED_SWORD_DAMAGE"
    assert upgrade["damage"]["constantSourceIni"] == "data/ini/gamedata.ini"
    assert upgrade["damageType"] == "SLASH"
    assert [(row["percent"], row["filter"]) for row in upgrade["damageScalars"]] == [
        (200.0, "ANY +INFANTRY -HERO"),
        (150.0, "ANY +HERO"),
    ]
    assert all(row["line"] > 0 for row in upgrade["damageScalars"])


def test_weapon_upgrade_compiles_warhead_upgrade() -> None:
    payload = _weapon_object(
        "  WeaponSet\n"
        "    Conditions = None\n"
        "    Weapon = PRIMARY TestBow\n"
        "  End\n",
        "  Behavior = WeaponSetUpgrade ModuleTag_FireArrows\n"
        "    TriggeredBy = Upgrade_TestFireArrows\n"
        "  End\n",
    )
    documents = _documents(payload)
    lineage, prepared = _lineage(documents)
    upgrades = compile_weapon_upgrades(
        documents,
        [lineage],
        base_weapon_targets(lineage),
        prepared.numeric_defines,
    )
    assert len(upgrades) == 1
    upgrade = upgrades[0]
    assert upgrade["kind"] == "warhead-upgrade"
    assert upgrade["warheadId"] == "TestBowFireWarhead"
    assert upgrade["replacesWarheadId"] == "TestBowWarhead"
    nuggets = [(row["damage"]["value"], row.get("damageType")) for row in upgrade["nuggets"]]
    assert nuggets == [(1, "FLAME"), (32, "FLAME"), (25, "PIERCE")]
    bonus = upgrade["nuggets"][1]
    assert bonus["damage"]["expression"] == "TEST_FIRE_BONUS_DAMAGE"
    assert [(row["percent"], row["filter"]) for row in bonus["damageScalars"]] == [
        (25.0, "ALL -STRUCTURE")
    ]


def test_weapon_upgrade_compiles_nugget_upgrade() -> None:
    payload = _weapon_object(
        "  WeaponSet\n"
        "    Conditions = None\n"
        "    Weapon = PRIMARY TestPike\n"
        "  End\n",
        "  Behavior = WeaponSetUpgrade WeaponSetUpgradeModuleTag\n"
        "    TriggeredBy = Upgrade_TestForgedBlades\n"
        "  End\n",
    )
    documents = _documents(payload)
    lineage, prepared = _lineage(documents)
    upgrades = compile_weapon_upgrades(
        documents,
        [lineage],
        base_weapon_targets(lineage),
        prepared.numeric_defines,
    )
    assert len(upgrades) == 1
    upgrade = upgrades[0]
    assert upgrade["kind"] == "nugget-upgrade"
    assert upgrade["weaponId"] == "TestPike"
    assert len(upgrade["nuggets"]) == 1
    nugget = upgrade["nuggets"][0]
    assert nugget["damage"]["value"] == 115
    assert nugget["damageType"] == "SPECIALIST"
    assert [(row["percent"], row["filter"]) for row in nugget["damageScalars"]] == [
        (200.0, "ANY +INFANTRY -HERO")
    ]


def test_weapon_upgrade_deduplicates_repeated_behaviors() -> None:
    payload = _weapon_object(
        "  WeaponSet\n"
        "    Conditions = None\n"
        "    Weapon = PRIMARY TestPike\n"
        "  End\n",
        "  Behavior = WeaponSetUpgrade ModuleTag_One\n"
        "    TriggeredBy = Upgrade_TestForgedBlades\n"
        "  End\n"
        "  Behavior = WeaponSetUpgrade ModuleTag_Two\n"
        "    TriggeredBy = Upgrade_TestForgedBlades\n"
        "  End\n",
    )
    documents = _documents(payload)
    lineage, prepared = _lineage(documents)
    upgrades = compile_weapon_upgrades(
        documents,
        [lineage],
        base_weapon_targets(lineage),
        prepared.numeric_defines,
    )
    assert len(upgrades) == 1
    assert len(upgrades[0]["additionalBehaviors"]) == 1


def test_weapon_upgrade_fails_closed_on_unresolvable_swap() -> None:
    payload = _weapon_object(
        "  WeaponSet\n"
        "    Conditions = None\n"
        "    Weapon = PRIMARY TestSword\n"
        "  End\n"
        "  WeaponSet\n"
        "    Conditions = PLAYER_UPGRADE\n"
        "    Weapon = PRIMARY TestBrokenUpgradedWeapon\n"
        "  End\n",
        "  Behavior = WeaponSetUpgrade WeaponSetUpgradeModuleTag\n"
        "    TriggeredBy = Upgrade_TestForgedBlades\n"
        "  End\n",
    )
    documents = _documents(payload)
    lineage, prepared = _lineage(documents)
    with pytest.raises(ArmorCompilerError, match="no resolvable authored damage"):
        compile_weapon_upgrades(
            documents,
            [lineage],
            base_weapon_targets(lineage),
            prepared.numeric_defines,
        )


def test_weapon_upgrade_fails_closed_without_any_effect() -> None:
    payload = _weapon_object(
        "  WeaponSet\n"
        "    Conditions = None\n"
        "    Weapon = PRIMARY TestSword\n"
        "  End\n",
        "  Behavior = WeaponSetUpgrade WeaponSetUpgradeModuleTag\n"
        "    TriggeredBy = Upgrade_TestFireArrows\n"
        "  End\n",
    )
    documents = _documents(payload)
    lineage, prepared = _lineage(documents)
    with pytest.raises(ArmorCompilerError, match="no unique upgrade-gated"):
        compile_weapon_upgrades(
            documents,
            [lineage],
            base_weapon_targets(lineage),
            prepared.numeric_defines,
        )


# --- Descriptor integration -------------------------------------------------


def _integration_documents() -> dict[str, bytes]:
    from importer.tests.test_playable_unit_compiler import _documents

    documents = _documents()
    objects_path = "data/ini/object/units/test_units.ini"
    objects = documents[objects_path].decode("utf-8")
    objects = objects.replace(
        "Object InfantryMember\n"
        "  KindOf = PRELOAD SELECTABLE INFANTRY\n",
        "Object InfantryMember\n"
        "  KindOf = PRELOAD SELECTABLE INFANTRY\n"
        "  ArmorSet\n"
        "    Conditions = None\n"
        "    Armor = TestArmor\n"
        "  End\n"
        "  ArmorSet\n"
        "    Conditions = PLAYER_UPGRADE\n"
        "    Armor = TestHeavyArmor\n"
        "  End\n"
        "  Behavior = ArmorUpgrade ArmorUpgradeModuleTag\n"
        "    TriggeredBy = Upgrade_TestHeavyArmor\n"
        "  End\n"
        "  WeaponSet\n"
        "    Conditions = None\n"
        "    Weapon = PRIMARY TestSword\n"
        "  End\n"
        "  WeaponSet\n"
        "    Conditions = PLAYER_UPGRADE\n"
        "    Weapon = PRIMARY TestSwordUpgraded\n"
        "  End\n"
        "  Behavior = WeaponSetUpgrade WeaponSetUpgradeModuleTag\n"
        "    TriggeredBy = Upgrade_TestForgedBlades\n"
        "  End\n",
        1,
    )
    documents[objects_path] = objects.encode("utf-8")
    documents["data/ini/armor.ini"] = ARMOR_INI
    documents["data/ini/weapon.ini"] = WEAPON_INI
    documents["data/ini/gamedata.ini"] = (
        documents["data/ini/gamedata.ini"] + GAMEDATA_INI
    )
    return documents


def test_unit_descriptor_carries_compiled_armor_and_forge_upgrades() -> None:
    from openbfme_importer.playable_unit_compiler import (
        compile_playable_unit_descriptor,
        validate_playable_unit_descriptor,
    )

    descriptor = compile_playable_unit_descriptor(
        "InfantryHorde", _integration_documents()
    )
    validate_playable_unit_descriptor(descriptor)
    simulation = descriptor["gameplay"]["simulation"]
    # The shared fixture leaves unrelated movement/display gaps unresolved;
    # the armor section asserts only its own contract.
    assert "armor" not in simulation["missing"]
    armor = simulation["resolved"]["armor"]
    assert armor["setId"] == "TestArmor"
    assert armor["table"]["scalars"]["pierce"]["percent"] == 25.0
    assert armor["table"]["sourceIni"] == "data/ini/armor.ini"
    assert armor["upgrades"][0]["upgradeId"] == "Upgrade_TestHeavyArmor"
    assert armor["upgrades"][0]["table"]["damageScalar"]["percent"] == 120.0
    upgrades = simulation["resolved"]["combat"]["upgrades"]
    assert upgrades[0]["kind"] == "weapon-swap"
    assert upgrades[0]["upgradeId"] == "Upgrade_TestForgedBlades"
    assert upgrades[0]["damage"]["value"] == 90
    provenance_paths = {row["virtualPath"] for row in descriptor["sourceDocuments"]}
    assert "data/ini/armor.ini" in provenance_paths


def test_structure_descriptor_carries_compiled_armor() -> None:
    from importer.tests.test_playable_structure_compiler import (
        _structure_documents,
    )
    from openbfme_importer.playable_structure_compiler import (
        compile_playable_structure_descriptor,
        validate_playable_structure_descriptor,
    )

    documents = _structure_documents()
    objects_path = "data/ini/object/units/test_units.ini"
    objects = documents[objects_path].decode("utf-8")
    objects = objects.replace(
        "Object TestKeep\n"
        "  CommandSet = TestKeepCommandSet\n",
        "Object TestKeep\n"
        "  CommandSet = TestKeepCommandSet\n"
        "  ArmorSet\n"
        "    Conditions = None\n"
        "    Armor = TestArmor\n"
        "  End\n",
        1,
    )
    documents[objects_path] = objects.encode("utf-8")
    documents["data/ini/armor.ini"] = ARMOR_INI

    descriptor = compile_playable_structure_descriptor("TestKeep", documents)
    validate_playable_structure_descriptor(descriptor)
    armor = descriptor["gameplay"]["armor"]
    assert armor["setId"] == "TestArmor"
    assert armor["table"]["scalars"]["slash"]["percent"] == 50.0
    provenance_paths = {row["virtualPath"] for row in descriptor["sourceDocuments"]}
    assert "data/ini/armor.ini" in provenance_paths


def test_unit_descriptor_fails_closed_on_unresolvable_armor_set() -> None:
    from openbfme_importer.playable_unit_compiler import (
        PlayableUnitCompilerError,
        compile_playable_unit_descriptor,
    )

    documents = _integration_documents()
    objects_path = "data/ini/object/units/test_units.ini"
    objects = documents[objects_path].decode("utf-8")
    documents[objects_path] = objects.replace(
        "Armor = TestArmor", "Armor = MissingArmor", 1
    ).encode("utf-8")

    with pytest.raises(PlayableUnitCompilerError, match="MissingArmor"):
        compile_playable_unit_descriptor("InfantryHorde", documents)
