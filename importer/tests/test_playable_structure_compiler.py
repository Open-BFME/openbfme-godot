import pytest

from openbfme_importer.playable_structure_compiler import (
    PlayableStructureCompilerError,
    compile_playable_structure_descriptor,
    validate_playable_structure_descriptor,
)
from openbfme_importer.playable_unit_compiler import prepare_playable_unit_compiler
from importer.tests.test_playable_unit_compiler import _documents


def _structure_documents() -> dict[str, bytes]:
    documents = _documents()
    objects_path = "data/ini/object/units/test_units.ini"
    objects = documents[objects_path].decode("utf-8")
    objects += """
Object PorterBuilder
  CommandSet = PorterCommandSet
  KindOf = INFANTRY DOZER
End

Object TestKeep
  CommandSet = TestKeepCommandSet
  KindOf = SELECTABLE STRUCTURE
  BuildCost = KEEP_BUILDCOST
  BuildTime = 45.0
  VisionRange = 200
  DisplayName = OBJECT:TestKeep
  SelectPortrait = UPTestKeep
  ButtonImage = BITestKeep
  SoundOnDamaged = KeepDamagedSound
  Draw = W3DScriptedModelDraw ModuleTag_Draw
    DefaultModelConditionState
      Model = Keep_SKN
    End
    IdleAnimationState
      Animation = Idle
        AnimationName = Keep_SKL.Keep_IDLA
      End
    End
    ModelConditionState = ACTIVELY_BEING_CONSTRUCTED PARTIALLY_CONSTRUCTED
      Model = Keep_CONS
    End
    AnimationState = ACTIVELY_BEING_CONSTRUCTED PARTIALLY_CONSTRUCTED
      Animation = Build
        AnimationName = Keep_SKL.Keep_CONSA
        AnimationMode = MANUAL
      End
    End
    ModelConditionState = DAMAGED
      Model = Keep_SKN
      ParticleSysBone = FireSmall01 FireBuildingMedium
      EnteringStateFX = FX_BuildingDamaged
    End
    ModelConditionState = REALLYDAMAGED
      Model = Keep_SKN
      EnteringStateFX = FX_BuildingReallyDamaged
    End
    ModelConditionState = RUBBLE
      Model = Keep_RUBBLE
      EnteringStateFX = FX_StructureMediumCollapse
    End
    AnimationState = RUBBLE
      Animation = Die
        AnimationName = Keep_SKL.Keep_LEVERA
        AnimationMode = ONCE
      End
    End
  End
  Draw = W3DFloorDraw ModuleTag_Bib
    ModelName = Keep_BIB
    HideIfModelConditions = AWAITING_CONSTRUCTION PARTIALLY_CONSTRUCTED
  End
  Body = StructureBody ModuleTag_Body
    MaxHealth = KEEP_HEALTH
    MaxHealthDamaged = KEEP_HEALTH_DAMAGED
    MaxHealthReallyDamaged = KEEP_HEALTH_REALLY_DAMAGED
  End
  Behavior = CommandSetUpgrade ModuleTag_Level2
    TriggeredBy = Upgrade_KeepLevel2
    CommandSet = TestKeepCommandSetLevel2
  End
  Behavior = StructureCollapseUpdate ModuleTag_Collapse
    MinCollapseDelay = 0
    MaxCollapseDelay = 0
    CollapseDamping = 0.5
    MaxShudder = 0.6
    MinBurstDelay = 250
    MaxBurstDelay = 800
    BigBurstFrequency = 4
    FXList = INITIAL FX_StructureMediumCollapse
    FXList = ALMOST_FINAL FX_StructureAlmostCollapse
    DestroyObjectWhenDone = Yes
    CollapseHeight = 155
  End
End

Object TestCitadel
  CommandSet = TestKeepCommandSet
  KindOf = STRUCTURE
  Body = StructureBody ModuleTag_Body
    MaxHealth = 500
  End
End

Object HollowKeep
  KindOf = STRUCTURE
End
"""
    documents[objects_path] = objects.encode("utf-8")
    documents["data/ini/commandset.ini"] = (
        documents["data/ini/commandset.ini"].decode("utf-8")
        + """
CommandSet PorterCommandSet
  1 = Command_ConstructTestKeep
End
CommandSet TestKeepCommandSet
  1 = Command_BuildInfantry
End
CommandSet TestKeepCommandSetLevel2
  1 = Command_BuildRanged
End
"""
    ).encode("utf-8")
    documents["data/ini/commandbutton.ini"] = (
        documents["data/ini/commandbutton.ini"].decode("utf-8")
        + """
CommandButton Command_ConstructTestKeep
  Command = PORTER_CONSTRUCT
  Object = TestKeep
  NeededUpgrade = Upgrade_StoneWork
  ButtonImage = BITestKeep
End
"""
    ).encode("utf-8")
    documents["data/ini/gamedata.ini"] = (
        documents["data/ini/gamedata.ini"]
        + b"#define KEEP_HEALTH 3000\n"
        + b"#define KEEP_HEALTH_DAMAGED 2000\n"
        + b"#define KEEP_HEALTH_REALLY_DAMAGED 1000\n"
    )
    return documents


