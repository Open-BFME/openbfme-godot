"""Upgrade-parity converter gates: research surfaces, upgrade effects,
structure-level presentations, and per-battalion purchase surfaces.

Fixtures are synthetic INI strings (never retail bytes), mirroring the
authored BFME2 1.06 shapes: the marketplace's PLAYER_UPGRADE sales and
CostModifierUpgrade/RefundDie rows, the farm's StructureLevel-bound
SubObjectsUpgrade rows and TerrainResourceBehavior bonus, and the horde
command sets' OBJECT_UPGRADE purchase buttons with LevelUpUpgrade Basic
Training.
"""

import pytest

from openbfme_importer.playable_structure_compiler import (
    PlayableStructureCompilerError,
    compile_playable_structure_descriptor,
    validate_playable_structure_descriptor,
)
from openbfme_importer.playable_unit_compiler import (
    PlayableUnitCompilerError,
    compile_playable_unit_descriptor,
    validate_playable_unit_descriptor,
)
from importer.tests.test_playable_structure_compiler import _structure_documents
from importer.tests.test_playable_unit_compiler import _documents


def _research_documents() -> dict[str, bytes]:
    documents = _structure_documents()
    objects_path = "data/ini/object/units/test_units.ini"
    documents[objects_path] = (
        documents[objects_path].decode("utf-8")
        + """
Object TestMarket
  CommandSet = TestMarketCommandSet
  KindOf = SELECTABLE STRUCTURE
  BuildCost = 600
  BuildTime = 40.0
  Body = StructureBody ModuleTag_Body
    MaxHealth = 2500
  End
  Behavior = RefundDie ModuleTag_refund
    UpgradeRequired = Upgrade_MarketDefiance
    BuildingRequired = ANY +TestMarket
    RefundPercent = 50%
  End
  Behavior = CostModifierUpgrade ModuleTag_IronOreUpgrades
    LabelForPalantirString = GUI:UPGRADE_DISCOUNT
    TriggeredBy = Upgrade_MarketIronOre
    UpgradeDiscount = Yes
    ApplyToTheseUpgrades = Upgrade_TestBlades Upgrade_TestArmor
    Percentage = -25%
  End
End

Object TestResourceFarm
  CommandSet = TestResourceFarmCommandSet
  KindOf = SELECTABLE STRUCTURE
  BuildCost = 300
  BuildTime = 25.0
  Body = StructureBody ModuleTag_Body
    MaxHealth = 2000
  End
  Behavior = SubObjectsUpgrade ModuleTag_HideAll
    TriggeredBy = Upgrade_StructureLevel1
    HideSubObjects = V1 V2 V1_PIECE* V2_PIECE*
    ShowSubObjects = V1HIDE V2HIDE
  End
  Behavior = SubObjectsUpgrade ModuleTag_ShowWalls
    TriggeredBy = Upgrade_StructureLevel2
    ShowSubObjects = V1 V2HIDE V1_PIECE*
    HideSubObjects = V2 V1HIDE V2_PIECE*
  End
  Behavior = SubObjectsUpgrade ModuleTag_ShowPillars
    TriggeredBy = Upgrade_StructureLevel3
    ShowSubObjects = V1 V2
    HideSubObjects = V1HIDE V2HIDE V1_PIECE* V2_PIECE*
  End
  Behavior = TerrainResourceBehavior ModuleTag_NewMoney
    Radius = FARM_MONEY_RANGE
    MaxIncome = FARM_MONEY_AMOUNT
    IncomeInterval = FARM_MONEY_TIME
    Upgrade = Upgrade_MarketGrandHarvest
    UpgradeBonusPercent = GRANDHARVEST_PRODUCTION_INCREASE %
    UpgradeMustBePresent = ANY +TestMarket
  End
End
"""
    ).encode("utf-8")
    documents["data/ini/commandset.ini"] = (
        documents["data/ini/commandset.ini"].decode("utf-8")
        .replace(
            "CommandSet PorterCommandSet\n  1 = Command_ConstructTestKeep\nEnd",
            "CommandSet PorterCommandSet\n  1 = Command_ConstructTestKeep\n"
            "  2 = Command_ConstructTestMarket\n"
            "  3 = Command_ConstructTestResourceFarm\n"
            "  4 = Command_ConstructTestOddKeep\nEnd",
            1,
        )
        + """
CommandSet TestMarketCommandSet
  1 = Command_ResearchGrandHarvest
  2 = Command_ResearchDefiance
  3 = Command_ResearchIronOre
End
CommandSet TestResourceFarmCommandSet
  1 = Command_BuildInfantry
End
"""
    ).encode("utf-8")
    documents["data/ini/commandbutton.ini"] = (
        documents["data/ini/commandbutton.ini"].decode("utf-8")
        + """
CommandButton Command_ConstructTestMarket
  Command = PORTER_CONSTRUCT
  Object = TestMarket
End
CommandButton Command_ConstructTestResourceFarm
  Command = PORTER_CONSTRUCT
  Object = TestResourceFarm
End
CommandButton Command_ConstructTestOddKeep
  Command = PORTER_CONSTRUCT
  Object = TestOddKeep
End
CommandButton Command_ResearchGrandHarvest
  Command = PLAYER_UPGRADE
  Upgrade = Upgrade_MarketGrandHarvest
  Options = CANCELABLE
  TextLabel = CONTROLBAR:ConstructGrandHarvestUpgrade
  DescriptLabel = CONTROLBAR:ToolTipBuildGrandHarvestUpgrade
  ButtonImage = BGMarketplace_GrandHarvest
End
CommandButton Command_ResearchDefiance
  Command = PLAYER_UPGRADE
  Upgrade = Upgrade_MarketDefiance
  Options = CANCELABLE
  TextLabel = CONTROLBAR:ConstructSiegeMaterialsUpgrade
  DescriptLabel = CONTROLBAR:ToolTipBuildSiegeMaterialsUpgrade
  ButtonImage = BGMarketplace_Defiance
End
CommandButton Command_ResearchIronOre
  Command = PLAYER_UPGRADE
  Upgrade = Upgrade_MarketIronOre
  NeededUpgrade = Upgrade_TechnologyIronOre
  Options = NEED_UPGRADE CANCELABLE
  TextLabel = CONTROLBAR:ConstructIronOreUpgrade
  DescriptLabel = CONTROLBAR:ToolTipBuildIronOreUpgrade
  ButtonImage = BGMarketplace_IronOre
  LacksPrerequisiteLabel = TOOLTIP:LackIronOreTechnology
End
"""
    ).encode("utf-8")
    documents["data/ini/upgrade.ini"] = b"""
Upgrade Upgrade_MarketGrandHarvest
  Type = PLAYER
  BuildCost = GRANDHARVEST_BUILDCOST
  BuildTime = GRANDHARVEST_BUILDTIME
End
Upgrade Upgrade_MarketDefiance
  Type = PLAYER
  BuildCost = 500
  BuildTime = 60
End
Upgrade Upgrade_MarketIronOre
  Type = PLAYER
  BuildCost = 500
  BuildTime = 60
End
Upgrade Upgrade_TechnologyIronOre
  Type = PLAYER
  BuildCost = 200
  BuildTime = 30
End
"""
    documents["data/ini/gamedata.ini"] = (
        documents["data/ini/gamedata.ini"]
        + b"#define GRANDHARVEST_BUILDCOST 1000\n"
        + b"#define GRANDHARVEST_BUILDTIME 60\n"
        + b"#define GRANDHARVEST_PRODUCTION_INCREASE 110\n"
        + b"#define FARM_MONEY_RANGE 200\n"
        + b"#define FARM_MONEY_AMOUNT 25\n"
        + b"#define FARM_MONEY_TIME 6000\n"
    )
    return documents


