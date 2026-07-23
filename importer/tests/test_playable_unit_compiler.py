from __future__ import annotations

from copy import deepcopy
import hashlib
import json

import pytest

from openbfme_importer.playable_unit_compiler import (
    PlayableUnitCompilerError,
    compile_playable_unit_descriptor,
    playable_object_kind_of,
    prepare_playable_unit_compiler,
    validate_playable_unit_descriptor,
)


def _object(
    name: str, kind_of: str, model: str, *, payload: str = "", special: bool = False
) -> str:
    contain = (
        "  Behavior = HordeContain ModuleTag_HordeContain\n"
        f"    InitialPayload = {payload}\n"
        "  End\n"
        if payload
        else ""
    )
    weapon = (
        "  WeaponSet\n"
        "    Conditions = None\n"
        "    Weapon = PRIMARY TestRangedWeapon\n"
        "  End\n"
        if "ARCHER" in kind_of.split()
        else ""
    )
    special_block = (
        "  Behavior = RespawnUpdate ModuleTag_Respawn\n    DeathAnim = DYING\n  End\n"
        if special
        else ""
    )
    return f"""
Object {name}
  KindOf = PRELOAD SELECTABLE {kind_of}
  BuildCost = 500
  BuildTime = 30
  CommandPoints = 20
  VisionRange = 300
  SelectPortrait = UP{name}
  VoiceSelect = {name}VoiceSelect
  VoicePriority = 43
  VoiceMove = {name}VoiceMove
  VoiceAttack = {name}VoiceAttack
  Draw = W3DScriptedModelDraw ModuleTag_Draw
    DefaultModelConditionState
      Model = {model}
    End
  End
{weapon}
{special_block}
{contain}End
"""


def _documents() -> dict[str, bytes]:
    objects = """
Object UniversalFactory
  CommandSet = UniversalFactoryCommandSet
  KindOf = PRELOAD SELECTABLE STRUCTURE
End

Object UpgradingFactory
  CommandSet = UpgradingFactoryCommandSet
  KindOf = PRELOAD SELECTABLE STRUCTURE
  Behavior = CommandSetUpgrade ModuleTag_Level2
    TriggeredBy = Upgrade_FactoryLevel2
    CommandSet = UpgradingFactoryCommandSetLevel2
  End
End

Object AlternateFactory
  CommandSet = AlternateFactoryCommandSet
  KindOf = PRELOAD SELECTABLE STRUCTURE
End
"""
    objects += _object("InfantryMember", "INFANTRY", "InfantryModel")
    objects += _object(
        "InfantryHorde",
        "HORDE",
        "InfantryHordeModel",
        payload="InfantryMember #MULTIPLY( GOOD_HORDE_SIZE 1 )",
    )
    objects += _object("RangedMember", "INFANTRY ARCHER", "RangedModel")
    objects += _object(
        "RangedHorde", "HORDE", "RangedHordeModel", payload="RangedMember 10"
    )
    objects += _object("CavalryMember", "CAVALRY ARCHER", "CavalryModel")
    objects += _object(
        "CavalryHorde", "HORDE", "CavalryHordeModel", payload="CavalryMember 5"
    )
    objects += _object("HeroUnit", "HERO INFANTRY", "HeroModel")
    objects += _object("SiegeUnit", "MACHINE SIEGEENGINE", "SiegeModel")
    objects += _object("MonsterUnit", "MONSTER", "MonsterModel", special=True)
    objects += _object("NavalUnit", "SHIP TRANSPORT", "NavalModel")
    objects += _object("ReplacementMember", "INFANTRY", "ReplacementModel")
    objects += _object(
        "ParentHorde", "HORDE", "ParentHordeModel", payload="InfantryMember 10"
    )
    objects += """
ChildObject ChildHorde ParentHorde
  Behavior = HordeContain ModuleTag_HordeContain
    InitialPayload = ReplacementMember 4
  End
End
"""

    command_sets = b"""
CommandSet UniversalFactoryCommandSet
  1 = Command_BuildInfantry
  2 = Command_BuildCavalry
  3 = Command_BuildHero
  4 = Command_BuildSiege
  5 = Command_BuildMonster
  6 = Command_BuildNaval
  7 = Command_BuildChildHorde
End
CommandSet AlternateFactoryCommandSet
  1 = Command_BuildInfantryAlternate
End
CommandSet UpgradingFactoryCommandSet
  1 = Command_PurchaseLevel2
End
CommandSet UpgradingFactoryCommandSetLevel2
  1 = Command_BuildRanged
End
"""
    commands: list[str] = []
    for command, target in (
        ("Command_BuildInfantry", "InfantryHorde"),
        ("Command_BuildRanged", "RangedHorde"),
        ("Command_BuildCavalry", "CavalryHorde"),
        ("Command_BuildHero", "HeroUnit"),
        ("Command_BuildSiege", "SiegeUnit"),
        ("Command_BuildMonster", "MonsterUnit"),
        ("Command_BuildNaval", "NavalUnit"),
        ("Command_BuildChildHorde", "ChildHorde"),
        ("Command_BuildInfantryAlternate", "InfantryHorde"),
    ):
        image = (
            "BIInfantryAlternate" if command.endswith("Alternate") else f"BI{target}"
        )
        commands.append(
            f"""
CommandButton {command}
  Command = UNIT_BUILD
  Object = {target}
  ButtonImage = {image}
  TextLabel = CONTROLBAR:{target}
  DescriptLabel = CONTROLBAR:ToolTip{target}
End
"""
        )
    commands.append(
        """
CommandButton Command_PurchaseLevel2
  Command = PURCHASE_UPGRADE
  Upgrade = Upgrade_FactoryLevel2
End
"""
    )
    return {
        "data/ini/object/units/test_units.ini": objects.encode("utf-8"),
        "data/ini/commandset.ini": command_sets,
        "data/ini/commandbutton.ini": "".join(commands).encode("utf-8"),
        "data/ini/gamedata.ini": b"#define GOOD_HORDE_SIZE 10\n",
    }


@pytest.mark.parametrize(
    ("target", "category", "member_count"),
    (
        ("InfantryHorde", "infantry", 10),
        ("RangedHorde", "ranged-infantry", 10),
        ("CavalryHorde", "cavalry", 5),
        ("HeroUnit", "hero", 1),
        ("SiegeUnit", "siege", 1),
        ("MonsterUnit", "monster", 1),
        ("NavalUnit", "naval", 1),
    ),
)
def test_compiles_categories_without_object_specific_rules(
    target: str, category: str, member_count: int
) -> None:
    documents = _documents()
    result = compile_playable_unit_descriptor(target, documents)
    repeated = compile_playable_unit_descriptor(
        target, dict(reversed(documents.items()))
    )

    validate_playable_unit_descriptor(result)
    assert result == repeated
    assert result["category"] == category
    assert result["composition"]["members"][0]["count"] == member_count
    assert result["production"][0]["slot"] >= 1
    assert result["presentation"]["ui"]["commands"][0]["fields"]["ButtonImage"]
    assert result["presentation"]["audioRoutes"]["container"]["VoiceSelect"]
    assert result["presentation"]["audioRoutes"]["primaryMember"]["VoiceSelect"]
    assert len(result["descriptorSha256"]) == 64


def test_prepared_inputs_preserve_descriptor_identity() -> None:
    documents = _documents()
    expected = compile_playable_unit_descriptor("InfantryHorde", documents)
    prepared = prepare_playable_unit_compiler(documents)

    actual = compile_playable_unit_descriptor(
        "InfantryHorde", documents, prepared=prepared
    )

    assert actual == expected


def test_prepared_inputs_reject_a_different_document_mapping() -> None:
    documents = _documents()
    prepared = prepare_playable_unit_compiler(documents)

    with pytest.raises(PlayableUnitCompilerError, match="different document mapping"):
        compile_playable_unit_descriptor(
            "InfantryHorde", dict(documents), prepared=prepared
        )


def test_malformed_sibling_does_not_erase_valid_retail_object() -> None:
    documents = _documents()
    documents["data/ini/object/civilian/large.ini"] = b"""
Object BrokenSibling
  KindOf = STRUCTURE
Object RecoveredBase
  KindOf = STRUCTURE IMMOBILE
End
"""

    prepared = prepare_playable_unit_compiler(documents)

    assert "brokensibling" not in prepared.objects
    assert playable_object_kind_of(prepared, "RecoveredBase") == (
        "IMMOBILE",
        "STRUCTURE",
    )
    assert prepared.objects["recoveredbase"].line == 4
    assert "data/ini/object/civilian/large.ini" in prepared.object_parse_errors


def test_kind_of_additive_modifier_preserves_parent_capabilities() -> None:
    documents = _documents()
    documents["data/ini/object/civilian/inheritance.ini"] = b"""
Object StructureBase
  KindOf = STRUCTURE IMMOBILE
End
ChildObject EconomyChild StructureBase
  KindOf = +ECONOMY_STRUCTURE -IMMOBILE
End
"""

    prepared = prepare_playable_unit_compiler(documents)

    assert playable_object_kind_of(prepared, "EconomyChild") == (
        "ECONOMY_STRUCTURE",
        "STRUCTURE",
    )


def test_discovers_upgraded_command_set_prerequisite() -> None:
    result = compile_playable_unit_descriptor("RangedHorde", _documents())

    production = result["production"][0]
    assert production["producerObjectId"] == "UpgradingFactory"
    assert production["commandSetId"] == "UpgradingFactoryCommandSetLevel2"
    assert production["commandId"] == "Command_BuildRanged"
    assert production["slot"] == 1
    assert production["prerequisites"] == ["Upgrade_FactoryLevel2"]
    assert production["commandSetTransition"][0]["triggeredBy"] == [
        "Upgrade_FactoryLevel2"
    ]


def test_symbolic_count_and_module_override_are_effective_not_concatenated() -> None:
    infantry = compile_playable_unit_descriptor("InfantryHorde", _documents())
    child = compile_playable_unit_descriptor("ChildHorde", _documents())

    assert infantry["composition"]["members"][0]["objectId"] == "InfantryMember"
    assert infantry["composition"]["members"][0]["count"] == 10
    assert infantry["composition"]["members"][0]["countExpression"] == (
        "#MULTIPLY( GOOD_HORDE_SIZE 1 )"
    )
    assert [row["objectId"] for row in child["composition"]["members"]] == [
        "ReplacementMember"
    ]
    assert child["composition"]["members"][0]["count"] == 4


def test_capabilities_are_evidence_backed_and_hybrid_traits_are_compositional() -> None:
    cavalry = compile_playable_unit_descriptor("CavalryHorde", _documents())
    siege = compile_playable_unit_descriptor("SiegeUnit", _documents())
    hero = compile_playable_unit_descriptor("HeroUnit", _documents())
    naval = compile_playable_unit_descriptor("NavalUnit", _documents())

    cavalry_capabilities = {row["id"] for row in cavalry["capabilities"]}
    assert cavalry["traits"] == ["mounted", "ranged"]
    assert "ranged-attack" in cavalry_capabilities
    assert "projectile" not in {row["id"] for row in siege["capabilities"]}
    assert not any(
        row["id"].startswith("special-module:") for row in hero["capabilities"]
    )
    assert "transport" in {row["id"] for row in naval["capabilities"]}


def test_each_production_route_retains_its_own_ui() -> None:
    result = compile_playable_unit_descriptor("InfantryHorde", _documents())
    bindings = {row["commandId"]: row["ui"] for row in result["production"]}

    assert set(bindings) == {
        "Command_BuildInfantry",
        "Command_BuildInfantryAlternate",
    }
    assert bindings["Command_BuildInfantry"]["ButtonImage"] == ["BIInfantryHorde"]
    assert bindings["Command_BuildInfantryAlternate"]["ButtonImage"] == [
        "BIInfantryAlternate"
    ]


def _hero_roster_fixture() -> tuple[dict[str, bytes], dict[str, object]]:
    documents = _documents()
    objects_path = "data/ini/object/units/test_units.ini"
    objects = documents[objects_path].decode("utf-8")
    for name in ("HeroSeven", "HeroEight"):
        objects += _object(name, "HERO INFANTRY", f"{name}Model").replace(
            f"  SelectPortrait = UP{name}\n",
            f"  SelectPortrait = UP{name}\n"
            f"  ButtonImage = HI{name}\n"
            f"  DisplayName = OBJECT:{name}\n"
            f"  DescriptionStrategic = CONTROLBAR:ToolTip{name}\n",
        )
    documents[objects_path] = objects.encode("utf-8")
    documents["data/ini/playertemplate.ini"] = b"""
PlayerTemplate FactionMen
  Side = Men
  StartingBuilding = UniversalFactory
  BuildableHeroesMP = Placeholder1 Placeholder2 Placeholder3 Placeholder4 Placeholder5 Placeholder6 HeroSeven HeroEight
End
"""
    graph = {
        "target": {"playerTemplate": "FactionMen"},
        "definitions": {
            "objects": [
                {"id": value, "edges": []}
                for value in ("UniversalFactory", "HeroSeven", "HeroEight")
            ],
            "commandButtons": [],
        },
    }
    return documents, graph


def test_hero_roster_preserves_ordinals_and_uses_a_separate_surface() -> None:
    documents, graph = _hero_roster_fixture()

    seven = compile_playable_unit_descriptor(
        "HeroSeven", documents, faction_graph=graph
    )
    eight = compile_playable_unit_descriptor(
        "HeroEight", documents, faction_graph=graph
    )

    assert seven["production"][0]["surface"] == "hero-roster"
    assert seven["production"][0]["rosterOrdinal"] == 7
    assert "slot" not in seven["production"][0]
    assert eight["production"][0]["rosterOrdinal"] == 8
    assert seven["production"][0]["ui"] == {
        "ButtonImage": ["HIHeroSeven"],
        "TextLabel": ["OBJECT:HeroSeven"],
        "DescriptLabel": ["CONTROLBAR:ToolTipHeroSeven"],
    }


def test_hero_roster_preserves_duplicate_authored_slots_as_separate_routes() -> None:
    documents, graph = _hero_roster_fixture()
    documents["data/ini/playertemplate.ini"] = documents[
        "data/ini/playertemplate.ini"
    ].replace(b"HeroSeven HeroEight", b"HeroSeven HeroSeven")

    result = compile_playable_unit_descriptor(
        "HeroSeven", documents, faction_graph=graph
    )

    validate_playable_unit_descriptor(result)
    routes = result["production"]
    assert [route["rosterOrdinal"] for route in routes] == [7, 8]
    assert all(route["surface"] == "hero-roster" for route in routes)
    assert all("slot" not in route for route in routes)


