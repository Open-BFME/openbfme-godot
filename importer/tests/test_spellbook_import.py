from __future__ import annotations

from copy import deepcopy

import pytest

from openbfme_importer.sage_gameplay import _digest as _gameplay_digest
from openbfme_importer.sage_ini import parse_flat_named_blocks
from openbfme_importer.spellbook_compiler import (
    SpellbookCompilerError,
    _digest,
    _particle_sys_bone_fields,
    compile_spellbook_descriptor,
    validate_spellbook_descriptor,
)
from openbfme_importer.spellbook_import import (
    _resolved_spellbook_media,
    _resolved_spellbook_strings,
    summarize_spellbook_lane,
)
from openbfme_importer.spellbook_pack_compiler import (
    SpellbookPackCompilerError,
    compile_spellbook_pack_recipe,
    compose_spellbook_runtime_document,
    validate_spellbook_pack_recipe,
)


def _documents() -> dict[str, bytes]:
    return {
        "data/ini/playertemplate.ini": b"""
PlayerTemplate FactionElves
  Side = Elves
  SpellBook = GoodSpellBook
  SpellBookMP = TestSpellBook
  PurchaseScienceCommandSetMP = TestSpellStoreCommandSet
  IntrinsicSciencesMP = SCIENCE_ELVES
End
""",
        "data/ini/commandset.ini": b"""
CommandSet TestSpellStoreCommandSet
  1 = Command_PurchaseSpellTestHeal
  2 = Command_PurchaseSpellTestVolley
End

CommandSet TestSpellBookCommandSet
  1 = Command_SpellBookTestHeal
  2 = Command_SpellBookTestVolley
End
""",
        "data/ini/commandbutton.ini": b"""
CommandButton Command_PurchaseSpellTestHeal
  Command = PURCHASE_SCIENCE
  ButtonBorderType = UPGRADE
  ButtonImage = SBTest_Heal
  Science = SCIENCE_TestHeal
  TextLabel = CONTROLBAR:TestHeal
  DescriptLabel = CONTROLBAR:TooltipTestHeal
End

CommandButton Command_PurchaseSpellTestVolley
  Command = PURCHASE_SCIENCE
  ButtonBorderType = UPGRADE
  ButtonImage = SBTest_Volley
  Science = SCIENCE_TestVolley
  TextLabel = CONTROLBAR:TestVolley
  DescriptLabel = CONTROLBAR:TooltipTestVolley
End

CommandButton Command_SpellBookTestHeal
  Command = SPELL_BOOK
  SpecialPower = SpellBookTestHeal
  Options = NEED_TARGET_POS
  TextLabel = CONTROLBAR:TestHeal
  ButtonImage = SBTest_Heal
  ButtonBorderType = ACTION
  DescriptLabel = CONTROLBAR:TooltipTestHeal
  RadiusCursorType = TestHealRadiusCursor
End

CommandButton Command_SpellBookTestVolley
  Command = SPELL_BOOK
  SpecialPower = SpellBookTestVolley
  Options = NEED_TARGET_POS
  TextLabel = CONTROLBAR:TestVolley
  ButtonImage = SBTest_Volley
  ButtonBorderType = ACTION
  DescriptLabel = CONTROLBAR:TooltipTestVolley
End
""",
        "data/ini/gamedata.ini": b"""
#define SPELL_RECHARGE_TIME_TIER_1 30000
#define TEST_BUFF_FILTER ANY +INFANTRY -HERO
""",
        "data/ini/science.ini": b"""
#define GOOD_RANK_1_COST 5

Science SCIENCE_ELVES
  PrerequisiteSciences = None
  SciencePurchasePointCost = 0
  IsGrantable = No
End

Science SCIENCE_GOOD
  PrerequisiteSciences = None
  SciencePurchasePointCost = 0
  IsGrantable = No
End

Science SCIENCE_TestHeal
  PrerequisiteSciences = SCIENCE_ELVES OR SCIENCE_GOOD
  SciencePurchasePointCost = GOOD_RANK_1_COST
  SciencePurchasePointCostMP = 5
  IsGrantable = Yes
End

Science SCIENCE_TestVolley
  PrerequisiteSciences = SCIENCE_ELVES SCIENCE_TestHeal
  SciencePurchasePointCost = 10
  SciencePurchasePointCostMP = 10
  IsGrantable = Yes
End
""",
        "data/ini/specialpower.ini": b"""
SpecialPower SpellBookTestHeal
  Enum = SPECIAL_SPELL_BOOK_TEST_HEAL
  RequiredSciences = SCIENCE_TestHeal
  ReloadTime = SPELL_RECHARGE_TIME_TIER_1
  RadiusCursorRadius = 75.0
  Flags = WATER_OK RESPECT_RECHARGE_TIME_DISCOUNT
  InitiateAtLocationSound = TestHealSound
End

SpecialPower SpellBookTestVolley
  Enum = SPECIAL_SPELL_BOOK_TEST_VOLLEY
  RequiredSciences = SCIENCE_TestVolley
  ReloadTime = 60000
  InitiateAtLocationSound = TestVolleySound
End
""",
        "data/ini/objectcreationlist.ini": b"""
ObjectCreationList OCL_TestHealPing
  CreateObject
    ObjectNames = TestHealPing
    Count = 1
    ParticleSystem = TestHealParticles
  End
End

ObjectCreationList OCL_TestVolley
  CreateObject
    ObjectNames = TestVolleyReceptacle
    Count = 1
  End
  CreateObject
    ObjectNames = TestSummonedHorde
    Count = 2
    Disposition = SPAWN_AROUND
  End
End
""",
        "data/ini/locomotor.ini": b"""
Locomotor TestLocomotor
  TurnTime = 500
  Acceleration = 510
  Braking = 510
End
""",
        "data/ini/fxlist.ini": b"""
FXList FX_TestHealBuff
  ParticleSystem
    Name = TestHealParticles
    Offset = X:0.0 Y:0.0 Z:3.0
  End
  Sound
    Name = TestHealSound
  End
End

FXList FX_TestWeaponFire
  ParticleSystem
    Name = TestWeaponFireParticles
  End
End
""",
        "data/ini/attributemodifier.ini": b"""
ModifierList TestRallyModifier
  Category = LEADERSHIP
  Modifier = DAMAGE_MULT 150%
  Duration = 60000
  FX = FX_TestHealBuff
End

ModifierList TestSummonBonusRank5
  Category = LEVEL
  Modifier = HEALTH 40
  Modifier = DAMAGE_ADD 15
  Duration = 0
End
""",
        "data/ini/experiencelevels.ini": b"""
ExperienceLevel TestSummonedHordeLevel5
  TargetNames = TestSummonedHorde
  RequiredExperience = 1
  Rank = 5
  AttributeModifiers = TestSummonBonusRank5
  Upgrades = Upgrade_ObjectLevel1 Upgrade_ObjectLevel2 Upgrade_ObjectLevel3 Upgrade_ObjectLevel4 Upgrade_ObjectLevel5
End
""",
        "data/ini/weapon.ini": b"""
Weapon TestVolleyWeapon
  RadiusDamageAffects = ENEMIES NEUTRALS
  FireFX = FX_TestWeaponFire
  DamageNugget
    Damage = 800
    Radius = 100
    DamageType = PIERCE
  End
End

Weapon TestReceptacleInternalWeapon
  RadiusDamageAffects = ENEMIES NEUTRALS
  DamageNugget
    Damage = 250
    Radius = 40
    DamageType = CAVALRY_RANGED
    DelayTime = 0
  End
End
""",
        "data/ini/upgrade.ini": b"""
Upgrade Upgrade_TestBlessing
  Type = OBJECT
End
""",
        "data/ini/fxparticlesystem.ini": b"""
FXParticleSystem TestHealParticles
  System
    Priority = HIGH_OR_ABOVE
  End
End

FXParticleSystem TestWeaponFireParticles
  System
    Priority = HIGH_OR_ABOVE
  End
End
""",
        "data/ini/object/system/test_system.ini": b"""
Object TestSpellBook
  EditorSorting = SYSTEM
  KindOf = SPELL_BOOK IMMOBILE IGNORES_SELECT_ALL INERT
  CommandSet = TestSpellBookCommandSet
  Behavior = CommandPointsUpgrade ModuleTag_CommandPointsUpgrade
    TriggeredBy = Upgrade_MarketplaceUpgradeGrandHarvest
    CommandPoints = 100
    RequiredObject = NONE +GondorMarketPlace
  End
  Behavior = PlayerHealSpecialPower ModuleTag_Heal
    SpecialPowerTemplate = SpellBookTestHeal
    HealAmount = 0.5
    HealFX = FX_TestHealBuff
    HealOCL = OCL_TestHealPing
    AvailableAtStart = No
  End
  Behavior = OCLSpecialPower ModuleTag_Volley
    SpecialPowerTemplate = SpellBookTestVolley
    OCL = OCL_TestVolley
    TriggerFX = FX_TestHealBuff
    AttributeModifier = TestRallyModifier
    AttributeModifierAffects = TEST_BUFF_FILTER
    UpgradeName = Upgrade_TestBlessing
    Weapon = TestVolleyWeapon
    CreateLocation = CREATE_AT_LOCATION
    AvailableAtStart = No
  End
End

Object TestHealPing
  Draw = W3DScriptedModelDraw ModuleTag_Draw
    DefaultModelConditionState
      Model = None
      ParticleSysBone = None TestHealParticles
    End
  End
  EditorSorting = SYSTEM
  KindOf = NO_COLLIDE IMMOBILE INERT
End

Object TestVolleyReceptacle
  EditorSorting = SYSTEM
  KindOf = NO_COLLIDE IMMOBILE INERT
  Behavior = FireWeaponUpdate ModuleTag_DamageHandler
    FireWeaponNugget
      WeaponName = TestReceptacleInternalWeapon
      FireDelay = 0
      OneShot = Yes
    End
  End
End

Object TestSummonedHorde
  EditorSorting = UNIT
  KindOf = SELECTABLE CAN_ATTACK INFANTRY HORDE SUMMONED
  EquivalentTo = TestSummonedHorde
  CommandPoints = 0
  Body = ImmortalBody ModuleTag_Body
    MaxHealth = 1
  End
  Behavior = LifetimeUpdate ModuleTag_Lifetime
    MinLifetime = 75000
    MaxLifetime = 75000
    DeathType = FADED
  End
  Behavior = ExperienceLevelCreate ModuleTag_LevelBonus
    LevelToGrant = 5
    MPOnly = No
  End
  Behavior = HordeContain ModuleTag_HordeContain
    InitialPayload = TestSummonedMember 5
    Slots = 5
    RankInfo = RankNumber:1 UnitType:TestSummonedMember Position:X:0 Y:0 Position:X:0 Y:20
  End
End

Object TestSummonedMember
  Draw = W3DScriptedModelDraw ModuleTag_Draw
    DefaultModelConditionState
      Model = TestMember_SKN
    End
  End
  EditorSorting = UNIT
  KindOf = INFANTRY SELECTABLE
  Body = ActiveBody ModuleTag_Body
    MaxHealth = 450
  End
  WeaponSet
    Weapon = PRIMARY TestVolleyWeapon
  End
  Behavior = LockWeaponCreate ModuleTag_LockWeapon
    SlotToLock = PRIMARY
  End
  LocomotorSet
    Locomotor = TestLocomotor
    Speed = 100
  End
End
""",
    }