def test_research_surface_compiles_buttons_gates_and_costs() -> None:
    descriptor = compile_playable_structure_descriptor(
        "TestMarket", _research_documents()
    )

    validate_playable_structure_descriptor(descriptor)
    research = descriptor["gameplay"]["research"]
    rows = research["upgrades"]
    assert [row["upgradeId"] for row in rows] == [
        "Upgrade_MarketGrandHarvest",
        "Upgrade_MarketDefiance",
        "Upgrade_MarketIronOre",
    ]
    harvest, defiance, iron_ore = rows
    assert harvest["commandId"] == "Command_ResearchGrandHarvest"
    assert harvest["commandSetId"] == "TestMarketCommandSet"
    assert harvest["slot"] == 1
    assert harvest["cost"] == 1000
    assert harvest["buildTimeSeconds"] == 60
    assert harvest["cancelable"] is True
    assert "neededUpgradeIds" not in harvest
    assert harvest["labelId"] == "CONTROLBAR:ConstructGrandHarvestUpgrade"
    assert harvest["tooltipId"] == "CONTROLBAR:ToolTipBuildGrandHarvestUpgrade"
    assert harvest["buttonImageId"] == "BGMarketplace_GrandHarvest"
    assert defiance["cost"] == 500
    assert iron_ore["neededUpgradeIds"] == ["Upgrade_TechnologyIronOre"]
    assert iron_ore["neededUpgradeAny"] is False
    assert iron_ore["lacksPrerequisiteLabelId"] == "TOOLTIP:LackIronOreTechnology"
    sources = {row["virtualPath"] for row in descriptor["sourceDocuments"]}
    assert "data/ini/upgrade.ini" in sources