def test_hero_roster_rejects_unreachable_starting_building() -> None:
    documents, graph = _hero_roster_fixture()
    graph["definitions"]["objects"] = [
        row
        for row in graph["definitions"]["objects"]
        if row["id"] != "UniversalFactory"
    ]

    with pytest.raises(PlayableUnitCompilerError, match="no reachable"):
        compile_playable_unit_descriptor("HeroSeven", documents, faction_graph=graph)


def test_hero_roster_rejects_conflicting_command_socket_route() -> None:
    documents, graph = _hero_roster_fixture()
    documents["data/ini/commandbutton.ini"] += b"""
CommandButton Command_BuildHeroSeven
  Command = HERO_BUILD
  Object = HeroSeven
  ButtonImage = HIHeroSeven
  TextLabel = OBJECT:HeroSeven
  DescriptLabel = CONTROLBAR:ToolTipHeroSeven
End
"""
    documents["data/ini/commandset.ini"] += b"""
CommandSet ConflictingHeroCommandSet
  1 = Command_BuildHeroSeven
End
"""
    objects_path = "data/ini/object/units/test_units.ini"
    documents[objects_path] += b"""
Object ConflictingHeroProducer
  CommandSet = ConflictingHeroCommandSet
  KindOf = PRELOAD SELECTABLE STRUCTURE
End
"""
    graph["definitions"]["objects"].append(
        {"id": "ConflictingHeroProducer", "edges": []}
    )

    with pytest.raises(PlayableUnitCompilerError, match="conflicting"):
        compile_playable_unit_descriptor("HeroSeven", documents, faction_graph=graph)


def test_faction_graph_audio_edges_reject_numeric_lookalikes() -> None:
    documents = _documents()
    documents["data/ini/playertemplate.ini"] = b"""
PlayerTemplate FactionMen
  Side = Men
  StartingBuilding = UniversalFactory
  BuildableHeroesMP = HeroUnit
End
"""
    object_ids = (
        "UniversalFactory",
        "AlternateFactory",
        "InfantryHorde",
        "InfantryMember",
    )
    graph = {
        "target": {"playerTemplate": "FactionMen"},
        "definitions": {
            "objects": [
                {
                    "id": identifier,
                    "edges": (
                        [
                            {
                                "field": field,
                                "targetId": f"{identifier}{suffix}",
                                "targetKind": "audio-definition",
                            }
                            for field, suffix in (
                                ("VoiceSelect", "VoiceSelect"),
                                ("VoiceMove", "VoiceMove"),
                                ("VoiceAttack", "VoiceAttack"),
                            )
                        ]
                        if identifier in {"InfantryHorde", "InfantryMember"}
                        else []
                    ),
                }
                for identifier in object_ids
            ],
            "commandButtons": [
                {
                    "id": "Command_BuildInfantry",
                    "audioRoutes": [
                        {
                            "field": "UnitSpecificSound",
                            "targetId": "InfantryVoicePurchase",
                            "tokenOrdinal": 0,
                            "resolution": "resolved",
                        },
                        {
                            "field": "UnitSpecificSound",
                            "targetId": "InfantryVoiceFormation",
                            "tokenOrdinal": 1,
                            "resolution": "resolved",
                        },
                    ],
                }
            ],
        },
    }
    result = compile_playable_unit_descriptor(
        "InfantryHorde", documents, faction_graph=graph
    )
    routes = result["presentation"]["audioRoutes"]
    assert "VoicePriority" not in routes["container"]
    assert "VoicePriority" not in routes["primaryMember"]
    assert routes["primaryMember"]["VoiceMove"][0]["id"] == ("InfantryMemberVoiceMove")
    commands = result["presentation"]["ui"]["commands"]
    purchase = next(
        row for row in commands if row["commandId"] == "Command_BuildInfantry"
    )
    assert [row["id"] for row in purchase["audioRoutes"]] == [
        "InfantryVoicePurchase",
        "InfantryVoiceFormation",
    ]


def test_special_modules_are_reported_as_unsupported_extensions() -> None:
    result = compile_playable_unit_descriptor("MonsterUnit", _documents())

    assert "RespawnUpdate" in result["specialCapabilities"]
    assert len(result["unsupportedCapabilities"]) == 1
    unsupported = result["unsupportedCapabilities"][0]
    assert unsupported["id"] == "module:container:RespawnUpdate:ModuleTag_Respawn"
    assert unsupported["reason"] == (
        "authored Behavior is not consumed by the shared runtime adapter"
    )
    assert len(unsupported["semanticSha256"]) == 64


def test_target_command_set_upgrade_is_not_falsely_consumed() -> None:
    documents = _documents()
    objects = documents["data/ini/object/units/test_units.ini"].decode("utf-8")
    objects = objects.replace(
        "Object HeroUnit\n",
        "Object HeroUnit\n"
        "  Behavior = CommandSetUpgrade ModuleTag_HeroLevel\n"
        "    TriggeredBy = Upgrade_HeroLevel2\n"
        "    CommandSet = HeroLevel2CommandSet\n"
        "  End\n",
    )
    documents["data/ini/object/units/test_units.ini"] = objects.encode("utf-8")

    result = compile_playable_unit_descriptor("HeroUnit", documents)

    assert "CommandSetUpgrade" in result["specialCapabilities"]
    evidence = next(
        row
        for row in result["runtimeModuleEvidence"]
        if row["kind"] == "CommandSetUpgrade"
    )
    assert evidence["ownerRole"] == "container"
    assert evidence["consumed"] is False


def test_only_payload_contributing_horde_module_is_consumed() -> None:
    documents = _documents()
    objects = documents["data/ini/object/units/test_units.ini"].decode("utf-8")
    objects = objects.replace(
        "Object InfantryHorde\n",
        "Object InfantryHorde\n"
        "  Behavior = HordeContain ModuleTag_UnusedContain\n"
        "    Slots = 999\n"
        "  End\n",
    )
    documents["data/ini/object/units/test_units.ini"] = objects.encode("utf-8")

    result = compile_playable_unit_descriptor("InfantryHorde", documents)
    evidence = {
        row["instanceTag"]: row["consumed"]
        for row in result["runtimeModuleEvidence"]
        if row["kind"] == "HordeContain"
    }

    assert evidence == {
        "ModuleTag_HordeContain": True,
        "ModuleTag_UnusedContain": False,
    }
    assert any(
        "ModuleTag_UnusedContain" in row["id"]
        for row in result["unsupportedCapabilities"]
    )


def test_traversed_behavior_semantics_change_identity() -> None:
    documents = _documents()
    baseline = compile_playable_unit_descriptor("MonsterUnit", documents)
    documents["data/ini/object/units/test_units.ini"] = documents[
        "data/ini/object/units/test_units.ini"
    ].replace(b"DeathAnim = DYING", b"DeathAnim = DEAD")
    changed = compile_playable_unit_descriptor("MonsterUnit", documents)

    assert changed["descriptorSha256"] != baseline["descriptorSha256"]
    assert (
        changed["runtimeModuleEvidence"][0]["semanticSha256"]
        != (baseline["runtimeModuleEvidence"][0]["semanticSha256"])
    )


def test_unrelated_source_does_not_invalidate_descriptor() -> None:
    documents = _documents()
    baseline = compile_playable_unit_descriptor("HeroUnit", documents)
    documents["data/ini/object/maps/irrelevant.ini"] = _object(
        "IrrelevantObject", "INFANTRY", "IrrelevantModel"
    ).encode("utf-8")
    documents["data/ini/commandbutton.ini"] += b"""
CommandButton Command_UnrelatedOtherFaction
  Command = UNIT_BUILD
  Object = IrrelevantObject
  ButtonImage = BIUnrelated
End
"""
    documents["data/ini/commandset.ini"] += b"""
CommandSet UnrelatedOtherFactionCommandSet
  1 = Command_UnrelatedOtherFaction
End
"""

    assert compile_playable_unit_descriptor("HeroUnit", documents) == baseline


def test_producer_command_set_upgrade_uses_effective_module_override() -> None:
    documents = _documents()
    documents["data/ini/object/units/test_units.ini"] += b"""
Object ParentOverrideFactory
  CommandSet = UpgradingFactoryCommandSet
  KindOf = PRELOAD SELECTABLE STRUCTURE
  Behavior = CommandSetUpgrade ModuleTag_Level2
    TriggeredBy = Upgrade_ParentLevel2
    CommandSet = UpgradingFactoryCommandSetLevel2
  End
End
ChildObject ChildOverrideFactory ParentOverrideFactory
  Behavior = CommandSetUpgrade ModuleTag_Level2
    TriggeredBy = Upgrade_ChildLevel2
    CommandSet = UpgradingFactoryCommandSetLevel2
  End
End
"""

    result = compile_playable_unit_descriptor("RangedHorde", documents)
    child = next(
        row
        for row in result["production"]
        if row["producerObjectId"] == "ChildOverrideFactory"
    )
    assert child["prerequisites"] == ["Upgrade_ChildLevel2"]
    assert [row["triggeredBy"] for row in child["commandSetTransition"]] == [
        ["Upgrade_ChildLevel2"]
    ]


def test_rejects_unreachable_unit_instead_of_inventing_producer() -> None:
    documents = _documents()
    documents["data/ini/object/units/test_units.ini"] += _object(
        "UnreachableUnit", "INFANTRY", "UnreachableModel"
    ).encode("utf-8")

    with pytest.raises(
        PlayableUnitCompilerError,
        match="not targeted by an authored UNIT_BUILD command",
    ):
        compile_playable_unit_descriptor("UnreachableUnit", documents)


def test_validation_rejects_descriptor_mutation() -> None:
    result = compile_playable_unit_descriptor("HeroUnit", _documents())
    corrupted = deepcopy(result)
    corrupted["category"] = "monster"

    with pytest.raises(PlayableUnitCompilerError, match="digest"):
        validate_playable_unit_descriptor(corrupted)


def test_validation_rejects_rehashed_structural_corruption() -> None:
    corrupted = compile_playable_unit_descriptor("HeroUnit", _documents())
    corrupted["production"] = [{"nonsense": True}]
    unsigned = dict(corrupted)
    unsigned.pop("descriptorSha256")
    corrupted["descriptorSha256"] = hashlib.sha256(
        json.dumps(
            unsigned,
            sort_keys=True,
            separators=(",", ":"),
            ensure_ascii=False,
            allow_nan=False,
        ).encode("utf-8")
    ).hexdigest()

    with pytest.raises(PlayableUnitCompilerError, match="production producerObjectId"):
        validate_playable_unit_descriptor(corrupted)


@pytest.mark.parametrize(
    ("mutation", "message"),
    (
        (lambda row: row.pop("gameplay"), "gameplay contract"),
        (lambda row: row["presentation"].update({"ui": {}}), "UI bindings"),
        (
            lambda row: row["presentation"].update({"audioRoutes": {}}),
            "container audio routes",
        ),
    ),
)
def test_validation_rejects_other_rehashed_missing_subtrees(
    mutation, message: str
) -> None:
    corrupted = compile_playable_unit_descriptor("HeroUnit", _documents())
    mutation(corrupted)
    unsigned = dict(corrupted)
    unsigned.pop("descriptorSha256")
    corrupted["descriptorSha256"] = hashlib.sha256(
        json.dumps(
            unsigned,
            sort_keys=True,
            separators=(",", ":"),
            ensure_ascii=False,
            allow_nan=False,
        ).encode("utf-8")
    ).hexdigest()

    with pytest.raises(PlayableUnitCompilerError, match=message):
        validate_playable_unit_descriptor(corrupted)


def test_validation_rejects_rehashed_malformed_nested_reference() -> None:
    corrupted = compile_playable_unit_descriptor("CavalryHorde", _documents())
    corrupted["gameplay"]["references"] = {"weapon": "not-a-list"}
    unsigned = dict(corrupted)
    unsigned.pop("descriptorSha256")
    corrupted["descriptorSha256"] = hashlib.sha256(
        json.dumps(
            unsigned,
            sort_keys=True,
            separators=(",", ":"),
            ensure_ascii=False,
            allow_nan=False,
        ).encode("utf-8")
    ).hexdigest()

    with pytest.raises(PlayableUnitCompilerError, match="reference collection"):
        validate_playable_unit_descriptor(corrupted)


def test_validation_cross_checks_module_evidence_fields() -> None:
    corrupted = compile_playable_unit_descriptor("MonsterUnit", _documents())
    corrupted["runtimeModules"] = ["FakeModule"]
    corrupted["unsupportedCapabilities"][0]["id"] = "module:fake"
    corrupted["unsupportedCapabilities"][0]["semanticSha256"] = "0" * 64
    unsigned = dict(corrupted)
    unsigned.pop("descriptorSha256")
    corrupted["descriptorSha256"] = hashlib.sha256(
        json.dumps(
            unsigned,
            sort_keys=True,
            separators=(",", ":"),
            ensure_ascii=False,
            allow_nan=False,
        ).encode("utf-8")
    ).hexdigest()

    with pytest.raises(PlayableUnitCompilerError, match="runtime modules disagree"):
        validate_playable_unit_descriptor(corrupted)


def test_descriptor_preserves_exact_mapped_image_crop_contract() -> None:
    image = {
        "id": "BIHeroUnit",
        "texture": "FixtureAtlas.tga",
        "textureWidth": 256,
        "textureHeight": 128,
        "coords": {"left": 16, "top": 32, "right": 80, "bottom": 96},
        "compiledTextureVirtualPath": "art/compiledtextures/fi/fixtureatlas.dds",
    }
    descriptor = compile_playable_unit_descriptor(
        "HeroUnit", _documents(), resolved_images={"BIHeroUnit": image}
    )
    validate_playable_unit_descriptor(descriptor)
    assert descriptor["presentation"]["resolvedImages"]["BIHeroUnit"] == image