def test_constructed_structure_compiles_deterministically() -> None:
    documents = _structure_documents()

    first = compile_playable_structure_descriptor("TestKeep", documents)
    second = compile_playable_structure_descriptor(
        "TestKeep", dict(reversed(documents.items()))
    )

    validate_playable_structure_descriptor(first)
    assert first == second
    assert first["category"] == "structure"
    assert first["production"]["evidence"] == "authored-construct-command"
    route = first["production"]["routes"][0]
    assert route["builderObjectId"] == "PorterBuilder"
    assert route["commandKind"] == "porter_construct"
    assert route["prerequisites"] == ["Upgrade_StoneWork"]
    assert route["buttonImageId"] == "BITestKeep"
    health = first["gameplay"]["health"]["primary"]
    assert health["maxHealth"] == {"authored": "KEEP_HEALTH", "value": 3000}
    assert health["maxHealthDamaged"]["value"] == 2000
    assert health["maxHealthReallyDamaged"]["value"] == 1000
    trained = {row["id"]: row for row in first["gameplay"]["trainedCommandSets"]}
    assert trained["TestKeepCommandSet"]["kind"] == "direct"
    assert trained["TestKeepCommandSetLevel2"]["kind"] == "upgraded"
    assert trained["TestKeepCommandSetLevel2"]["triggeredBy"] == [
        "Upgrade_KeepLevel2"
    ]
    assert first["presentation"]["ui"]["DisplayName"]
    assert len(first["descriptorSha256"]) == 64


def test_prepared_inputs_preserve_structure_identity() -> None:
    documents = _structure_documents()
    expected = compile_playable_structure_descriptor("TestKeep", documents)
    prepared = prepare_playable_unit_compiler(documents)

    actual = compile_playable_structure_descriptor(
        "TestKeep", documents, prepared=prepared
    )

    assert actual == expected


def test_engine_spawned_composite_requires_declared_policy() -> None:
    documents = _structure_documents()

    with pytest.raises(
        PlayableStructureCompilerError, match="not a declared engine-spawned"
    ):
        compile_playable_structure_descriptor("TestCitadel", documents)

    descriptor = compile_playable_structure_descriptor(
        "TestCitadel", documents, engine_spawned_roots=("TestCitadel",)
    )
    assert descriptor["production"]["evidence"] == "engine-spawned-composite"
    assert descriptor["production"]["routes"] == []