def test_research_surface_requires_player_upgrade_type() -> None:
    documents = _research_documents()
    documents["data/ini/upgrade.ini"] = documents["data/ini/upgrade.ini"].replace(
        b"Upgrade Upgrade_MarketGrandHarvest\n  Type = PLAYER",
        b"Upgrade Upgrade_MarketGrandHarvest\n  Type = OBJECT",
    )

    with pytest.raises(PlayableStructureCompilerError, match="not a PLAYER upgrade"):
        compile_playable_structure_descriptor("TestMarket", documents)


def test_research_surface_missing_upgrade_block_fails_closed() -> None:
    documents = _research_documents()
    documents["data/ini/upgrade.ini"] = documents["data/ini/upgrade.ini"].replace(
        b"Upgrade Upgrade_MarketDefiance\n  Type = PLAYER\n  BuildCost = 500\n  BuildTime = 60\nEnd\n",
        b"",
    )

    with pytest.raises(PlayableStructureCompilerError, match="has no"):
        compile_playable_structure_descriptor("TestMarket", documents)


def test_upgrade_effects_compile_discount_refund_and_income_bonus() -> None:
    market = compile_playable_structure_descriptor(
        "TestMarket", _research_documents()
    )
    effects = market["gameplay"]["upgradeEffects"]["effects"]
    assert effects[0] == {
        "upgradeId": "Upgrade_MarketDefiance",
        "kind": "refund-on-death",
        "refundPercent": 50.0,
        "sourceIni": "data/ini/object/units/test_units.ini",
        "line": effects[0]["line"],
        "buildingRequired": "ANY +TestMarket",
    }
    assert effects[1] == {
        "upgradeId": "Upgrade_MarketIronOre",
        "kind": "upgrade-discount",
        "applyToUpgradeIds": ["Upgrade_TestBlades", "Upgrade_TestArmor"],
        "percent": -25.0,
        "upgradeDiscount": True,
        "sourceIni": "data/ini/object/units/test_units.ini",
        "line": effects[1]["line"],
        "labelId": "GUI:UPGRADE_DISCOUNT",
    }
    assert market["gameplay"]["upgradeEffects"]["unsupportedEffects"] == []

    farm = compile_playable_structure_descriptor(
        "TestResourceFarm", _research_documents()
    )
    farm_effects = farm["gameplay"]["upgradeEffects"]["effects"]
    assert farm_effects == [
        {
            "upgradeId": "Upgrade_MarketGrandHarvest",
            "kind": "income-bonus",
            "bonusPercent": 110.0,
            "sourceIni": "data/ini/object/units/test_units.ini",
            "line": farm_effects[0]["line"],
            "upgradeMustBePresent": "ANY +TestMarket",
        }
    ]


