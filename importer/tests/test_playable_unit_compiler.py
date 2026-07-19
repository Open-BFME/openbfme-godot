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
