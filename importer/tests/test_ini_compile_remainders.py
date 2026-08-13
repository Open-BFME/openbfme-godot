"""Remaining CORE INI compile gaps that land without a pack recook.

Retail cites:
- weapon.ini:5378 GondorSword FlankingBonus = 50%
- gondorfighter.ini:735 CrushRevengeWeapon = BasicInfantryCrushRevenge
  (gamedata.ini:7949 BASIC_INFANTRY_CRUSH_REVENGE_DAMAGE = 10)
- locomotor.ini:713 WaitForFormation = Yes; :722 MaxTurnWithoutReform = 45
  on NormalMeleeHordeLocomotor (horde owner, not member HumanLocomotor)
- menhordes.ini:66 KindOf tokens; descriptor already has kindOf
- menhordes.ini:80 StanceTemplate = FighterHorde; attributemodifier.ini:3132-3141
"""

from __future__ import annotations

import pytest

from openbfme_importer.playable_unit_compiler import compile_playable_unit_descriptor
from openbfme_importer.playable_unit_pack_compiler import compile_playable_unit_pack_recipe
from importer.tests.test_playable_unit_pack_compiler import _closure, _descriptor


def _remainder_documents() -> dict[str, bytes]:
    objects = """
Object UniversalFactory
  CommandSet = UniversalFactoryCommandSet
  KindOf = PRELOAD SELECTABLE STRUCTURE
End

Object RemainderInfantryMember
  KindOf = PRELOAD SELECTABLE INFANTRY PATH_THROUGH_EACH_OTHER
  BuildCost = 80
  BuildTime = 15
  CommandPoints = 10
  VisionRange = 175
  DisplayName = OBJECT:RemainderInfantry
  SelectPortrait = UPRemainderInfantry
  VoiceSelect = RemainderInfantryVoiceSelect
  VoicePriority = 43
  VoiceMove = RemainderInfantryVoiceMove
  VoiceAttack = RemainderInfantryVoiceAttack
  CrushableLevel = 0
  CrushRevengeWeapon = BasicInfantryCrushRevenge
  LocomotorSet
    Locomotor = HumanLocomotor
    Condition = SET_NORMAL
    Speed = 40
  End
  WeaponSet
    Conditions = None
    Weapon = PRIMARY GondorSword
  End
  Draw = W3DScriptedModelDraw ModuleTag_Draw
    DefaultModelConditionState
      Model = RemainderInfantryModel
    End
  End
  Body = ActiveBody ModuleTag_Body
    MaxHealth = 200
  End
End

Object RemainderInfantryHorde
  KindOf = PRELOAD SELECTABLE INFANTRY HORDE MELEE_HORDE LARGE_RECTANGLE_PATHFIND
  BuildCost = 80
  BuildTime = 15
  CommandPoints = 10
  VisionRange = 175
  DisplayName = OBJECT:RemainderInfantry
  SelectPortrait = UPRemainderInfantry
  VoiceSelect = RemainderInfantryVoiceSelect
  VoicePriority = 43
  VoiceMove = RemainderInfantryVoiceMove
  VoiceAttack = RemainderInfantryVoiceAttack
  CommandSet = RemainderInfantryHordeCommandSet
  Draw = W3DScriptedModelDraw ModuleTag_Draw
    DefaultModelConditionState
      Model = RemainderInfantryHordeModel
    End
  End
  Behavior = StancesBehavior ModuleTag_StancesBehavior
    StanceTemplate = FighterHorde
  End
  Behavior = HordeContain ModuleTag_HordeContain
    InitialPayload = RemainderInfantryMember 5
  End
  LocomotorSet
    Locomotor = NormalMeleeHordeLocomotor
    Condition = SET_NORMAL
    Speed = 55
  End
End

Object RemainderSoloMember
  KindOf = PRELOAD SELECTABLE INFANTRY
  BuildCost = 80
  BuildTime = 15
  CommandPoints = 10
  VisionRange = 175
  DisplayName = OBJECT:RemainderSolo
  SelectPortrait = UPRemainderSolo
  VoiceSelect = RemainderSoloVoiceSelect
  VoicePriority = 43
  VoiceMove = RemainderSoloVoiceMove
  VoiceAttack = RemainderSoloVoiceAttack
  LocomotorSet
    Locomotor = HumanLocomotor
    Condition = SET_NORMAL
    Speed = 40
  End
  WeaponSet
    Conditions = None
    Weapon = PRIMARY RemainderPlainSword
  End
  Draw = W3DScriptedModelDraw ModuleTag_Draw
    DefaultModelConditionState
      Model = RemainderSoloModel
    End
  End
  Body = ActiveBody ModuleTag_Body
    MaxHealth = 200
  End
End
"""
    return {
        "data/ini/object/units/test_units.ini": objects.encode("utf-8"),
        "data/ini/commandset.ini": (
            b"CommandSet UniversalFactoryCommandSet\n"
            b"  1 = Command_BuildRemainderInfantry\n"
            b"  2 = Command_BuildRemainderSolo\n"
            b"End\n"
            b"CommandSet RemainderInfantryHordeCommandSet\n"
            b"  1 = Command_Stop\n"
            b"  2 = Command_ToggleStance\n"
            b"End\n"
        ),
        "data/ini/commandbutton.ini": (
            b"CommandButton Command_BuildRemainderInfantry\n"
            b"  Command = UNIT_BUILD\n"
            b"  Object = RemainderInfantryHorde\n"
            b"  ButtonImage = BIRemainderInfantry\n"
            b"  TextLabel = CONTROLBAR:RemainderInfantry\n"
            b"  DescriptLabel = CONTROLBAR:ToolTipRemainderInfantry\n"
            b"End\n"
            b"CommandButton Command_BuildRemainderSolo\n"
            b"  Command = UNIT_BUILD\n"
            b"  Object = RemainderSoloMember\n"
            b"  ButtonImage = BIRemainderSolo\n"
            b"  TextLabel = CONTROLBAR:RemainderSolo\n"
            b"  DescriptLabel = CONTROLBAR:ToolTipRemainderSolo\n"
            b"End\n"
            b"CommandButton Command_Stop\n"
            b"  Command = STOP\n"
            b"  ButtonImage = SBStop\n"
            b"  TextLabel = CONTROLBAR:Stop\n"
            b"  DescriptLabel = CONTROLBAR:ToolTipStop\n"
            b"End\n"
            b"CommandButton Command_ToggleStance\n"
            b"  Command = TOGGLE_STANCE\n"
            b"  ButtonImage = UCCommon_HoldGroundStance UCCommon_BattleStance UCCommon_AggresiveStance\n"
            b"  TextLabel = CONTROLBAR:ToggleStanceHoldGround CONTROLBAR:ToggleStanceBattle CONTROLBAR:ToggleStanceAggressive\n"
            b"  DescriptLabel = CONTROLBAR:ToolTipToggleStanceHoldGround CONTROLBAR:ToolTipToggleStanceBattle CONTROLBAR:ToolTipToggleStanceAggressive\n"
            b"End\n"
        ),
        "data/ini/gamedata.ini": (
            b"#define GONDOR_SOLDIER_SWORD 40\n"
            b"#define BASIC_INFANTRY_CRUSH_REVENGE_DAMAGE 10\n"
        ),
        "data/ini/weapon.ini": (
            b"Weapon GondorSword\n"
            b"  AttackRange = 20.0\n"
            b"  DelayBetweenShots = 1000\n"
            b"  PreAttackDelay = 200\n"
            b"  FiringDuration = 200\n"
            b"  DamageNugget\n"
            b"    Damage = GONDOR_SOLDIER_SWORD\n"
            b"    DamageType = SLASH\n"
            b"    FlankingBonus = 50%\n"
            b"  End\n"
            b"End\n"
            b"Weapon RemainderPlainSword\n"
            b"  AttackRange = 20.0\n"
            b"  DelayBetweenShots = 1000\n"
            b"  PreAttackDelay = 200\n"
            b"  FiringDuration = 200\n"
            b"  DamageNugget\n"
            b"    Damage = 10\n"
            b"    DamageType = SLASH\n"
            b"  End\n"
            b"End\n"
            b"Weapon BasicInfantryCrushRevenge\n"
            b"  AttackRange = 10.0\n"
            b"  DamageNugget\n"
            b"    Damage = BASIC_INFANTRY_CRUSH_REVENGE_DAMAGE\n"
            b"    DamageType = SLASH\n"
            b"  End\n"
            b"End\n"
        ),
        "data/ini/locomotor.ini": (
            b"Locomotor HumanLocomotor\n"
            b"  Surfaces = GROUND\n"
            b"  Speed = 40\n"
            b"  Acceleration = 200\n"
            b"  Braking = 200\n"
            b"  TurnTime = 500\n"
            b"End\n"
            b"Locomotor NormalMeleeHordeLocomotor\n"
            b"  Surfaces = GROUND\n"
            b"  Speed = 55\n"
            b"  Acceleration = 500\n"
            b"  Braking = 500\n"
            b"  TurnTime = 2000\n"
            b"  WaitForFormation = Yes\n"
            b"  MaxTurnWithoutReform = 45\n"
            b"End\n"
        ),
        "data/ini/stances.ini": (
            b"StanceTemplate FighterHorde\n"
            b"    Stance Aggressive\n"
            b"        AttributeModifier   FighterHordeStanceAggressive\n"
            b"    End\n"
            b"    Stance HoldGround\n"
            b"        AttributeModifier   FighterHordeStanceHoldGround\n"
            b"    End\n"
            b"    Stance HoldGroundMoving\n"
            b"        AttributeModifier   FighterHordeStanceHoldGround\n"
            b"    End\n"
            b"End\n"
        ),
        "data/ini/attributemodifier.ini": (
            b"ModifierList FighterHordeStanceAggressive\n"
            b"    Modifier = ARMOR -15%\n"
            b"    Modifier = DAMAGE_MULT 125%\n"
            b"    Modifier = VISION 100% // Hmm not sure how this works, is it additive?\n"
            b"End\n"
            b"ModifierList FighterHordeStanceHoldGround\n"
            b"    Modifier = ARMOR 25%\n"
            b"    Modifier = DAMAGE_MULT 85%\n"
            b"    Modifier = VISION -90%\n"
            b"End\n"
        ),
    }