def test_unsupported_upgrade_effect_module_is_recorded() -> None:
    documents = _research_documents()
    objects_path = "data/ini/object/units/test_units.ini"
    documents[objects_path] = (
        documents[objects_path].decode("utf-8")
        + """
Object TestOddKeep
  CommandSet = TestOddKeepCommandSet
  KindOf = SELECTABLE STRUCTURE
  Body = StructureBody ModuleTag_Body
    MaxHealth = 1000
  End
  Behavior = StatusBitsUpgrade ModuleTag_Odd
    TriggeredBy = Upgrade_MarketDefiance
  End
End
"""
    ).encode("utf-8")
    documents["data/ini/commandset.ini"] = (
        documents["data/ini/commandset.ini"].decode("utf-8")
        + """
CommandSet TestOddKeepCommandSet
  1 = Command_BuildInfantry
End
"""
    ).encode("utf-8")

    descriptor = compile_playable_structure_descriptor("TestOddKeep", documents)

    unsupported = descriptor["gameplay"]["upgradeEffects"]["unsupportedEffects"]
    assert unsupported == [
        {
            "upgradeId": "Upgrade_MarketDefiance",
            "module": "StatusBitsUpgrade",
            "sourceIni": "data/ini/object/units/test_units.ini",
            "line": unsupported[0]["line"],
            "reason": "authored module kind is not a supported structure upgrade effect",
        }
    ]


def test_structure_level_presentation_compiles_without_level_up_modules() -> None:
    descriptor = compile_playable_structure_descriptor(
        "TestResourceFarm", _research_documents()
    )

    validate_playable_structure_descriptor(descriptor)
    assert "upgradeChain" not in descriptor["gameplay"]
    presentation = descriptor["gameplay"]["structureLevelPresentation"]
    assert presentation["triggerUpgrades"] == [
        "Upgrade_StructureLevel1",
        "Upgrade_StructureLevel2",
        "Upgrade_StructureLevel3",
    ]
    levels = presentation["levels"]
    assert levels["1"]["visibleSubObjects"] == ["V1HIDE", "V2HIDE"]
    assert levels["1"]["hiddenSubObjects"] == ["V1", "V1_PIECE*", "V2", "V2_PIECE*"]
    assert levels["2"]["visibleSubObjects"] == ["V1", "V1_PIECE*", "V2HIDE"]
    assert levels["2"]["hiddenSubObjects"] == ["V1HIDE", "V2", "V2_PIECE*"]
    assert levels["3"]["visibleSubObjects"] == ["V1", "V2"]
    assert levels["3"]["hiddenSubObjects"] == [
        "V1_PIECE*",
        "V1HIDE",
        "V2_PIECE*",
        "V2HIDE",
    ]


def test_structure_without_subobject_rows_has_no_presentation_key() -> None:
    descriptor = compile_playable_structure_descriptor(
        "TestKeep", _structure_documents()
    )
    assert "structureLevelPresentation" not in descriptor["gameplay"]
    assert "research" not in descriptor["gameplay"]
    assert "upgradeEffects" not in descriptor["gameplay"]


