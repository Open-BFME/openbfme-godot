"""Crush + cavalry locomotor compile. Failing-first against authored INI.

Retail knights (knightsofdolamroth.ini:494-502) author CrusherLevel=2,
CrushableLevel=1, CrushWeapon=DolAmrothLancerCrush, MinCrushVelocityPercent=40.
DolAmrothLancerCrush damage is GONDOR_KNIGHTSOFDOL_CRUSH_DAMAGE = 250
(gamedata.ini:1892). Cavalry horde locomotors author TurnTime=1000 and
MaxTurnWithoutReform=100 (locomotor.ini:837-847). Infantry stay 2000 / 45.
"""

from __future__ import annotations

from openbfme_importer.playable_unit_compiler import compile_playable_unit_descriptor


def _crush_documents() -> dict[str, bytes]:
    objects = """
Object UniversalFactory
  CommandSet = UniversalFactoryCommandSet
  KindOf = PRELOAD SELECTABLE STRUCTURE
End

Object CrushInfantryMember
  KindOf = PRELOAD SELECTABLE INFANTRY
  BuildCost = 80
  BuildTime = 15
  CommandPoints = 10
  VisionRange = 175
  SelectPortrait = UPCrushInfantry
  VoiceSelect = CrushInfantryVoiceSelect
  VoicePriority = 43
  VoiceMove = CrushInfantryVoiceMove
  VoiceAttack = CrushInfantryVoiceAttack
  CrushableLevel = 0
  LocomotorSet
    Locomotor = NormalMeleeHordeLocomotor
    Condition = SET_NORMAL
    Speed = 50
  End
  WeaponSet
    Conditions = None
    Weapon = PRIMARY CrushInfantrySword
  End
  Draw = W3DScriptedModelDraw ModuleTag_Draw
    DefaultModelConditionState
      Model = CrushInfantryModel
    End
  End
  Body = ActiveBody ModuleTag_Body
    MaxHealth = 200
  End
End

Object CrushInfantryHorde
  KindOf = PRELOAD SELECTABLE HORDE
  BuildCost = 80
  BuildTime = 15
  CommandPoints = 10
  VisionRange = 175
  SelectPortrait = UPCrushInfantry
  VoiceSelect = CrushInfantryVoiceSelect
  VoicePriority = 43
  VoiceMove = CrushInfantryVoiceMove
  VoiceAttack = CrushInfantryVoiceAttack
  CommandSet = CrushInfantryHordeCommandSet
  Draw = W3DScriptedModelDraw ModuleTag_Draw
    DefaultModelConditionState
      Model = CrushInfantryHordeModel
    End
  End
  Behavior = HordeContain ModuleTag_HordeContain
    InitialPayload = CrushInfantryMember 5
  End
End

Object CrushCavalryMember
  KindOf = PRELOAD SELECTABLE CAVALRY
  BuildCost = 400
  BuildTime = 30
  CommandPoints = 40
  VisionRange = 175
  SelectPortrait = UPCrushCavalry
  VoiceSelect = CrushCavalryVoiceSelect
  VoicePriority = 43
  VoiceMove = CrushCavalryVoiceMove
  VoiceAttack = CrushCavalryVoiceAttack
  CrushableLevel = 1
  CrusherLevel = 2
  CrushWeapon = DolAmrothLancerCrush
  MinCrushVelocityPercent = 40
  CrushDecelerationPercent = 20
  CrushKnockback = 10
  CrushZFactor = 1.0
  LocomotorSet
    Locomotor = NormalCavalryHordeLocomotor
    Condition = SET_NORMAL
    Speed = 90
  End
  WeaponSet
    Conditions = None
    Weapon = PRIMARY CrushCavalryLance
  End
  Draw = W3DScriptedModelDraw ModuleTag_Draw
    DefaultModelConditionState
      Model = CrushCavalryModel
    End
  End
  Body = ActiveBody ModuleTag_Body
    MaxHealth = 400
  End
End

Object CrushCavalryHorde
  KindOf = PRELOAD SELECTABLE HORDE
  BuildCost = 400
  BuildTime = 30
  CommandPoints = 40
  VisionRange = 175
  SelectPortrait = UPCrushCavalry
  VoiceSelect = CrushCavalryVoiceSelect
  VoicePriority = 43
  VoiceMove = CrushCavalryVoiceMove
  VoiceAttack = CrushCavalryVoiceAttack
  CommandSet = CrushCavalryHordeCommandSet
  Draw = W3DScriptedModelDraw ModuleTag_Draw
    DefaultModelConditionState
      Model = CrushCavalryHordeModel
    End
  End
  Behavior = HordeContain ModuleTag_HordeContain
    InitialPayload = CrushCavalryMember 5
  End
End
"""
    return {
        "data/ini/object/units/test_units.ini": objects.encode("utf-8"),
        "data/ini/commandset.ini": (
            b"CommandSet UniversalFactoryCommandSet\n"
            b"  1 = Command_BuildCrushInfantry\n"
            b"  2 = Command_BuildCrushCavalry\n"
            b"End\n"
            b"CommandSet CrushInfantryHordeCommandSet\n"
            b"  1 = Command_Stop\n"
            b"End\n"
            b"CommandSet CrushCavalryHordeCommandSet\n"
            b"  1 = Command_Stop\n"
            b"End\n"
        ),
        "data/ini/commandbutton.ini": (
            b"CommandButton Command_BuildCrushInfantry\n"
            b"  Command = UNIT_BUILD\n"
            b"  Object = CrushInfantryHorde\n"
            b"  ButtonImage = BICrushInfantry\n"
            b"  TextLabel = CONTROLBAR:CrushInfantry\n"
            b"  DescriptLabel = CONTROLBAR:ToolTipCrushInfantry\n"
            b"End\n"
            b"CommandButton Command_BuildCrushCavalry\n"
            b"  Command = UNIT_BUILD\n"
            b"  Object = CrushCavalryHorde\n"
            b"  ButtonImage = BICrushCavalry\n"
            b"  TextLabel = CONTROLBAR:CrushCavalry\n"
            b"  DescriptLabel = CONTROLBAR:ToolTipCrushCavalry\n"
            b"End\n"
            b"CommandButton Command_Stop\n"
            b"  Command = STOP\n"
            b"  ButtonImage = SBStop\n"
            b"  TextLabel = CONTROLBAR:Stop\n"
            b"  DescriptLabel = CONTROLBAR:ToolTipStop\n"
            b"End\n"
        ),
        "data/ini/gamedata.ini": (
            b"#define GONDOR_KNIGHTSOFDOL_CRUSH_DAMAGE 250\n"
            b"#define KNIGHT_CRUSH_DAMAGE 80\n"
        ),
        "data/ini/weapon.ini": (
            b"Weapon CrushInfantrySword\n"
            b"  AttackRange = 20.0\n"
            b"  DelayBetweenShots = 1000\n"
            b"  PreAttackDelay = 200\n"
            b"  FiringDuration = 200\n"
            b"  DamageNugget\n"
            b"    Damage = 10\n"
            b"    DamageType = SLASH\n"
            b"  End\n"
            b"End\n"
            b"Weapon CrushCavalryLance\n"
            b"  AttackRange = 20.0\n"
            b"  DelayBetweenShots = 1000\n"
            b"  PreAttackDelay = 200\n"
            b"  FiringDuration = 200\n"
            b"  DamageNugget\n"
            b"    Damage = 80\n"
            b"    DamageType = HERO\n"
            b"  End\n"
            b"End\n"
            b"Weapon DolAmrothLancerCrush\n"
            b"  AttackRange = 0.0\n"
            b"  DelayBetweenShots = 0\n"
            b"  PreAttackDelay = 0\n"
            b"  FiringDuration = 0\n"
            b"  DamageNugget\n"
            b"    Damage = GONDOR_KNIGHTSOFDOL_CRUSH_DAMAGE\n"
            b"    DamageType = CRUSH\n"
            b"    DamageFXType = SWORD_HIT\n"
            b"    DeathType = CRUSHED\n"
            b"  End\n"
            b"End\n"
        ),
        "data/ini/locomotor.ini": (
            b"Locomotor NormalMeleeHordeLocomotor\n"
            b"  Surfaces = GROUND\n"
            b"  Speed = 50\n"
            b"  Acceleration = 200\n"
            b"  Braking = 200\n"
            b"  TurnTime = 2000\n"
            b"  MaxTurnWithoutReform = 45\n"
            b"End\n"
            b"Locomotor NormalCavalryHordeLocomotor\n"
            b"  Surfaces = GROUND\n"
            b"  Speed = 90\n"
            b"  Acceleration = 200\n"
            b"  Braking = 200\n"
            b"  TurnTime = 1000\n"
            b"  MaxTurnWithoutReform = 100\n"
            b"End\n"
        ),
    }