def test_foundation_construct_command_is_an_authored_route() -> None:
    documents = _structure_documents()
    documents["data/ini/commandbutton.ini"] = (
        documents["data/ini/commandbutton.ini"].decode("utf-8")
        + """
CommandButton Command_ConstructTestCitadel
  Command = FOUNDATION_CONSTRUCT
  Object = TestCitadel
End
"""
    ).encode("utf-8")
    documents["data/ini/commandset.ini"] = (
        documents["data/ini/commandset.ini"].decode("utf-8")
        + """
CommandSet FoundationCommandSet
  1 = Command_ConstructTestCitadel
End
"""
    ).encode("utf-8")
    objects_path = "data/ini/object/units/test_units.ini"
    documents[objects_path] = (
        documents[objects_path].decode("utf-8")
        + """
Object FoundationPad
  CommandSet = FoundationCommandSet
  KindOf = STRUCTURE BASE_FOUNDATION
End
"""
    ).encode("utf-8")

    descriptor = compile_playable_structure_descriptor("TestCitadel", documents)

    assert descriptor["production"]["evidence"] == "authored-construct-command"
    route = descriptor["production"]["routes"][0]
    assert route["commandKind"] == "foundation_construct"
    assert route["builderObjectId"] == "FoundationPad"
    # The fixture's foundation button authors no ButtonImage: the route keeps
    # the key absent so downstream image binding records an explicit gap.
    assert "buttonImageId" not in route


def test_wall_template_policy_admits_template_only_structures() -> None:
    documents = _structure_documents()
    objects_path = "data/ini/object/units/test_units.ini"
    documents[objects_path] = (
        documents[objects_path].decode("utf-8")
        + """
Object TestWallSegment
  KindOf = STRUCTURE
  Body = StructureBody ModuleTag_Body
    MaxHealth = 500
  End
End
"""
    ).encode("utf-8")

    with pytest.raises(PlayableStructureCompilerError, match="wall-template"):
        compile_playable_structure_descriptor("TestWallSegment", documents)

    descriptor = compile_playable_structure_descriptor(
        "TestWallSegment", documents, wall_template_roots=("TestWallSegment",)
    )
    assert descriptor["production"]["evidence"] == "wall-template"
    assert descriptor["production"]["routes"] == []


def test_foundation_without_health_is_admitted_with_evidence() -> None:
    documents = _structure_documents()
    objects_path = "data/ini/object/units/test_units.ini"
    documents[objects_path] = (
        documents[objects_path].decode("utf-8")
        + """
Object BareFoundation
  KindOf = STRUCTURE BASE_FOUNDATION
End
"""
    ).encode("utf-8")

    descriptor = compile_playable_structure_descriptor(
        "BareFoundation", documents, engine_spawned_roots=("BareFoundation",)
    )
    validate_playable_structure_descriptor(descriptor)
    assert descriptor["gameplay"]["health"] is None


def test_non_structure_object_is_rejected() -> None:
    documents = _structure_documents()

    with pytest.raises(
        PlayableStructureCompilerError, match="no structure KindOf"
    ):
        compile_playable_structure_descriptor("InfantryHorde", documents)


def test_structure_without_body_health_fails_closed() -> None:
    documents = _structure_documents()

    with pytest.raises(
        PlayableStructureCompilerError, match="no authored body health"
    ):
        compile_playable_structure_descriptor(
            "HollowKeep", documents, engine_spawned_roots=("HollowKeep",)
        )


def test_unresolved_health_constant_fails_closed() -> None:
    documents = _structure_documents()
    documents["data/ini/gamedata.ini"] = documents[
        "data/ini/gamedata.ini"
    ].replace(b"#define KEEP_HEALTH 3000\n", b"")

    with pytest.raises(
        PlayableStructureCompilerError, match="unresolved GameData constant"
    ):
        compile_playable_structure_descriptor("TestKeep", documents)


def test_tampered_descriptor_digest_is_rejected() -> None:
    documents = _structure_documents()
    descriptor = compile_playable_structure_descriptor("TestKeep", documents)
    descriptor["descriptorSha256"] = "0" * 64

    with pytest.raises(
        PlayableStructureCompilerError, match="digest is invalid"
    ):
        validate_playable_structure_descriptor(descriptor)


