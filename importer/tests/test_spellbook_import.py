from __future__ import annotations

from copy import deepcopy

import pytest

from openbfme_importer.sage_gameplay import _digest as _gameplay_digest
from openbfme_importer.sage_ini import parse_flat_named_blocks
from openbfme_importer.spellbook_compiler import (
    SpellbookCompilerError,
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
    UpgradeName = Upgrade_TestBlessing
    Weapon = TestVolleyWeapon
    CreateLocation = CREATE_AT_LOCATION
    AvailableAtStart = No
  End
End

Object TestHealPing
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
    }

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
    # Receptacle-internal weapons are object-lane payload and never traversed.
    assert "TestReceptacleInternalWeapon" not in weapons
    modifiers = {row["id"]: row for row in leaves["attributeModifiers"]}
    assert modifiers["TestRallyModifier"]["fxLists"] == ["FX_TestHealBuff"]
    particles = {row["id"] for row in leaves["particles"]}
    assert particles == {"TestHealParticles", "TestWeaponFireParticles"}
    objects = {row["id"] for row in leaves["objects"]}
    assert objects == {"TestHealPing", "TestVolleyReceptacle"}

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


def test_pack_recipe_and_runtime_bind_media_and_power_tree() -> None:
    descriptor = _compile()
    recipe = compile_spellbook_pack_recipe(descriptor)
    validate_spellbook_pack_recipe(recipe)
    kinds = {(row["kind"], row["converter"]) for row in recipe["resources"]}
    assert kinds == {("ui", "texture-atlas-crops"), ("audio", "audio")}
    registration = recipe["runtimeRegistration"]
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
    assert summary["leafCounts"]["weapons"] == 1
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
            "missing FXList: FX_TestWeaponFire",
        ),
        (
            "data/ini/fxparticlesystem.ini",
            b"FXParticleSystem TestWeaponFireParticles",
            b"FXParticleSystem RenamedParticles",
            "missing particle definition",
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

    with pytest.raises(ValueError, match="required spellbook mapped image is unresolved"):
        _resolved_spellbook_media(broken, draft)

    broken = deepcopy(graph)
    broken["resolvedLeaves"]["audio"]["events"] = [
        row
        for row in broken["resolvedLeaves"]["audio"]["events"]
        if row["id"] != "TestVolleySound"
    ]

    with pytest.raises(ValueError, match="audio dependency is unresolved"):
        _resolved_spellbook_media(broken, draft)


def test_string_resolution_fails_closed_on_missing_record() -> None:
    documents, graph = _fixture()
    draft = compile_spellbook_descriptor(graph, documents)

    with pytest.raises(ValueError, match="required localized string is unresolved"):
        _resolved_spellbook_strings(_FakeCatalog(b""), draft)


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