def test_validator_rejects_mapped_image_crop_outside_atlas() -> None:
    image = {
        "id": "BIHeroUnit",
        "texture": "FixtureAtlas.tga",
        "textureWidth": 32,
        "textureHeight": 32,
        "coords": {"left": 0, "top": 0, "right": 64, "bottom": 32},
        "compiledTextureVirtualPath": "art/compiledtextures/fi/fixtureatlas.dds",
    }
    descriptor = compile_playable_unit_descriptor("HeroUnit", _documents())
    descriptor["presentation"]["resolvedImages"] = {"BIHeroUnit": image}
    unsigned = dict(descriptor)
    unsigned.pop("descriptorSha256")
    descriptor["descriptorSha256"] = hashlib.sha256(
        json.dumps(
            unsigned,
            sort_keys=True,
            separators=(",", ":"),
            ensure_ascii=False,
            allow_nan=False,
        ).encode("utf-8")
    ).hexdigest()
    with pytest.raises(PlayableUnitCompilerError, match="mapped image crop"):
        validate_playable_unit_descriptor(descriptor)


def _ring_hero_fixture() -> tuple[dict[str, bytes], dict[str, object]]:
    documents, graph = _hero_roster_fixture()
    objects_path = "data/ini/object/units/test_units.ini"
    objects = documents[objects_path].decode("utf-8")
    objects += _object("HeroRing", "HERO INFANTRY", "HeroRingModel").replace(
        "  SelectPortrait = UPHeroRing\n",
        "  SelectPortrait = UPHeroRing\n"
        "  ButtonImage = HIHeroRing\n"
        "  DisplayName = OBJECT:HeroRing\n"
        "  DescriptionStrategic = CONTROLBAR:ToolTipHeroRing\n",
    )
    documents[objects_path] = objects.encode("utf-8")
    documents["data/ini/commandbutton.ini"] += b"""
CommandButton Command_RingHeroReviveSlot
  Command = REVIVE
  TextLabel = CONTROLBAR:GenericReviveHero
  DescriptLabel = CONTROLBAR:ToolTipGenericReviveHero
End
"""
    documents["data/ini/playertemplate.ini"] = documents[
        "data/ini/playertemplate.ini"
    ].replace(b"End", b"  BuildableRingHeroesMP = HeroRing\nEnd")
    graph["definitions"]["objects"].append({"id": "HeroRing", "edges": []})
    return documents, graph


def test_ring_hero_uses_the_engine_ring_roster_route() -> None:
    documents, graph = _ring_hero_fixture()

    result = compile_playable_unit_descriptor("HeroRing", documents, faction_graph=graph)

    validate_playable_unit_descriptor(result)
    assert len(result["production"]) == 1
    route = result["production"][0]
    assert route["surface"] == "hero-roster"
    assert route["commandSetId"] == "__engine__/BuildableRingHeroesMP"
    assert route["commandId"] == "__engine__/RING_HERO_BUILD/HeroRing"
    assert route["sourceField"] == "BuildableRingHeroesMP"
    assert route["producerObjectId"] == "UniversalFactory"
    # The ring slot follows the eight authored hero roster slots.
    assert route["rosterOrdinal"] == 9
    assert "slot" not in route
    assert route["ui"] == {
        "ButtonImage": ["HIHeroRing"],
        "TextLabel": ["CONTROLBAR:GenericReviveHero"],
        "DescriptLabel": ["CONTROLBAR:ToolTipGenericReviveHero"],
    }


def test_ring_hero_rejects_duplicate_ring_roster_entries() -> None:
    documents, graph = _ring_hero_fixture()
    documents["data/ini/playertemplate.ini"] = documents[
        "data/ini/playertemplate.ini"
    ].replace(
        b"BuildableRingHeroesMP = HeroRing", b"BuildableRingHeroesMP = HeroRing HeroRing"
    )

    with pytest.raises(
        PlayableUnitCompilerError, match="duplicate BuildableRingHeroesMP"
    ):
        compile_playable_unit_descriptor("HeroRing", documents, faction_graph=graph)


def test_ring_hero_rejects_conflicting_command_socket_route() -> None:
    documents, graph = _ring_hero_fixture()
    documents["data/ini/commandbutton.ini"] += b"""
CommandButton Command_BuildHeroRing
  Command = HERO_BUILD
  Object = HeroRing
End
"""
    documents["data/ini/commandset.ini"] += b"""
CommandSet ConflictingRingCommandSet
  1 = Command_BuildHeroRing
End
"""
    objects_path = "data/ini/object/units/test_units.ini"
    documents[objects_path] += b"""
Object ConflictingRingProducer
  CommandSet = ConflictingRingCommandSet
  KindOf = PRELOAD SELECTABLE STRUCTURE
End
"""
    graph["definitions"]["objects"].append(
        {"id": "ConflictingRingProducer", "edges": []}
    )

    with pytest.raises(PlayableUnitCompilerError, match="conflicting"):
        compile_playable_unit_descriptor("HeroRing", documents, faction_graph=graph)


def test_member_resolves_through_an_inherited_initial_payload() -> None:
    documents = _documents()
    objects_path = "data/ini/object/units/test_units.ini"
    objects = documents[objects_path].decode("utf-8")
    objects += _object("LegacyPayloadMember", "INFANTRY", "LegacyPayloadModel")
    objects += """
Object LegacyPayloadBase
  KindOf = PRELOAD SELECTABLE HORDE
  Behavior = HordeContain ModuleTag_HordeContain
    InitialPayload = LegacyPayloadMember 3
  End
End

ChildObject LegacyPayloadHorde LegacyPayloadBase
End
"""
    documents[objects_path] = objects.encode("utf-8")
    documents["data/ini/commandbutton.ini"] += b"""
CommandButton Command_BuildLegacyPayloadHorde
  Command = UNIT_BUILD
  Object = LegacyPayloadHorde
  ButtonImage = BILegacyPayloadHorde
End
"""
    documents["data/ini/commandset.ini"] += b"""
CommandSet UniversalFactoryLegacyCommandSet
  1 = Command_BuildLegacyPayloadHorde
End
"""
    documents[objects_path] = documents[objects_path].replace(
        b"Object UniversalFactory\n  CommandSet = UniversalFactoryCommandSet",
        b"Object UniversalFactory\n  CommandSet = UniversalFactoryLegacyCommandSet",
    )

    result = compile_playable_unit_descriptor("LegacyPayloadMember", documents)

    validate_playable_unit_descriptor(result)
    assert result["objectId"] == "LegacyPayloadHorde"
    assert result["requestedObjectId"] == "LegacyPayloadMember"
    assert result["composition"]["containerObjectId"] == "LegacyPayloadHorde"
    assert result["composition"]["primaryMemberObjectId"] == "LegacyPayloadMember"
    assert result["production"][0]["commandId"] == "Command_BuildLegacyPayloadHorde"


def test_ring_hero_requires_the_authored_revive_slot_button() -> None:
    documents, graph = _ring_hero_fixture()
    documents["data/ini/commandbutton.ini"] = documents[
        "data/ini/commandbutton.ini"
    ].replace(b"CommandButton Command_RingHeroReviveSlot", b"CommandButton Command_RenamedReviveSlot")

    with pytest.raises(
        PlayableUnitCompilerError, match="Command_RingHeroReviveSlot"
    ):
        compile_playable_unit_descriptor("HeroRing", documents, faction_graph=graph)


def _combat_documents(
    object_rows: str,
    weapon_ini: str,
    command_rows: str,
    button_rows: str,
    *,
    defines: str = "",
) -> dict[str, bytes]:
    documents = _documents()
    units_path = "data/ini/object/units/test_units.ini"
    documents[units_path] = (
        documents[units_path].decode() + object_rows
    ).encode()
    command_sets = documents["data/ini/commandset.ini"].decode()
    marker = "  7 = Command_BuildChildHorde\nEnd"
    documents["data/ini/commandset.ini"] = command_sets.replace(
        marker, "  7 = Command_BuildChildHorde" + command_rows + "\nEnd", 1
    ).encode()
    buttons_path = "data/ini/commandbutton.ini"
    documents[buttons_path] = (
        documents[buttons_path].decode() + button_rows
    ).encode()
    documents["data/ini/weapon.ini"] = weapon_ini.encode()
    documents["data/ini/gamedata.ini"] = (
        documents["data/ini/gamedata.ini"].decode() + defines
    ).encode()
    return documents


def _combat_object(name: str, kind_of: str, weapon_set: str) -> str:
    return (
        f"\nObject {name}\n"
        f"  KindOf = PRELOAD SELECTABLE {kind_of}\n"
        "  BuildCost = 500\n"
        "  BuildTime = 30\n"
        "  CommandPoints = 20\n"
        "  VisionRange = 300\n"
        f"  SelectPortrait = UP{name}\n"
        f"  VoiceSelect = {name}VoiceSelect\n"
        "  VoicePriority = 43\n"
        f"  VoiceMove = {name}VoiceMove\n"
        f"  VoiceAttack = {name}VoiceAttack\n"
        "  Draw = W3DScriptedModelDraw ModuleTag_Draw\n"
        "    DefaultModelConditionState\n"
        f"      Model = {name}Model\n"
        "    End\n"
        "  End\n"
        f"{weapon_set}"
        "End\n"
    )


def _combat_command(name: str, slot: int, target: str) -> tuple[str, str]:
    return (
        f"\n  {slot} = Command_Build{name}",
        f"\nCommandButton Command_Build{name}\n"
        "  Command = UNIT_BUILD\n"
        f"  Object = {target}\n"
        f"  ButtonImage = BI{target}\n"
        f"  TextLabel = CONTROLBAR:{target}\n"
        f"  DescriptLabel = CONTROLBAR:ToolTip{target}\n"
        "End\n",
    )


def _combat_hero_documents(
    weapon_set: str, weapon_ini: str, *, defines: str = ""
) -> dict[str, bytes]:
    command_row, button_row = _combat_command("CombatHero", 8, "CombatHero")
    return _combat_documents(
        _combat_object("CombatHero", "HERO INFANTRY", weapon_set),
        weapon_ini,
        command_row,
        button_row,
        defines=defines,
    )


def test_hero_multi_nugget_damage_resolves_base_total() -> None:
    documents = _combat_hero_documents(
        "  WeaponSet\n    Conditions = None\n    Weapon = PRIMARY HeroNuggetSword\n  End\n",
        "Weapon HeroNuggetSword\n"
        "  MeleeWeapon = Yes\n"
        "  AttackRange = 20.0\n"
        "  DelayBetweenShots = 1000\n"
        "  PreAttackDelay = 500\n"
        "  FiringDuration = 800\n"
        "  DamageNugget\n"
        "    Damage = HERO_SWORD_DAMAGE\n"
        "    DamageType = HERO\n"
        "  End\n"
        "  DamageNugget\n"
        "    Damage = 20\n"
        "    DamageType = SLASH\n"
        "  End\n"
        "End\n",
        defines="#define HERO_SWORD_DAMAGE 180\n",
    )

    descriptor = compile_playable_unit_descriptor("CombatHero", documents)

    validate_playable_unit_descriptor(descriptor)
    simulation = descriptor["gameplay"]["simulation"]
    combat = simulation["resolved"]["combat"]
    assert combat["weaponId"] == "HeroNuggetSword"
    damage = combat["damage"]
    assert damage["value"] == 200
    assert [row["value"] for row in damage["components"]] == [180, 20]
    assert [row["damageType"] for row in damage["components"]] == ["HERO", "SLASH"]
    assert damage["components"][0]["constantSourceIni"] == "data/ini/gamedata.ini"
    assert "combat.damage" not in simulation["missing"]


def test_hero_flat_damage_wins_over_nugget_aggregation() -> None:
    documents = _combat_hero_documents(
        "  WeaponSet\n    Conditions = None\n    Weapon = PRIMARY HeroNuggetSword\n  End\n",
        "Weapon HeroNuggetSword\n"
        "  MeleeWeapon = Yes\n"
        "  AttackRange = 20.0\n"
        "  DelayBetweenShots = 1000\n"
        "  PreAttackDelay = 500\n"
        "  FiringDuration = 800\n"
        "  DamageNugget\n"
        "    Damage = HERO_SWORD_DAMAGE\n"
        "    DamageType = HERO\n"
        "  End\n"
        "  DamageNugget\n"
        "    Damage = #MULTIPLY( HERO_SWORD_DAMAGE 0.25 )\n"
        "    DamageType = HERO\n"
        "  End\n"
        "End\n",
        defines="#define HERO_SWORD_DAMAGE 250\n",
    )

    descriptor = compile_playable_unit_descriptor("CombatHero", documents)

    validate_playable_unit_descriptor(descriptor)
    damage = descriptor["gameplay"]["simulation"]["resolved"]["combat"]["damage"]
    assert damage["value"] == 250
    assert "components" not in damage


def test_hero_multiplicative_cadence_fields_resolve() -> None:
    documents = _combat_hero_documents(
        "  WeaponSet\n    Conditions = None\n    Weapon = PRIMARY HeroNuggetSword\n  End\n",
        "Weapon HeroNuggetSword\n"
        "  MeleeWeapon = Yes\n"
        "  AttackRange = 20.0\n"
        "  DelayBetweenShots = #MULTIPLY( HERO_DELAY 0.8 )\n"
        "  PreAttackDelay = #MULTIPLY( HERO_PREATTACK 0.1 )\n"
        "  FiringDuration = 1000\n"
        "  DamageNugget\n"
        "    Damage = 150\n"
        "    DamageType = HERO\n"
        "  End\n"
        "End\n",
        defines="#define HERO_DELAY 1000\n#define HERO_PREATTACK 500\n",
    )

    descriptor = compile_playable_unit_descriptor("CombatHero", documents)

    validate_playable_unit_descriptor(descriptor)
    simulation = descriptor["gameplay"]["simulation"]
    combat = simulation["resolved"]["combat"]
    assert combat["delayBetweenShotsMs"]["value"] == 800
    assert combat["preAttackDelayMs"]["value"] == 50
    assert combat["preAttackDelayMs"]["expression"] == "#MULTIPLY( HERO_PREATTACK 0.1 )"
    assert "combat.delayBetweenShotsMs" not in simulation["missing"]
    assert "combat.preAttackDelayMs" not in simulation["missing"]