def test_wall_upgrade_command_is_authored_production_evidence() -> None:
    documents = _structure_documents()
    objects_path = "data/ini/object/units/test_units.ini"
    documents[objects_path] = (
        documents[objects_path].decode("utf-8")
        + """
Object TestWallHub
  CommandSet = TestWallHubCommandSet
  KindOf = SELECTABLE STRUCTURE
  Body = StructureBody ModuleTag_Body
    MaxHealth = 900
  End
End

Object TestWallGate
  KindOf = SELECTABLE STRUCTURE
  Body = StructureBody ModuleTag_Body
    MaxHealth = 1200
  End
End
"""
    ).encode("utf-8")
    documents["data/ini/commandset.ini"] = (
        documents["data/ini/commandset.ini"].decode("utf-8")
        + """
CommandSet TestWallHubCommandSet
  2 = Command_WallUpgradeToGate
End
"""
    ).encode("utf-8")
    documents["data/ini/commandbutton.ini"] = (
        documents["data/ini/commandbutton.ini"].decode("utf-8")
        + """
CommandButton Command_WallUpgradeToGate
  Command = OBJECT_UPGRADE
  Options = CANCELABLE NOT_QUEUEABLE
  Object = TestWallGate
  Upgrade = Upgrade_TestWallGate
End
"""
    ).encode("utf-8")

    descriptor = compile_playable_structure_descriptor("TestWallGate", documents)

    validate_playable_structure_descriptor(descriptor)
    assert descriptor["production"]["evidence"] == "authored-wall-upgrade-command"
    route = descriptor["production"]["routes"][0]
    assert route["surface"] == "wall-upgrade"
    assert route["commandKind"] == "object_upgrade"
    assert route["builderObjectId"] == "TestWallHub"
    assert route["commandSetId"] == "TestWallHubCommandSet"
    assert route["slot"] == 2
    assert route["upgrade"] == ["Upgrade_TestWallGate"]


def test_construct_route_wins_over_wall_upgrade_for_one_structure() -> None:
    documents = _structure_documents()
    documents["data/ini/commandbutton.ini"] = (
        documents["data/ini/commandbutton.ini"].decode("utf-8")
        + """
CommandButton Command_WallUpgradeToKeep
  Command = OBJECT_UPGRADE
  Object = TestKeep
  Upgrade = Upgrade_TestKeep
End
"""
    ).encode("utf-8")
    documents["data/ini/commandset.ini"] = (
        documents["data/ini/commandset.ini"].decode("utf-8")
        + """
CommandSet TestWallUpgradeCommandSet
  1 = Command_WallUpgradeToKeep
End
"""
    ).encode("utf-8")
    objects_path = "data/ini/object/units/test_units.ini"
    documents[objects_path] = (
        documents[objects_path].decode("utf-8")
        + """
Object TestWallUpgradeHub
  CommandSet = TestWallUpgradeCommandSet
  KindOf = STRUCTURE
End
"""
    ).encode("utf-8")

    descriptor = compile_playable_structure_descriptor("TestKeep", documents)

    validate_playable_structure_descriptor(descriptor)
    assert descriptor["production"]["evidence"] == "authored-construct-command"
    surfaces = {route["surface"] for route in descriptor["production"]["routes"]}
    assert surfaces == {"construct", "wall-upgrade"}


# ---------------------------------------------------------------------------
# Purchased structure level chain tests.
# ---------------------------------------------------------------------------