def _resolved(target: str = "RemainderInfantryHorde") -> dict:
    result = compile_playable_unit_descriptor(target, _remainder_documents())
    return result["gameplay"]["simulation"]["resolved"]


def test_flanking_bonus_compiles_from_damage_nugget() -> None:
    combat = _resolved()["combat"]
    assert combat["flankingBonus"]["value"] == 50.0
    assert "50%" in str(combat["flankingBonus"]["expression"])


def test_absent_flanking_bonus_is_not_invented() -> None:
    combat = _resolved("RemainderSoloMember")["combat"]
    assert "flankingBonus" not in combat


def test_crush_revenge_weapon_compiles_damage_10() -> None:
    crush = _resolved()["crush"]
    assert crush["crushRevengeWeaponId"] == "BasicInfantryCrushRevenge"
    assert crush["crushRevengeDamage"]["value"] == 10
    fields = compile_playable_unit_descriptor(
        "RemainderInfantryHorde", _remainder_documents()
    )["gameplay"]["memberFields"]
    assert fields["CrushRevengeWeapon"]["expression"] == "BasicInfantryCrushRevenge"


def test_absent_crush_revenge_is_not_invented() -> None:
    resolved = _resolved("RemainderSoloMember")
    crush = resolved.get("crush", {})
    assert "crushRevengeWeaponId" not in crush
    assert "crushRevengeDamage" not in crush