def _purchase_documents() -> dict[str, bytes]:
    documents = _documents()
    objects_path = "data/ini/object/units/test_units.ini"
    objects = documents[objects_path].decode("utf-8")
    objects += """
Object PurchaseFactory
  CommandSet = PurchaseFactoryCommandSet
  KindOf = PRELOAD SELECTABLE STRUCTURE
End

Object UpgradeHordeMember
  KindOf = PRELOAD SELECTABLE INFANTRY
  BuildCost = 100
  BuildTime = 10
  VisionRange = 200
  SelectPortrait = UPUpgradeHordeMember
  Draw = W3DScriptedModelDraw ModuleTag_Draw
    DefaultModelConditionState
      Model = UpgradeHordeMemberModel
    End
  End
End

Object UpgradeHorde
  KindOf = PRELOAD SELECTABLE HORDE
  BuildCost = 500
  BuildTime = 30
  CommandPoints = 20
  VisionRange = 300
  SelectPortrait = UPUpgradeHorde
  CommandSet = UpgradeHordeCommandSet
  Draw = W3DScriptedModelDraw ModuleTag_Draw
    DefaultModelConditionState
      Model = UpgradeHordeModel
    End
  End
  Behavior = HordeContain ModuleTag_HordeContain
    InitialPayload = UpgradeHordeMember 10
  End
  Behavior = LevelUpUpgrade ModuleTag_BasicTraining
    TriggeredBy = Upgrade_TestBasicTraining
    LevelsToGain = 1
    LevelCap = 2
  End
End
"""
    documents[objects_path] = objects.encode("utf-8")
    documents["data/ini/commandset.ini"] = (
        documents["data/ini/commandset.ini"].decode("utf-8")
        + """
CommandSet PurchaseFactoryCommandSet
  1 = Command_BuildUpgradeHorde
End
CommandSet UpgradeHordeCommandSet
  1 = Command_ToggleStance
  3 = Command_PurchaseTestBlades
  4 = Command_PurchaseTestArmor
  5 = Command_PurchaseTestTraining
End
"""
    ).encode("utf-8")
    documents["data/ini/commandbutton.ini"] = (
        documents["data/ini/commandbutton.ini"].decode("utf-8")
        + """
CommandButton Command_BuildUpgradeHorde
  Command = UNIT_BUILD
  Object = UpgradeHorde
  ButtonImage = BIUpgradeHorde
  TextLabel = CONTROLBAR:UpgradeHorde
  DescriptLabel = CONTROLBAR:ToolTipUpgradeHorde
End
CommandButton Command_ToggleStance
  Command = TOGGLE_STANCE
End
CommandButton Command_PurchaseTestBlades
  Command = OBJECT_UPGRADE
  Upgrade = Upgrade_TestBlades
  NeededUpgrade = Upgrade_TechnologyTestBlades
  Options = NEED_UPGRADE OK_FOR_MULTI_SELECT CANCELABLE
  TextLabel = CONTROLBAR:PurchaseUpgradeTestBlades
  DescriptLabel = CONTROLBAR:ToolTipPurchaseUpgradeTestBlades
  ButtonImage = BRArmory_ForgedBlades
End
CommandButton Command_PurchaseTestArmor
  Command = OBJECT_UPGRADE
  Upgrade = Upgrade_TestArmor
  NeededUpgrade = Upgrade_TechnologyTestArmor
  Options = NEED_UPGRADE CANCELABLE
  ButtonImage = BGBlacksmith_HeavyArmor
End
CommandButton Command_PurchaseTestTraining
  Command = OBJECT_UPGRADE
  Upgrade = Upgrade_TestBasicTraining
  NeededUpgrade = Upgrade_TechnologyTestBasicTraining
  Options = NEED_UPGRADE OK_FOR_MULTI_SELECT CANCELABLE
  ButtonImage = BGBlacksmith_SilverTreeBanner
End
"""
    ).encode("utf-8")
    documents["data/ini/upgrade.ini"] = b"""
Upgrade Upgrade_TechnologyTestBlades
  Type = PLAYER
  BuildCost = 1000
  BuildTime = 30
End
Upgrade Upgrade_TechnologyTestArmor
  Type = PLAYER
  BuildCost = 1000
  BuildTime = 30
End
Upgrade Upgrade_TechnologyTestBasicTraining
  Type = PLAYER
  BuildCost = 400
  BuildTime = 15
End
Upgrade Upgrade_TestBlades
  Type = OBJECT
  BuildCost = TEST_BLADES_COST
  BuildTime = TEST_BLADES_BUILDTIME
End
Upgrade Upgrade_TestArmor
  Type = OBJECT
  BuildCost = 300
  BuildTime = 10
End
Upgrade Upgrade_TestBasicTraining
  Type = OBJECT
  BuildCost = 300
  BuildTime = 10
End
"""
    documents["data/ini/gamedata.ini"] = (
        documents["data/ini/gamedata.ini"]
        + b"#define TEST_BLADES_COST 300\n"
        + b"#define TEST_BLADES_BUILDTIME 10\n"
    )
    return documents