def _upgradeable_documents() -> dict[str, bytes]:
    documents = _structure_documents()
    objects_path = "data/ini/object/units/test_units.ini"
    documents[objects_path] = (
        documents[objects_path].decode("utf-8")
        + """
Object UpgradeableKeep
  CommandSet = UpgradeableKeepCommandSet
  KindOf = SELECTABLE STRUCTURE
  BuildCost = 800
  BuildTime = 45.0
  VisionRange = 200
  DisplayName = OBJECT:UpgradeableKeep
  Body = StructureBody ModuleTag_Body
    MaxHealth = 3000
  End
  Behavior = SubObjectsUpgrade ModuleTag_HideAll
    TriggeredBy = Upgrade_StructureLevel1
    HideSubObjects = V1 V1FLAG V2 V2A V1_PIECE* V2_PIECE*
  End
  Behavior = SubObjectsUpgrade ModuleTag_ShowWallsAndFlag
    TriggeredBy = Upgrade_KeepLevel2
    ShowSubObjects = V1 V1FLAG V1_PIECE*
    HideSubObjects = V2 V2A V2_PIECE*
  End
  Behavior = SubObjectsUpgrade ModuleTag_ShowTowers
    TriggeredBy = Upgrade_KeepLevel3
    ShowSubObjects = V1 V2 V2A V1_PIECE* V2_PIECE*
    HideSubObjects = V1FLAG
  End
  Behavior = LevelUpUpgrade ModuleTag_KeepLevel2
    TriggeredBy = Upgrade_KeepLevel2
    LevelsToGain = 1
    LevelCap = 3
  End
  Behavior = LevelUpUpgrade ModuleTag_KeepLevel3
    TriggeredBy = Upgrade_KeepLevel3
    LevelsToGain = 1
    LevelCap = 3
  End
  Behavior = CommandSetUpgrade ModuleTag_KeepLevel2Set
    TriggeredBy = Upgrade_KeepLevel2
    CommandSet = UpgradeableKeepCommandSetLevel2
  End
  Behavior = CommandSetUpgrade ModuleTag_KeepLevel3Set
    TriggeredBy = Upgrade_KeepLevel3
    CommandSet = UpgradeableKeepCommandSetLevel3
  End
End
"""
    ).encode("utf-8")
    documents["data/ini/commandset.ini"] = (
        documents["data/ini/commandset.ini"].decode("utf-8")
        + """
CommandSet UpgradeableKeepCommandSet
  1 = Command_BuildInfantry
  5 = Command_PurchaseUpgradeKeepLevel2
End
CommandSet UpgradeableKeepCommandSetLevel2
  1 = Command_BuildRanged
  5 = Command_PurchaseUpgradeKeepLevel3
End
CommandSet UpgradeableKeepCommandSetLevel3
  1 = Command_BuildRanged
End
"""
    ).encode("utf-8")
    documents["data/ini/commandbutton.ini"] = (
        documents["data/ini/commandbutton.ini"].decode("utf-8")
        + """
CommandButton Command_ConstructUpgradeableKeep
  Command = PORTER_CONSTRUCT
  Object = UpgradeableKeep
End
CommandButton Command_PurchaseUpgradeKeepLevel2
  Command = OBJECT_UPGRADE
  Upgrade = Upgrade_KeepLevel2
  Options = CANCELABLE
  TextLabel = CONTROLBAR:KeepLevel2
  DescriptLabel = CONTROLBAR:ToolTipKeepLevel2
  ButtonImage = UCCommon_UpgradeStructureNew
End
CommandButton Command_PurchaseUpgradeKeepLevel3
  Command = OBJECT_UPGRADE
  Upgrade = Upgrade_KeepLevel3
  TextLabel = CONTROLBAR:KeepLevel3
  DescriptLabel = CONTROLBAR:ToolTipKeepLevel3
End
"""
    ).encode("utf-8")
    documents["data/ini/commandset.ini"] = (
        documents["data/ini/commandset.ini"].decode("utf-8").replace(
            "CommandSet PorterCommandSet\n  1 = Command_ConstructTestKeep\nEnd",
            "CommandSet PorterCommandSet\n  1 = Command_ConstructTestKeep\n  2 = Command_ConstructUpgradeableKeep\nEnd",
            1,
        )
    ).encode("utf-8")
    documents["data/ini/upgrade.ini"] = b"""
Upgrade Upgrade_KeepLevel2
  Type = OBJECT
  BuildCost = KEEP_LEVEL2_COST
  BuildTime = KEEP_LEVEL2_BUILDTIME
  DisplayName = Upgrade:KeepLevel2
End
Upgrade Upgrade_KeepLevel3
  Type = OBJECT
  BuildCost = KEEP_LEVEL3_COST
  BuildTime = KEEP_LEVEL3_BUILDTIME
  DisplayName = Upgrade:KeepLevel3
End
"""
    documents["data/ini/experiencelevels.ini"] = b"""
ExperienceLevel KeepLevel1
  TargetNames = UpgradeableKeep
  RequiredExperience = 1
  ExperienceAward = 50
  Rank = 1
End
ExperienceLevel KeepLevel2
  TargetNames = UpgradeableKeep
  RequiredExperience = 100
  ExperienceAward = 60
  Rank = 2
  AttributeModifiers = KeepBuildSpeedModLvl2
  Upgrades = Upgrade_KeepLevel2
End
ExperienceLevel KeepLevel3
  TargetNames = UpgradeableKeep
  RequiredExperience = 1000
  ExperienceAward = 70
  Rank = 3
  AttributeModifiers = KeepBuildSpeedModLvl3
  Upgrades = Upgrade_KeepLevel3
End
"""
    documents["data/ini/attributemodifier.ini"] = b"""
ModifierList KeepBuildSpeedModLvl2
  Category = STRUCTURE
  Modifier = PRODUCTION KEEP_LVL2_BUILD_SPEED
  Modifier = HEALTH KEEP_LVL2_HP_ADD
  Duration = 0
End
ModifierList KeepBuildSpeedModLvl3
  Category = STRUCTURE
  Modifier = PRODUCTION 1.25
  Modifier = HEALTH 1500
  Duration = 0
End
"""
    documents["data/ini/gamedata.ini"] = (
        documents["data/ini/gamedata.ini"]
        + b"#define KEEP_LEVEL2_COST 500\n"
        + b"#define KEEP_LEVEL2_BUILDTIME 30\n"
        + b"#define KEEP_LEVEL3_COST 650\n"
        + b"#define KEEP_LEVEL3_BUILDTIME 60\n"
        + b"#define KEEP_LVL2_BUILD_SPEED 1.10\n"
        + b"#define KEEP_LVL2_HP_ADD 1500\n"
    )
    return documents