def test_hero_multiplicative_unknown_constant_fails_closed() -> None:
    documents = _combat_hero_documents(
        "  WeaponSet\n    Conditions = None\n    Weapon = PRIMARY HeroNuggetSword\n  End\n",
        "Weapon HeroNuggetSword\n"
        "  MeleeWeapon = Yes\n"
        "  AttackRange = 20.0\n"
        "  DelayBetweenShots = #MULTIPLY( UNDEFINED_HERO_DELAY 0.8 )\n"
        "  PreAttackDelay = 500\n"
        "  FiringDuration = 1000\n"
        "  DamageNugget\n"
        "    Damage = 150\n"
        "    DamageType = HERO\n"
        "  End\n"
        "End\n",
    )

    descriptor = compile_playable_unit_descriptor("CombatHero", documents)

    validate_playable_unit_descriptor(descriptor)
    simulation = descriptor["gameplay"]["simulation"]
    assert "delayBetweenShotsMs" not in simulation["resolved"]["combat"]
    assert "combat.delayBetweenShotsMs" in simulation["missing"]


def test_hero_secondary_only_weapon_set_resolves_standard_weapon() -> None:
    documents = _combat_hero_documents(
        "  WeaponSet\n"
        "    Weapon = SECONDARY HeroSwoopWeapon\n"
        "    Weapon = TERTIARY HeroClawWeapon\n"
        "    OnlyAgainst = TERTIARY MONSTER\n"
        "  End\n",
        "Weapon HeroSwoopWeapon\n"
        "  MeleeWeapon = No\n"
        "  AttackRange = 24.0\n"
        "  DelayBetweenShots = 4000\n"
        "  PreAttackDelay = 100\n"
        "  FiringDuration = 4500\n"
        "  DamageNugget\n"
        "    Damage = 250\n"
        "    DamageType = SIEGE\n"
        "  End\n"
        "End\n"
        "Weapon HeroClawWeapon\n"
        "  MeleeWeapon = Yes\n"
        "  AttackRange = 20.0\n"
        "  DelayBetweenShots = 1000\n"
        "  PreAttackDelay = 500\n"
        "  FiringDuration = 800\n"
        "  DamageNugget\n"
        "    Damage = 300\n"
        "    DamageType = HERO\n"
        "  End\n"
        "End\n",
    )

    descriptor = compile_playable_unit_descriptor("CombatHero", documents)

    validate_playable_unit_descriptor(descriptor)
    combat = descriptor["gameplay"]["simulation"]["resolved"]["combat"]
    assert combat["weaponId"] == "HeroSwoopWeapon"
    assert combat["damage"]["value"] == 250


def test_hero_absent_preattack_delay_records_engine_default() -> None:
    documents = _combat_hero_documents(
        "  WeaponSet\n    Conditions = None\n    Weapon = PRIMARY HeroNuggetSword\n  End\n",
        "Weapon HeroNuggetSword\n"
        "  MeleeWeapon = No\n"
        "  AttackRange = 24.0\n"
        "  DelayBetweenShots = 4000\n"
        "  FiringDuration = 4500\n"
        "  DamageNugget\n"
        "    Damage = 250\n"
        "    DamageType = SIEGE\n"
        "  End\n"
        "End\n",
    )

    descriptor = compile_playable_unit_descriptor("CombatHero", documents)

    validate_playable_unit_descriptor(descriptor)
    simulation = descriptor["gameplay"]["simulation"]
    pre_attack = simulation["resolved"]["combat"]["preAttackDelayMs"]
    assert pre_attack["value"] == 0
    assert "engine default" in pre_attack["semantic"]
    assert "combat.preAttackDelayMs" not in simulation["missing"]


def test_hero_unresolvable_preattack_delay_is_not_defaulted() -> None:
    documents = _combat_hero_documents(
        "  WeaponSet\n    Conditions = None\n    Weapon = PRIMARY HeroNuggetSword\n  End\n",
        "Weapon HeroNuggetSword\n"
        "  MeleeWeapon = No\n"
        "  AttackRange = 24.0\n"
        "  DelayBetweenShots = 4000\n"
        "  PreAttackDelay = UNDEFINED_HERO_PREATTACK\n"
        "  FiringDuration = 4500\n"
        "  DamageNugget\n"
        "    Damage = 250\n"
        "    DamageType = SIEGE\n"
        "  End\n"
        "End\n",
    )

    descriptor = compile_playable_unit_descriptor("CombatHero", documents)

    validate_playable_unit_descriptor(descriptor)
    simulation = descriptor["gameplay"]["simulation"]
    assert "preAttackDelayMs" not in simulation["resolved"]["combat"]
    assert "combat.preAttackDelayMs" in simulation["missing"]


def test_hero_upgrade_locked_nugget_is_excluded_from_base_total() -> None:
    documents = _combat_hero_documents(
        "  WeaponSet\n    Conditions = None\n    Weapon = PRIMARY HeroNuggetSword\n  End\n",
        "Weapon HeroNuggetSword\n"
        "  MeleeWeapon = Yes\n"
        "  AttackRange = 20.0\n"
        "  DelayBetweenShots = 1000\n"
        "  PreAttackDelay = 500\n"
        "  FiringDuration = 800\n"
        "  DamageNugget\n"
        "    Damage = 100\n"
        "    DamageType = HERO\n"
        "    ForbiddenUpgradeNames = Upgrade_CombatHeroForgedBlades\n"
        "  End\n"
        "  DamageNugget\n"
        "    Damage = 300\n"
        "    DamageType = HERO\n"
        "    RequiredUpgradeNames = Upgrade_CombatHeroForgedBlades\n"
        "  End\n"
        "End\n",
    )

    descriptor = compile_playable_unit_descriptor("CombatHero", documents)

    validate_playable_unit_descriptor(descriptor)
    damage = descriptor["gameplay"]["simulation"]["resolved"]["combat"]["damage"]
    assert damage["value"] == 100
    assert [row["value"] for row in damage["components"]] == [100]
    assert [row["reason"] for row in damage["excludedNuggets"]] == [
        "required-upgrade"
    ]


def test_hero_unresolvable_nugget_component_fails_closed() -> None:
    documents = _combat_hero_documents(
        "  WeaponSet\n    Conditions = None\n    Weapon = PRIMARY HeroNuggetSword\n  End\n",
        "Weapon HeroNuggetSword\n"
        "  MeleeWeapon = Yes\n"
        "  AttackRange = 20.0\n"
        "  DelayBetweenShots = 1000\n"
        "  PreAttackDelay = 500\n"
        "  FiringDuration = 800\n"
        "  DamageNugget\n"
        "    Damage = 100\n"
        "    DamageType = HERO\n"
        "  End\n"
        "  DamageNugget\n"
        "    Damage = 200\n"
        "    DamageType = SLASH\n"
        "  End\n"
        "  DamageNugget\n"
        "    Damage = UNDEFINED_HERO_DAMAGE\n"
        "    DamageType = MAGIC\n"
        "  End\n"
        "End\n",
    )

    descriptor = compile_playable_unit_descriptor("CombatHero", documents)

    validate_playable_unit_descriptor(descriptor)
    simulation = descriptor["gameplay"]["simulation"]
    assert "damage" not in simulation["resolved"]["combat"]
    assert "combat.damage" in simulation["missing"]


def test_hero_filtered_nugget_is_excluded_from_base_total() -> None:
    documents = _combat_hero_documents(
        "  WeaponSet\n    Conditions = None\n    Weapon = PRIMARY HeroNuggetSword\n  End\n",
        "Weapon HeroNuggetSword\n"
        "  MeleeWeapon = Yes\n"
        "  AttackRange = 20.0\n"
        "  DelayBetweenShots = 1000\n"
        "  PreAttackDelay = 500\n"
        "  FiringDuration = 800\n"
        "  DamageNugget\n"
        "    Damage = 100\n"
        "    DamageType = HERO\n"
        "  End\n"
        "  DamageNugget\n"
        "    SpecialObjectFilter = NONE +STRUCTURE\n"
        "    Damage = 40\n"
        "    DamageType = SIEGE\n"
        "  End\n"
        "End\n",
    )

    descriptor = compile_playable_unit_descriptor("CombatHero", documents)

    validate_playable_unit_descriptor(descriptor)
    damage = descriptor["gameplay"]["simulation"]["resolved"]["combat"]["damage"]
    assert damage["value"] == 100
    assert [row["value"] for row in damage["components"]] == [100]
    assert [row["reason"] for row in damage["excludedNuggets"]] == [
        "special-object-filter"
    ]


def test_non_hero_multi_nugget_damage_resolves_base_total() -> None:
    command_row, button_row = _combat_command("CombatInfantry", 9, "CombatInfantry")
    documents = _combat_documents(
        _combat_object(
            "CombatInfantry",
            "INFANTRY",
            "  WeaponSet\n    Conditions = None\n    Weapon = PRIMARY TroopNuggetSword\n  End\n",
        ),
        "Weapon TroopNuggetSword\n"
        "  MeleeWeapon = Yes\n"
        "  AttackRange = 20.0\n"
        "  DelayBetweenShots = 1000\n"
        "  PreAttackDelay = 500\n"
        "  FiringDuration = 800\n"
        "  DamageNugget\n"
        "    Damage = 80\n"
        "    DamageType = SLASH\n"
        "    ForbiddenUpgradeNames = Upgrade_CombatInfantryForgedBlades\n"
        "  End\n"
        "  DamageNugget\n"
        "    Damage = 120\n"
        "    DamageType = SLASH\n"
        "    RequiredUpgradeNames = Upgrade_CombatInfantryForgedBlades\n"
        "  End\n"
        "End\n",
        command_row,
        button_row,
    )

    descriptor = compile_playable_unit_descriptor("CombatInfantry", documents)

    validate_playable_unit_descriptor(descriptor)
    assert descriptor["category"] == "infantry"
    simulation = descriptor["gameplay"]["simulation"]
    damage = simulation["resolved"]["combat"]["damage"]
    assert damage["value"] == 80
    assert [row["value"] for row in damage["components"]] == [80]
    assert [row["reason"] for row in damage["excludedNuggets"]] == [
        "required-upgrade"
    ]
    assert "combat.damage" not in simulation["missing"]