def _definition_row(source: bytes, kind: str, identifier: str) -> dict[str, object]:
    blocks = {
        block.name.casefold(): block
        for block in parse_flat_named_blocks(source, kind)
    }
    block = blocks[identifier.casefold()]
    return {"id": block.name, "definitionSha256": _gameplay_digest(block)}


def _mapped_image(
    identifier: str, left: int, top: int, right: int, bottom: int
) -> dict[str, object]:
    return {
        "id": identifier,
        "texture": "testicons_001.tga",
        "textureWidth": 256,
        "textureHeight": 128,
        "coords": {"left": left, "top": top, "right": right, "bottom": bottom},
        "compiledTextureVirtualPath": "art/compiledtextures/te/testicons_001.tga",
    }


def _graph(documents: dict[str, bytes]) -> dict[str, object]:
    sciences = [
        _definition_row(documents["data/ini/science.ini"], "Science", identifier)
        for identifier in (
            "SCIENCE_ELVES",
            "SCIENCE_GOOD",
            "SCIENCE_TestHeal",
            "SCIENCE_TestVolley",
        )
    ]
    powers = [
        _definition_row(
            documents["data/ini/specialpower.ini"], "SpecialPower", identifier
        )
        for identifier in ("SpellBookTestHeal", "SpellBookTestVolley")
    ]
    return {
        "target": {"playerTemplate": "FactionElves", "faction": "Elves"},
        "inputSetSha256": "a" * 64,
        "summary": {"unresolvedCount": 0},
        "roots": [
            {
                "sourceField": "SpellBookMP",
                "id": "TestSpellBook",
                "edgeKind": "object",
            },
            {
                "sourceField": "PurchaseScienceCommandSetMP",
                "id": "TestSpellStoreCommandSet",
                "edgeKind": "command-set",
            },
            {
                "sourceField": "IntrinsicSciencesMP",
                "id": "SCIENCE_ELVES",
                "edgeKind": "science",
            },
        ],
        "definitions": {
            "sciences": sciences,
            "specialPowers": powers,
            "upgrades": [],
        },
        "dependencies": {
            "spellbookSciences": ["SCIENCE_TestHeal", "SCIENCE_TestVolley"],
            "spellbookSpecialPowers": ["SpellBookTestHeal", "SpellBookTestVolley"],
        },
        "resolvedLeaves": {
            "mappedImages": [
                _mapped_image("SBTest_Heal", 0, 0, 32, 32),
                _mapped_image("SBTest_Volley", 32, 0, 64, 32),
            ],
            "audio": {
                "rootIds": ["TestHealSound", "TestVolleySound"],
                "events": [
                    {
                        "id": "TestHealSound",
                        "sounds": [{"id": "testheal_s1"}],
                        "parameters": [],
                    },
                    {
                        "id": "TestVolleySound",
                        "sounds": [{"id": "testvolley_s1"}],
                        "parameters": [],
                    },
                ],
                "multisounds": [],
                "sampleIds": ["testheal_s1", "testvolley_s1"],
                "samplePaths": [
                    {
                        "id": "testheal_s1",
                        "virtualPath": "data/audio/sounds/testheal_s1.wav",
                    },
                    {
                        "id": "testvolley_s1",
                        "virtualPath": "data/audio/sounds/testvolley_s1.wav",
                    },
                ],
            },
        },
    }


def _string_catalog() -> bytes:
    return b"""
CONTROLBAR:TestHeal
"Test Heal"
END
CONTROLBAR:TooltipTestHeal
"Test Heal Tooltip"
END
CONTROLBAR:TestVolley
"Test Volley"
END
CONTROLBAR:TooltipTestVolley
"Test Volley Tooltip"
END
"""