def test_cavalry_compiles_authored_crush_and_locomotor() -> None:
    result = compile_playable_unit_descriptor("CrushCavalryHorde", _crush_documents())
    resolved = result["gameplay"]["simulation"]["resolved"]
    crush = resolved["crush"]
    assert crush["crusherLevel"]["value"] == 2
    assert crush["crushableLevel"]["value"] == 1
    assert crush["crushWeaponId"] == "DolAmrothLancerCrush"
    assert crush["crushDamage"]["value"] == 250
    assert crush["minCrushVelocityPercent"]["value"] == 40
    assert crush["crushDecelerationPercent"]["value"] == 20
    assert crush["crushKnockback"]["value"] == 10
    movement = resolved["movement"]
    assert movement["turnRateDegreesPerSecond"]["value"] == 360.0
    assert movement["maxTurnWithoutReformDegrees"]["value"] == 100


def test_infantry_compiles_crushable_and_reform_45() -> None:
    result = compile_playable_unit_descriptor("CrushInfantryHorde", _crush_documents())
    resolved = result["gameplay"]["simulation"]["resolved"]
    crush = resolved["crush"]
    assert crush["crushableLevel"]["value"] == 0
    assert "crusherLevel" not in crush or crush["crusherLevel"]["value"] == 0
    assert "crushDamage" not in crush
    movement = resolved["movement"]
    assert movement["turnRateDegreesPerSecond"]["value"] == 180.0
    assert movement["maxTurnWithoutReformDegrees"]["value"] == 45
