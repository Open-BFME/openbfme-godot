"""Cross-faction structure-upgrade pack completeness probe.

The N-team sim-completion packet reported that cross-faction matches field a
faction's builder + fortress but not its combat hordes, because horde
OBJECT_UPGRADE purchases (e.g. Elven forged blades) draw their damage/effect
from structure-upgrade contracts that (in the *simulation*) collapse to a
single faction. This module proves the boundary is NOT in the importer: every
faction's compiled pack documents already carry that faction's complete
horde-referenced structure-upgrade contracts, and two factions sharing one
effective INI view never drop or overwrite each other's contracts.

The compilers are per-target and read a shared effective INI view, so the only
honest way to demonstrate completeness is to compile both factions from ONE
document mapping that carries both — exactly the cross-faction condition the
sim hits — and assert each descriptor is self-contained.

Fixtures are synthetic INI strings (never retail bytes): the base view is the
proven "Men" integration fixture (InfantryHorde with a WeaponSetUpgrade forged
-blades effect); an "Elven" faction (horde + member + forge) is layered on top.
"""

from __future__ import annotations

from openbfme_importer.playable_structure_compiler import (
    compile_playable_structure_descriptor,
    validate_playable_structure_descriptor,
)
from openbfme_importer.playable_unit_compiler import (
    compile_playable_unit_descriptor,
    validate_playable_unit_descriptor,
)
from importer.tests.test_armor_compiler import (
    GAMEDATA_INI,
    WEAPON_INI,
    _integration_documents,
)


_OBJECTS_PATH = "data/ini/object/units/test_units.ini"


def _cross_faction_documents() -> dict[str, bytes]:
    """One effective INI view carrying two factions' upgrade contracts.

    The base "Men" faction (InfantryHorde) already resolves a WeaponSetUpgrade
    forged-blades effect (Upgrade_TestForgedBlades) into combat.upgrades. A
    second "Elven" faction is layered on: a horde whose member authors its own
    forged-blades WeaponSetUpgrade (Upgrade_ElvenForgedBlades), an OBJECT_UPGRADE
    purchase button on the horde command set, and a forge structure selling the
    gating PLAYER technology. Both factions' contracts coexist in ONE view.
    """

    documents = _integration_documents()
    objects = documents[_OBJECTS_PATH].decode("utf-8")
    objects += """
Object ElvenWarriorMember
  KindOf = PRELOAD SELECTABLE INFANTRY
  BuildCost = 100
  BuildTime = 10
  VisionRange = 200
  SelectPortrait = UPElvenWarriorMember
  Draw = W3DScriptedModelDraw ModuleTag_Draw
    DefaultModelConditionState
      Model = ElvenWarriorMemberModel
    End
  End
  ArmorSet
    Conditions = None
    Armor = TestArmor
  End
  WeaponSet
    Conditions = None
    Weapon = PRIMARY ElvenBlade
  End
  WeaponSet
    Conditions = PLAYER_UPGRADE
    Weapon = PRIMARY ElvenBladeUpgraded
  End
  Behavior = WeaponSetUpgrade ElvenForgedBladesModuleTag
    TriggeredBy = Upgrade_ElvenForgedBlades
  End
End

Object ElvenWarriorHorde
  KindOf = PRELOAD SELECTABLE HORDE
  BuildCost = 500
  BuildTime = 30
  CommandPoints = 20
  VisionRange = 300
  SelectPortrait = UPElvenWarriorHorde
  CommandSet = ElvenWarriorHordeCommandSet
  VoiceSelect = ElvenWarriorHordeVoiceSelect
  VoicePriority = 43
  VoiceMove = ElvenWarriorHordeVoiceMove
  VoiceAttack = ElvenWarriorHordeVoiceAttack
  Draw = W3DScriptedModelDraw ModuleTag_Draw
    DefaultModelConditionState
      Model = ElvenWarriorHordeModel
    End
  End
  Behavior = HordeContain ModuleTag_HordeContain
    InitialPayload = ElvenWarriorMember 10
  End
End

Object ElvenBuilder
  CommandSet = ElvenBuilderCommandSet
  KindOf = INFANTRY DOZER
End

Object ElvenForge
  CommandSet = ElvenForgeCommandSet
  KindOf = SELECTABLE STRUCTURE
  BuildCost = 400
  BuildTime = 30.0
  Body = StructureBody ModuleTag_Body
    MaxHealth = 2000
  End
End
"""
    documents[_OBJECTS_PATH] = objects.encode("utf-8")

    # Give the Elven horde an authored UNIT_BUILD producer (the universal
    # factory already exists in the base view); a horde no structure trains is
    # not a playable unit.
    documents["data/ini/commandset.ini"] = (
        documents["data/ini/commandset.ini"]
        .decode("utf-8")
        .replace(
            "CommandSet UniversalFactoryCommandSet\n  1 = Command_BuildInfantry\n",
            "CommandSet UniversalFactoryCommandSet\n  1 = Command_BuildInfantry\n"
            "  8 = Command_BuildElvenWarriorHorde\n",
            1,
        )
        + """
CommandSet ElvenWarriorHordeCommandSet
  1 = Command_PurchaseElvenForgedBlades
End
CommandSet ElvenForgeCommandSet
  1 = Command_ResearchElvenForgedBlades
End
CommandSet ElvenBuilderCommandSet
  1 = Command_ConstructElvenForge
End
"""
    ).encode("utf-8")

    documents["data/ini/commandbutton.ini"] = (
        documents["data/ini/commandbutton.ini"].decode("utf-8")
        + """
CommandButton Command_BuildElvenWarriorHorde
  Command = UNIT_BUILD
  Object = ElvenWarriorHorde
  ButtonImage = BIElvenWarriorHorde
  TextLabel = CONTROLBAR:ElvenWarriorHorde
  DescriptLabel = CONTROLBAR:ToolTipElvenWarriorHorde
End
CommandButton Command_PurchaseElvenForgedBlades
  Command = OBJECT_UPGRADE
  Upgrade = Upgrade_ElvenForgedBlades
  NeededUpgrade = Upgrade_ElvenTechForgedBlades
  Options = NEED_UPGRADE OK_FOR_MULTI_SELECT CANCELABLE
  TextLabel = CONTROLBAR:PurchaseElvenForgedBlades
  DescriptLabel = CONTROLBAR:ToolTipPurchaseElvenForgedBlades
  ButtonImage = BRElvenForgedBlades
End
CommandButton Command_ResearchElvenForgedBlades
  Command = PLAYER_UPGRADE
  Upgrade = Upgrade_ElvenTechForgedBlades
  Options = CANCELABLE
  TextLabel = CONTROLBAR:ResearchElvenForgedBlades
  DescriptLabel = CONTROLBAR:ToolTipResearchElvenForgedBlades
  ButtonImage = BGElvenForgedBlades
End
CommandButton Command_ConstructElvenForge
  Command = PORTER_CONSTRUCT
  Object = ElvenForge
  ButtonImage = BEElvenForge
End
"""
    ).encode("utf-8")

    documents["data/ini/upgrade.ini"] = b"""
Upgrade Upgrade_ElvenForgedBlades
  Type = OBJECT
  BuildCost = 300
  BuildTime = 10
End
Upgrade Upgrade_ElvenTechForgedBlades
  Type = PLAYER
  BuildCost = 1000
  BuildTime = 30
End
"""

    documents["data/ini/weapon.ini"] = WEAPON_INI + b"""
Weapon ElvenBlade
  AttackRange = 10
  Damage = 45
  DamageType = SLASH
End

Weapon ElvenBladeUpgraded
  AttackRange = 10
  DamageNugget
    Damage = ELVEN_UPGRADED_SWORD_DAMAGE
    DamageType = SLASH
  End
End
"""
    documents["data/ini/gamedata.ini"] = (
        documents["data/ini/gamedata.ini"]
        + GAMEDATA_INI
        + b"#define ELVEN_UPGRADED_SWORD_DAMAGE 95\n"
    )
    return documents