class _FakeCatalog:
    def __init__(self, source: bytes):
        self._source = source

    def resolve_exact(self, virtual_path: str) -> object | None:
        return object() if virtual_path == "data/lotr.str" else None

    def open_archive_for(self, entry: object) -> "_FakeCatalog":
        return self

    def as_entry(self, entry: object) -> object:
        return entry

    def read_entry(self, entry: object, *, max_bytes: int) -> bytes:
        assert len(self._source) <= max_bytes
        return self._source


def _fixture() -> tuple[dict[str, bytes], dict[str, object]]:
    documents = _documents()
    return documents, _graph(documents)


def _compile() -> dict[str, object]:
    documents, graph = _fixture()
    draft = compile_spellbook_descriptor(graph, documents)
    images, audio = _resolved_spellbook_media(graph, draft)
    strings = _resolved_spellbook_strings(_FakeCatalog(_string_catalog()), draft)
    return compile_spellbook_descriptor(
        graph,
        documents,
        resolved_images=images,
        resolved_audio=audio,
        resolved_strings=strings,
    )


def test_descriptor_resolves_tree_costs_prerequisites_and_effect_leaves() -> None:
    descriptor = _compile()
    validate_spellbook_descriptor(descriptor)
    assert descriptor["spellBook"] == {
        "objectId": "TestSpellBook",
        "kindOf": ["IGNORES_SELECT_ALL", "IMMOBILE", "INERT", "SPELL_BOOK"],
        "commandSetId": "TestSpellBookCommandSet",
        "spellStoreCommandSetId": "TestSpellStoreCommandSet",
        "intrinsicSciences": ["SCIENCE_ELVES"],
        "commandPointsUpgrade": {
            "triggeredBy": "Upgrade_MarketplaceUpgradeGrandHarvest",
            "commandPoints": 100,
            "requiredObject": "NONE +GondorMarketPlace",
            "module": "CommandPointsUpgrade",
            "sourceIni": "data/ini/object/system/test_system.ini",
            "line": 6,
        },
    }
    summoned_member = next(
        row
        for row in descriptor["leaves"]["objects"]
        if row["id"] == "TestSummonedMember"
    )
    assert summoned_member["weaponSlot"] == "PRIMARY"
    assert summoned_member["permanentWeaponLocks"][0]["module"] == (
        "LockWeaponCreate"
    )
    summoned_horde = next(
        row
        for row in descriptor["leaves"]["objects"]
        if row["id"] == "TestSummonedHorde"
    )
    assert summoned_horde["experienceLevelCreate"]["rank"] == 5
    assert summoned_horde["experience"]["initialRank"] == 5
    assert "experienceAward" not in summoned_horde["experience"]["levels"][0]
    assert (
        summoned_horde["experience"]["levels"][0]["experienceAwardStatus"]
        == "unauthored"
    )
    assert summoned_horde["experience"]["levels"][0]["attributeModifiers"][0][
        "id"
    ] == "TestSummonBonusRank5"
    assert summoned_horde["experience"]["levels"][0]["upgrades"] == [
        "Upgrade_ObjectLevel1",
        "Upgrade_ObjectLevel2",
        "Upgrade_ObjectLevel3",
        "Upgrade_ObjectLevel4",
        "Upgrade_ObjectLevel5",
    ]
    assert "ExperienceLevelCreate" not in summoned_horde.get(
        "unconvertedBehaviors", []
    )

    corrupted = deepcopy(descriptor)
    corrupted_member = next(
        row
        for row in corrupted["leaves"]["objects"]
        if row["id"] == "TestSummonedMember"
    )
    corrupted_member["permanentWeaponLocks"][0].pop("sourceIni")
    corrupted["descriptorSha256"] = _digest(
        {
            key: item
            for key, item in corrupted.items()
            if key != "descriptorSha256"
        }
    )
    with pytest.raises(
        SpellbookCompilerError,
        match="LockWeaponCreate leaf evidence is invalid",
    ):
        validate_spellbook_descriptor(corrupted)

    sciences = {row["id"]: row for row in descriptor["sciences"]}
    assert set(sciences) == {
        "SCIENCE_TestHeal",
        "SCIENCE_TestVolley",
        "SCIENCE_ELVES",
        "SCIENCE_GOOD",
    }
    heal = sciences["SCIENCE_TestHeal"]
    assert heal["pointCost"] == {"value": 5, "expression": "GOOD_RANK_1_COST"}
    assert heal["pointCostMP"] == {"value": 5, "expression": "5"}
    assert heal["isGrantable"] is True
    assert heal["prerequisiteGroups"] == [["SCIENCE_ELVES"], ["SCIENCE_GOOD"]]
    assert heal["purchase"]["slot"] == 1
    assert heal["purchase"]["iconIds"] == ["SBTest_Heal"]
    volley = sciences["SCIENCE_TestVolley"]
    assert volley["prerequisiteGroups"] == [["SCIENCE_ELVES", "SCIENCE_TestHeal"]]
    assert volley["prerequisites"] == ["SCIENCE_ELVES", "SCIENCE_TestHeal"]
    intrinsic = sciences["SCIENCE_ELVES"]
    assert "purchase" not in intrinsic and "pointCostMP" not in intrinsic
    assert intrinsic["pointCost"] == {"value": 0, "expression": "0"}

    powers = {row["id"]: row for row in descriptor["powers"]}
    assert set(powers) == {"SpellBookTestHeal", "SpellBookTestVolley"}
    heal_power = powers["SpellBookTestHeal"]
    assert heal_power["enum"] == "SPECIAL_SPELL_BOOK_TEST_HEAL"
    assert heal_power["reloadTimeMs"] == {
        "value": 30000,
        "expression": "SPELL_RECHARGE_TIME_TIER_1",
    }
    assert heal_power["requiredSciences"] == ["SCIENCE_TestHeal"]
    assert heal_power["initiateSoundId"] == "TestHealSound"
    assert heal_power["radiusCursorRadius"] == {"value": 75.0, "expression": "75.0"}
    assert heal_power["flags"] == ["RESPECT_RECHARGE_TIME_DISCOUNT", "WATER_OK"]
    assert heal_power["cast"]["radiusCursorType"] == "TestHealRadiusCursor"
    assert heal_power["cast"]["options"] == ["NEED_TARGET_POS"]
    effect = heal_power["effect"]
    assert effect["module"] == "PlayerHealSpecialPower"
    assert effect["moduleTag"] == "ModuleTag_Heal"
    assert effect["references"] == {
        "objectCreationLists": ["OCL_TestHealPing"],
        "fxLists": ["FX_TestHealBuff"],
    }
    volley_power = powers["SpellBookTestVolley"]
    assert volley_power["reloadTimeMs"] == {"value": 60000, "expression": "60000"}
    references = volley_power["effect"]["references"]
    assert references["objectCreationLists"] == ["OCL_TestVolley"]
    assert references["fxLists"] == ["FX_TestHealBuff"]
    assert references["attributeModifiers"] == ["TestRallyModifier"]
    assert references["upgrades"] == ["Upgrade_TestBlessing"]
    assert references["weapons"] == ["TestVolleyWeapon"]
    effect_fields = {
        str(row["key"]): row for row in volley_power["effect"]["fields"]
    }
    assert effect_fields["AttributeModifierAffects"]["value"] == "TEST_BUFF_FILTER"
    assert effect_fields["AttributeModifierAffects"]["resolvedText"] == "ANY +INFANTRY -HERO"

    leaves = descriptor["leaves"]
    ocls = {row["id"]: row for row in leaves["objectCreationLists"]}
    heal_ocl = ocls["OCL_TestHealPing"]["createObjects"][0]
    assert heal_ocl["objects"] == ["TestHealPing"]
    assert heal_ocl["particleSystems"] == ["TestHealParticles"]
    fx_lists = {row["id"]: row for row in leaves["fxLists"]}
    heal_fx_kinds = [nugget["kind"] for nugget in fx_lists["FX_TestHealBuff"]["nuggets"]]
    assert heal_fx_kinds == ["ParticleSystem", "Sound"]
    assert fx_lists["FX_TestHealBuff"]["nuggets"][1]["soundId"] == "TestHealSound"
    weapons = {row["id"]: row for row in leaves["weapons"]}
    assert weapons["TestVolleyWeapon"]["fireFx"] == ["FX_TestWeaponFire"]
    assert weapons["TestVolleyWeapon"]["nuggets"][0]["kind"] == "DamageNugget"
    assert weapons["TestVolleyWeapon"]["damageNuggets"] == [
        {"damage": 800, "radius": 100, "damagetype": "PIERCE"}
    ]
    assert weapons["TestVolleyWeapon"]["radiusDamageAffects"] == "ENEMIES NEUTRALS"
    # Receptacle-internal weapons are traversed with resolved damage nuggets.
    assert weapons["TestReceptacleInternalWeapon"]["damageNuggets"] == [
        {"damage": 250, "radius": 40, "damagetype": "CAVALRY_RANGED", "delaytime": 0}
    ]
    receptacle = {row["id"]: row for row in leaves["objects"]}["TestVolleyReceptacle"]
    assert receptacle["fireWeapons"] == [
        {"weapon": "TestReceptacleInternalWeapon", "fireDelayMs": 0, "oneShot": "Yes"}
    ]
    object_rows = {row["id"]: row for row in leaves["objects"]}
    horde = object_rows["TestSummonedHorde"]
    assert horde["horde"] == {
        "memberObject": "TestSummonedMember",
        "memberCount": 5,
        "slots": 5,
        "ranks": [{"rank": 1, "positions": [[0.0, 0.0], [0.0, 20.0]]}],
    }
    assert horde["lifetime"] == {"minMs": 75000, "maxMs": 75000, "deathType": "FADED"}
    assert horde["immortal"] is True
    assert horde["commandPoints"] == 0
    member = object_rows["TestSummonedMember"]
    # Authored Draw evidence rides every effect leaf: without it a summoned
    # battalion reaches the runtime with no art and presents as synthetic kit
    # geometry.
    assert member["draw"] == [
        {
            "drawModule": "W3DScriptedModelDraw",
            "conditions": [],
            "models": ["TestMember_SKN"],
        }
    ]
    # ``Model = None`` is the authored absence, recorded as such; the object's
    # only art is its ParticleSysBone system (retail CloudBreakSunbeam /
    # ElvenGrove shape).
    assert object_rows["TestHealPing"]["draw"] == [
        {
            "drawModule": "W3DScriptedModelDraw",
            "conditions": [],
            "particleSysBones": [
                {"bone": "None", "particleSystem": "TestHealParticles"}
            ],
        }
    ]
    assert "models" not in object_rows["TestHealPing"]["draw"][0]
    # Objects retail authors with no Draw module at all carry no invented one.
    assert "draw" not in object_rows["TestVolleyReceptacle"]
    assert member["maxHealth"] == 450
    assert member["weaponId"] == "TestVolleyWeapon"
    assert member["locomotor"] == {
        "id": "TestLocomotor",
        "speed": 100,
        "acceleration": 510,
        "braking": 510,
        "turnRateDegreesPerSecond": 720.0,
    }
    source_paths = {row["virtualPath"] for row in descriptor["sourceDocuments"]}
    assert "data/ini/locomotor.ini" in source_paths
    modifiers = {row["id"]: row for row in leaves["attributeModifiers"]}
    assert modifiers["TestRallyModifier"]["fxLists"] == ["FX_TestHealBuff"]
    particles = {row["id"] for row in leaves["particles"]}
    assert particles == {"TestHealParticles"}
    objects = {row["id"] for row in leaves["objects"]}
    assert objects == {
        "TestHealPing",
        "TestVolleyReceptacle",
        "TestSummonedHorde",
        "TestSummonedMember",
    }

    assert descriptor["requirements"] == {
        "mappedImages": ["SBTest_Heal", "SBTest_Volley"],
        "audio": ["TestHealSound", "TestVolleySound"],
        "strings": [
            "CONTROLBAR:TestHeal",
            "CONTROLBAR:TestVolley",
            "CONTROLBAR:TooltipTestHeal",
            "CONTROLBAR:TooltipTestVolley",
        ],
    }
    presentation = descriptor["presentation"]
    assert presentation["resolvedStrings"]["CONTROLBAR:TestHeal"] == "Test Heal"
    assert presentation["resolvedAudio"]["TestHealSound"] == [
        "data/audio/sounds/testheal_s1.wav"
    ]
    assert (
        presentation["resolvedImages"]["SBTest_Heal"]["compiledTextureVirtualPath"]
        == "art/compiledtextures/te/testicons_001.tga"
    )
    assert descriptor["descriptorSha256"] == _compile()["descriptorSha256"]


