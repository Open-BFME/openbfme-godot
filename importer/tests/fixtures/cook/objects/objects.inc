Object CookBase
  Side = CookFaction
  BuildCost = COOK_COST
  BuildTime = COOK_TIME
  CommandPoints = 25
  KindOf = COOK_KINDOF
  Geometry = BOX
  GeometryMajorRadius = 12
  GeometryMinorRadius = 7.5
  GeometryHeight = 20
  VoiceSelect = CookVoiceOne
  VoiceSelect = CookVoiceTwo

  Body = ActiveBody ModuleTag_Body
    MaxHealth = COOK_HEALTH
    MaxHealthDamaged = 600
  End

  #include "base-behavior.inc"

  Draw = W3DScriptedModelDraw ModuleTag_Draw
    DefaultModelConditionState
      Model = COOK_MODEL
    End
  End

  WeaponSet
    Conditions = None
    Weapon = PRIMARY CookSword
    Weapon = SECONDARY CookBow
    Damage = COOK_DAMAGE
  End
End

ChildObject CookChild CookBase
  BuildCost = 800
  Behavior = ProductionUpdate ModuleTag_Production
    QueueMax = 5
  End
End

ObjectReskin CookReskin CookBase
  Draw = W3DScriptedModelDraw ModuleTag_Draw
    DefaultModelConditionState
      Model = COOK_RESKIN_MODEL
    End
  End
End

Object CookMystery
  Behavior = MadeUpBehavior ModuleTag_Mystery
    RawText = keep this text verbatim
    Repeated = alpha
    Repeated = beta
  End
End