def test_horde_locomotor_owns_wait_for_formation_and_reform() -> None:
    resolved = _resolved()
    movement = resolved["movement"]
    assert movement["locomotorId"] == "NormalMeleeHordeLocomotor"
    assert movement["turnRateDegreesPerSecond"]["value"] == 180.0
    assert movement["maxTurnWithoutReformDegrees"]["value"] == 45
    assert movement["waitForFormation"]["value"] is True
    assert resolved["speed"]["value"] == 55
    assert resolved["memberSpeed"]["value"] == 40
    assert resolved["memberSpeed"]["definitionId"] == "HumanLocomotor"


def test_member_only_locomotor_has_no_wait_for_formation() -> None:
    movement = _resolved("RemainderSoloMember")["movement"]
    assert movement["locomotorId"] == "HumanLocomotor"
    assert movement["turnRateDegreesPerSecond"]["value"] == 720.0
    assert "waitForFormation" not in movement
    assert "maxTurnWithoutReformDegrees" not in movement


def test_kind_of_tokens_on_descriptor() -> None:
    descriptor = compile_playable_unit_descriptor(
        "RemainderInfantryHorde", _remainder_documents()
    )
    kind_of = descriptor["kindOf"]
    assert "HORDE" in kind_of["container"]
    assert "MELEE_HORDE" in kind_of["container"]
    assert "LARGE_RECTANGLE_PATHFIND" in kind_of["container"]
    assert "INFANTRY" in kind_of["primaryMember"]
    assert "PATH_THROUGH_EACH_OTHER" in kind_of["primaryMember"]