def test_grantable_science_qualifier_is_preserved() -> None:
    documents = _documents()
    documents["data/ini/science.ini"] = documents["data/ini/science.ini"].replace(
        b"IsGrantable = Yes\nEnd\n\nScience SCIENCE_TestVolley",
        b"IsGrantable = Yes SCIENCE_ELVES\nEnd\n\nScience SCIENCE_TestVolley",
        1,
    )
    graph = _graph(documents)
    draft = compile_spellbook_descriptor(graph, documents)
    row = next(
        item for item in draft["sciences"] if item["id"] == "SCIENCE_TestHeal"
    )
    assert row["isGrantable"] is True
    assert row["isGrantableQualifierSciences"] == ["SCIENCE_ELVES"]


def test_object_aura_preserves_explicit_target_polarity() -> None:
    documents = _documents()
    path = "data/ini/object/system/test_system.ini"
    documents[path] = documents[path].replace(
        b"  KindOf = NO_COLLIDE IMMOBILE INERT\nEnd\n",
        b"""  KindOf = NO_COLLIDE IMMOBILE INERT
  Behavior = AttributeModifierAuraUpdate ModuleTag_EnemyAura
    BonusName = TestRallyModifier
    RefreshDelay = 1000
    Range = 200
    TargetEnemy = Yes
    TargetAllies = No
    ObjectFilter = ANY +INFANTRY
  End
End
""",
        1,
    )
    descriptor = compile_spellbook_descriptor(_graph(documents), documents)
    ping = {
        row["id"]: row for row in descriptor["leaves"]["objects"]
    }["TestHealPing"]

    assert ping["aura"]["targetEnemy"] == "Yes"
    assert ping["aura"]["targetAllies"] == "No"


def test_command_points_upgrade_rejects_nonretail_points() -> None:
    documents, graph = _fixture()
    path = "data/ini/object/system/test_system.ini"
    documents[path] = documents[path].replace(
        b"    CommandPoints = 100\n",
        b"    CommandPoints = 99\n",
        1,
    )

    with pytest.raises(SpellbookCompilerError, match="outside the retail corpus"):
        compile_spellbook_descriptor(graph, documents)