def test_upgrade_chain_compiles_costs_sets_and_level_effects() -> None:
    documents = _upgradeable_documents()

    descriptor = compile_playable_structure_descriptor("UpgradeableKeep", documents)

    validate_playable_structure_descriptor(descriptor)
    chain = descriptor["gameplay"]["upgradeChain"]
    assert chain["levelCap"] == 3
    assert [step["toLevel"] for step in chain["steps"]] == [2, 3]
    level_two, level_three = chain["steps"]
    assert level_two["upgradeId"] == "Upgrade_KeepLevel2"
    assert level_two["commandId"] == "Command_PurchaseUpgradeKeepLevel2"
    assert level_two["slot"] == 5
    assert level_two["cost"] == 500
    assert level_two["buildTimeSeconds"] == 30
    assert level_two["cancelable"] is True
    assert level_two["fromCommandSet"] == "UpgradeableKeepCommandSet"
    assert level_two["toCommandSet"] == "UpgradeableKeepCommandSetLevel2"
    assert "requiresUpgradeId" not in level_two
    assert level_two["effects"] == [
        {
            "id": "KeepBuildSpeedModLvl2",
            "modifiers": [
                {"kind": "PRODUCTION", "value": 1.1, "application": "multiplicative"},
                {"kind": "HEALTH", "value": 1500, "application": "additive"},
            ],
            "sourceIni": "data/ini/attributemodifier.ini",
            "category": "STRUCTURE",
        }
    ]
    assert level_two["buttonLabels"] == [
        "CONTROLBAR:KeepLevel2",
        "CONTROLBAR:ToolTipKeepLevel2",
        "UCCommon_UpgradeStructureNew",
    ]
    assert level_two["labelId"] == "CONTROLBAR:KeepLevel2"
    assert level_two["tooltipId"] == "CONTROLBAR:ToolTipKeepLevel2"
    assert level_two["buttonImageId"] == "UCCommon_UpgradeStructureNew"
    # Per-level model variants: the authored SubObjectsUpgrade directives ride
    # each step with the cumulative visibility resolved from the level-one
    # base state.
    level_one = chain["levelOne"]
    assert level_one["hiddenSubObjects"] == [
        "V1", "V1_PIECE*", "V1FLAG", "V2", "V2_PIECE*", "V2A"
    ]
    assert level_one["visibleSubObjects"] == []
    presentation_two = level_two["presentation"]
    assert presentation_two["subObjects"] == [
        {
            "show": ["V1", "V1FLAG", "V1_PIECE*"],
            "hide": ["V2", "V2A", "V2_PIECE*"],
            "sourceIni": "data/ini/object/units/test_units.ini",
            "line": presentation_two["subObjects"][0]["line"],
        }
    ]
    assert presentation_two["visibleSubObjects"] == ["V1", "V1_PIECE*", "V1FLAG"]
    assert presentation_two["hiddenSubObjects"] == ["V2", "V2_PIECE*", "V2A"]
    presentation_three = level_three["presentation"]
    assert presentation_three["visibleSubObjects"] == [
        "V1", "V1_PIECE*", "V2", "V2_PIECE*", "V2A"
    ]
    assert presentation_three["hiddenSubObjects"] == ["V1FLAG"]
    assert level_three["requiresUpgradeId"] == "Upgrade_KeepLevel2"
    assert level_three["fromCommandSet"] == "UpgradeableKeepCommandSetLevel2"
    assert level_three["toCommandSet"] == "UpgradeableKeepCommandSetLevel3"
    assert level_three["cancelable"] is False
    assert level_three["cost"] == 650
    sources = {row["virtualPath"] for row in descriptor["sourceDocuments"]}
    assert "data/ini/upgrade.ini" in sources
    assert "data/ini/experiencelevels.ini" in sources
    assert "data/ini/attributemodifier.ini" in sources