def test_unit_upgrade_purchase_commands_compile() -> None:
    descriptor = compile_playable_unit_descriptor(
        "UpgradeHorde", _purchase_documents()
    )

    validate_playable_unit_descriptor(descriptor)
    rows = descriptor["gameplay"]["upgradeCommands"]
    assert [row["upgradeId"] for row in rows] == [
        "Upgrade_TestBlades",
        "Upgrade_TestArmor",
        "Upgrade_TestBasicTraining",
    ]
    blades, armor, training = rows
    assert blades["commandId"] == "Command_PurchaseTestBlades"
    assert blades["commandSetId"] == "UpgradeHordeCommandSet"
    assert blades["slot"] == 3
    assert blades["cost"] == 300
    assert blades["buildTimeSeconds"] == 10
    assert blades["cancelable"] is True
    assert blades["multiSelect"] is True
    assert blades["neededUpgradeIds"] == ["Upgrade_TechnologyTestBlades"]
    assert blades["neededUpgradeAny"] is False
    assert blades["labelId"] == "CONTROLBAR:PurchaseUpgradeTestBlades"
    assert blades["tooltipId"] == "CONTROLBAR:ToolTipPurchaseUpgradeTestBlades"
    assert blades["buttonImageId"] == "BRArmory_ForgedBlades"
    assert armor["slot"] == 4
    assert armor["multiSelect"] is False
    assert training["slot"] == 5
    assert training["buttonImageId"] == "BGBlacksmith_SilverTreeBanner"
    sources = {row["virtualPath"] for row in descriptor["sourceDocuments"]}
    assert "data/ini/upgrade.ini" in sources


def test_level_up_upgrades_compile_and_leave_unsupported_record() -> None:
    descriptor = compile_playable_unit_descriptor(
        "UpgradeHorde", _purchase_documents()
    )

    level_upgrades = descriptor["gameplay"]["levelUpgrades"]
    assert level_upgrades == [
        {
            "upgradeId": "Upgrade_TestBasicTraining",
            "levelsToGain": 1,
            "levelCap": 2,
            "sourceIni": "data/ini/object/units/test_units.ini",
            "line": level_upgrades[0]["line"],
        }
    ]
    unsupported_ids = {row["id"] for row in descriptor["unsupportedCapabilities"]}
    assert not any("LevelUpUpgrade" in identifier for identifier in unsupported_ids)


def test_unit_purchase_requires_object_upgrade_type() -> None:
    documents = _purchase_documents()
    documents["data/ini/upgrade.ini"] = documents["data/ini/upgrade.ini"].replace(
        b"Upgrade Upgrade_TestBlades\n  Type = OBJECT",
        b"Upgrade Upgrade_TestBlades\n  Type = PLAYER",
    )

    with pytest.raises(PlayableUnitCompilerError, match="not an OBJECT upgrade"):
        compile_playable_unit_descriptor("UpgradeHorde", documents)


def test_unit_purchase_missing_upgrade_block_fails_closed() -> None:
    documents = _purchase_documents()
    documents["data/ini/upgrade.ini"] = documents["data/ini/upgrade.ini"].replace(
        b"Upgrade Upgrade_TestArmor\n  Type = OBJECT\n  BuildCost = 300\n  BuildTime = 10\nEnd\n",
        b"",
    )

    with pytest.raises(PlayableUnitCompilerError, match="has no"):
        compile_playable_unit_descriptor("UpgradeHorde", documents)


def test_unit_without_purchase_buttons_has_no_new_keys() -> None:
    descriptor = compile_playable_unit_descriptor("InfantryHorde", _documents())
    assert "upgradeCommands" not in descriptor["gameplay"]
    assert "levelUpgrades" not in descriptor["gameplay"]