def test_pack_recipe_and_runtime_bind_media_and_power_tree() -> None:
    descriptor = _compile()
    recipe = compile_spellbook_pack_recipe(descriptor)
    validate_spellbook_pack_recipe(recipe)
    kinds = {(row["kind"], row["converter"]) for row in recipe["resources"]}
    assert kinds == {("ui", "texture-atlas-crops"), ("audio", "audio")}
    registration = recipe["runtimeRegistration"]
    assert registration["spellBook"]["commandPointsUpgrade"] == (
        descriptor["spellBook"]["commandPointsUpgrade"]
    )
    assert registration["imageBindings"]["SBTest_Heal"].startswith(
        "assets/ui/spellbook/testspellbook/"
    )
    assert registration["imageBindingMetadata"]["SBTest_Volley"] == {
        "width": 32,
        "height": 32,
    }
    assert registration["audioBindings"]["TestHealSound"] == [
        output
        for output in registration["audioBindings"]["TestHealSound"]
        if output.startswith("assets/audio/spellbook/testspellbook/")
    ]
    assert registration["stringBindings"]["CONTROLBAR:TestVolley"] == "Test Volley"
    assert len(registration["powers"]) == 2

    runtime = compose_spellbook_runtime_document(descriptor, recipe)
    assert runtime["schema"] == "openbfme.spellbook-runtime"
    assert runtime["descriptorSha256"] == descriptor["descriptorSha256"]
    assert runtime["recipeSha256"] == recipe["recipeSha256"]
    assert runtime["registration"]["spellBook"]["commandPointsUpgrade"] == (
        descriptor["spellBook"]["commandPointsUpgrade"]
    )
    tree = runtime["registration"]["powerTree"]
    assert len(tree["sciences"]) == 4
    assert len(tree["powers"]) == 2
    assert runtime["registration"]["resourceIds"] == sorted(
        row["id"] for row in recipe["resources"]
    )

    summary = summarize_spellbook_lane(
        {"descriptor": descriptor, "recipe": recipe, "runtime": runtime}
    )
    assert summary["scienceCount"] == 4
    assert summary["purchasableScienceCount"] == 2
    assert summary["powerCount"] == 2
    assert summary["leafCounts"]["weapons"] == 2
    assert summary["resourceCount"] == 3


@pytest.mark.parametrize(
    ("path", "old", "new", "message"),
    (
        (
            "data/ini/specialpower.ini",
            b"  ReloadTime = 60000\n",
            b"  ReloadTime = 61000\n",
            "no longer matches its census definition digest",
        ),
        (
            "data/ini/objectcreationlist.ini",
            b"ObjectCreationList OCL_TestVolley",
            b"ObjectCreationList OCL_Renamed",
            "missing ObjectCreationList: OCL_TestVolley",
        ),
        (
            "data/ini/fxlist.ini",
            b"FXList FX_TestWeaponFire",
            b"FXList FX_Renamed",
            "references a missing FXList: FX_TestWeaponFire",
        ),
        (
            "data/ini/weapon.ini",
            b"Weapon TestVolleyWeapon",
            b"Weapon RenamedWeapon",
            "missing Weapon: TestVolleyWeapon",
        ),
        (
            "data/ini/attributemodifier.ini",
            b"ModifierList TestRallyModifier",
            b"ModifierList RenamedModifier",
            "missing ModifierList: TestRallyModifier",
        ),
        (
            "data/ini/upgrade.ini",
            b"Upgrade Upgrade_TestBlessing",
            b"Upgrade Upgrade_Renamed",
            "missing Upgrade: Upgrade_TestBlessing",
        ),
        (
            "data/ini/object/system/test_system.ini",
            b"Object TestHealPing",
            b"Object RenamedPing",
            "references a missing Object: TestHealPing",
        ),
        (
            "data/ini/gamedata.ini",
            b"#define SPELL_RECHARGE_TIME_TIER_1 30000",
            b"",
            "unresolved expression: SPELL_RECHARGE_TIME_TIER_1",
        ),
        (
            "data/ini/science.ini",
            b"Science SCIENCE_TestHeal",
            b"Science SCIENCE_Renamed",
            "effective Science is missing: SCIENCE_TestHeal",
        ),
        (
            "data/ini/object/system/test_system.ini",
            b"    SpecialPowerTemplate = SpellBookTestVolley\n",
            b"    SpecialPowerTemplate = SpellBookTestHeal\n",
            "bound by multiple modules",
        ),
        (
            "data/ini/object/system/test_system.ini",
            b"    HealOCL = OCL_TestHealPing\n",
            b"    StartOCL = OCL_TestHealPing\n",
            "unsupported effect leaf field: StartOCL",
        ),
    ),
)
def test_lane_fails_closed_on_missing_or_drifted_leaves(
    path: str, old: bytes, new: bytes, message: str
) -> None:
    documents, graph = _fixture()
    assert old in documents[path]
    documents[path] = documents[path].replace(old, new)

    with pytest.raises(SpellbookCompilerError, match=message):
        compile_spellbook_descriptor(graph, documents)


def test_lane_fails_closed_when_power_module_is_missing() -> None:
    documents, graph = _fixture()
    path = "data/ini/object/system/test_system.ini"
    start = documents[path].index(b"  Behavior = OCLSpecialPower ModuleTag_Volley")
    end = documents[path].index(b"  End\n", start) + len(b"  End\n")
    documents[path] = documents[path][:start] + documents[path][end:]

    with pytest.raises(SpellbookCompilerError, match="has no spell-power module"):
        compile_spellbook_descriptor(graph, documents)


def test_lane_fails_closed_on_unresolved_census_leaves() -> None:
    documents, graph = _fixture()
    graph["summary"]["unresolvedCount"] = 2

    with pytest.raises(SpellbookCompilerError, match="unresolved census leaves"):
        compile_spellbook_descriptor(graph, documents)


def test_lane_fails_closed_when_tree_disagrees_with_census() -> None:
    documents, graph = _fixture()
    graph["dependencies"]["spellbookSciences"] = ["SCIENCE_TestHeal"]

    with pytest.raises(
        SpellbookCompilerError, match="disagrees with census spellbookSciences"
    ):
        compile_spellbook_descriptor(graph, documents)


def test_layered_authority_uses_its_command_surface_over_stale_census_sets() -> None:
    documents, graph = _fixture()
    graph["spellbookDefinitionAuthority"] = "layered-effective-assets"
    graph["dependencies"]["spellbookSciences"] = ["SCIENCE_TestHeal"]
    graph["dependencies"]["spellbookSpecialPowers"] = ["SpellBookTestHeal"]
    compile_spellbook_descriptor(graph, documents)

    documents, graph = _fixture()
    graph["spellbookDefinitionAuthority"] = "layered-effective-assets"
    graph["dependencies"]["spellbookSciences"].append("SCIENCE_DroppedByLayer")
    compile_spellbook_descriptor(graph, documents)

    documents, graph = _fixture()
    graph["spellbookDefinitionAuthority"] = "layered-effective-assets"
    graph["dependencies"]["spellbookSpecialPowers"].append(
        "SpellBookDroppedByLayer"
    )
    compile_spellbook_descriptor(graph, documents)

    documents, graph = _fixture()
    graph["dependencies"]["spellbookSpecialPowers"] = ["SpellBookTestHeal"]

    with pytest.raises(
        SpellbookCompilerError, match="disagrees with census spellbookSpecialPowers"
    ):
        compile_spellbook_descriptor(graph, documents)