def _hero_ability_documents() -> dict[str, bytes]:
    """Synthetic hero with one authored SPECIAL_POWER ability per effect kind."""

    command_row, button_row = _combat_command("AbilityHero", 8, "AbilityHero")
    hero_object = (
        "\nObject AbilityHero\n"
        "  KindOf = PRELOAD SELECTABLE HERO INFANTRY\n"
        "  BuildCost = 1000\n"
        "  BuildTime = 45\n"
        "  CommandPoints = 50\n"
        "  VisionRange = 300\n"
        "  SelectPortrait = HPAbilityHero\n"
        "  ButtonImage = HIAbilityHero\n"
        "  DisplayName = OBJECT:AbilityHero\n"
        "  DescriptionStrategic = CONTROLBAR:ToolTipAbilityHero\n"
        "  CommandSet = AbilityHeroCommandSet\n"
        "  Draw = W3DScriptedModelDraw ModuleTag_Draw\n"
        "    DefaultModelConditionState\n"
        "      Model = AbilityHeroModel\n"
        "    End\n"
        "  End\n"
        "  WeaponSet\n"
        "    Conditions = None\n"
        "    Weapon = PRIMARY AbilityHeroSword\n"
        "  End\n"
        "  Behavior = UnpauseSpecialPowerUpgrade ModuleTag_BlastEnabler\n"
        "    SpecialPowerTemplate = SpecialAbilityFixtureBlast\n"
        "    TriggeredBy = Upgrade_FixtureBlast\n"
        "  End\n"
        "  Behavior = SpecialPowerModule ModuleTag_BlastStarter\n"
        "    SpecialPowerTemplate = SpecialAbilityFixtureBlast\n"
        "    UpdateModuleStartsAttack = Yes\n"
        "    StartsPaused = Yes\n"
        "  End\n"
        "  Behavior = WeaponFireSpecialAbilityUpdate ModuleTag_BlastUpdate\n"
        "    SpecialPowerTemplate = SpecialAbilityFixtureBlast\n"
        "    SpecialWeapon = FixtureHeroBlast\n"
        "    StartAbilityRange = 80.0\n"
        "    WhichSpecialWeapon = 1\n"
        "  End\n"
        "  Behavior = PlayerHealSpecialPower ModuleTag_HealStarter\n"
        "    SpecialPowerTemplate = SpecialAbilityFixtureHeal\n"
        "    HealAmount = 0.25\n"
        "    HealRadius = 120\n"
        "    HealFX = FX_FixtureHeal\n"
        "  End\n"
        "  Behavior = OCLSpecialPower ModuleTag_SummonStarter\n"
        "    SpecialPowerTemplate = SpecialAbilityFixtureSummon\n"
        "    OCL = OCL_FixtureSummon\n"
        "    CreateLocation = CREATE_AT_LOCATION\n"
        "  End\n"
        "  Behavior = HeroModeSpecialAbilityUpdate ModuleTag_RageUpdate\n"
        "    SpecialPowerTemplate = SpecialAbilityFixtureRage\n"
        "    HeroAttributeModifier = FixtureRage\n"
        "    HeroEffectDuration = 20000\n"
        "  End\n"
        "  Behavior = SpecialPowerModule ModuleTag_MountStarter\n"
        "    SpecialPowerTemplate = SpecialAbilityToggleMounted\n"
        "    UpdateModuleStartsAttack = Yes\n"
        "  End\n"
        "  Behavior = ToggleMountedSpecialAbilityUpdate ModuleTag_MountToggle\n"
        "    SpecialPowerTemplate = SpecialAbilityToggleMounted\n"
        "    UnpackTime = 1000\n"
        "  End\n"
        "  Behavior = WeaponFireSpecialAbilityUpdate ModuleTag_BrokenUpdate\n"
        "    SpecialPowerTemplate = SpecialAbilityFixtureBroken\n"
        "    SpecialWeapon = MissingWeapon\n"
        "  End\n"
        "  Behavior = SpecialPowerModule ModuleTag_LeadershipDisplay\n"
        "    SpecialPowerTemplate = SpecialAbilityFakeLeadership\n"
        "    StartsPaused = No\n"
        "  End\n"
        "  Behavior = SpecialPowerModule ModuleTag_GraceStarter\n"
        "    SpecialPowerTemplate = SpecialAbilityFixtureGrace\n"
        "    UpdateModuleStartsAttack = Yes\n"
        "    TriggerFX = FX_FixtureGrace\n"
        "  End\n"
        "  Behavior = SpecialAbilityUpdate ModuleTag_GraceUpdate\n"
        "    SpecialPowerTemplate = SpecialAbilityFixtureGrace\n"
        "    UnpackTime = 1\n"
        "  End\n"
        "  Behavior = AutoHealBehavior ModuleTag_GraceHealing\n"
        "    StartsActive = Yes\n"
        "    ButtonTriggered = Yes\n"
        "    HealingAmount = 500\n"
        "    Radius = 150\n"
        "    SingleBurst = Yes\n"
        "    UnitHealPulseFX = FX_FixtureGrace\n"
        "  End\n"
        "End\n"
        "\nObject SummonMinion\n"
        "  KindOf = PRELOAD SELECTABLE INFANTRY\n"
        "  BuildCost = 100\n"
        "  BuildTime = 10\n"
        "  CommandPoints = 5\n"
        "  VisionRange = 100\n"
        "  SelectPortrait = UPSummonMinion\n"
        "  Draw = W3DScriptedModelDraw ModuleTag_Draw\n"
        "    DefaultModelConditionState\n"
        "      Model = SummonMinionModel\n"
        "    End\n"
        "  End\n"
        "End\n"
    )
    command_sets = (
        "\nCommandSet AbilityHeroCommandSet\n"
        "  1 = Command_FixtureStance\n"
        "  2 = Command_FixtureBlast\n"
        "  3 = Command_FixtureHeal\n"
        "  4 = Command_FixtureSummon\n"
        "  5 = Command_FixtureRage\n"
        "  6 = Command_FixtureMount\n"
        "  7 = Command_FixtureLeadership\n"
        "  8 = Command_FixtureGrace\n"
        "  9 = Command_FixtureBroken\n"
        "End\n"
    )
    buttons = (
        "\nCommandButton Command_FixtureStance\n"
        "  Command = TOGGLE_STANCE\n"
        "End\n"
        "\nCommandButton Command_FixtureBlast\n"
        "  Command = SPECIAL_POWER\n"
        "  SpecialPower = SpecialAbilityFixtureBlast\n"
        "  TextLabel = CONTROLBAR:FixtureBlast\n"
        "  DescriptLabel = CONTROLBAR:ToolTipFixtureBlast\n"
        "  ButtonImage = HSFixtureBlast\n"
        "  Options = NEED_TARGET_ENEMY_OBJECT\n"
        "End\n"
        "\nCommandButton Command_FixtureHeal\n"
        "  Command = SPECIAL_POWER\n"
        "  SpecialPower = SpecialAbilityFixtureHeal\n"
        "  TextLabel = CONTROLBAR:FixtureHeal\n"
        "  DescriptLabel = CONTROLBAR:ToolTipFixtureHeal\n"
        "  ButtonImage = HSFixtureHeal\n"
        "  Options = NEED_TARGET_POS\n"
        "  RadiusCursorType = HealRadiusCursor\n"
        "End\n"
        "\nCommandButton Command_FixtureSummon\n"
        "  Command = SPECIAL_POWER\n"
        "  SpecialPower = SpecialAbilityFixtureSummon\n"
        "  TextLabel = CONTROLBAR:FixtureSummon\n"
        "  DescriptLabel = CONTROLBAR:ToolTipFixtureSummon\n"
        "  ButtonImage = HSFixtureSummon\n"
        "  Options = NEED_TARGET_POS\n"
        "End\n"
        "\nCommandButton Command_FixtureRage\n"
        "  Command = SPECIAL_POWER\n"
        "  SpecialPower = SpecialAbilityFixtureRage\n"
        "  TextLabel = CONTROLBAR:FixtureRage\n"
        "  DescriptLabel = CONTROLBAR:ToolTipFixtureRage\n"
        "  ButtonImage = HSFixtureRage\n"
        "End\n"
        "\nCommandButton Command_FixtureMount\n"
        "  Command = SPECIAL_POWER\n"
        "  SpecialPower = SpecialAbilityToggleMounted\n"
        "  TextLabel = CONTROLBAR:FixtureMount\n"
        "  DescriptLabel = CONTROLBAR:ToolTipFixtureMount\n"
        "  ButtonImage = HSFixtureMount\n"
        "End\n"
        "\nCommandButton Command_FixtureLeadership\n"
        "  Command = SPECIAL_POWER\n"
        "  SpecialPower = SpecialAbilityFakeLeadership\n"
        "  TextLabel = CONTROLBAR:FixtureLeadership\n"
        "  DescriptLabel = CONTROLBAR:ToolTipFixtureLeadership\n"
        "  ButtonImage = HSFixtureLeadership\n"
        "  Options = NONPRESSABLE\n"
        "End\n"
        "\nCommandButton Command_FixtureGrace\n"
        "  Command = SPECIAL_POWER\n"
        "  SpecialPower = SpecialAbilityFixtureGrace\n"
        "  TextLabel = CONTROLBAR:FixtureGrace\n"
        "  DescriptLabel = CONTROLBAR:ToolTipFixtureGrace\n"
        "  ButtonImage = HSFixtureGrace\n"
        "End\n"
        "\nCommandButton Command_FixtureBroken\n"
        "  Command = SPECIAL_POWER\n"
        "  SpecialPower = SpecialAbilityFixtureBroken\n"
        "  TextLabel = CONTROLBAR:FixtureBroken\n"
        "  DescriptLabel = CONTROLBAR:ToolTipFixtureBroken\n"
        "  ButtonImage = HSFixtureBroken\n"
        "End\n"
    )
    documents = _combat_documents(
        hero_object,
        "Weapon FixtureHeroBlast\n"
        "  AttackRange = 110.0\n"
        "  DamageNugget\n"
        "    Damage = 350\n"
        "    Radius = 40.0\n"
        "    DamageType = MAGIC\n"
        "  End\n"
        "End\n"
        "Weapon AbilityHeroSword\n"
        "  MeleeWeapon = Yes\n"
        "  AttackRange = 5.0\n"
        "  DelayBetweenShots = 1000\n"
        "  PreAttackDelay = 400\n"
        "  FiringDuration = 400\n"
        "  DamageNugget\n"
        "    Damage = 120\n"
        "    DamageType = SLASH\n"
        "  End\n"
        "End\n",
        command_row,
        button_row + buttons,
    )
    documents["data/ini/commandset.ini"] += command_sets.encode()
    documents["data/ini/specialpower.ini"] = b"""
SpecialPower SpecialAbilityFixtureBlast
  Enum = SPECIAL_GENERAL_TARGETLESS
  ReloadTime = 60000
End
SpecialPower SpecialAbilityFixtureHeal
  Enum = SPECIAL_ATHELAS
  ReloadTime = 90000
  RadiusCursorRadius = 150.0
  InitiateAtLocationSound = FixtureHealSound
End
SpecialPower SpecialAbilityFixtureSummon
  Enum = SPECIAL_SPAWN_OATHBREAKERS
  ReloadTime = 120000
End
SpecialPower SpecialAbilityFixtureRage
  Enum = SPECIAL_HERO_MODE
  ReloadTime = 45000
End
SpecialPower SpecialAbilityToggleMounted
  Enum = SPECIAL_TOGGLE_MOUNTED
  ReloadTime = 1000
End
SpecialPower SpecialAbilityFakeLeadership
  Enum = SPECIAL_FAKE_LEADERSHIP_BUTTON
  ReloadTime = 1
End
SpecialPower SpecialAbilityFixtureGrace
  Enum = SPECIAL_ATHELAS
  ReloadTime = 70000
End
SpecialPower SpecialAbilityFixtureBroken
  Enum = SPECIAL_GENERAL_TARGETLESS
  ReloadTime = 30000
End
"""
    documents["data/ini/attributemodifier.ini"] = b"""
ModifierList FixtureRage
  Category = SPELL
  Modifier = ARMOR 50%
  Modifier = DAMAGE_MULT 150%
  Modifier = CRUSH_DECELERATE 0%
  Duration = 20000
End
"""
    documents["data/ini/objectcreationlist.ini"] = b"""
ObjectCreationList OCL_FixtureSummon
  CreateObject
    ObjectNames = SummonMinion
    Count = 2
    Disposition = LIKE_EXISTING
  End
End
"""
    documents["data/ini/experiencelevels.ini"] = b"""
#define FIXTUREHERO AbilityHero
ExperienceLevel FixtureHeroLevel1
  TargetNames = FIXTUREHERO
  RequiredExperience = 1
  ExperienceAward = 20
  Rank = 1
  SelectionDecal
    Texture = decal_hero_good
  End
End
ExperienceLevel FixtureHeroLevel2
  TargetNames = FIXTUREHERO
  RequiredExperience = 100
  ExperienceAward = 25
  Rank = 2
  Upgrades = Upgrade_FixtureBlast
  SelectionDecal
    Texture = decal_hero_good
  End
End
"""
    return documents


def _abilities_by_id(descriptor: dict[str, object]) -> dict[str, dict[str, object]]:
    return {
        str(row["id"]): row
        for row in descriptor["abilities"]  # type: ignore[index]
    }


def test_hero_abilities_emit_each_effect_kind_with_evidence() -> None:
    documents = _hero_ability_documents()

    descriptor = compile_playable_unit_descriptor("AbilityHero", documents)

    validate_playable_unit_descriptor(descriptor)
    assert descriptor["category"] == "hero"
    abilities = _abilities_by_id(descriptor)
    assert set(abilities) == {
        "Command_FixtureBlast",
        "Command_FixtureHeal",
        "Command_FixtureSummon",
        "Command_FixtureRage",
        "Command_FixtureMount",
        "Command_FixtureLeadership",
        "Command_FixtureGrace",
        "Command_FixtureBroken",
    }

    blast = abilities["Command_FixtureBlast"]
    assert blast["slot"] == 2
    assert blast["specialPowerId"] == "SpecialAbilityFixtureBlast"
    assert blast["cooldownMs"] == 60000
    assert blast["targeting"] == "enemy-object"
    assert blast["levelGate"] == {
        "upgradeIds": ["Upgrade_FixtureBlast"],
        "requiredLevel": 2,
        "sourceIni": "data/ini/experiencelevels.ini",
    }
    assert blast["button"]["iconIds"] == ["HSFixtureBlast"]
    assert blast["button"]["labelIds"] == ["CONTROLBAR:FixtureBlast"]
    assert blast["button"]["tooltipIds"] == ["CONTROLBAR:ToolTipFixtureBlast"]
    assert blast["implementation"]["status"] == "implemented"
    effect = blast["effect"]
    assert effect["kind"] == "weapon-blast"
    assert effect["weaponId"] == "FixtureHeroBlast"
    assert effect["damage"] == 350
    assert effect["damageRadius"] == 40.0
    assert effect["damageType"] == "MAGIC"
    assert effect["attackRange"] == 110.0
    assert effect["startAbilityRange"] == 80.0

    heal = abilities["Command_FixtureHeal"]
    assert heal["targeting"] == "point"
    assert heal["initiateSoundId"] == "FixtureHealSound"
    assert heal["radiusCursorRadius"] == 150.0
    assert heal["implementation"]["status"] == "implemented"
    assert heal["effect"] == {
        "kind": "heal",
        "module": "PlayerHealSpecialPower",
        "amountKind": "fraction",
        "amount": 0.25,
        "radius": 120,
        "onlyOthers": False,
        "sourceIni": "data/ini/object/units/test_units.ini",
        "line": heal["effect"]["line"],
        "healFxId": "FX_FixtureHeal",
    }

    summon = abilities["Command_FixtureSummon"]
    assert summon["implementation"]["status"] == "implemented"
    assert summon["effect"]["kind"] == "summon"
    assert summon["effect"]["oclId"] == "OCL_FixtureSummon"
    assert summon["effect"]["createLocation"] == "CREATE_AT_LOCATION"
    assert summon["effect"]["objects"] == [
        {
            "id": "SummonMinion",
            "count": 2,
            "sourceIni": "data/ini/objectcreationlist.ini",
            "line": summon["effect"]["objects"][0]["line"],
        }
    ]

    rage = abilities["Command_FixtureRage"]
    assert rage["targeting"] == "self"
    assert rage["implementation"]["status"] == "implemented"
    assert rage["effect"]["kind"] == "attribute-modifier"
    assert rage["effect"]["modifierId"] == "FixtureRage"
    assert rage["effect"]["durationMs"] == 20000
    assert rage["effect"]["affectsSelf"] is True
    assert rage["effect"]["modifiers"] == [
        {"kind": "ARMOR", "value": 0.5, "application": "additive"},
        {"kind": "DAMAGE_MULT", "value": 1.5, "application": "multiplicative"},
    ]
    assert rage["implementation"]["limitations"] == [
        "modifier kinds not applied by the runtime: CRUSH_DECELERATE"
    ]

    grace = abilities["Command_FixtureGrace"]
    assert grace["implementation"]["status"] == "implemented"
    assert grace["effect"]["kind"] == "heal"
    assert grace["effect"]["module"] == "AutoHealBehavior"
    assert grace["effect"]["amountKind"] == "flat"
    assert grace["effect"]["amount"] == 500
    assert grace["effect"]["radius"] == 150
    assert grace["effect"]["healFxId"] == "FX_FixtureGrace"


def test_hero_abilities_fail_closed_per_ability_never_faked() -> None:
    documents = _hero_ability_documents()

    descriptor = compile_playable_unit_descriptor("AbilityHero", documents)

    validate_playable_unit_descriptor(descriptor)
    abilities = _abilities_by_id(descriptor)

    mount = abilities["Command_FixtureMount"]
    assert mount["implementation"]["status"] == "unimplemented"
    assert "mount" in mount["implementation"]["reason"]
    assert mount["effect"] == {"kind": "none"}

    leadership = abilities["Command_FixtureLeadership"]
    assert leadership["implementation"]["status"] == "passive"
    assert "NONPRESSABLE" in leadership["implementation"]["reason"]
    assert leadership["effect"] == {"kind": "none"}

    broken = abilities["Command_FixtureBroken"]
    assert broken["implementation"]["status"] == "unimplemented"
    assert "MissingWeapon" in broken["implementation"]["reason"]
    assert broken["effect"] == {"kind": "none"}


