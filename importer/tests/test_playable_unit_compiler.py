from __future__ import annotations

from copy import deepcopy
import hashlib
import json

import pytest

from openbfme_importer.playable_unit_compiler import (
    PlayableUnitCompilerError,
    compile_playable_unit_descriptor,
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
        "  Behavior = RespawnUpdate ModuleTag_Respawn\n"
        "    DeathAnim = DYING\n"
        "  End\n"
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
        "InfantryHorde", "HORDE", "InfantryHordeModel", payload="InfantryMember #MULTIPLY( GOOD_HORDE_SIZE 1 )"
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
        image = "BIInfantryAlternate" if command.endswith("Alternate") else f"BI{target}"
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
    repeated = compile_playable_unit_descriptor(target, dict(reversed(documents.items())))

    validate_playable_unit_descriptor(result)
    assert result == repeated
    assert result["category"] == category
    assert result["composition"]["members"][0]["count"] == member_count
    assert result["production"][0]["slot"] >= 1
    assert result["presentation"]["ui"]["commands"][0]["fields"]["ButtonImage"]
    assert result["presentation"]["audioRoutes"]["container"]["VoiceSelect"]
    assert result["presentation"]["audioRoutes"]["primaryMember"]["VoiceSelect"]
    assert len(result["descriptorSha256"]) == 64


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
    assert changed["runtimeModuleEvidence"][0]["semanticSha256"] != (
        baseline["runtimeModuleEvidence"][0]["semanticSha256"]
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
def test_validation_rejects_other_rehashed_missing_subtrees(mutation, message: str) -> None:
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

    with pytest.raises(
        PlayableUnitCompilerError, match="reference collection"
    ):
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

    with pytest.raises(
        PlayableUnitCompilerError, match="runtime modules disagree"
    ):
        validate_playable_unit_descriptor(corrupted)