def test_media_resolution_fails_closed_on_unresolved_image_or_audio() -> None:
    documents, graph = _fixture()
    draft = compile_spellbook_descriptor(graph, documents)
    broken = deepcopy(graph)
    broken["resolvedLeaves"]["mappedImages"][0]["compiledTextureResolution"] = "missing"
    del broken["resolvedLeaves"]["mappedImages"][0]["compiledTextureVirtualPath"]

    fallback_image = b"""
MappedImage SBTest_Heal
  Texture = testicons_001.tga
  TextureWidth = 256
  TextureHeight = 128
  Coords = Left:0 Top:0 Right:32 Bottom:32
End
"""
    with pytest.raises(
        ValueError, match="mapped image census leaf is unresolved"
    ):
        _resolved_spellbook_media(
            broken,
            draft,
            fallback_mapped_image_sources=(fallback_image,),
            fallback_virtual_paths=(
                "art/compiledtextures/te/testicons_001.tga",
            ),
        )

    broken = deepcopy(graph)
    broken["resolvedLeaves"]["audio"]["events"] = [
        row
        for row in broken["resolvedLeaves"]["audio"]["events"]
        if row["id"] != "TestVolleySound"
    ]

    with pytest.raises(ValueError, match="audio dependency is unresolved"):
        _resolved_spellbook_media(broken, draft)


def test_audio_fallback_only_applies_when_root_definition_is_absent() -> None:
    documents, graph = _fixture()
    draft = compile_spellbook_descriptor(graph, documents)
    broken = deepcopy(graph)
    broken["resolvedLeaves"]["audio"]["samplePaths"] = [
        row
        for row in broken["resolvedLeaves"]["audio"]["samplePaths"]
        if row["id"] != "testvolley_s1"
    ]
    fallback = b"""
AudioEvent TestVolleySound
  Sounds = testvolley_s1
End
"""
    with pytest.raises(ValueError, match="audio dependency is unresolved"):
        _resolved_spellbook_media(
            broken,
            draft,
            fallback_audio_source=fallback,
            fallback_virtual_paths=("data/audio/sounds/testvolley_s1.wav",),
        )


def test_fxlist_preserves_authored_particle_absent_from_shipped_definitions() -> None:
    documents = _documents()
    documents["data/ini/fxlist.ini"] = documents["data/ini/fxlist.ini"].replace(
        b"Name = TestHealParticles",
        b"Name = RetailAbsentFxParticle",
        1,
    )
    descriptor = _compile_with(documents, _graph(documents))

    fx_lists = {row["id"]: row for row in descriptor["leaves"]["fxLists"]}
    particle = fx_lists["FX_TestHealBuff"]["nuggets"][0]
    assert particle["unresolvedParticleSystem"] == {
        "id": "RetailAbsentFxParticle",
        "definitionFamily": "unknown-legacy-family-not-in-view",
        "reason": "authored FXList particle has no shipped definition",
    }
    validate_spellbook_descriptor(descriptor)


def test_mapped_image_fallback_only_applies_when_definition_is_absent() -> None:
    documents, graph = _fixture()
    draft = compile_spellbook_descriptor(graph, documents)
    missing = deepcopy(graph)
    missing["resolvedLeaves"]["mappedImages"] = [
        row
        for row in missing["resolvedLeaves"]["mappedImages"]
        if row["id"] != "SBTest_Heal"
    ]
    fallback = b"""
MappedImage SBTest_Heal
  Texture = testicons_001.tga
  TextureWidth = 256
  TextureHeight = 128
  Coords = Left:0 Top:0 Right:32 Bottom:32
End
"""
    images, _audio = _resolved_spellbook_media(
        missing,
        draft,
        fallback_mapped_image_sources=(fallback,),
        fallback_virtual_paths=("art/compiledtextures/te/testicons_001.tga",),
    )
    assert images["SBTest_Heal"]["compiledTextureVirtualPath"] == (
        "art/compiledtextures/te/testicons_001.tga"
    )


def test_string_resolution_fails_closed_on_missing_record() -> None:
    documents, graph = _fixture()
    draft = compile_spellbook_descriptor(graph, documents)

    with pytest.raises(ValueError, match="required localized string is unresolved"):
        _resolved_spellbook_strings(_FakeCatalog(b""), draft)


def test_layered_source_null_spellbook_string_is_recorded_not_invented() -> None:
    documents, graph = _fixture()
    draft = compile_spellbook_descriptor(graph, documents)
    layered = deepcopy(graph)
    layered["layeredDocumentAuthority"] = "layered-effective-assets"
    layered["layeredSourceNullTextIds"] = []

    resolved = _resolved_spellbook_strings(_FakeCatalog(b""), draft, graph=layered)
    assert resolved == {}
    assert layered["layeredSourceNullTextIds"]
    images, audio = _resolved_spellbook_media(layered, draft)

    descriptor = compile_spellbook_descriptor(
        layered,
        documents,
        resolved_images=images,
        resolved_audio=audio,
        resolved_strings=resolved,
    )
    source_null_ids = descriptor["presentation"]["sourceNullStringIds"]
    assert source_null_ids == sorted(
        draft["requirements"]["strings"], key=str.casefold
    )
    recipe = compile_spellbook_pack_recipe(descriptor)
    assert recipe["runtimeRegistration"]["stringBindings"] == {}
    assert recipe["runtimeRegistration"]["sourceNullStringIds"] == source_null_ids
    runtime = compose_spellbook_runtime_document(descriptor, recipe)
    assert runtime["registration"]["presentation"]["sourceNullStringIds"] == (
        source_null_ids
    )


def test_pack_recipe_fails_closed_on_unresolved_media() -> None:
    documents, graph = _fixture()
    draft = compile_spellbook_descriptor(graph, documents)

    with pytest.raises(
        SpellbookPackCompilerError, match="string bindings are unresolved"
    ):
        compile_spellbook_pack_recipe(draft)


def test_runtime_composition_fails_closed_on_identity_drift() -> None:
    descriptor = _compile()
    recipe = compile_spellbook_pack_recipe(descriptor)
    drifted = deepcopy(recipe)
    drifted["descriptorSha256"] = "0" * 64

    with pytest.raises(
        SpellbookPackCompilerError, match="identities differ|digest is invalid"
    ):
        compose_spellbook_runtime_document(descriptor, drifted)


def test_spell_store_rejects_duplicate_science_slots() -> None:
    documents, graph = _fixture()
    path = "data/ini/commandset.ini"
    documents[path] += b"""
CommandSet TestSpellStoreCommandSetDuplicate
  1 = Command_PurchaseSpellTestHeal
End
"""
    documents[path] = documents[path].replace(
        b"  2 = Command_PurchaseSpellTestVolley\nEnd",
        b"  2 = Command_PurchaseSpellTestHeal\nEnd",
        1,
    )

    with pytest.raises(SpellbookCompilerError, match="multiple slots"):
        compile_spellbook_descriptor(graph, documents)


def _compile_with(documents: dict[str, bytes], graph: dict[str, object]) -> dict[str, object]:
    draft = compile_spellbook_descriptor(graph, documents)
    images, audio = _resolved_spellbook_media(graph, draft)
    strings = _resolved_spellbook_strings(_FakeCatalog(_string_catalog()), draft)
    return compile_spellbook_descriptor(
        graph,
        documents,
        resolved_images=images,
        resolved_audio=audio,
        resolved_strings=strings,
    )