def _combat_upgrades(descriptor: dict) -> list[dict]:
    combat = descriptor["gameplay"]["simulation"]["resolved"].get("combat", {})
    return list(combat.get("upgrades", []))


def test_non_men_pack_carries_its_horde_referenced_upgrade_contracts() -> None:
    # The Elven horde's structure-upgrade contract is complete in its own pack
    # document: the OBJECT_UPGRADE purchase surface (where/what it costs) AND
    # the WeaponSetUpgrade damage effect (what the purchase does).
    documents = _cross_faction_documents()
    elf = compile_playable_unit_descriptor("ElvenWarriorHorde", documents)
    validate_playable_unit_descriptor(elf)

    purchases = elf["gameplay"]["upgradeCommands"]
    assert [row["upgradeId"] for row in purchases] == ["Upgrade_ElvenForgedBlades"]
    assert purchases[0]["neededUpgradeIds"] == ["Upgrade_ElvenTechForgedBlades"]
    assert purchases[0]["cost"] == 300

    effects = _combat_upgrades(elf)
    assert [row["upgradeId"] for row in effects] == ["Upgrade_ElvenForgedBlades"]
    assert effects[0]["kind"] == "weapon-swap"
    assert effects[0]["damage"]["value"] == 95

    # The gating PLAYER technology's sale is complete in the Elven forge's own
    # pack document.
    forge = compile_playable_structure_descriptor("ElvenForge", documents)
    validate_playable_structure_descriptor(forge)
    research_rows = forge["gameplay"]["research"]["upgrades"]
    assert [row["upgradeId"] for row in research_rows] == [
        "Upgrade_ElvenTechForgedBlades"
    ]
    assert research_rows[0]["cost"] == 1000


def test_coexisting_factions_never_drop_or_leak_each_others_contracts() -> None:
    # Both factions share ONE effective INI view (the cross-faction condition
    # the sim hits). Each faction's pack document stays self-contained: the
    # Elven horde carries only the Elven forged-blades effect and the Men horde
    # carries only its own. Nothing collapses to a single faction.
    documents = _cross_faction_documents()

    elf_effects = _combat_upgrades(
        compile_playable_unit_descriptor("ElvenWarriorHorde", documents)
    )
    men_effects = _combat_upgrades(
        compile_playable_unit_descriptor("InfantryHorde", documents)
    )

    elf_ids = {row["upgradeId"] for row in elf_effects}
    men_ids = {row["upgradeId"] for row in men_effects}

    assert elf_ids == {"Upgrade_ElvenForgedBlades"}
    assert men_ids == {"Upgrade_TestForgedBlades"}
    # No cross-faction contamination in either direction.
    assert "Upgrade_TestForgedBlades" not in elf_ids
    assert "Upgrade_ElvenForgedBlades" not in men_ids