def test_structure_without_level_upgrades_has_no_chain_key() -> None:
    documents = _upgradeable_documents()

    descriptor = compile_playable_structure_descriptor("TestKeep", documents)

    validate_playable_structure_descriptor(descriptor)
    assert "upgradeChain" not in descriptor["gameplay"]


def test_upgrade_chain_missing_purchase_button_fails_closed() -> None:
    documents = _upgradeable_documents()
    documents["data/ini/commandset.ini"] = (
        documents["data/ini/commandset.ini"].decode("utf-8").replace(
            "  5 = Command_PurchaseUpgradeKeepLevel3\n", "", 1
        )
    ).encode("utf-8")

    with pytest.raises(PlayableStructureCompilerError, match="not purchasable"):
        compile_playable_structure_descriptor("UpgradeableKeep", documents)


def test_upgrade_chain_missing_upgrade_block_fails_closed() -> None:
    documents = _upgradeable_documents()
    documents["data/ini/upgrade.ini"] = b"""
Upgrade Upgrade_KeepLevel2
  Type = OBJECT
  BuildCost = 500
  BuildTime = 30
End
"""

    with pytest.raises(PlayableStructureCompilerError, match="no .*Upgrade.* block|Upgrade_KeepLevel3"):
        compile_playable_structure_descriptor("UpgradeableKeep", documents)


def test_upgrade_chain_missing_experience_chain_fails_closed() -> None:
    documents = _upgradeable_documents()
    documents["data/ini/experiencelevels.ini"] = b"""
ExperienceLevel UnrelatedLevel1
  TargetNames = UnrelatedKeep
  RequiredExperience = 1
  ExperienceAward = 50
  Rank = 1
End
"""

    with pytest.raises(PlayableStructureCompilerError, match="no ExperienceLevel chain"):
        compile_playable_structure_descriptor("UpgradeableKeep", documents)


def test_upgrade_chain_unresolvable_cost_fails_closed() -> None:
    documents = _upgradeable_documents()
    documents["data/ini/upgrade.ini"] = (
        documents["data/ini/upgrade.ini"]
        .decode("utf-8")
        .replace("KEEP_LEVEL2_COST", "UNDEFINED_KEEP_COST", 1)
        .encode("utf-8")
    )

    with pytest.raises(PlayableStructureCompilerError, match="GameData constant"):
        compile_playable_structure_descriptor("UpgradeableKeep", documents)