def test_case_variant_icon_casings_emit_one_crop_and_keep_all_bindings() -> None:
    documents, graph = _fixture()
    path = "data/ini/commandbutton.ini"
    old = b"  ButtonImage = SBTest_Volley\n  ButtonBorderType = ACTION"
    assert documents[path].count(old) == 1
    documents[path] = documents[path].replace(
        old, b"  ButtonImage = SBTest_vOlley\n  ButtonBorderType = ACTION"
    )

    descriptor = _compile_with(documents, graph)
    # Both authored casings remain faithful requirements evidence.
    assert descriptor["requirements"]["mappedImages"] == [
        "SBTest_Heal",
        "SBTest_Volley",
        "SBTest_vOlley",
    ]

    recipe = compile_spellbook_pack_recipe(descriptor)
    validate_spellbook_pack_recipe(recipe)
    crops = [
        crop
        for row in recipe["resources"]
        if row["kind"] == "ui"
        for crop in row["options"]["crops"]
    ]
    logical_names = [crop["logicalName"] for crop in crops]
    assert logical_names == ["image-sbtest-heal", "image-sbtest-volley"]

    registration = recipe["runtimeRegistration"]
    bindings = registration["imageBindings"]
    assert bindings["SBTest_Volley"] == bindings["SBTest_vOlley"]
    assert registration["imageBindingMetadata"]["SBTest_Volley"] == (
        registration["imageBindingMetadata"]["SBTest_vOlley"]
    )
    runtime = compose_spellbook_runtime_document(descriptor, recipe)
    assert runtime["registration"]["presentation"]["imageBindings"]["SBTest_vOlley"] == (
        bindings["SBTest_Volley"]
    )
    # The dedupe is deterministic across repeated compilations.
    assert (
        compile_spellbook_pack_recipe(_compile_with(documents, graph))["recipeSha256"]
        == recipe["recipeSha256"]
    )


def test_mordor_rainoffire_case_variants_emit_one_crop_and_keep_bindings() -> None:
    """Retail Mordor authors SBEvil_RainOfFire and SBEvil_RainofFire for one atlas rect.

    Those identifiers casefold to one logicalName (image-sbevil-rainoffire). The pack
    compiler must emit a single crop while retaining both imageBindings so compose/load
    validation does not reject duplicate texture-atlas logicalName values.
    """
    documents, graph = _fixture()
    path = "data/ini/commandbutton.ini"
    assert documents[path].count(b"ButtonImage = SBTest_Volley") == 2
    documents[path] = documents[path].replace(
        b"ButtonImage = SBTest_Volley", b"ButtonImage = SBEvil_RainOfFire", 1
    )
    documents[path] = documents[path].replace(
        b"ButtonImage = SBTest_Volley", b"ButtonImage = SBEvil_RainofFire", 1
    )
    images = graph["resolvedLeaves"]["mappedImages"]
    assert isinstance(images, list)
    for index, row in enumerate(images):
        if row["id"] == "SBTest_Volley":
            # Retail atlas rect for Rain of Fire: crop [256, 0, 64, 64].
            images[index] = _mapped_image("SBEvil_RainOfFire", 256, 0, 320, 64)

    descriptor = _compile_with(documents, graph)
    assert descriptor["requirements"]["mappedImages"] == [
        "SBEvil_RainOfFire",
        "SBEvil_RainofFire",
        "SBTest_Heal",
    ]

    recipe = compile_spellbook_pack_recipe(descriptor)
    validate_spellbook_pack_recipe(recipe)
    crops = [
        crop
        for row in recipe["resources"]
        if row["kind"] == "ui"
        for crop in row["options"]["crops"]
    ]
    rain_crops = [
        crop for crop in crops if crop["logicalName"] == "image-sbevil-rainoffire"
    ]
    assert len(rain_crops) == 1
    assert rain_crops[0]["crop"] == [256, 0, 64, 64]
    assert rain_crops[0]["output"] == "sbevil-rainoffire-a9e1e5a2.png"

    registration = recipe["runtimeRegistration"]
    bindings = registration["imageBindings"]
    assert bindings["SBEvil_RainOfFire"] == bindings["SBEvil_RainofFire"]
    assert bindings["SBEvil_RainOfFire"].endswith("/sbevil-rainoffire-a9e1e5a2.png")
    assert registration["imageBindingMetadata"]["SBEvil_RainOfFire"] == {
        "width": 64,
        "height": 64,
    }
    assert registration["imageBindingMetadata"]["SBEvil_RainofFire"] == {
        "width": 64,
        "height": 64,
    }
    runtime = compose_spellbook_runtime_document(descriptor, recipe)
    assert runtime["registration"]["presentation"]["imageBindings"][
        "SBEvil_RainofFire"
    ] == bindings["SBEvil_RainOfFire"]


def test_distinct_images_colliding_on_logical_name_fail_closed() -> None:
    documents, graph = _fixture()
    path = "data/ini/commandbutton.ini"
    old = b"  ButtonImage = SBTest_Volley\n  Science = SCIENCE_TestVolley"
    assert documents[path].count(old) == 1
    documents[path] = documents[path].replace(
        old, b"  ButtonImage = SBTest.Volley\n  Science = SCIENCE_TestVolley"
    )
    graph["resolvedLeaves"]["mappedImages"].append(
        _mapped_image("SBTest.Volley", 64, 0, 96, 32)
    )

    descriptor = _compile_with(documents, graph)
    with pytest.raises(SpellbookPackCompilerError, match="collide"):
        compile_spellbook_pack_recipe(descriptor)


class TestParticleSysBoneFields:
    """RotWK authors bone names as quoted strings containing spaces.

    ``data/ini/object/neutral/neutralunits.ini:5969`` (AngmarShadeWolf, the
    Angmar spellbook's summoned Shade Wolf) authors
    ``ParticleSysBone = "Bip L Finger2" SoWolf_Ambient_fog FollowBone:YES``.
    Splitting that on whitespace makes the second field ``L`` — a fragment of
    the BONE name read as a particle-system name — which fails the definition
    lookup and costs the whole faction its spellbook object.
    """

    def test_quoted_bone_with_spaces_keeps_the_real_system_name(self) -> None:
        assert _particle_sys_bone_fields(
            '"Bip L Finger2" SoWolf_Ambient_fog FollowBone:YES'
        ) == ("Bip L Finger2", "SoWolf_Ambient_fog")
        assert _particle_sys_bone_fields(
            '"Bip R Finger2" SoWolf_Ambient_EmbersHero FollowBone:YES'
        ) == ("Bip R Finger2", "SoWolf_Ambient_EmbersHero")

    def test_unquoted_form_is_unchanged(self) -> None:
        assert _particle_sys_bone_fields("None SoWolf_Ambient_fog01") == (
            "None",
            "SoWolf_Ambient_fog01",
        )
        assert _particle_sys_bone_fields(
            "STAFF GandalfMoriaLight FollowBone:Yes"
        ) == ("STAFF", "GandalfMoriaLight")

    def test_value_without_both_fields_binds_nothing(self) -> None:
        assert _particle_sys_bone_fields("None") is None
        assert _particle_sys_bone_fields("") is None
        assert _particle_sys_bone_fields('"Bip L Finger2"') is None