def test_hero_abilities_record_unresolved_level_gates_and_missing_powers() -> None:
    documents = _hero_ability_documents()
    del documents["data/ini/experiencelevels.ini"]
    del documents["data/ini/specialpower.ini"]

    descriptor = compile_playable_unit_descriptor("AbilityHero", documents)

    validate_playable_unit_descriptor(descriptor)
    abilities = _abilities_by_id(descriptor)
    blast = abilities["Command_FixtureBlast"]
    assert blast["levelGate"]["upgradeIds"] == ["Upgrade_FixtureBlast"]
    assert blast["levelGate"]["requiredLevel"] is None
    assert "experience level source" in blast["levelGate"]["limitation"]
    assert blast["implementation"]["status"] == "unimplemented"
    assert "SpecialPower is missing" in blast["implementation"]["reason"]
    assert blast["effect"] == {"kind": "none"}


def test_heroes_without_special_power_commands_emit_an_empty_array() -> None:
    documents = _documents()

    hero = compile_playable_unit_descriptor("HeroUnit", documents)
    infantry = compile_playable_unit_descriptor("InfantryHorde", documents)

    validate_playable_unit_descriptor(hero)
    assert hero["category"] == "hero"
    assert hero["abilities"] == []
    assert "abilities" not in infantry


def test_validation_rejects_ability_row_mutation() -> None:
    documents = _hero_ability_documents()
    descriptor = compile_playable_unit_descriptor("AbilityHero", documents)
    validate_playable_unit_descriptor(descriptor)

    mutated = deepcopy(descriptor)
    mutated["abilities"][0]["cooldownMs"] = 1
    with pytest.raises(PlayableUnitCompilerError, match="digest"):
        validate_playable_unit_descriptor(mutated)

    corrupted = deepcopy(descriptor)
    del corrupted["abilities"]
    corrupted["descriptorSha256"] = hashlib.sha256(
        json.dumps(
            {key: value for key, value in corrupted.items() if key != "descriptorSha256"},
            sort_keys=True,
            separators=(",", ":"),
            ensure_ascii=False,
        ).encode("utf-8")
    ).hexdigest()
    with pytest.raises(PlayableUnitCompilerError, match="abilities"):
        validate_playable_unit_descriptor(corrupted)

    rehashed = deepcopy(descriptor)
    rehashed["abilities"][0]["implementation"]["status"] = "passive"
    rehashed["descriptorSha256"] = hashlib.sha256(
        json.dumps(
            {key: value for key, value in rehashed.items() if key != "descriptorSha256"},
            sort_keys=True,
            separators=(",", ":"),
            ensure_ascii=False,
        ).encode("utf-8")
    ).hexdigest()
    with pytest.raises(PlayableUnitCompilerError, match="effect"):
        validate_playable_unit_descriptor(rehashed)


def test_negated_condition_default_weapon_set_resolves_member_weapon() -> None:
    member_row, member_button = _combat_command("CombatMember", 8, "CombatMember")
    horde_row, horde_button = _combat_command("CombatHorde", 9, "CombatHorde")
    documents = _combat_documents(
        _combat_object(
            "CombatMember",
            "INFANTRY",
            "  WeaponSet\n"
            "    Conditions = -WEAPONSET_TOGGLE_1\n"
            "    Weapon = PRIMARY CombatMemberSpear\n"
            "  End\n"
            "  WeaponSet\n"
            "    Conditions = WEAPONSET_TOGGLE_1\n"
            "    Weapon = SECONDARY CombatMemberBow\n"
            "  End\n",
        )
        + (
            "\nObject CombatHorde\n"
            "  KindOf = PRELOAD SELECTABLE HORDE\n"
            "  BuildCost = 500\n"
            "  BuildTime = 30\n"
            "  CommandPoints = 20\n"
            "  VisionRange = 300\n"
            "  SelectPortrait = UPCombatHorde\n"
            "  VoiceSelect = CombatHordeVoiceSelect\n"
            "  VoicePriority = 43\n"
            "  VoiceMove = CombatHordeVoiceMove\n"
            "  VoiceAttack = CombatHordeVoiceAttack\n"
            "  Draw = W3DScriptedModelDraw ModuleTag_Draw\n"
            "    DefaultModelConditionState\n"
            "      Model = CombatHordeModel\n"
            "    End\n"
            "  End\n"
            "  WeaponSet\n"
            "    Conditions = None\n"
            "    Weapon = PRIMARY CombatHordeRangefinder\n"
            "  End\n"
            "  Behavior = HordeContain ModuleTag_HordeContain\n"
            "    InitialPayload = CombatMember 5\n"
            "  End\n"
            "End\n"
        ),
        "Weapon CombatMemberSpear\n"
        "  MeleeWeapon = Yes\n"
        "  AttackRange = 11.5\n"
        "  DelayBetweenShots = 1000\n"
        "  PreAttackDelay = 500\n"
        "  FiringDuration = 1000\n"
        "  DamageNugget\n"
        "    Damage = 80\n"
        "    DamageType = CAVALRY\n"
        "    ForbiddenUpgradeNames = Upgrade_CombatForgedBlades\n"
        "  End\n"
        "  DamageNugget\n"
        "    Damage = 120\n"
        "    DamageType = CAVALRY\n"
        "    RequiredUpgradeNames = Upgrade_CombatForgedBlades\n"
        "  End\n"
        "End\n"
        "Weapon CombatHordeRangefinder\n"
        "  MeleeWeapon = Yes\n"
        "  AttackRange = 12.0\n"
        "  DelayBetweenShots = 1000\n"
        "  HordeAttackNugget\n"
        "  End\n"
        "End\n",
        member_row + horde_row,
        member_button + horde_button,
    )

    descriptor = compile_playable_unit_descriptor("CombatHorde", documents)

    validate_playable_unit_descriptor(descriptor)
    simulation = descriptor["gameplay"]["simulation"]
    combat = simulation["resolved"]["combat"]
    assert combat["weaponId"] == "CombatMemberSpear"
    damage = combat["damage"]
    assert damage["value"] == 80
    assert [row["value"] for row in damage["components"]] == [80]
    assert [row["reason"] for row in damage["excludedNuggets"]] == [
        "required-upgrade"
    ]
    assert "combat.weapon" not in simulation["missing"]


def test_warhead_nugget_damage_resolves_from_projectile_warhead() -> None:
    command_row, button_row = _combat_command("CombatThrower", 9, "CombatThrower")
    documents = _combat_documents(
        _combat_object(
            "CombatThrower",
            "INFANTRY ARCHER",
            "  WeaponSet\n    Conditions = None\n    Weapon = PRIMARY CombatThrowingAxe\n  End\n",
        ),
        "Weapon CombatThrowingAxe\n"
        "  AttackRange = 250.0\n"
        "  DelayBetweenShots = 1200\n"
        "  PreAttackDelay = 666\n"
        "  FiringDuration = 2000\n"
        "  ProjectileNugget\n"
        "    ProjectileTemplateName = CombatAxeProjectile\n"
        "    WarheadTemplateName = CombatAxeThrowWarhead\n"
        "  End\n"
        "End\n"
        "Weapon CombatAxeThrowWarhead\n"
        "  RadiusDamageAffects = ENEMIES\n"
        "  DamageNugget\n"
        "    Damage = 55\n"
        "    DamageType = SLASH\n"
        "    ForbiddenUpgradeNames = Upgrade_CombatForgedBlades\n"
        "  End\n"
        "  DamageNugget\n"
        "    Damage = 90\n"
        "    DamageType = SLASH\n"
        "    RequiredUpgradeNames = Upgrade_CombatForgedBlades\n"
        "  End\n"
        "End\n",
        command_row,
        button_row,
    )

    descriptor = compile_playable_unit_descriptor("CombatThrower", documents)

    validate_playable_unit_descriptor(descriptor)
    combat = descriptor["gameplay"]["simulation"]["resolved"]["combat"]
    assert combat["warheadId"] == "CombatAxeThrowWarhead"
    damage = combat["damage"]
    assert damage["value"] == 55
    assert [row["value"] for row in damage["components"]] == [55]
    assert [row["reason"] for row in damage["excludedNuggets"]] == [
        "required-upgrade"
    ]


def test_warhead_nugget_damage_sums_unrestricted_components() -> None:
    command_row, button_row = _combat_command("CombatCatapult", 9, "CombatCatapult")
    documents = _combat_documents(
        _combat_object(
            "CombatCatapult",
            "MACHINE SIEGEENGINE",
            "  WeaponSet\n    Conditions = None\n    Weapon = PRIMARY CombatCatapultRock\n  End\n",
        ),
        "Weapon CombatCatapultRock\n"
        "  AttackRange = 350.0\n"
        "  DelayBetweenShots = 6000\n"
        "  PreAttackDelay = 2000\n"
        "  FiringDuration = 3000\n"
        "  ProjectileNugget\n"
        "    ProjectileTemplateName = CombatRockProjectile\n"
        "    WarheadTemplateName = CombatRockWarhead\n"
        "  End\n"
        "End\n"
        "Weapon CombatRockWarhead\n"
        "  RadiusDamageAffects = ENEMIES\n"
        "  DamageNugget\n"
        "    Damage = 400\n"
        "    DamageType = SIEGE\n"
        "  End\n"
        "  DamageNugget\n"
        "    Damage = 60\n"
        "    DamageType = FLAME\n"
        "  End\n"
        "  DamageNugget\n"
        "    Damage = 400\n"
        "    DamageType = SIEGE\n"
        "  End\n"
        "End\n",
        command_row,
        button_row,
    )

    descriptor = compile_playable_unit_descriptor("CombatCatapult", documents)

    validate_playable_unit_descriptor(descriptor)
    combat = descriptor["gameplay"]["simulation"]["resolved"]["combat"]
    assert combat["warheadId"] == "CombatRockWarhead"
    damage = combat["damage"]
    assert damage["value"] == 860
    assert [row["value"] for row in damage["components"]] == [400, 60, 400]
    assert "excludedNuggets" not in damage


def test_absent_delay_between_shots_records_engine_default() -> None:
    command_row, button_row = _combat_command("CombatLancer", 9, "CombatLancer")
    documents = _combat_documents(
        _combat_object(
            "CombatLancer",
            "INFANTRY",
            "  WeaponSet\n    Conditions = None\n    Weapon = PRIMARY CombatLanceThrown\n  End\n",
        ),
        "Weapon CombatLanceThrown\n"
        "  AttackRange = 250.0\n"
        "  LeechRangeWeapon = Yes\n"
        "  PreAttackDelay = 1500\n"
        "  FiringDuration = 1000\n"
        "  ClipSize = 1\n"
        "  ClipReloadTime = 2800\n"
        "  DamageNugget\n"
        "    Damage = 60\n"
        "    DamageType = CAVALRY\n"
        "  End\n"
        "End\n",
        command_row,
        button_row,
    )

    descriptor = compile_playable_unit_descriptor("CombatLancer", documents)

    validate_playable_unit_descriptor(descriptor)
    simulation = descriptor["gameplay"]["simulation"]
    combat = simulation["resolved"]["combat"]
    assert combat["delayBetweenShotsMs"]["value"] == 0
    assert "engine default" in combat["delayBetweenShotsMs"]["semantic"]
    assert combat["clipReloadTimeMs"]["value"] == 2800
    assert "combat.delayBetweenShotsMs" not in simulation["missing"]


def test_authored_unresolvable_delay_between_shots_is_not_defaulted() -> None:
    command_row, button_row = _combat_command("CombatLancer", 9, "CombatLancer")
    documents = _combat_documents(
        _combat_object(
            "CombatLancer",
            "INFANTRY",
            "  WeaponSet\n    Conditions = None\n    Weapon = PRIMARY CombatLanceThrown\n  End\n",
        ),
        "Weapon CombatLanceThrown\n"
        "  AttackRange = 250.0\n"
        "  LeechRangeWeapon = Yes\n"
        "  DelayBetweenShots = UNDEFINED_LANCER_DELAY\n"
        "  PreAttackDelay = 1500\n"
        "  FiringDuration = 1000\n"
        "  DamageNugget\n"
        "    Damage = 60\n"
        "    DamageType = CAVALRY\n"
        "  End\n"
        "End\n",
        command_row,
        button_row,
    )

    descriptor = compile_playable_unit_descriptor("CombatLancer", documents)

    validate_playable_unit_descriptor(descriptor)
    simulation = descriptor["gameplay"]["simulation"]
    assert "delayBetweenShotsMs" not in simulation["resolved"]["combat"]
    assert "combat.delayBetweenShotsMs" in simulation["missing"]


# ---------------------------------------------------------------------------
# Experience economy contract tests.
# ---------------------------------------------------------------------------


def _experience_documents(
    chain_ini: str,
    *,
    defines: str = "",
    modifiers: str = "",
) -> dict[str, bytes]:
    documents = _documents()
    documents["data/ini/experiencelevels.ini"] = chain_ini.encode()
    documents["data/ini/gamedata.ini"] = (
        documents["data/ini/gamedata.ini"].decode() + defines
    ).encode()
    if modifiers:
        documents["data/ini/attributemodifier.ini"] = modifiers.encode()
    return documents


_TROOP_CHAIN = """
#define FIXTURE_TROOPS InfantryMember InfantryHorde RangedMember RangedHorde CavalryMember CavalryHorde
ExperienceLevel FixtureTroopLevel1
  TargetNames = FIXTURE_TROOPS
  RequiredExperience = 1
  ExperienceAward = FIXTURE_AWARD_1
  Rank = 1
  SelectionDecal
    Texture = decal_G_level1
  End
End
ExperienceLevel FixtureTroopLevel2
  TargetNames = FIXTURE_TROOPS
  RequiredExperience = FIXTURE_NEEDED_2
  ExperienceAward = FIXTURE_AWARD_2
  Rank = 2
  AttributeModifiers = FixtureTroopBonusRank2
  Upgrades = Upgrade_ObjectLevel2
  LevelUpFx = FX:FixtureLevelUp2
  SelectionDecal
    Texture = decal_G_level2
  End
End
ExperienceLevel FixtureTroopLevel3
  TargetNames = FIXTURE_TROOPS
  RequiredExperience = FIXTURE_NEEDED_3
  ExperienceAward = FIXTURE_AWARD_3
  Rank = 3
  AttributeModifiers = FixtureTroopBonusRank3 FixtureTroopBonusSpeed
  SelectionDecal
    Texture = decal_G_level3
  End
End
"""

_TROOP_DEFINES = (
    "#define FIXTURE_NEEDED_2 50\n"
    "#define FIXTURE_NEEDED_3 100\n"
    "#define FIXTURE_AWARD_1 3\n"
    "#define FIXTURE_AWARD_2 4\n"
    "#define FIXTURE_AWARD_3 5\n"
    "#define FIXTURE_HP_ADD_2 20\n"
    "#define FIXTURE_DAM_ADD_2 10\n"
)