def test_pack_compiler_copies_kind_of_onto_registration() -> None:
    descriptor = _descriptor("InfantryHorde")
    assert "kindOf" in descriptor
    recipe = compile_playable_unit_pack_recipe(descriptor, _closure(descriptor))
    assert recipe["runtimeRegistration"]["kindOf"] == descriptor["kindOf"]
    assert "HORDE" in recipe["runtimeRegistration"]["kindOf"]["container"]
    assert "INFANTRY" in recipe["runtimeRegistration"]["kindOf"]["primaryMember"]


def test_stance_template_compiles_fighter_horde_modifiers() -> None:
    stances = _resolved()["stances"]
    assert stances["template"] == "FighterHorde"
    assert stances["default"] == "Battle"
    aggressive = stances["states"]["Aggressive"]
    hold = stances["states"]["HoldGround"]
    battle = stances["states"]["Battle"]
    assert aggressive["damageMultiplier"] == pytest.approx(1.25)
    assert aggressive["incomingDamageMultiplier"] == pytest.approx(1.15)
    assert aggressive["visionMultiplier"] == pytest.approx(2.0)
    assert hold["damageMultiplier"] == pytest.approx(0.85)
    assert hold["incomingDamageMultiplier"] == pytest.approx(0.75)
    assert hold["visionMultiplier"] == pytest.approx(0.1)
    assert battle["damageMultiplier"] == pytest.approx(1.0)
    assert battle["incomingDamageMultiplier"] == pytest.approx(1.0)


def test_absent_stance_template_is_not_invented() -> None:
    resolved = _resolved("RemainderSoloMember")
    assert "stances" not in resolved


def test_unit_selection_commands_are_own_commandset_not_construct() -> None:
    result = compile_playable_unit_descriptor(
        "RemainderInfantryHorde", _remainder_documents()
    )
    ui = result["presentation"]["ui"]
    selection = ui["selectionCommands"]
    assert [row["commandId"] for row in selection] == [
        "Command_Stop",
        "Command_ToggleStance",
    ]
    assert selection[0]["commandSetId"] == "RemainderInfantryHordeCommandSet"
    stance_fields = selection[1]["fields"]
    assert stance_fields["TextLabel"] == [
        "CONTROLBAR:ToggleStanceHoldGround",
        "CONTROLBAR:ToggleStanceBattle",
        "CONTROLBAR:ToggleStanceAggressive",
    ]
    assert stance_fields["DescriptLabel"] == [
        "CONTROLBAR:ToolTipToggleStanceHoldGround",
        "CONTROLBAR:ToolTipToggleStanceBattle",
        "CONTROLBAR:ToolTipToggleStanceAggressive",
    ]
    from openbfme_importer.playable_unit_import import _required_string_ids

    required = _required_string_ids(result)
    assert "CONTROLBAR:ToggleStanceHoldGround" in required
    assert "CONTROLBAR:Stop" in required
    assert "CONTROLBAR:RemainderInfantry" in required
    construct_ids = [row["commandId"] for row in ui["commands"]]
    assert construct_ids
    assert "Command_Stop" not in construct_ids
    assert all("Build" in command_id or "Construct" in command_id for command_id in construct_ids)