def test_unresolvable_particle_sys_bone_is_recorded_with_its_source_line():
    """A system retail names but never defines is evidence, not an invention.

    ``neutralunits.ini:6016`` authors ``SoWolf_Ambient_snowFollowBone:YES`` —
    one missing space away from the real ``SoWolf_Ambient_snow``. SAGE looks
    that name up, misses, and draws nothing. The leaf keeps the authored
    reference with its source line so the gap is quotable, and no definition
    is substituted.
    """

    documents = _documents()
    documents["data/ini/object/system/test_system.ini"] = documents[
        "data/ini/object/system/test_system.ini"
    ].replace(
        b"      ParticleSysBone = None TestHealParticles\n",
        b"      ParticleSysBone = None TestHealParticles\n"
        b"      ParticleSysBone = None TestHealParticlesFollowBone:YES\n",
    )
    descriptor = _compile_with(documents, _graph(documents))
    ping = {row["id"]: row for row in descriptor["leaves"]["objects"]}["TestHealPing"]
    state = ping["draw"][0]
    # The well-formed line still binds.
    assert state["particleSysBones"] == [
        {"bone": "None", "particleSystem": "TestHealParticles"}
    ]
    unresolved = state["unresolvedParticleSysBones"]
    assert len(unresolved) == 1
    assert unresolved[0]["particleSystem"] == "TestHealParticlesFollowBone:YES"
    assert (
        unresolved[0]["authoredValue"] == "None TestHealParticlesFollowBone:YES"
    )
    assert unresolved[0]["sourceIni"] == "data/ini/object/system/test_system.ini"
    assert isinstance(unresolved[0]["line"], int)
    # The legacy family is not in this view, so the record says so rather than
    # asserting an absence it never checked.
    assert unresolved[0]["authoredFamily"] == "unknown-legacy-family-not-in-view"
    # The unresolved name is never promoted into the converted particle leaves.
    assert {row["id"] for row in descriptor["leaves"]["particles"]} == {
        "TestHealParticles"
    }


def test_unresolvable_particle_is_classified_against_the_legacy_family():
    """When the legacy family IS in view, the record names it exactly.

    Retail ships two particle families; this lane binds only
    ``FXParticleSystem``. ``RainOfFireProjectileSmoke`` and
    ``InfantryDustTrails`` exist only in ``data/ini/particlesystem.ini``,
    while ``BalrogSword`` and ``GoblinKingTaint`` are in neither file. Those
    are different gaps and the evidence has to distinguish them.
    """

    documents = _documents()
    documents["data/ini/particlesystem.ini"] = b"""
ParticleSystem LegacyOnlyParticles
  Priority = AREA_EFFECT
  ParticleName = EXSmoke.tga
End
"""
    documents["data/ini/object/system/test_system.ini"] = documents[
        "data/ini/object/system/test_system.ini"
    ].replace(
        b"      ParticleSysBone = None TestHealParticles\n",
        b"      ParticleSysBone = None TestHealParticles\n"
        b"      ParticleSysBone = None LegacyOnlyParticles\n"
        b"      ParticleSysBone = None NotAnywhereParticles\n",
    )
    descriptor = _compile_with(documents, _graph(documents))
    ping = {row["id"]: row for row in descriptor["leaves"]["objects"]}["TestHealPing"]
    families = {
        row["particleSystem"]: row["authoredFamily"]
        for row in ping["draw"][0]["unresolvedParticleSysBones"]
    }
    assert families == {
        "LegacyOnlyParticles": "ParticleSystem",
        "NotAnywhereParticles": "none",
    }


def test_effect_geometry_rides_the_recipe_and_the_runtime_document() -> None:
    """A summoned member's model reaches the runtime as a real pack GLB.

    Before this lane the spellbook recipe had no W3D stage at all, so a
    summoned battalion arrived at the presentation bridge with no mesh path and
    fell back to the synthetic multi-part kit. The horde container the OCL
    actually creates draws nothing itself, so it binds to its authored
    MemberObject's model; the particle-only ping stays invisible.
    """

    from openbfme_importer.playable_unit_pack_compiler import _digest as _closure_digest

    descriptor = _compile()
    closure: dict[str, object] = {
        "schema": "openbfme.retail-visual-closure",
        "schemaVersion": 1,
        "targets": [{"name": "TestSummonedMember", "status": "resolved"}],
        "exactLeaves": [
            {
                "targetObject": "TestSummonedMember",
                "identifier": "TestMember_SKN",
                "kind": "model",
                "usage": "model",
                "status": "resolved",
                "conditions": [],
                "physicalVirtualPaths": ["art/w3d/tt/testmember_skn.w3d"],
                "provenance": {
                    "definingObject": "TestSummonedMember",
                    "virtualPath": "data/ini/object/system/test_system.ini",
                    "line": 1,
                    "scopePath": ["W3DScriptedModelDraw ModuleTag_01"],
                },
            }
        ],
        "semanticLeaves": [],
        "unresolved": {"graphDiagnostics": [], "references": []},
        "scannedW3d": [
            {
                "virtualPath": "art/w3d/tt/testmember_skn.w3d",
                "byteLength": 2048,
                "headerIds": {"hierarchyIds": ["TESTMEMBER_SKL"], "animationIds": []},
                "modelHierarchyIdentifiers": [],
                "embeddedAnimationChannelCount": 0,
            }
        ],
        "w3dDependencyClosure": {
            "embeddedTextures": [
                {
                    "sourceW3dVirtualPath": "art/w3d/tt/testmember_skn.w3d",
                    "identifier": "testmember.tga",
                    "status": "resolved",
                    "physicalVirtualPaths": ["art/textures/testmember.tga"],
                }
            ]
        },
        "summary": {"ready": True},
    }
    closure["aggregateSha256"] = _closure_digest(closure)

    baseline = compile_spellbook_pack_recipe(descriptor)
    recipe = compile_spellbook_pack_recipe(
        descriptor, visual_closures={"TestSummonedMember": closure}
    )
    validate_spellbook_pack_recipe(recipe)
    # Absent closures keep the pre-visual bytes exactly.
    assert "visualBindings" not in baseline["runtimeRegistration"]
    assert len(recipe["resources"]) == len(baseline["resources"]) + 2

    model = next(
        row
        for row in recipe["resources"]
        if row["kind"] == "model"
    )
    assert model["converter"] == "w3d-hierarchical"
    assert model["output"] == (
        "assets/models/spellbook/testspellbook/testsummonedmember.glb"
    )

    objects = recipe["runtimeRegistration"]["visualBindings"]["objects"]
    assert objects["TestSummonedMember"]["status"] == "model"
    assert objects["TestSummonedHorde"] == {
        "status": "horde-member",
        "memberObjectId": "TestSummonedMember",
        "resourceId": model["id"],
        "model": model["output"],
        "sourceW3d": "art/w3d/tt/testmember_skn.w3d",
        "converter": "w3d-hierarchical",
    }
    assert objects["TestHealPing"]["status"] == "authored-invisible"

    runtime = compose_spellbook_runtime_document(descriptor, recipe)
    presented = runtime["registration"]["presentation"]["visualBindings"]["objects"]
    assert presented["TestSummonedHorde"]["model"] == model["output"]


def test_recipe_rejects_a_visual_binding_with_no_owning_resource() -> None:
    descriptor = _compile()
    recipe = compile_spellbook_pack_recipe(descriptor)
    tampered = deepcopy(recipe)
    tampered["runtimeRegistration"]["visualBindings"] = {
        "objects": {
            "TestSummonedMember": {
                "status": "model",
                "resourceId": "not-a-resource",
                "model": "assets/models/spellbook/testspellbook/x.glb",
            }
        },
        "summary": {},
    }
    tampered.pop("recipeSha256")
    from openbfme_importer.spellbook_pack_compiler import _digest as _recipe_digest

    tampered["recipeSha256"] = _recipe_digest(tampered)
    with pytest.raises(SpellbookPackCompilerError):
        validate_spellbook_pack_recipe(tampered)