_TROOP_MODIFIERS = """
ModifierList FixtureTroopBonusRank2
  Category = LEVEL
  Modifier = HEALTH FIXTURE_HP_ADD_2
  Modifier = DAMAGE_ADD FIXTURE_DAM_ADD_2
  Duration = 0
End
ModifierList FixtureTroopBonusRank3
  Category = LEVEL
  Modifier = HEALTH 20
  Modifier = DAMAGE_ADD 10
  Duration = 0
End
ModifierList FixtureTroopBonusSpeed
  Category = LEVEL
  Modifier = SPEED 110%
  Duration = 0
End
"""


def test_experience_chain_compiles_thresholds_awards_and_modifiers() -> None:
    documents = _experience_documents(
        _TROOP_CHAIN, defines=_TROOP_DEFINES, modifiers=_TROOP_MODIFIERS
    )

    descriptor = compile_playable_unit_descriptor("InfantryHorde", documents)

    validate_playable_unit_descriptor(descriptor)
    experience = descriptor["experience"]
    assert experience["status"] == "compiled"
    assert experience["sourceIni"] == "data/ini/experiencelevels.ini"
    assert experience["maxLevel"] == 3
    assert experience["modifierApplication"] == "cumulative-per-level"
    assert experience["targetCount"] == 6
    levels = experience["levels"]
    assert [row["rank"] for row in levels] == [1, 2, 3]
    assert [row["requiredExperience"] for row in levels] == [1, 50, 100]
    assert [row["experienceAward"] for row in levels] == [3, 4, 5]
    assert levels[0]["experienceId"] == "FixtureTroopLevel1"
    assert levels[0]["selectionDecalTextureId"] == "decal_G_level1"
    assert "attributeModifiers" not in levels[0]
    rank_two_modifiers = levels[1]["attributeModifiers"]
    assert len(rank_two_modifiers) == 1
    assert rank_two_modifiers[0]["id"] == "FixtureTroopBonusRank2"
    assert rank_two_modifiers[0]["modifiers"] == [
        {"kind": "HEALTH", "value": 20, "application": "additive"},
        {"kind": "DAMAGE_ADD", "value": 10, "application": "additive"},
    ]
    assert levels[1]["upgrades"] == ["Upgrade_ObjectLevel2"]
    assert levels[1]["levelUpFxId"] == "FX:FixtureLevelUp2"
    # The constant-backed rows name gamedata.ini as their provenance.
    assert levels[1]["constantSourceIni"] == "data/ini/gamedata.ini"
    # Unsupported kinds are recorded on their leaf, never applied.
    rank_three_modifiers = levels[2]["attributeModifiers"]
    assert len(rank_three_modifiers) == 2
    speed_leaf = next(
        leaf for leaf in rank_three_modifiers if leaf["id"] == "FixtureTroopBonusSpeed"
    )
    assert speed_leaf["modifiers"] == []
    assert speed_leaf["unsupportedModifiers"] == ["SPEED"]


def test_experience_member_name_matches_the_horde_chain() -> None:
    documents = _experience_documents(
        _TROOP_CHAIN, defines=_TROOP_DEFINES, modifiers=_TROOP_MODIFIERS
    )

    descriptor = compile_playable_unit_descriptor("RangedHorde", documents)

    validate_playable_unit_descriptor(descriptor)
    assert descriptor["experience"]["status"] == "compiled"
    assert descriptor["experience"]["maxLevel"] == 3


def test_experience_specific_chain_wins_over_define_chain() -> None:
    documents = _experience_documents(
        _TROOP_CHAIN
        + """
ExperienceLevel InfantryOwnLevel1
  TargetNames = InfantryHorde
  RequiredExperience = 1
  ExperienceAward = 9
  Rank = 1
End
""",
        defines=_TROOP_DEFINES,
        modifiers=_TROOP_MODIFIERS,
    )

    descriptor = compile_playable_unit_descriptor("InfantryHorde", documents)

    validate_playable_unit_descriptor(descriptor)
    experience = descriptor["experience"]
    assert experience["maxLevel"] == 1
    assert experience["levels"][0]["experienceId"] == "InfantryOwnLevel1"
    assert experience["levels"][0]["experienceAward"] == 9


def test_experience_unauthored_chain_is_recorded_not_invented() -> None:
    documents = _experience_documents(_TROOP_CHAIN, defines=_TROOP_DEFINES)

    descriptor = compile_playable_unit_descriptor("HeroUnit", documents)

    validate_playable_unit_descriptor(descriptor)
    experience = descriptor["experience"]
    assert experience["status"] == "unauthored"
    assert "no ExperienceLevel chain" in experience["note"]


def test_experience_missing_source_is_recorded() -> None:
    documents = _documents()

    descriptor = compile_playable_unit_descriptor("InfantryHorde", documents)

    validate_playable_unit_descriptor(descriptor)
    assert descriptor["experience"]["status"] == "unavailable"


def test_experience_unresolvable_threshold_fails_closed() -> None:
    documents = _experience_documents(
        _TROOP_CHAIN.replace("FIXTURE_NEEDED_2", "UNDEFINED_CONSTANT"),
        defines=_TROOP_DEFINES,
    )

    with pytest.raises(PlayableUnitCompilerError, match="RequiredExperience"):
        compile_playable_unit_descriptor("InfantryHorde", documents)


def test_experience_missing_award_fails_closed() -> None:
    documents = _experience_documents(
        _TROOP_CHAIN.replace("  ExperienceAward = FIXTURE_AWARD_2\n", ""),
        defines=_TROOP_DEFINES,
    )

    with pytest.raises(PlayableUnitCompilerError, match="ExperienceAward"):
        compile_playable_unit_descriptor("InfantryHorde", documents)


def test_experience_noncontiguous_ranks_fail_closed() -> None:
    documents = _experience_documents(
        _TROOP_CHAIN + """
ExperienceLevel FixtureTroopDuplicate
  TargetNames = FIXTURE_TROOPS
  RequiredExperience = 150
  ExperienceAward = 8
  Rank = 3
End
""",
        defines=_TROOP_DEFINES,
    )

    with pytest.raises(PlayableUnitCompilerError, match="duplicate or invalid Rank"):
        compile_playable_unit_descriptor("InfantryHorde", documents)


def test_experience_top_rank_summon_chain_compiles_initial_rank() -> None:
    # Retail summons (ring hero, Treebeard) author a single rank-10 row: the
    # unit enters at the top rank and never levels further.
    documents = _experience_documents(
        """
ExperienceLevel FixtureSummonLevel1
  TargetNames = InfantryHorde
  RequiredExperience = 1
  ExperienceAward = 100
  Rank = 10
  SelectionDecal
    Texture = decal_hero_good
  End
End
"""
    )

    descriptor = compile_playable_unit_descriptor("InfantryHorde", documents)

    validate_playable_unit_descriptor(descriptor)
    experience = descriptor["experience"]
    assert experience["status"] == "compiled"
    assert experience["initialRank"] == 10
    assert experience["maxLevel"] == 10
    assert len(experience["levels"]) == 1
    assert experience["levels"][0]["rank"] == 10
    assert experience["levels"][0]["experienceAward"] == 100


def test_experience_ambiguous_specificity_fails_closed() -> None:
    documents = _experience_documents(
        """
#define CHAIN_A InfantryHorde OtherA
#define CHAIN_B InfantryHorde OtherB
ExperienceLevel ChainALevel1
  TargetNames = CHAIN_A
  RequiredExperience = 1
  ExperienceAward = 3
  Rank = 1
End
ExperienceLevel ChainBLevel1
  TargetNames = CHAIN_B
  RequiredExperience = 1
  ExperienceAward = 4
  Rank = 1
End
"""
    )

    with pytest.raises(PlayableUnitCompilerError, match="equal specificity"):
        compile_playable_unit_descriptor("InfantryHorde", documents)


def test_experience_missing_modifier_list_fails_closed() -> None:
    documents = _experience_documents(
        _TROOP_CHAIN,
        defines=_TROOP_DEFINES,
        modifiers="ModifierList UnrelatedBonus\n  Category = LEVEL\n  Modifier = HEALTH 5\n  Duration = 0\nEnd\n",
    )

    with pytest.raises(PlayableUnitCompilerError, match="missing ModifierList"):
        compile_playable_unit_descriptor("InfantryHorde", documents)


def test_validation_rejects_experience_mutation() -> None:
    documents = _experience_documents(
        _TROOP_CHAIN, defines=_TROOP_DEFINES, modifiers=_TROOP_MODIFIERS
    )
    descriptor = compile_playable_unit_descriptor("InfantryHorde", documents)
    validate_playable_unit_descriptor(descriptor)

    mutated = deepcopy(descriptor)
    mutated["experience"]["levels"][1]["requiredExperience"] = 999
    with pytest.raises(PlayableUnitCompilerError, match="digest"):
        validate_playable_unit_descriptor(mutated)

    corrupted = deepcopy(descriptor)
    del corrupted["experience"]
    corrupted["descriptorSha256"] = hashlib.sha256(
        json.dumps(
            {key: value for key, value in corrupted.items() if key != "descriptorSha256"},
            sort_keys=True,
            separators=(",", ":"),
            ensure_ascii=False,
        ).encode("utf-8")
    ).hexdigest()
    with pytest.raises(PlayableUnitCompilerError, match="experience"):
        validate_playable_unit_descriptor(corrupted)


# ---------------------------------------------------------------------------
# Real hero-ability effect extraction: leadership auras, terror pulses,
# blast knockback, weapon-set toggles, extended timed-buff modifier kinds,
# and the fear-resistance unit flag.
# ---------------------------------------------------------------------------

_HERO_OBJECT_MARKER = "  End\nEnd\n\nObject SummonMinion"

_FIXTURE_EMOTIONS = b"""
EmotionNugget Terror_Base
  Type = TERROR
  AIState = RUN_AWAY_PANIC
  Duration = 9000
  PreventPlayerCommands = Yes
End
EmotionNugget Taunt_Civilian
  Type = TAUNT
  AIState = RUN_AWAY_PANIC
  Duration = 4000
  PreventPlayerCommands = Yes
End
EmotionNugget Point_Base
  Type = POINT
End
"""


def _with_hero_modules(documents: dict[str, bytes], modules: str) -> None:
    path = "data/ini/object/units/test_units.ini"
    text = documents[path].decode()
    assert _HERO_OBJECT_MARKER in text
    documents[path] = text.replace(
        _HERO_OBJECT_MARKER, "  End\n" + modules + "End\n\nObject SummonMinion", 1
    ).encode()


def _aura_documents(
    *,
    modifier_rows: str = (
        "  Modifier = ARMOR 25%\n"
        "  Modifier = DAMAGE_MULT 150%\n"
        "  Modifier = EXPERIENCE 200%\n"
        "  Modifier = VISION 120%\n"
    ),
    extra_aura_module: str = "",
) -> dict[str, bytes]:
    documents = _hero_ability_documents()
    _with_hero_modules(
        documents,
        "  Behavior = UnpauseSpecialPowerUpgrade ModuleTag_LeadershipEnabler\n"
        "    SpecialPowerTemplate = SpecialAbilityFakeLeadership\n"
        "    TriggeredBy = Upgrade_FixtureLeadership\n"
        "  End\n"
        "  Behavior = AttributeModifierAuraUpdate ModuleTag_LeadershipAura\n"
        "    StartsActive = No\n"
        "    BonusName = FixtureLeadershipBonus\n"
        "    TriggeredBy = Upgrade_FixtureLeadership\n"
        "    RefreshDelay = 2000\n"
        "    Range = 200\n"
        "    AllowSelf = Yes\n"
        "    ObjectFilter = FIXTURE_BUFF_FILTER\n"
        "  End\n" + extra_aura_module,
    )
    documents["data/ini/attributemodifier.ini"] += (
        "\nModifierList FixtureLeadershipBonus\n"
        "  Category = LEADERSHIP\n"
        + modifier_rows
        + "  Duration = 3000\n"
        "  FX = FX_FixtureLeadership\n"
        "End\n"
    ).encode()
    documents["data/ini/gamedata.ini"] += (
        b"\n#define FIXTURE_BUFF_FILTER ANY +INFANTRY +CAVALRY -HORDE -HERO\n"
    )
    documents["data/ini/experiencelevels.ini"] += (
        b"\nExperienceLevel FixtureHeroLevel3\n"
        b"  TargetNames = FIXTUREHERO\n"
        b"  RequiredExperience = 300\n"
        b"  ExperienceAward = 30\n"
        b"  Rank = 3\n"
        b"  Upgrades = Upgrade_FixtureLeadership\n"
        b"End\n"
    )
    return documents


def test_leadership_aura_compiles_from_nonpressable_button() -> None:
    documents = _aura_documents()

    descriptor = compile_playable_unit_descriptor("AbilityHero", documents)

    validate_playable_unit_descriptor(descriptor)
    leadership = _abilities_by_id(descriptor)["Command_FixtureLeadership"]
    assert leadership["implementation"]["status"] == "passive"
    assert leadership["levelGate"]["requiredLevel"] == 3
    effect = leadership["effect"]
    assert effect["kind"] == "leadership-aura"
    assert effect["bonusName"] == "FixtureLeadershipBonus"
    assert effect["range"] == 200
    assert effect["affectsSelf"] is True
    assert effect["startsActive"] is True
    # ALL/ANY normalize and HORDE terms drop (the runtime battalion entity
    # proxies the members the retail filter buffs).
    assert effect["affects"] == "ANY +INFANTRY +CAVALRY -HERO"
    assert effect["modifiers"] == [
        {"kind": "ARMOR", "value": 0.25, "application": "additive"},
        {"kind": "DAMAGE_MULT", "value": 1.5, "application": "multiplicative"},
        {"kind": "EXPERIENCE", "value": 2.0, "application": "multiplicative"},
        {"kind": "VISION", "value": 1.2, "application": "multiplicative"},
    ]
    assert effect["fxIds"] == ["FX_FixtureLeadership"]


def test_leadership_aura_without_level_grant_stays_off() -> None:
    documents = _aura_documents()
    # Remove the authored grant: the gate no longer resolves to a level.
    documents["data/ini/experiencelevels.ini"] = documents[
        "data/ini/experiencelevels.ini"
    ].replace(b"  Upgrades = Upgrade_FixtureLeadership\n", b"")

    descriptor = compile_playable_unit_descriptor("AbilityHero", documents)

    validate_playable_unit_descriptor(descriptor)
    leadership = _abilities_by_id(descriptor)["Command_FixtureLeadership"]
    assert leadership["implementation"]["status"] == "passive"
    assert leadership["effect"]["kind"] == "leadership-aura"
    assert leadership["effect"]["startsActive"] is False
    assert any(
        "aura upgrade gate" in item
        for item in leadership["implementation"]["limitations"]
    )


def test_leadership_aura_ambiguous_binding_keeps_the_gap_row() -> None:
    documents = _aura_documents(
        extra_aura_module=(
            "  Behavior = AttributeModifierAuraUpdate ModuleTag_LeadershipAura2\n"
            "    StartsActive = No\n"
            "    BonusName = FixtureLeadershipBonus\n"
            "    TriggeredBy = Upgrade_FixtureLeadership\n"
            "    Range = 100\n"
            "  End\n"
        )
    )

    descriptor = compile_playable_unit_descriptor("AbilityHero", documents)

    validate_playable_unit_descriptor(descriptor)
    leadership = _abilities_by_id(descriptor)["Command_FixtureLeadership"]
    assert leadership["implementation"]["status"] == "passive"
    assert leadership["effect"] == {"kind": "none"}
    assert any(
        "multiple AttributeModifierAuraUpdate" in item
        for item in leadership["implementation"]["limitations"]
    )


def test_leadership_aura_with_no_supported_modifiers_keeps_the_gap_row() -> None:
    documents = _aura_documents(
        modifier_rows="  Modifier = BOUNTY_PERCENTAGE 100.0%\n"
    )

    descriptor = compile_playable_unit_descriptor("AbilityHero", documents)

    validate_playable_unit_descriptor(descriptor)
    leadership = _abilities_by_id(descriptor)["Command_FixtureLeadership"]
    assert leadership["effect"] == {"kind": "none"}
    assert any(
        "no runtime-supported Modifier rows" in item
        for item in leadership["implementation"]["limitations"]
    )


def _terror_documents() -> dict[str, bytes]:
    documents = _hero_ability_documents()
    _with_hero_modules(
        documents,
        "  Behavior = SpecialPowerModule ModuleTag_ScreechStarter\n"
        "    SpecialPowerTemplate = SpecialAbilityFixtureScreech\n"
        "    UpdateModuleStartsAttack = Yes\n"
        "    AntiCategory = LEADERSHIP\n"
        "  End\n"
        "  Behavior = SpecialAbilityUpdate ModuleTag_ScreechUpdate\n"
        "    SpecialPowerTemplate = SpecialAbilityFixtureScreech\n"
        "    UnpackTime = 1\n"
        "    GenerateTerror = Yes\n"
        "    EmotionPulseRadius = 150\n"
        "    ObjectFilter = ALL -SummonMinion ENEMIES\n"
        "  End\n",
    )
    command_sets = documents["data/ini/commandset.ini"].decode()
    documents["data/ini/commandset.ini"] = command_sets.replace(
        "  9 = Command_FixtureBroken\nEnd",
        "  9 = Command_FixtureBroken\n  10 = Command_FixtureScreech\nEnd",
        1,
    ).encode()
    documents["data/ini/commandbutton.ini"] += (
        b"\nCommandButton Command_FixtureScreech\n"
        b"  Command = SPECIAL_POWER\n"
        b"  SpecialPower = SpecialAbilityFixtureScreech\n"
        b"  TextLabel = CONTROLBAR:FixtureScreech\n"
        b"  DescriptLabel = CONTROLBAR:ToolTipFixtureScreech\n"
        b"  ButtonImage = HSFixtureScreech\n"
        b"End\n"
    )
    documents["data/ini/specialpower.ini"] += (
        b"\nSpecialPower SpecialAbilityFixtureScreech\n"
        b"  Enum = SPECIAL_SCREECH\n"
        b"  ReloadTime = 180000\n"
        b"End\n"
    )
    documents["data/ini/emotions.ini"] = _FIXTURE_EMOTIONS
    return documents


def test_terror_effect_compiles_from_generate_terror() -> None:
    documents = _terror_documents()

    descriptor = compile_playable_unit_descriptor("AbilityHero", documents)

    validate_playable_unit_descriptor(descriptor)
    screech = _abilities_by_id(descriptor)["Command_FixtureScreech"]
    assert screech["implementation"]["status"] == "implemented"
    effect = screech["effect"]
    assert effect["kind"] == "terror"
    assert effect["radius"] == 150
    assert effect["durationMs"] == 9000
    assert effect["emotionNuggetId"] == "Terror_Base"
    assert effect["affects"] == "ANY -SummonMinion ENEMIES"
    assert effect["modifiers"][0]["kind"] == "DAMAGE_MULT"
    assert effect["modifiers"][0]["value"] == 0.0
    limitations = screech["implementation"]["limitations"]
    assert any("no-fight debuff" in item for item in limitations)
    assert any("anti-category strip (LEADERSHIP)" in item for item in limitations)


def test_terror_without_emotion_source_keeps_the_gap_row() -> None:
    documents = _terror_documents()
    del documents["data/ini/emotions.ini"]

    descriptor = compile_playable_unit_descriptor("AbilityHero", documents)

    validate_playable_unit_descriptor(descriptor)
    screech = _abilities_by_id(descriptor)["Command_FixtureScreech"]
    assert screech["implementation"]["status"] == "unimplemented"
    assert "data/ini/emotions.ini" in screech["implementation"]["reason"]
    assert screech["effect"] == {"kind": "none"}


def test_weapon_blast_knockback_compiles_from_meta_impact_nugget() -> None:
    documents = _hero_ability_documents()
    documents["data/ini/weapon.ini"] = documents["data/ini/weapon.ini"].replace(
        b"Weapon FixtureHeroBlast\n"
        b"  AttackRange = 110.0\n",
        b"Weapon FixtureHeroBlast\n"
        b"  AttackRange = 110.0\n"
        b"  MetaImpactNugget\n"
        b"    ShockWaveAmount = 70.0\n"
        b"    ShockWaveRadius = 110.0\n"
        b"    ShockWaveTaperOff = 0.75\n"
        b"  End\n",
        1,
    )

    descriptor = compile_playable_unit_descriptor("AbilityHero", documents)

    validate_playable_unit_descriptor(descriptor)
    blast = _abilities_by_id(descriptor)["Command_FixtureBlast"]
    assert blast["implementation"]["status"] == "implemented"
    assert blast["effect"]["knockbackStrength"] == 70.0
    assert blast["effect"]["knockbackRadius"] == 110.0
    assert blast["effect"]["knockbackWeaponId"] == "FixtureHeroBlast"


def test_ambiguous_meta_impact_nuggets_record_a_limitation() -> None:
    documents = _hero_ability_documents()
    documents["data/ini/weapon.ini"] = documents["data/ini/weapon.ini"].replace(
        b"Weapon FixtureHeroBlast\n"
        b"  AttackRange = 110.0\n",
        b"Weapon FixtureHeroBlast\n"
        b"  AttackRange = 110.0\n"
        b"  MetaImpactNugget\n"
        b"    ShockWaveAmount = 70.0\n"
        b"    ShockWaveRadius = 110.0\n"
        b"  End\n"
        b"  MetaImpactNugget\n"
        b"    ShockWaveAmount = 20.0\n"
        b"    ShockWaveRadius = 30.0\n"
        b"  End\n",
        1,
    )

    descriptor = compile_playable_unit_descriptor("AbilityHero", documents)

    validate_playable_unit_descriptor(descriptor)
    blast = _abilities_by_id(descriptor)["Command_FixtureBlast"]
    assert "knockbackStrength" not in blast["effect"]
    assert any(
        "multiple MetaImpactNuggets" in item
        for item in blast["implementation"]["limitations"]
    )


def test_weapon_toggle_rows_record_the_authored_contract() -> None:
    documents = _hero_ability_documents()
    text = documents["data/ini/object/units/test_units.ini"].decode()
    documents["data/ini/object/units/test_units.ini"] = text.replace(
        "  WeaponSet\n"
        "    Conditions = None\n"
        "    Weapon = PRIMARY AbilityHeroSword\n"
        "  End\n",
        "  WeaponSet\n"
        "    Conditions = None\n"
        "    Weapon = PRIMARY AbilityHeroSword\n"
        "  End\n"
        "  WeaponSet\n"
        "    Conditions = WEAPONSET_TOGGLE_1\n"
        "    Weapon = PRIMARY FixtureToggleBow\n"
        "  End\n",
        1,
    ).encode()
    command_sets = documents["data/ini/commandset.ini"].decode()
    documents["data/ini/commandset.ini"] = command_sets.replace(
        "  9 = Command_FixtureBroken\nEnd",
        "  9 = Command_FixtureBroken\n  10 = Command_FixtureToggle\nEnd",
        1,
    ).encode()
    documents["data/ini/commandbutton.ini"] += (
        b"\nCommandButton Command_FixtureToggle\n"
        b"  Command = TOGGLE_WEAPONSET\n"
        b"  FlagsUsedForToggle = WEAPONSET_TOGGLE_1\n"
        b"  TextLabel = CONTROLBAR:FixtureToggle\n"
        b"  DescriptLabel = CONTROLBAR:ToolTipFixtureToggle\n"
        b"  ButtonImage = HSFixtureToggle\n"
        b"End\n"
    )
    documents["data/ini/weapon.ini"] += (
        b"\nWeapon FixtureToggleBow\n"
        b"  AttackRange = 320.0\n"
        b"  DamageNugget\n"
        b"    Damage = 90\n"
        b"    DamageType = PIERCE\n"
        b"  End\n"
        b"End\n"
    )

    descriptor = compile_playable_unit_descriptor("AbilityHero", documents)

    validate_playable_unit_descriptor(descriptor)
    toggle = _abilities_by_id(descriptor)["Command_FixtureToggle"]
    assert toggle["command"] == "TOGGLE_WEAPONSET"
    assert toggle["specialPowerId"] == ""
    assert toggle["implementation"]["status"] == "unimplemented"
    assert "runtime weapon-mode wiring" in toggle["implementation"]["reason"]
    assert toggle["effect"] == {"kind": "none"}
    evidence = toggle["weaponToggle"]
    assert evidence["toggleFlag"] == "WEAPONSET_TOGGLE_1"
    assert evidence["defaultWeaponId"] == "AbilityHeroSword"
    assert evidence["toggledWeaponId"] == "FixtureToggleBow"
    assert evidence["toggledWeapon"]["damage"] == 90
    assert evidence["toggledWeapon"]["attackRange"] == 320.0


def test_extended_timed_buff_modifier_kinds_compile() -> None:
    documents = _hero_ability_documents()
    documents["data/ini/attributemodifier.ini"] = (
        b"\nModifierList FixtureRage\n"
        b"  Category = SPELL\n"
        b"  Modifier = ARMOR 50%\n"
        b"  Modifier = DAMAGE_MULT 150%\n"
        b"  Modifier = VISION 200%\n"
        b"  Modifier = RESIST_FEAR 100%\n"
        b"  Modifier = CRUSH 150%\n"
        b"  Modifier = HEALTH 120%\n"
        b"  Modifier = CRUSH_DECELERATE 0%\n"
        b"  Duration = 20000\n"
        b"End\n"
    )

    descriptor = compile_playable_unit_descriptor("AbilityHero", documents)

    validate_playable_unit_descriptor(descriptor)
    rage = _abilities_by_id(descriptor)["Command_FixtureRage"]
    assert rage["implementation"]["status"] == "implemented"
    kinds = [row["kind"] for row in rage["effect"]["modifiers"]]
    assert kinds == [
        "ARMOR",
        "DAMAGE_MULT",
        "VISION",
        "RESIST_FEAR",
        "CRUSH",
        "HEALTH",
    ]
    assert rage["implementation"]["limitations"] == [
        "modifier kinds not applied by the runtime: CRUSH_DECELERATE"
    ]


def test_fear_resistance_flag_compiles_from_emotion_tracker() -> None:
    resistant = _hero_ability_documents()
    resistant["data/ini/emotions.ini"] = _FIXTURE_EMOTIONS
    _with_hero_modules(
        resistant,
        "  Behavior = EmotionTrackerUpdate ModuleTag_EmotionTracker\n"
        "    AddEmotion = Point_Base\n"
        "  End\n",
    )
    descriptor = compile_playable_unit_descriptor("AbilityHero", resistant)
    validate_playable_unit_descriptor(descriptor)
    flag = descriptor["gameplay"]["simulation"]["resolved"]["fearResistant"]
    assert flag["value"] is True
    assert flag["sourceIni"] == "data/ini/object/units/test_units.ini"

    fearful = _hero_ability_documents()
    fearful["data/ini/emotions.ini"] = _FIXTURE_EMOTIONS
    _with_hero_modules(
        fearful,
        "  Behavior = EmotionTrackerUpdate ModuleTag_EmotionTracker\n"
        "    AddEmotion = Point_Base\n"
        "    AddEmotion = OVERRIDE Terror_Base\n"
        "    End\n"
        "  End\n",
    )
    descriptor = compile_playable_unit_descriptor("AbilityHero", fearful)
    validate_playable_unit_descriptor(descriptor)
    flag = descriptor["gameplay"]["simulation"]["resolved"]["fearResistant"]
    assert flag["value"] is False

    absent = _hero_ability_documents()
    absent["data/ini/emotions.ini"] = _FIXTURE_EMOTIONS
    descriptor = compile_playable_unit_descriptor("AbilityHero", absent)
    validate_playable_unit_descriptor(descriptor)
    assert "fearResistant" not in descriptor["gameplay"]["simulation"]["resolved"]

    no_emotions = _hero_ability_documents()
    _with_hero_modules(
        no_emotions,
        "  Behavior = EmotionTrackerUpdate ModuleTag_EmotionTracker\n"
        "    AddEmotion = Point_Base\n"
        "  End\n",
    )
    descriptor = compile_playable_unit_descriptor("AbilityHero", no_emotions)
    validate_playable_unit_descriptor(descriptor)
    assert "fearResistant" not in descriptor["gameplay"]["simulation"]["resolved"]
