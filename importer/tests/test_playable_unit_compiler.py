from __future__ import annotations

from copy import deepcopy
import hashlib
import json

import pytest

from openbfme_importer.catalog import InstallCatalog
from openbfme_importer.module_census import census_catalog_paths, read_catalog_documents
from openbfme_importer.playable_unit_compiler import (
    PlayableUnitCompilerError,
    _ancestry,
    _apply_nugget_damage_types,
    _base_weapon_damage,
    _audio_routes,
    _default_set_target,
    _numeric_defines,
    _object_index,
    _permanent_weapon_locks,
    _hero_ability_effect,
    compile_playable_unit_descriptor,
    playable_object_kind_of,
    prepare_playable_unit_compiler,
    validate_playable_unit_descriptor,
)
from openbfme_importer.sage_cst import parse_sage_document


_RETAIL_CATALOGS = census_catalog_paths()


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
        "    AutoRespawnAtObjectFilter = NONE +CASTLE_KEEP\n"
        "    ButtonImage = HIFixtureRespawn\n"
        "    RespawnRules = AutoSpawn:No Cost:500 Time:60000 Health:100%\n"
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


def test_graphless_audio_routes_classify_silence_eva_and_additive_sound() -> None:
    documents = _documents()
    path = "data/ini/object/units/test_units.ini"
    marker = b"Object InfantryHorde\n"
    documents[path] = documents[path].replace(
        marker,
        marker
        + b"  VoiceCreated = EVA:InfantryCreated\n"
        + b"  VoiceCreated = +SOUND:InfantryCreatedSound\n"
        + b"  VoiceFear = NoSound\n",
        1,
    )

    result = compile_playable_unit_descriptor("InfantryHorde", documents)
    routes = result["presentation"]["audioRoutes"]["container"]

    assert [row["id"] for row in routes["VoiceCreated"]] == [
        "InfantryCreatedSound"
    ]
    assert "VoiceFear" not in routes
    assert all(
        row["id"] not in {"NoSound", "EVA", "+SOUND", "SOUND"}
        for rows in routes.values()
        for row in rows
    )


def _shroud_documents() -> dict[str, bytes]:
    """`_documents()` with a ShroudClearingRange injected into two objects.

    A SEPARATE corpus on purpose. The first attempt added the field to the
    shared `_object()` template, and even for objects that authored nothing the
    template left a blank line where the value would go - which shifted every
    later object's provenance LINE NUMBERS by one, changed every descriptor
    digest, and broke `test_playable_unit_death_model`'s pinned recipe hashes.
    Digests here are pinned by other modules; this corpus is used only by the
    two tests below, so it cannot move any of them.

    The values are the retail shape: the MEMBER authors SHROUD_CLEAR_STANDARD
    (25) so horde members do not each deshroud, and the real radius lives on the
    horde CONTAINER (800, against a VisionRange of 300 so no test can pass by
    reading the wrong field).
    """
    documents = dict(_documents())
    key = "data/ini/object/units/test_units.ini"
    lines = documents[key].decode("utf-8").splitlines(keepends=True)
    wanted = {"InfantryMember": 25, "InfantryHorde": 800}
    output: list[str] = []
    current: str | None = None
    for line in lines:
        stripped = line.strip()
        if stripped.startswith("Object "):
            current = stripped.split()[1]
        output.append(line)
        if (
            current in wanted
            and stripped.startswith("VisionRange")
        ):
            output.append("  ShroudClearingRange = %d\n" % wanted[current])
            current = None
    documents[key] = "".join(output).encode("utf-8")
    return documents


def test_shroud_clearing_range_is_compiled_off_the_horde_container() -> None:
    """The deshroud radius comes off the CONTAINER, never the member.

    The fixture authors VisionRange 300 on both, ShroudClearingRange 25 on the
    member and 800 on the horde - the retail shape, where
    SHROUD_CLEAR_STANDARD (25) exists so horde members do not each deshroud and
    the real radius sits on the parent (GondorFighter 25 versus
    GondorFighterHorde 400).

    Three distinct ways to get this wrong, and each is asserted against:
    reading the member (25), deriving it from VisionRange (300), and defaulting
    it when unauthored.
    """
    documents = _shroud_documents()
    result = compile_playable_unit_descriptor("InfantryHorde", documents)
    resolved = result["gameplay"]["simulation"]["resolved"]
    assert resolved["shroudClearingRange"]["value"] == 800
    assert resolved["visionRange"]["value"] == 300
    assert resolved["shroudClearingRange"]["value"] != resolved["visionRange"]["value"]


def test_an_object_with_no_shroud_clearing_range_compiles_without_the_key() -> None:
    """Absent stays absent - it is not defaulted to 0 or to VisionRange.

    352 shipped objects author VisionRange only, and Carn Dum's map.ini authors
    an explicit ShroudClearingRange of 0 for nine props. A defaulted value would
    make those two cases indistinguishable downstream, and it would add a key to
    the runtime's hashed entity row.
    """
    documents = _shroud_documents()
    # HeroUnit authors no ShroudClearingRange even in the injected corpus.
    result = compile_playable_unit_descriptor("HeroUnit", documents)
    resolved = result["gameplay"]["simulation"]["resolved"]
    assert "shroudClearingRange" not in resolved
    assert resolved["visionRange"]["value"] == 300


def test_bounty_value_is_resolved_from_retail_define_without_defaulting_absent() -> None:
    documents = dict(_documents())
    key = "data/ini/object/units/test_units.ini"
    text = documents[key].decode("utf-8")
    marker = "Object MonsterUnit\n"
    assert marker in text
    documents[key] = text.replace(
        marker, marker + "  BountyValue = MONSTER_BOUNTY_VALUE\n", 1
    ).encode("utf-8")
    documents["data/ini/gamedata.ini"] += b"#define MONSTER_BOUNTY_VALUE 75\n"
    monster = compile_playable_unit_descriptor("MonsterUnit", documents)
    bounty = monster["gameplay"]["simulation"]["resolved"]["bountyValue"]
    assert bounty["value"] == 75
    assert bounty["expression"] == "MONSTER_BOUNTY_VALUE"
    hero = compile_playable_unit_descriptor("HeroUnit", documents)
    assert "bountyValue" not in hero["gameplay"]["simulation"]["resolved"]


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


def test_duplicate_child_object_resolves_last_declaration_wins() -> None:
    # RotWK re-declares objects via a repeated ``ChildObject <Name> <Parent>``
    # incremental-override.  Retail SAGE resolves this last-declaration-wins:
    # ThingFactory::parseObjectDefinition re-enters the reskin path and calls
    # copyFrom(parent) (a full ``*this = *that`` reset) before applying the new
    # body, so the final declaration re-inherits from its stated parent and
    # supersedes the earlier one.  The object index must resolve to the LAST
    # declaration rather than raising or keeping the first.
    documents = _documents()
    documents["data/ini/object/neutral/dup.ini"] = b"""
Object DuplicateBase
  KindOf = STRUCTURE
End
ChildObject DuplicateChild DuplicateBase
  KindOf = +FIRST_DECL_ONLY
End
ChildObject DuplicateChild DuplicateBase
  KindOf = +SECOND_DECL_ONLY
End
"""

    prepared = prepare_playable_unit_compiler(documents)

    # Last declaration wins: its body (SECOND_DECL_ONLY) is effective and the
    # first declaration's body (FIRST_DECL_ONLY) is fully superseded.
    assert playable_object_kind_of(prepared, "DuplicateChild") == (
        "SECOND_DECL_ONLY",
        "STRUCTURE",
    )
    # The retained definition is the second one (line 8 of this fragment).
    assert prepared.objects["duplicatechild"].line == 8


def test_non_duplicate_corpus_object_index_is_unaffected() -> None:
    # A corpus with no duplicate object names must be indexed exactly as before
    # the last-wins tolerance was added: every distinct name is kept, none is
    # dropped or merged.  This guards BFME2 1.06 (which ships no duplicate object
    # names) against any behavioural drift from the duplicate-tolerance change.
    documents = _documents()

    prepared = prepare_playable_unit_compiler(documents)

    assert set(prepared.objects) == {
        "universalfactory",
        "upgradingfactory",
        "alternatefactory",
        "infantrymember",
        "infantryhorde",
        "rangedmember",
        "rangedhorde",
        "cavalrymember",
        "cavalryhorde",
        "herounit",
        "siegeunit",
        "monsterunit",
        "navalunit",
        "replacementmember",
        "parenthorde",
        "childhorde",
    }


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


def _ranged_button_documents(*, any_flag: bool) -> dict[str, bytes]:
    """RangedHorde's UNIT_BUILD button gated by two NeededUpgrade tokens.

    Mirrors commandbutton.ini:7513-7519 (Command_ConstructGondorRangerHorde),
    which names two NeededUpgrade tokens and sets NeededUpgradeAny = Yes.
    """

    documents = _documents()
    extra = "  NeededUpgrade = Upgrade_RangedGateA Upgrade_RangedGateB\n"
    if any_flag:
        extra += "  NeededUpgradeAny = Yes\n"
    documents["data/ini/commandbutton.ini"] = documents[
        "data/ini/commandbutton.ini"
    ].replace(
        b"CommandButton Command_BuildRanged\n  Command = UNIT_BUILD\n",
        b"CommandButton Command_BuildRanged\n  Command = UNIT_BUILD\n"
        + extra.encode("utf-8"),
    )
    return documents


def _angmar_style_button_documents() -> dict[str, bytes]:
    """A gate whose upgrade id has NO underscore after "Upgrade".

    This is not a contrived shape. Pure RotWK 2.01 authors exactly FOUR
    upgrades whose id lacks that underscore, and all four are the Angmar
    structure-level upgrades that gate Angmar's tier-2/tier-3 units:
    `UpgradeAngmarBarracksLevel2`, `UpgradeAngmarBarracksLevel3`,
    `UpgradeAngmarDenLevel2`, `UpgradeAngmarDenLevel3` (501 other ids do use the
    `Upgrade_` form). e.g. commandbutton.ini:15133-15137

        CommandButton Command_ConstructAngmarDarkDunedainHorde
            Options          = NEED_UPGRADE CANCELABLE
            NeededUpgrade    = UpgradeAngmarBarracksLevel2
            NeededUpgradeAny = Yes
    """

    documents = _documents()
    extra = (
        "  Options = NEED_UPGRADE CANCELABLE\n"
        "  NeededUpgrade = UpgradeAngmarBarracksLevel2\n"
        "  NeededUpgradeAny = Yes\n"
    )
    documents["data/ini/commandbutton.ini"] = documents[
        "data/ini/commandbutton.ini"
    ].replace(
        b"CommandButton Command_BuildRanged\n  Command = UNIT_BUILD\n",
        b"CommandButton Command_BuildRanged\n  Command = UNIT_BUILD\n"
        + extra.encode("utf-8"),
    )
    return documents


def test_upgrade_token_without_underscore_still_gates_production() -> None:
    """Regression: the `Upgrade_` prefix filter silently ungated 4 Angmar units.

    The token filter required `Upgrade_`, so `UpgradeAngmarBarracksLevel2` was
    discarded. The consequence was NOT a missing ANY-of group only - the base
    command set's `prerequisites` also came out empty, so the units shipped
    buildable from a level-1 structure with nothing owned.
    """

    result = compile_playable_unit_descriptor(
        "RangedHorde", _angmar_style_button_documents()
    )

    validate_playable_unit_descriptor(result)
    production = result["production"][0]
    assert production["prerequisiteAnyOf"] == ["UpgradeAngmarBarracksLevel2"]


def test_needed_upgrade_any_compiles_to_an_any_of_production_gate() -> None:
    result = compile_playable_unit_descriptor(
        "RangedHorde", _ranged_button_documents(any_flag=True)
    )

    validate_playable_unit_descriptor(result)
    production = result["production"][0]
    # The commandSetTransition requirement stays ALL-of; only the button's
    # NeededUpgrade set becomes the ANY-of group.
    assert production["prerequisites"] == ["Upgrade_FactoryLevel2"]
    assert production["prerequisiteAnyOf"] == [
        "Upgrade_RangedGateA",
        "Upgrade_RangedGateB",
    ]


def test_any_of_group_may_contain_the_command_set_transition_upgrade() -> None:
    """The ANY-of group legitimately OVERLAPS the ALL-of set.

    This is the real retail shape, not a contrived one.
    commandbutton.ini:7513-7519 authors
    `NeededUpgrade = Upgrade_GondorArcheryRangeLevel2 Upgrade_CustomGenericUpgrade1`
    with `NeededUpgradeAny = Yes`, while
    object/goodfaction/structures/men/archerrange.ini:418 makes that SAME
    `Upgrade_GondorArcheryRangeLevel2` the CommandSetUpgrade trigger. So the
    token is both an ALL-of requirement (the producer must sit on the upgraded
    CommandSet at all) and a member of the ANY-of group. Requiring the two sets
    to be disjoint rejected six real retail units — GondorRanger(Horde),
    GondorTowerShieldGuard(Horde) and RohanRohirrim(Horde) — and silently
    dropped them from the published pack.
    """

    documents = _documents()
    documents["data/ini/commandbutton.ini"] = documents[
        "data/ini/commandbutton.ini"
    ].replace(
        b"CommandButton Command_BuildRanged\n  Command = UNIT_BUILD\n",
        b"CommandButton Command_BuildRanged\n  Command = UNIT_BUILD\n"
        b"  NeededUpgrade = Upgrade_FactoryLevel2 Upgrade_RangedGateB\n"
        b"  NeededUpgradeAny = Yes\n",
    )

    result = compile_playable_unit_descriptor("RangedHorde", documents)

    validate_playable_unit_descriptor(result)
    production = result["production"][0]
    assert production["prerequisites"] == ["Upgrade_FactoryLevel2"]
    assert production["prerequisiteAnyOf"] == [
        "Upgrade_FactoryLevel2",
        "Upgrade_RangedGateB",
    ]


def test_without_needed_upgrade_any_the_gate_stays_all_of() -> None:
    result = compile_playable_unit_descriptor(
        "RangedHorde", _ranged_button_documents(any_flag=False)
    )

    validate_playable_unit_descriptor(result)
    production = result["production"][0]
    assert production["prerequisites"] == [
        "Upgrade_FactoryLevel2",
        "Upgrade_RangedGateA",
        "Upgrade_RangedGateB",
    ]
    assert "prerequisiteAnyOf" not in production


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


def test_schema_audio_routes_never_treat_numeric_voice_priority_as_event() -> None:
    prepared = prepare_playable_unit_compiler(_documents())
    member = prepared.objects["infantrymember"]

    routes = _audio_routes(_ancestry(prepared.objects, member))

    assert "VoicePriority" not in routes
    assert all(
        row["id"] != "43"
        for owner_rows in routes.values()
        for row in owner_rows
    )
    assert routes["VoiceSelect"][0]["id"] == "InfantryMemberVoiceSelect"


def test_typed_closed_slow_death_is_executable_contract_not_unsupported_extension() -> None:
    documents = _documents()
    key = "data/ini/object/units/test_units.ini"
    documents[key] = documents[key].replace(
        b"Object MonsterUnit\n",
        b"Object MonsterUnit\n"
        b"  Behavior = SlowDeathBehavior ModuleTag_UnmappedDeath\n"
        b"    DeathTypes = ALL\n"
        b"  End\n",
        1,
    )

    result = compile_playable_unit_descriptor("MonsterUnit", documents)

    assert "RespawnUpdate" not in result["specialCapabilities"]
    assert "SlowDeathBehavior" not in result["specialCapabilities"]
    assert result["unsupportedCapabilities"] == []
    contract = next(
        row for row in result["gameplay"]["simulation"]["resolved"]["moduleContracts"]
        if row["module"] == "SlowDeathBehavior"
    )
    assert contract["runtimeStatus"] == "executable"
    assert contract["fields"]["deathTypes"] == "ALL"
    assert contract["effectGraph"]["executionEligibility"] == {
        "status": "evidence-closed-core",
        "blockers": [],
        "runtimeStatus": "executable",
    }


def test_nonshipping_special_power_contracts_stay_deferred_on_mod_objects() -> None:
    documents = _documents()
    key = "data/ini/object/units/test_units.ini"
    documents[key] = documents[key].replace(
        b"Object MonsterUnit\n",
        b"Object MonsterUnit\n"
        b"  Behavior = DeflectSpecialPower ModuleTag_ModDeflect\n"
        b"    SpecialPowerTemplate = SpecialAbilityModDeflect\n"
        b"  End\n"
        b"  Behavior = SplitHordeSpecialPower ModuleTag_ModSplit\n"
        b"    SpecialPowerTemplate = SpecialAbilityModSplit\n"
        b"  End\n",
        1,
    )

    descriptor = compile_playable_unit_descriptor("MonsterUnit", documents)
    validate_playable_unit_descriptor(descriptor)
    contracts = {
        row["module"]: row
        for row in descriptor["gameplay"]["simulation"]["resolved"][
            "moduleContracts"
        ]
        if row["module"] in {"DeflectSpecialPower", "SplitHordeSpecialPower"}
    }
    assert set(contracts) == {"DeflectSpecialPower", "SplitHordeSpecialPower"}
    for row in contracts.values():
        assert row["runtimeStatus"] == "deferred"
        assert row["effectGraph"]["subclassFields"] == []
        assert row["effectGraph"]["executionEligibility"] == {
            "runtimeStatus": "deferred",
            "shippingAdmission": False,
            "retailOwnerMatch": False,
            "disposition": "unadmitted-owner",
        }
    assert "DeflectSpecialPower" not in descriptor["specialCapabilities"]
    assert "SplitHordeSpecialPower" not in descriptor["specialCapabilities"]


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
    assert any(
        row["id"].endswith(":CommandSetUpgrade:ModuleTag_HeroLevel")
        for row in result["unsupportedCapabilities"]
    )


def test_typed_unused_horde_module_is_not_duplicated_as_unsupported() -> None:
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
    assert not any(
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


def test_authored_banner_dependency_uses_engine_spawn_surface() -> None:
    documents = _documents()
    documents["data/ini/object/units/test_units.ini"] += _object(
        "BannerDireSnowTroll", "INFANTRY", "BannerDireSnowTrollModel"
    ).encode("utf-8")

    descriptor = compile_playable_unit_descriptor(
        "BannerDireSnowTroll",
        documents,
        engine_spawned_banner_carrier=True,
    )

    assert descriptor["production"][0]["surface"] == "banner-carrier"
    assert descriptor["production"][0]["sourceField"] == "BannerCarriersAllowed"
    assert descriptor["production"][0]["evidence"] == "banner-carriers-allowed-edge"


def test_banner_name_without_kindof_or_edge_does_not_invent_producer() -> None:
    documents = _documents()
    documents["data/ini/object/units/test_units.ini"] += _object(
        "SuspiciousBanner", "INFANTRY", "SuspiciousBannerModel"
    ).encode("utf-8")
    with pytest.raises(
        PlayableUnitCompilerError,
        match="not targeted by an authored UNIT_BUILD command",
    ):
        compile_playable_unit_descriptor("SuspiciousBanner", documents)


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


def test_validation_rejects_rehashed_typed_contract_identity_tamper() -> None:
    corrupted = compile_playable_unit_descriptor("MonsterUnit", _documents())
    contracts = corrupted["gameplay"]["simulation"]["resolved"]["moduleContracts"]
    respawn = next(row for row in contracts if row["module"] == "RespawnUpdate")
    respawn["tag"] = "ModuleTag_Tampered"
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
        PlayableUnitCompilerError,
        match="typed module contracts disagree with runtime module evidence",
    ):
        validate_playable_unit_descriptor(corrupted)


def test_validation_rejects_rehashed_typed_module_in_unsupported_capabilities() -> None:
    corrupted = compile_playable_unit_descriptor("MonsterUnit", _documents())
    evidence = next(
        row
        for row in corrupted["runtimeModuleEvidence"]
        if row["kind"] == "RespawnUpdate"
    )
    corrupted["unsupportedCapabilities"] = [
        {
            "id": "module:container:RespawnUpdate:ModuleTag_Respawn",
            "reason": "authored Behavior is not consumed by the shared runtime adapter",
            "semanticSha256": evidence["semanticSha256"],
        }
    ]
    corrupted["specialCapabilities"] = ["RespawnUpdate"]
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
        PlayableUnitCompilerError,
        match="unsupported modules disagree with special capabilities",
    ):
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
  NeededUpgrade = Upgrade_RingHero Upgrade_FortressRingHero
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
    assert route["prerequisites"] == [
        "Upgrade_RingHero",
        "Upgrade_FortressRingHero",
    ]
    # The ring slot follows the eight authored hero roster slots.
    assert route["rosterOrdinal"] == 9
    assert "slot" not in route
    assert route["ui"] == {
        "ButtonImage": ["HIHeroRing"],
        "TextLabel": ["CONTROLBAR:GenericReviveHero"],
        "DescriptLabel": ["CONTROLBAR:ToolTipGenericReviveHero"],
    }


def test_regular_hero_roster_route_does_not_inherit_ring_prerequisites() -> None:
    documents, graph = _hero_roster_fixture()

    result = compile_playable_unit_descriptor("HeroSeven", documents, faction_graph=graph)

    assert result["production"][0]["commandSetId"] == "__engine__/BuildableHeroesMP"
    assert result["production"][0]["prerequisites"] == []


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


def _weapon_audio_documents() -> dict[str, bytes]:
    ## Mirrors the retail chain `Weapon BoromirSword / FireFX =
    ## FX_GondorSwordHit` (weapon.ini:5616-5624) -> `FXList FX_GondorSwordHit
    ## / Sound / Name = ImpactSword01` (fxlist.ini:7584-7586).
    command_row, button_row = _combat_command("Swordsman", 8, "Swordsman")
    documents = _combat_documents(
        _combat_object(
            "Swordsman",
            "INFANTRY",
            "  WeaponSet\n"
            "    Conditions = None\n"
            "    Weapon = PRIMARY SwordsmanSword\n"
            "    Weapon = SECONDARY SwordsmanBow\n"
            "  End\n",
        ),
        "Weapon SwordsmanSword\n"
        "  MeleeWeapon = Yes\n"
        "  AttackRange = 20.0\n"
        "  DelayBetweenShots = 1000\n"
        "  FireFX = FX_SwordHit\n"
        "  DamageNugget\n"
        "    Damage = 40\n"
        "    DamageType = SLASH\n"
        "  End\n"
        "End\n"
        "Weapon SwordsmanBow\n"
        "  AttackRange = 300.0\n"
        "  FireFX = FX_BowRelease\n"
        "  ProjectileDetonationFX = FX_ArrowHit\n"
        "  DamageNugget\n"
        "    Damage = 30\n"
        "    DamageType = PIERCE\n"
        "  End\n"
        "End\n",
        command_row,
        button_row,
    )
    documents["data/ini/fxlist.ini"] = (
        b"FXList FX_SwordHit\n"
        b"  Sound\n"
        b"    Name = ImpactSwordFx\n"
        b"  End\n"
        b"End\n"
        b"FXList FX_ArrowHit\n"
        b"  Sound\n"
        b"    Name = ImpactArrowFx\n"
        b"  End\n"
        b"End\n"
        b"FXList FX_BowRelease\n"
        b"  ParticleSystem\n"
        b"    Name = BowFlare\n"
        b"  End\n"
        b"End\n"
    )
    return documents


def test_weapon_fire_fx_sound_chain_is_emitted_as_weapon_audio_routes() -> None:
    documents = _weapon_audio_documents()

    descriptor = compile_playable_unit_descriptor("Swordsman", documents)

    validate_playable_unit_descriptor(descriptor)
    routes = descriptor["presentation"]["audioRoutes"]["weapon"]
    (sword_row,) = [
        row for row in routes["FireFX"] if row["weaponId"] == "SwordsmanSword"
    ]
    assert sword_row["id"] == "ImpactSwordFx"
    assert sword_row["ownerRole"] == "object"
    assert sword_row["weaponSlot"] == "PRIMARY"
    assert sword_row["defaultSet"] is True
    assert sword_row["fxListId"] == "FX_SwordHit"
    assert sword_row["sourceIni"] == "data/ini/weapon.ini"
    assert isinstance(sword_row["line"], int) and sword_row["line"] > 0
    assert sword_row["fxSourceIni"] == "data/ini/fxlist.ini"
    assert isinstance(sword_row["fxLine"], int) and sword_row["fxLine"] > 0
    (arrow_row,) = routes["ProjectileDetonationFX"]
    assert arrow_row["id"] == "ImpactArrowFx"
    assert arrow_row["weaponId"] == "SwordsmanBow"
    assert arrow_row["weaponSlot"] == "SECONDARY"
    # FX_BowRelease authors no Sound: recorded as an authored-silent gap, not
    # dropped and not invented.
    gaps = descriptor["presentation"]["weaponAudioGaps"]
    (bow_gap,) = [
        row for row in gaps if row.get("fxListId") == "FX_BowRelease"
    ]
    assert bow_gap["reason"] == "fxlist-authors-no-sound"
    assert bow_gap["weaponId"] == "SwordsmanBow"


def test_weapon_audio_without_fxlist_document_records_the_gap() -> None:
    documents = _weapon_audio_documents()
    del documents["data/ini/fxlist.ini"]

    descriptor = compile_playable_unit_descriptor("Swordsman", documents)

    validate_playable_unit_descriptor(descriptor)
    assert descriptor["presentation"]["audioRoutes"]["weapon"] == {}
    gaps = descriptor["presentation"]["weaponAudioGaps"]
    assert gaps and all(
        row["reason"] == "fxlist-document-not-in-view" for row in gaps
    )
    assert {row["fxListId"] for row in gaps} == {
        "FX_SwordHit",
        "FX_BowRelease",
        "FX_ArrowHit",
    }


def test_weapon_audio_missing_definitions_record_gaps() -> None:
    documents = _weapon_audio_documents()
    units_path = "data/ini/object/units/test_units.ini"
    documents[units_path] = documents[units_path].replace(
        b"    Weapon = SECONDARY SwordsmanBow\n",
        b"    Weapon = SECONDARY SwordsmanBow\n"
        b"    Weapon = TERTIARY GhostWeapon\n",
        1,
    )
    documents["data/ini/weapon.ini"] += (
        b"Weapon GhostWeapon\n"
        b"  AttackRange = 10.0\n"
        b"  FireFX = FX_NeverAuthored\n"
        b"  DamageNugget\n"
        b"    Damage = 1\n"
        b"    DamageType = SLASH\n"
        b"  End\n"
        b"End\n"
    )

    descriptor = compile_playable_unit_descriptor("Swordsman", documents)

    validate_playable_unit_descriptor(descriptor)
    gaps = descriptor["presentation"]["weaponAudioGaps"]
    (missing_fx,) = [
        row for row in gaps if row.get("fxListId") == "FX_NeverAuthored"
    ]
    assert missing_fx["reason"] == "fxlist-definition-missing"
    assert missing_fx["weaponId"] == "GhostWeapon"
    # The resolved chains still emit alongside the recorded gap.
    routes = descriptor["presentation"]["audioRoutes"]["weapon"]
    assert {row["id"] for row in routes["FireFX"]} == {"ImpactSwordFx"}


def test_weapon_audio_route_rows_fail_validation_when_tampered() -> None:
    descriptor = compile_playable_unit_descriptor(
        "Swordsman", _weapon_audio_documents()
    )
    tampered = deepcopy(descriptor)
    tampered["presentation"]["audioRoutes"]["weapon"]["FireFX"][0].pop(
        "weaponId"
    )
    tampered.pop("descriptorSha256")
    tampered["descriptorSha256"] = hashlib.sha256(
        json.dumps(
            tampered,
            sort_keys=True,
            separators=(",", ":"),
            ensure_ascii=False,
        ).encode("utf-8")
    ).hexdigest()
    with pytest.raises(PlayableUnitCompilerError, match="weapon audio route"):
        validate_playable_unit_descriptor(tampered)


def test_slow_death_destruction_delay_is_recorded_per_object() -> None:
    ## Retail authors the per-object FADED fade window as SlowDeathBehavior
    ## DestructionDelay (pure retail range 1000..10000 ms, e.g.
    ## gwaihir.ini:453-460 = 2500). It used to be dropped silently, so the
    ## simulation saw 0 and erased the row in the same tick. A module with no
    ## authored delay stays recorded as absent, never defaulted to 0.
    documents = _documents()
    units_path = "data/ini/object/units/test_units.ini"
    documents[units_path] = documents[units_path].decode().replace(
        "Object MonsterUnit\n",
        "Object MonsterUnit\n"
        "  Behavior = SlowDeathBehavior ModuleTag_FadeDeath\n"
        "    DeathTypes = NONE +FADED\n"
        "    DestructionDelay = MONSTER_FADE_DELAY\n"
        "  End\n"
        "  Behavior = SlowDeathBehavior ModuleTag_Corpse\n"
        "    DeathTypes = ALL -FADED\n"
        "  End\n",
        1,
    ).encode()
    documents["data/ini/gamedata.ini"] += b"#define MONSTER_FADE_DELAY 2500\n"

    descriptor = compile_playable_unit_descriptor("MonsterUnit", documents)

    validate_playable_unit_descriptor(descriptor)
    fade, corpse = descriptor["gameplay"]["simulation"]["resolved"]["slowDeaths"]
    assert fade["ownerRole"] == "object"
    assert fade["module"] == "SlowDeathBehavior"
    assert fade["moduleTag"] == "ModuleTag_FadeDeath"
    assert fade["deathTypes"] == ["NONE", "+FADED"]
    assert fade["destructionDelayAuthored"] is True
    assert fade["destructionDelayMs"] == 2500
    assert fade["sourceIni"] == units_path
    assert isinstance(fade["line"], int) and fade["line"] > 0
    assert corpse["moduleTag"] == "ModuleTag_Corpse"
    assert corpse["deathTypes"] == ["ALL", "-FADED"]
    assert corpse["destructionDelayAuthored"] is False
    assert "destructionDelayMs" not in corpse
    # Evidence only: recording the authored window does NOT claim the shared
    # runtime adapter consumes the module.
    evidence = [
        row
        for row in descriptor["runtimeModuleEvidence"]
        if row["kind"] == "SlowDeathBehavior"
    ]
    assert evidence and all(row["consumed"] is False for row in evidence)


def test_unauthored_slow_death_emits_no_rows() -> None:
    descriptor = compile_playable_unit_descriptor("MonsterUnit", _documents())
    assert "slowDeaths" not in descriptor["gameplay"]["simulation"]["resolved"]


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
  PublicTimer = Yes
  SharedSyncedTimer = No
  ObjectFilter = ANY +HERO -STRUCTURE
  ForbiddenObjectFilter = ANY +MACHINE
  ForbiddenObjectRange = 75
  ViewObjectRange = 300
  ViewObjectDuration = 5000
  MaxCastRange = 450
  UnitCost = 2
  UnitCostDeathType = NORMAL CRUSHED
  PreventActivationConditions = MOVING FIRING_A
  UnitSpecificSoundToUseAsInitiateIntendToDoVoice = FixtureIntent
  UnitSpecificSoundToUseAsEnterStateInitiateIntendToDoVoice = FixtureEnter
  EvaEventToPlayOnSuccess = FixtureSuccess
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
  Flags = LIMIT_DISTANCE NO_FORBIDDEN_OBJECTS
  MaxCastRange = 200
  ForbiddenObjectFilter = NO_SUMMON_NEAR_OBJECT_FILTER
  ForbiddenObjectRange = 60
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


def _move_fixture_rage_power_to_cah(documents: dict[str, bytes]) -> None:
    block = (
        b"SpecialPower SpecialAbilityFixtureRage\n"
        b"  Enum = SPECIAL_HERO_MODE\n"
        b"  ReloadTime = 45000\n"
        b"End\n"
    )
    assert documents["data/ini/specialpower.ini"].count(block) == 1
    documents["data/ini/specialpower.ini"] = documents[
        "data/ini/specialpower.ini"
    ].replace(block, b"", 1)
    documents["data/ini/createaherospecialpowers.ini"] = block.replace(
        b"End\n", b"  PublicTimer = No\nEnd\n", 1
    )


def test_normal_hero_resolves_only_its_referenced_cah_special_power() -> None:
    documents = _hero_ability_documents()
    _move_fixture_rage_power_to_cah(documents)
    baseline = compile_playable_unit_descriptor("AbilityHero", documents)
    # A same-name CaH declaration must not override a primary SpecialPower.
    documents["data/ini/createaherospecialpowers.ini"] += b"""
SpecialPower SpecialAbilityFixtureHeal
  Enum = SPECIAL_ATHELAS
  ReloadTime = 1
End
"""

    descriptor = compile_playable_unit_descriptor("AbilityHero", documents)

    validate_playable_unit_descriptor(descriptor)
    abilities = _abilities_by_id(descriptor)
    rage = abilities["Command_FixtureRage"]
    assert rage["implementation"]["status"] == "implemented"
    assert rage["cooldownMs"] == 45000
    assert rage["specialPowerContract"]["sourceIni"] == (
        "data/ini/createaherospecialpowers.ini"
    )
    assert abilities["Command_FixtureHeal"]["cooldownMs"] == 90000
    baseline_sources = {
        row["virtualPath"]: row["semanticSha256"]
        for row in baseline["sourceDocuments"]
    }
    actual_sources = {
        row["virtualPath"]: row["semanticSha256"]
        for row in descriptor["sourceDocuments"]
    }
    assert "data/ini/createaherospecialpowers.ini" in actual_sources
    assert actual_sources == baseline_sources


def test_referenced_cah_special_power_still_fails_closed_on_unsupported_fields() -> None:
    documents = _hero_ability_documents()
    _move_fixture_rage_power_to_cah(documents)
    documents["data/ini/createaherospecialpowers.ini"] = documents[
        "data/ini/createaherospecialpowers.ini"
    ].replace(b"  ReloadTime = 45000\n", b"  InventedField = 1\n", 1)

    descriptor = compile_playable_unit_descriptor("AbilityHero", documents)

    rage = _abilities_by_id(descriptor)["Command_FixtureRage"]
    assert rage["implementation"]["status"] == "unimplemented"
    assert "unsupported fields: inventedfield" in rage["implementation"]["reason"]
    assert rage["effect"] == {"kind": "none"}


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
    assert blast["specialPowerContract"] == {
        "publicTimer": True,
        "sharedSyncedTimer": False,
        "objectFilter": ["ANY", "+HERO", "-STRUCTURE"],
        "forbiddenObjectFilter": ["ANY", "+MACHINE"],
        "preventActivationConditions": ["MOVING", "FIRING_A"],
        "unitCostDeathTypes": ["NORMAL", "CRUSHED"],
        "forbiddenObjectRange": 75,
        "viewObjectRange": 300,
        "viewObjectDurationMs": 5000,
        "maxCastRange": 450,
        "unitCost": 2,
        "initiateIntentSoundId": "FixtureIntent",
        "enterStateIntentSoundId": "FixtureEnter",
        "successEvaEventId": "FixtureSuccess",
        "sourceIni": "data/ini/specialpower.ini",
    }
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
    assert summon["specialPowerContract"] == {
        "flags": ["LIMIT_DISTANCE", "NO_FORBIDDEN_OBJECTS"],
        "forbiddenObjectFilter": ["NO_SUMMON_NEAR_OBJECT_FILTER"],
        "forbiddenObjectRange": 60,
        "maxCastRange": 200,
        "sourceIni": "data/ini/specialpower.ini",
    }
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
    # This fixture's effective INI view carries no FXList/particle/upgrade
    # documents, so the shared leaf resolver cannot run and the effect stays
    # byte-identical to the pre-closure shape (import-unit lane).
    assert "leaves" not in summon["effect"]

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


def test_activate_module_special_power_emits_ordered_resolved_effect_graph() -> None:
    document = parse_sage_document(
        b"""
Object AbilityHero
  Behavior = PlayerHealSpecialPower ModuleTag_Heal
    SpecialPowerTemplate = SpecialAbilityActivateeDummy
    HealAmount = 1.0
    HealAsPercent = Yes
    HealAffects = INFANTRY HERO
    HealRadius = 100
  End
  Behavior = ActivateModuleSpecialPower ModuleTag_Activate
    SpecialPowerTemplate = SpecialAbilityFixtureActivate
    StartAbilityRange = FIXTURE_ACTIVATE_RANGE
    SpecialPowerDuration = CREATE_A_HERO_POWER_DURATION
    TriggerSpecialPower = ModuleTag_Heal OBJECTPOS
  End
End
""",
        virtual_path="data/ini/object/units/activate_fixture.ini",
    )
    lineage = [document.objects[0]]
    blocks = list(document.objects[0].blocks)
    activate = next(block for block in blocks if block.kind == "ActivateModuleSpecialPower")
    limitations: list[str] = []
    effect = _hero_ability_effect(
        "AbilityHero/Activate", [activate], [], [], {}, None, {}, {},
        {"FIXTURE_ACTIVATE_RANGE": 300, "CREATE_A_HERO_POWER_DURATION": 15000},
        {}, limitations,
        member_lineage=lineage,
        behavior_modules=blocks,
        named_definition_cache={},
        cache_lock=None,
    )
    assert effect["kind"] == "activate-module-graph"
    assert effect["startAbilityRange"] == 300
    assert effect["timingMs"] == {"SpecialPowerDuration": 15000}
    assert effect["routes"][0]["moduleTag"] == "ModuleTag_Heal"
    assert effect["routes"][0]["targetMode"] == "CURRENT_TARGET"
    assert effect["routes"][0]["effect"]["kind"] == "heal"
    assert effect["routes"][0]["effect"]["amount"] == 1.0


def test_activate_module_special_power_does_not_rebind_itself_when_templates_match() -> None:
    document = parse_sage_document(
        b"""
Object AbilityHero
  Behavior = PlayerHealSpecialPower ModuleTag_Heal
    SpecialPowerTemplate = SpecialAbilityShared
    HealAmount = 1.0
    HealAsPercent = Yes
    HealAffects = HERO
    HealRadius = 100
  End
  Behavior = ActivateModuleSpecialPower ModuleTag_Activate
    SpecialPowerTemplate = SpecialAbilityShared
    StartAbilityRange = 200
    TriggerSpecialPower = ModuleTag_Heal OBJECTPOS
  End
End
""",
        virtual_path="data/ini/object/units/activate_shared_template_fixture.ini",
    )
    lineage = [document.objects[0]]
    blocks = list(document.objects[0].blocks)
    activate = next(block for block in blocks if block.kind == "ActivateModuleSpecialPower")
    effect = _hero_ability_effect(
        "AbilityHero/SharedActivate", [activate], [], [], {}, None, {}, {}, {}, {}, [],
        member_lineage=lineage,
        behavior_modules=blocks,
        named_definition_cache={},
        cache_lock=None,
    )
    assert effect["kind"] == "activate-module-graph"
    assert effect["routes"][0]["effect"]["kind"] == "heal"


def test_weapon_mode_special_power_emits_resolved_mode_and_modifier() -> None:
    document = parse_sage_document(
        b"""
Object AbilityHero
  Behavior = WeaponModeSpecialPowerUpdate ModuleTag_Mode
    SpecialPowerTemplate = SpecialAbilityFixtureMode
    Duration = FIXTURE_MODE_DURATION
    AttributeModifier = FixtureModeBonus
    WeaponSetFlags = WEAPONSET_TOGGLE_1
    StartsPaused = Yes
  End
End
""",
        virtual_path="data/ini/object/units/weapon_mode_fixture.ini",
    )
    lineage = [document.objects[0]]
    mode = document.objects[0].blocks[0]
    from openbfme_importer.sage_ini import parse_flat_named_blocks
    modifier_block = parse_flat_named_blocks(
        b"ModifierList FixtureModeBonus\n  Category = SPELL\n  Modifier = DAMAGE_MULT 150%\nEnd\n",
        "ModifierList",
    )[0]
    effect = _hero_ability_effect(
        "AbilityHero/Mode", [mode], [], [], {"fixturemodebonus": modifier_block},
        None, {}, {}, {"FIXTURE_MODE_DURATION": 25000}, {}, [],
        member_lineage=lineage,
        behavior_modules=[mode],
        named_definition_cache={},
        cache_lock=None,
    )
    assert effect["kind"] == "weapon-mode-special-power"
    assert effect["durationMs"] == 25000
    assert effect["startsPaused"] is True
    assert effect["weaponSetFlags"] == ["WEAPONSET_TOGGLE_1"]
    assert effect["attributeModifier"]["id"] == "FixtureModeBonus"
    assert effect["attributeModifier"]["modifiers"] == [
        {"kind": "DAMAGE_MULT", "value": 1.5, "application": "multiplicative"}
    ]


def test_dominate_enemy_special_power_emits_resolved_allegiance_graph() -> None:
    document = parse_sage_document(
        b"""
Object AbilityHero
  Behavior = DominateEnemySpecialPower ModuleTag_Dominate
    SpecialPowerTemplate = SpecialAbilityFixtureDominate
    StartAbilityRange = FIXTURE_DOMINATE_RANGE
    AttributeModifierAffects = FIXTURE_DOMINATE_FILTER ENEMIES NEUTRAL
    DominateRadius = 60
    DominatedFX = FX_Dominated
    TriggerFX = FX_Trigger
    PermanentlyConvert = Yes
    UnpackTime = 2000
    FreezeAfterTriggerDuration = 2500
  End
End
""",
        virtual_path="data/ini/object/units/dominate_fixture.ini",
    )
    lineage = [document.objects[0]]
    dominate = document.objects[0].blocks[0]
    effect = _hero_ability_effect(
        "AbilityHero/Dominate", [dominate], [], [], {}, None, {}, {},
        {"FIXTURE_DOMINATE_RANGE": 200},
        {"fixture_dominate_filter": ("ANY", "-HERO")}, [],
        member_lineage=lineage,
        behavior_modules=[dominate],
        named_definition_cache={},
        cache_lock=None,
    )
    assert effect["kind"] == "dominate-enemy"
    assert effect["startAbilityRange"] == 200
    assert effect["dominateRadius"] == 60
    assert effect["affectsFilter"] == "ANY -HERO ENEMIES NEUTRAL"
    assert effect["permanentlyConvert"] is True
    assert effect["timingMs"] == {
        "UnpackTime": 2000, "FreezeAfterTriggerDuration": 2500,
    }


def test_grab_passenger_special_power_emits_typed_grab_graph() -> None:
    document = parse_sage_document(
        b"""
Object AbilityMonster
  Behavior = GrabPassengerSpecialPower ModuleTag_Grab
    SpecialPowerTemplate = SpecialAbilityGrabPassenger
    UpdateModuleStartsAttack = Yes
    AllowTree = Yes
    InitiateFX = FX_TrollGrabInitiate
  End
  Behavior = SpecialAbilityUpdate ModuleTag_GrabUpdate
    SpecialPowerTemplate = SpecialAbilityGrabPassenger
    StartAbilityRange = 8
    UnpackTime = 300
    PreparationTime = 1
    PersistentPrepTime = 630
    PackTime = 1000
    GrabPassengerAnimAndDuration = AnimState:EATING AnimTime:3000 TriggerTime:1400
    AwardXPForTriggering = 0
    RejectedConditions = WEAPON_TOGGLE
  End
  Behavior = TransportContain ModuleTag_Contain
    ObjectStatusOfContained = UNSELECTABLE
    PassengerFilter = ANY +CLUB +ORC
    ManualPickUpFilter = ANY +CLUB -ORC
    Slots = 1
    ShowPips = No
    AllowEnemiesInside = Yes
    AllowNeutralInside = Yes
    AllowAlliesInside = Yes
    DamagePercentToUnits = 0%
    TypeOneForWeaponSet = CLUB
    TypeOneForWeaponState = CLUB
    PassengerBonePrefix = PassengerBone:Trunk KindOf:CLUB
    EjectPassengersOnDeath = No
  End
End
""",
        virtual_path="data/ini/object/units/grab_fixture.ini",
    )
    lineage = [document.objects[0]]
    grab = document.objects[0].blocks[0]
    blocks = list(document.objects[0].blocks)
    effect = _hero_ability_effect(
        "AbilityMonster/Grab", [grab], [], [], {}, None, {}, {}, {}, {}, [],
        member_lineage=lineage,
        behavior_modules=blocks,
        named_definition_cache={},
        cache_lock=None,
    )
    assert effect["kind"] == "grab-passenger"
    assert effect["specialPowerTemplateId"] == "SpecialAbilityGrabPassenger"
    assert effect["allowTree"] is True
    assert effect["acquire"]["startAbilityRange"] == 8
    assert effect["acquire"]["timingMs"] == {
        "UnpackTime": 300, "PreparationTime": 1,
        "PersistentPrepTime": 630, "PackTime": 1000,
    }
    assert effect["containment"]["slots"] == 1
    assert effect["containment"]["manualPickUpFilter"] == "ANY +CLUB -ORC"
    assert effect["targetAdmission"]["treeKindOf"] == "CLUB"


def test_fling_passenger_special_ability_emits_resolved_landing_graph() -> None:
    document = parse_sage_document(
        b"""
Object AbilityMonster
  Behavior = FlingPassengerSpecialAbilityUpdate ModuleTag_Fling
    SpecialPowerTemplate = SpecialAbilityFixtureFling
    UnpackTime = 1250
    FlingPassengerVelocity = X:0 Y:0 Z:0
    FlingPassengerLandingWarhead = FixtureLandingWarhead
    MustFinishAbility = Yes
  End
End
""",
        virtual_path="data/ini/object/units/fling_fixture.ini",
    )
    weapon = b"""
Weapon FixtureLandingWarhead
  DamageNugget
    SpecialObjectFilter = NONE +INFANTRY -HERO
    Radius = 0
    DamageType = CRUSH
    DeathType = CRUSHED
    ForceKillObjectFilter = NONE +INFANTRY -HERO
  End
End
"""
    lineage = [document.objects[0]]
    fling = document.objects[0].blocks[0]
    effect = _hero_ability_effect(
        "AbilityMonster/Fling", [fling], [], [], {}, None, {},
        {"data/ini/weapon.ini": weapon}, {}, {}, [],
        member_lineage=lineage,
        behavior_modules=[fling],
        named_definition_cache={},
        cache_lock=None,
    )
    assert effect["kind"] == "fling-passenger"
    assert effect["timingMs"] == {"UnpackTime": 1250}
    assert effect["velocity"] == {"x": 0.0, "y": 0.0, "z": 0.0}
    assert effect["mustFinishAbility"] is True
    assert effect["landingWarhead"] == {
        "id": "FixtureLandingWarhead",
        "radius": 0,
        "damageType": "CRUSH",
        "deathType": "CRUSHED",
        "specialObjectFilter": "NONE +INFANTRY -HERO",
        "forceKillObjectFilter": "NONE +INFANTRY -HERO",
        "sourceIni": "data/ini/weapon.ini",
        "line": 3,
    }


def test_repair_special_power_emits_target_rate_contact_and_economy_seams() -> None:
    document = parse_sage_document(
        b"""
Object Repairer
  Behavior = WorkerAIUpdate ModuleTag_Worker
    RepairHealthPercentPerSecond = 0.2%
    SpecialContactPoints = Repair
  End
  Behavior = RepairSpecialPower ModuleTag_Repair
    SpecialPowerTemplate = SpecialRepairStructure
  End
End
""",
        virtual_path="data/ini/object/units/repair_fixture.ini",
    )
    lineage = [document.objects[0]]
    blocks = list(document.objects[0].blocks)
    repair = blocks[1]
    effect = _hero_ability_effect(
        "Repairer/Repair", [repair], [], [], {}, None, {}, {}, {}, {}, [],
        member_lineage=lineage, behavior_modules=blocks,
        named_definition_cache={}, cache_lock=None,
    )
    assert effect["kind"] == "repair-structure"
    assert effect["targeting"] == {
        "relation": "ALLY", "kindOf": ["STRUCTURE"],
        "requiresDamaged": True, "rangeMode": "REPAIR_CONTACT_POINT",
    }
    assert effect["repairRate"]["maxHealthFractionPerSecond"] == 0.002
    assert effect["contactPoint"]["authored"] is True
    assert effect["economy"] == {
        "status": "no-authored-resource-field", "resourceCost": None,
    }


def test_horde_dispatch_special_power_emits_member_effect_and_payload() -> None:
    document = parse_sage_document(
        b"""
Object FixtureHorde
  Behavior = HordeContain ModuleTag_HordeContain
    InitialPayload = FixtureMember 5
  End
  Behavior = HordeDispatchSpecialPower ModuleTag_Dispatch
    SpecialPowerTemplate = SpecialAbilityFixtureDispatch
    UpdateModuleStartsAttack = Yes
    StartsPaused = No
  End
  Behavior = WeaponModeSpecialPowerUpdate ModuleTag_MemberWeaponMode
    SpecialPowerTemplate = SpecialAbilityFixtureDispatch
    Duration = 20000
    WeaponSetFlags = WEAPONSET_TOGGLE_1
    StartsPaused = No
  End
End
""",
        virtual_path="data/ini/object/units/horde_dispatch_fixture.ini",
    )
    lineage = [document.objects[0]]
    blocks = list(document.objects[0].blocks)
    dispatch = blocks[1]
    effect = _hero_ability_effect(
        "FixtureHorde/Dispatch", [dispatch], [], [], {}, None, {}, {}, {}, {}, [],
        member_lineage=lineage, behavior_modules=blocks,
        named_definition_cache={}, cache_lock=None,
    )
    assert effect["kind"] == "horde-dispatch"
    assert effect["specialPowerTemplateId"] == "SpecialAbilityFixtureDispatch"
    assert effect["startsPaused"] is False
    assert effect["updateModuleStartsAttack"] is True
    assert effect["memberObjectId"] == "FixtureMember"
    assert effect["memberCount"] == 5
    assert effect["targeting"] == "PER_MEMBER_INHERIT_EFFECT"
    assert effect["memberEffect"]["kind"] == "weapon-mode-special-power"
    assert effect["memberEffect"]["durationMs"] == 20000


def test_horde_dispatch_graphs_cover_exact_effective_retail_corpora() -> None:
    from openbfme_importer.catalog import InstallCatalog
    from openbfme_importer.module_census import (
        census_catalog_paths,
        read_catalog_documents,
    )

    actual: dict[str, list[tuple[object, ...]]] = {}
    for edition, path in census_catalog_paths().items():
        documents = dict(read_catalog_documents(InstallCatalog.load(path)))
        prepared = prepare_playable_unit_compiler(documents)
        signatures: list[tuple[object, ...]] = []
        for object_id in ("GoblinFighterHorde", "ElvenMithlondSentryHorde"):
            descriptor = compile_playable_unit_descriptor(
                object_id, documents, prepared=prepared,
                game="rotwk" if edition == "rotwk-retail" else "bfme2",
            )
            rows = [
                row for row in descriptor["gameplay"]["simulation"]["resolved"]["moduleContracts"]
                if row["module"] == "HordeDispatchSpecialPower"
            ]
            assert len(rows) == 1
            row = rows[0]
            graph = row["effectGraph"]
            member = graph["memberEffect"]
            signatures.append((
                graph["specialPowerTemplateId"], graph["memberObjectId"],
                graph["memberCount"], graph["updateModuleStartsAttack"],
                member["kind"], member.get("durationMs"),
                tuple(sorted(member.get("timingMs", {}).items())),
                member.get("startAbilityRange"), member.get("mustFinishAbility"),
                row["commandExposure"]["status"], row["runtimeStatus"],
            ))
        actual[edition] = signatures
    expected = [
        (
            "SpecialAbilityGoblinFighterPoisonedBlades", "GoblinFighter", 20,
            False, "weapon-mode-special-power", 20000, (), None, None,
            "exposed", "deferred",
        ),
        (
            "SpecialAbilityZephyrStrike", "ElvenMithlondSentry", 15, True,
            "weapon-blast", None,
            (("FreezeAfterTriggerDuration", 2500), ("PackTime", 1), ("UnpackTime", 1700)),
            80, True, "not-in-effective-command-set", "deferred",
        ),
    ]
    assert actual == {"bfme2-retail": expected, "rotwk-retail": expected}


def test_hero_abilities_fail_closed_per_ability_never_faked() -> None:
    documents = _hero_ability_documents()

    descriptor = compile_playable_unit_descriptor("AbilityHero", documents)

    validate_playable_unit_descriptor(descriptor)
    abilities = _abilities_by_id(descriptor)

    # The fixture hero authors the toggle module but no SET_MOUNTED
    # locomotor: the mount stays a recorded gap, never a partial stat swap.
    mount = abilities["Command_FixtureMount"]
    assert mount["implementation"]["status"] == "unimplemented"
    assert "SET_MOUNTED LocomotorSet" in mount["implementation"]["reason"]
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


def test_default_weapon_set_ignores_explicit_none_slot() -> None:
    documents = {
        "data/ini/object/test.ini": b"""
Object EmptyWeaponObject
  WeaponSet
    Conditions = None
    Weapon = PRIMARY None
  End
End
"""
    }
    object_index = _object_index(documents)
    ancestry = _ancestry(object_index, object_index["emptyweaponobject"])

    assert _default_set_target(ancestry, "WeaponSet", "Weapon") is None


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


def test_ranged_clip_reload_time_resolves_to_the_authored_max() -> None:
    """Retail horde archers author `ClipReloadTime = Min:X Max:Y`
    (weapon.ini:4239 GondorArcherBow -> gamedata.ini:1858-1859 Min 1500 /
    Max 2000; same form at :10409 MordorArcherBow, :1836 LorienElvenBow,
    :1260 MirkwoodArcherBow). The plain expression resolver reads none of
    that form, so the field used to vanish from the pack and the runtime
    reload collapsed to 0 ms. The deterministic runtime fires on the
    authored Max (the spellbook resolvedMax convention)."""
    command_row, button_row = _combat_command("CombatArcher", 9, "CombatArcher")
    documents = _combat_documents(
        _combat_object(
            "CombatArcher",
            "INFANTRY",
            "  WeaponSet\n    Conditions = None\n    Weapon = PRIMARY CombatArcherBow\n  End\n",
        ),
        "Weapon CombatArcherBow\n"
        "  AttackRange = COMBAT_ARCHER_RANGE\n"
        "  DelayBetweenShots = 0\n"
        "  PreAttackDelay = 1000\n"
        "  FiringDuration = 0\n"
        "  ClipSize = 1\n"
        "  AutoReloadsClip = Yes\n"
        "  ClipReloadTime = Min:COMBAT_ARCHER_RELOAD_MIN Max:COMBAT_ARCHER_RELOAD_MAX\n"
        "  ContinuousFireOne = 0\n"
        "  ContinuousFireCoast = 2000\n"
        "  DamageNugget\n"
        "    Damage = 35\n"
        "    DamageType = PIERCE\n"
        "  End\n"
        "End\n",
        command_row,
        button_row,
        defines=(
            "\n#define COMBAT_ARCHER_RANGE 300\n"
            "#define COMBAT_ARCHER_RELOAD_MIN 1500\n"
            "#define COMBAT_ARCHER_RELOAD_MAX 2000\n"
        ),
    )

    descriptor = compile_playable_unit_descriptor("CombatArcher", documents)

    validate_playable_unit_descriptor(descriptor)
    combat = descriptor["gameplay"]["simulation"]["resolved"]["combat"]
    reload = combat["clipReloadTimeMs"]
    assert reload["value"] == 2000
    assert reload["valueMin"] == 1500
    assert reload["valueMax"] == 2000
    assert reload["expression"] == (
        "Min:COMBAT_ARCHER_RELOAD_MIN Max:COMBAT_ARCHER_RELOAD_MAX"
    )


def test_pre_attack_type_compiles_from_the_authored_token() -> None:
    """Retail authors PreAttackType per weapon (weapon.ini:4233 GondorArcherBow
    = PER_POSITION; :4783 FaramirBow = PER_POSITION; :10808 HaradrimBow =
    PER_SHOT). The compiler used to ignore the token, so every volley charged
    PreAttackDelay. PER_POSITION means windup only on a new target/position
    (and the first shot); compiling the token is what lets the runtime honor
    it instead of hardcoding Gondor."""
    command_row, button_row = _combat_command("CombatArcher", 9, "CombatArcher")
    documents = _combat_documents(
        _combat_object(
            "CombatArcher",
            "INFANTRY",
            "  WeaponSet\n    Conditions = None\n    Weapon = PRIMARY CombatArcherBow\n  End\n",
        ),
        "Weapon CombatArcherBow\n"
        "  AttackRange = COMBAT_ARCHER_RANGE\n"
        "  DelayBetweenShots = 0\n"
        "  PreAttackDelay = 1000\n"
        "  PreAttackRandomAmount = 200\n"
        "  PreAttackType = PER_POSITION\n"
        "  FiringDuration = 0\n"
        "  ClipSize = 1\n"
        "  AutoReloadsClip = Yes\n"
        "  ClipReloadTime = Min:COMBAT_ARCHER_RELOAD_MIN Max:COMBAT_ARCHER_RELOAD_MAX\n"
        "  ContinuousFireOne = 0\n"
        "  ContinuousFireCoast = COMBAT_ARCHER_RELOAD_MAX\n"
        "  DamageNugget\n"
        "    Damage = 35\n"
        "    DamageType = PIERCE\n"
        "  End\n"
        "End\n",
        command_row,
        button_row,
        defines=(
            "\n#define COMBAT_ARCHER_RANGE 300\n"
            "#define COMBAT_ARCHER_RELOAD_MIN 1500\n"
            "#define COMBAT_ARCHER_RELOAD_MAX 2000\n"
        ),
    )

    descriptor = compile_playable_unit_descriptor("CombatArcher", documents)

    validate_playable_unit_descriptor(descriptor)
    combat = descriptor["gameplay"]["simulation"]["resolved"]["combat"]
    assert combat["preAttackType"]["value"] == "PER_POSITION"
    assert combat["preAttackType"]["expression"].strip() == "PER_POSITION"
    random_amount = combat["preAttackRandomAmountMs"]
    assert random_amount["value"] == 200
    assert random_amount["deterministicUse"] == "deferred"


def test_pre_attack_type_per_shot_is_not_rewritten_to_per_position() -> None:
    """HaradrimBow (weapon.ini:10808) authors PER_SHOT. Compiling the token
    must keep that type; a Gondor-shaped default would drop the every-shot
    windup."""
    command_row, button_row = _combat_command("CombatHaradrim", 9, "CombatHaradrim")
    documents = _combat_documents(
        _combat_object(
            "CombatHaradrim",
            "INFANTRY",
            "  WeaponSet\n    Conditions = None\n    Weapon = PRIMARY CombatHaradrimBow\n  End\n",
        ),
        "Weapon CombatHaradrimBow\n"
        "  AttackRange = 275.0\n"
        "  DelayBetweenShots = 900\n"
        "  PreAttackDelay = 2100\n"
        "  PreAttackRandomAmount = 200\n"
        "  PreAttackType = PER_SHOT\n"
        "  FiringDuration = 700\n"
        "  DamageNugget\n"
        "    Damage = 40\n"
        "    DamageType = PIERCE\n"
        "  End\n"
        "End\n",
        command_row,
        button_row,
    )

    descriptor = compile_playable_unit_descriptor("CombatHaradrim", documents)

    validate_playable_unit_descriptor(descriptor)
    combat = descriptor["gameplay"]["simulation"]["resolved"]["combat"]
    assert combat["preAttackType"]["value"] == "PER_SHOT"


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
  ExperienceAwardOwnGuysDie = 2
  InformUpdateModule = Yes
  EmotionType = CHEER
  ShowLevelUpTint = Yes
  LevelUpTintColor = R:255 G:128 B:0
  LevelUpTintPreColorTime = 100
  LevelUpTintSustainColorTime = 200
  LevelUpTintPostColorTime = 300
  Rank = 1
  SelectionDecal
    Texture = decal_G_level1
    Texture2 = decal_G_level1_extra
    MinRadius = 5
    MaxRadius = 20
    OpacityMin = 25%
    OpacityMax = 100%
    MaxSelectedUnits = 1
    Style = SHADOW_ALPHA_DECAL
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
    assert levels[0]["experienceAwardOwnGuysDie"] == 2
    assert levels[0]["selectionDecalTextureId"] == "decal_G_level1"
    assert levels[0]["selectionDecal"] == {
        "textureId": "decal_G_level1",
        "texture2Id": "decal_G_level1_extra",
        "minRadius": 5,
        "maxRadius": 20,
        "opacityMin": 0.25,
        "opacityMax": 1.0,
        "maxSelectedUnits": 1,
        "style": "SHADOW_ALPHA_DECAL",
    }
    assert levels[0]["levelUpPresentation"] == {
        "informUpdateModule": True,
        "emotionType": "CHEER",
        "showLevelUpTint": True,
        "levelUpTintColorRgb": [255, 128, 0],
        "levelUpTintPreColorTimeMs": 100,
        "levelUpTintSustainColorTimeMs": 200,
        "levelUpTintPostColorTimeMs": 300,
    }
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


def test_experience_specific_row_wins_only_at_its_own_rank() -> None:
    """A narrower TargetNames list overrides that RANK, not the whole chain.

    SAGE grants experience per object name and applies every authored
    ExperienceLevel whose TargetNames covers the object, so a specific block
    can only outrank a general one at the rank they share. Retail depends on
    this: `EvilLevel1` (experiencelevels.ini:9444) comments out its
    `EVIL_TROOPS` macro and repeats it as a literal 40-name list while
    EvilLevel2..5 keep the 42-name macro. Treating the narrow list as a
    replacement chain capped every EVIL_TROOPS horde at rank 1, which
    contradicts the retail game where Uruk-hai reach rank 5.
    """

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
    # Rank 1 comes from the narrow list; ranks 2..3 still come from the define
    # chain instead of being discarded with it.
    assert experience["maxLevel"] == 3
    assert experience["levels"][0]["experienceId"] == "InfantryOwnLevel1"
    assert experience["levels"][0]["experienceAward"] == 9


def test_experience_narrow_subset_list_keeps_the_general_higher_ranks() -> None:
    """Regression for the retail EVIL_TROOPS shape, in miniature.

    A narrow rank-1 list that is a strict SUBSET of the general chain's target
    set must not swallow the chain: the unit keeps every rank the general
    blocks author.
    """

    documents = _experience_documents(
        _TROOP_CHAIN
        + """
ExperienceLevel NarrowLevel1
  TargetNames = InfantryHorde RangedHorde
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
    assert experience["maxLevel"] == 3
    assert [level["rank"] for level in experience["levels"]] == [1, 2, 3]
    assert experience["levels"][0]["experienceId"] == "NarrowLevel1"


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


@pytest.mark.parametrize("expression", ["MISSING_PERCENT_DEFINE", "25%%"])
def test_experience_selection_decal_percent_expression_fails_closed(
    expression: str,
) -> None:
    documents = _experience_documents(
        _TROOP_CHAIN.replace("OpacityMin = 25%", f"OpacityMin = {expression}"),
        defines=_TROOP_DEFINES,
    )

    with pytest.raises(PlayableUnitCompilerError, match="SelectionDecal opacitymin"):
        compile_playable_unit_descriptor("InfantryHorde", documents)


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


# ---------------------------------------------------------------------------
# GameData define-expression evaluator tests.  RotWK authors experience
# awards and modifier percentages as nested macro defines; the evaluator
# resolves the measured grammar (#ADD/#SUBTRACT/#MULTIPLY/#DIVIDE, binary,
# chained defines) and fails closed on everything else.
# ---------------------------------------------------------------------------


def test_nested_divide_chain_award_resolves_and_truncates() -> None:
    # The exact RotWK Angmar shape: level 1 divides a shared target by the
    # horde size; level 2 chains through level 1 with a nested #MULTIPLY.
    documents = _experience_documents(
        _TROOP_CHAIN,
        defines=(
            "#define FIXTURE_NEEDED_2 50\n"
            "#define FIXTURE_NEEDED_3 100\n"
            "#define FIXTURE_EXP_TARGET 60\n"
            "#define FIXTURE_HORDE_SIZE 8\n"
            "#define FIXTURE_LEVEL_FACTOR_2 110\n"
            "#define FIXTURE_DIV_FACTOR 100\n"
            "#define FIXTURE_AWARD_1 #DIVIDE( FIXTURE_EXP_TARGET FIXTURE_HORDE_SIZE )\n"
            "#define FIXTURE_AWARD_2 #DIVIDE( #MULTIPLY( FIXTURE_AWARD_1 "
            "FIXTURE_LEVEL_FACTOR_2 ) FIXTURE_DIV_FACTOR )\n"
            "#define FIXTURE_AWARD_3 #ADD( FIXTURE_AWARD_1 2 )\n"
        ),
        modifiers=_TROOP_MODIFIERS.replace("FIXTURE_HP_ADD_2", "20").replace(
            "FIXTURE_DAM_ADD_2", "10"
        ),
    )

    descriptor = compile_playable_unit_descriptor("InfantryHorde", documents)

    validate_playable_unit_descriptor(descriptor)
    levels = descriptor["experience"]["levels"]
    # 60 / 8 = 7.5 and 7.5 * 110 / 100 = 8.25: SAGE's integer scanner
    # truncates both, so the compiled awards are 7 and 8.
    assert [row["experienceAward"] for row in levels] == [7, 8, 9]
    assert levels[0]["constantSourceIni"] == "data/ini/gamedata.ini"


def test_percent_define_resolves_only_in_modifier_context() -> None:
    documents = _experience_documents(
        _TROOP_CHAIN,
        defines=_TROOP_DEFINES + "#define FIXTURE_LEVEL_PCT 110%\n",
        modifiers=_TROOP_MODIFIERS.replace(
            "Modifier = SPEED 110%", "Modifier = PRODUCTION FIXTURE_LEVEL_PCT"
        ),
    )

    descriptor = compile_playable_unit_descriptor("InfantryHorde", documents)

    validate_playable_unit_descriptor(descriptor)
    levels = descriptor["experience"]["levels"]
    production_leaf = next(
        leaf
        for leaf in levels[2]["attributeModifiers"]
        if leaf["id"] == "FixtureTroopBonusSpeed"
    )
    assert production_leaf["modifiers"] == [
        {"kind": "PRODUCTION", "value": 1.1, "application": "multiplicative"}
    ]
    # The same percent define never resolves in a plain numeric context.
    numeric = _experience_documents(
        _TROOP_CHAIN.replace("FIXTURE_AWARD_2", "FIXTURE_LEVEL_PCT"),
        defines=_TROOP_DEFINES + "#define FIXTURE_LEVEL_PCT 110%\n",
    )
    with pytest.raises(PlayableUnitCompilerError, match="ExperienceAward"):
        compile_playable_unit_descriptor("InfantryHorde", numeric)


def test_define_cycle_fails_closed() -> None:
    documents = _experience_documents(
        _TROOP_CHAIN,
        defines=(
            "#define FIXTURE_NEEDED_2 50\n"
            "#define FIXTURE_NEEDED_3 100\n"
            "#define FIXTURE_AWARD_1 FIXTURE_AWARD_2\n"
            "#define FIXTURE_AWARD_2 #ADD( FIXTURE_AWARD_1 1 )\n"
            "#define FIXTURE_AWARD_3 5\n"
        ),
    )

    with pytest.raises(PlayableUnitCompilerError, match="ExperienceAward"):
        compile_playable_unit_descriptor("InfantryHorde", documents)


def test_unknown_identifier_in_expression_fails_closed() -> None:
    documents = _experience_documents(
        _TROOP_CHAIN,
        defines=_TROOP_DEFINES.replace(
            "#define FIXTURE_AWARD_2 4\n",
            "#define FIXTURE_AWARD_2 #DIVIDE( FIXTURE_MISSING 2 )\n",
        ),
    )

    with pytest.raises(PlayableUnitCompilerError, match="ExperienceAward"):
        compile_playable_unit_descriptor("InfantryHorde", documents)


def test_division_by_zero_fails_closed() -> None:
    documents = _experience_documents(
        _TROOP_CHAIN,
        defines=_TROOP_DEFINES.replace(
            "#define FIXTURE_AWARD_2 4\n",
            "#define FIXTURE_ZERO 0\n"
            "#define FIXTURE_AWARD_2 #DIVIDE( 10 FIXTURE_ZERO )\n",
        ),
    )

    with pytest.raises(PlayableUnitCompilerError, match="ExperienceAward"):
        compile_playable_unit_descriptor("InfantryHorde", documents)


def test_unknown_function_fails_closed() -> None:
    documents = _experience_documents(
        _TROOP_CHAIN,
        defines=_TROOP_DEFINES.replace(
            "#define FIXTURE_AWARD_2 4\n",
            "#define FIXTURE_AWARD_2 #MODULO( 10 3 )\n",
        ),
    )

    with pytest.raises(PlayableUnitCompilerError, match="ExperienceAward"):
        compile_playable_unit_descriptor("InfantryHorde", documents)


def test_numeric_defines_evaluates_the_measured_grammar() -> None:
    table = _numeric_defines(
        {
            "data/ini/gamedata.ini": (
                b"#define BASE 60\n"
                b"#define SIZE 10\n"
                b"#define EXACT #DIVIDE( BASE SIZE )\n"
                b"#define FRACTIONAL #DIVIDE( BASE 8 )\n"
                b"#define CHAINED #SUBTRACT( EXACT 1 )\n"
                b"#define ALIASED EXACT\n"
                b"#define SCALED 110% // comment\n"
                b"#define FILTERED ANY +INFANTRY -HERO\n"
            )
        }
    )

    assert table["exact"] == 6 and isinstance(table["exact"], int)
    assert table["fractional"] == 7.5
    assert table["chained"] == 5
    assert table["aliased"] == 6
    # Percent defines live only in the reserved percent namespace.
    assert "scaled" not in table
    assert table["scaled%"] == 1.1
    # Non-numeric bodies stay out of the table entirely.
    assert "filtered" not in table


def test_experience_top_rank_summon_chain_compiles_initial_rank() -> None:
    # Retail summons (ring hero, Treebeard) author a single rank-10 row: the
    # unit enters at the top rank and never levels further.
    documents = _experience_documents(
        """
ExperienceLevel FixtureSummonNormalLevel
  TargetNames = InfantryHorde
  RequiredExperience = 1
  ExperienceAward = 100
  Rank = 1
End
ExperienceLevel FixtureSummonLevel1
  TargetNames = InfantryHorde
  RequiredExperience = 1
  Rank = 10
  AttributeModifiers = FixtureSummonBonus
  Upgrades = Upgrade_ObjectLevel1 Upgrade_ObjectLevel10
  SelectionDecal
    Texture = decal_hero_good
  End
End
""",
        modifiers="""
ModifierList FixtureSummonBonus
  Category = LEVEL
  Modifier = HEALTH 40
  Modifier = DAMAGE_ADD 15
  Modifier = DAMAGE_MULT 120%
  Modifier = SPELL_DAMAGE 150%
  Duration = 0
End
""",
    )
    object_path = "data/ini/object/units/test_units.ini"
    documents[object_path] = documents[object_path].replace(
        b"Object InfantryHorde\n",
        (
            b"Object InfantryHorde\n"
            b"  Behavior = ExperienceLevelCreate ModuleTag_LevelBonus\n"
            b"    LevelToGrant = 10\n"
            b"    MPOnly = No\n"
            b"  End\n"
        ),
        1,
    )

    descriptor = compile_playable_unit_descriptor("InfantryHorde", documents)

    validate_playable_unit_descriptor(descriptor)
    experience = descriptor["experience"]
    assert experience["status"] == "compiled"
    assert experience["initialRank"] == 10
    assert experience["experienceLevelCreate"]["rank"] == 10
    assert experience["experienceLevelCreate"]["mpOnly"] is False
    evidence = next(
        row
        for row in descriptor["runtimeModuleEvidence"]
        if row["kind"] == "ExperienceLevelCreate"
    )
    assert evidence["consumed"] is True
    assert "ExperienceLevelCreate" not in descriptor["specialCapabilities"]
    assert experience["maxLevel"] == 10
    assert len(experience["levels"]) == 2
    assert experience["levels"][1]["rank"] == 10
    assert "experienceAward" not in experience["levels"][1]
    assert experience["levels"][1]["experienceAwardStatus"] == "unauthored"
    assert experience["levels"][1]["attributeModifiers"][0]["id"] == (
        "FixtureSummonBonus"
    )
    assert {
        row["kind"]
        for row in experience["levels"][1]["attributeModifiers"][0]["modifiers"]
    } == {"HEALTH", "DAMAGE_ADD", "DAMAGE_MULT", "SPELL_DAMAGE"}
    assert experience["levels"][1]["upgrades"] == [
        "Upgrade_ObjectLevel1",
        "Upgrade_ObjectLevel10",
    ]


def test_top_rank_chain_without_creation_module_starts_at_rank_one() -> None:
    documents = _experience_documents(
        """
ExperienceLevel FixtureTopRankOnly
  TargetNames = InfantryHorde
  RequiredExperience = 1
  ExperienceAward = 100
  Rank = 10
End
""",
    )

    descriptor = compile_playable_unit_descriptor("InfantryHorde", documents)

    validate_playable_unit_descriptor(descriptor)
    experience = descriptor["experience"]
    assert experience["status"] == "compiled"
    assert experience["initialRank"] == 1
    assert "experienceLevelCreate" not in experience
    assert not any(
        row["kind"] == "ExperienceLevelCreate"
        for row in descriptor["runtimeModuleEvidence"]
    )


def test_experience_level_create_rejects_unproven_mp_only_mode() -> None:
    documents = _experience_documents(
        """
ExperienceLevel FixtureSummonLevel1
  TargetNames = InfantryHorde
  RequiredExperience = 1
  ExperienceAward = 100
  Rank = 10
End
""",
    )
    object_path = "data/ini/object/units/test_units.ini"
    documents[object_path] = documents[object_path].replace(
        b"Object InfantryHorde\n",
        (
            b"Object InfantryHorde\n"
            b"  Behavior = ExperienceLevelCreate ModuleTag_LevelBonus\n"
            b"    LevelToGrant = 10\n"
            b"    MPOnly = Yes\n"
            b"  End\n"
        ),
        1,
    )

    with pytest.raises(PlayableUnitCompilerError, match="MPOnly"):
        compile_playable_unit_descriptor("InfantryHorde", documents)


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


def test_elven_grace_enum_binds_the_unique_button_triggered_burst_heal() -> None:
    documents = _hero_ability_documents()
    object_path = "data/ini/object/units/test_units.ini"
    documents[object_path] = documents[object_path].replace(
        b"    TriggerFX = FX_FixtureGrace\n", b"", 1
    ).replace(
        b"    UnitHealPulseFX = FX_FixtureGrace\n",
        b"    UnitHealPulseFX = FX_UnrelatedPresentationOnly\n",
        1,
    )
    documents["data/ini/specialpower.ini"] = documents[
        "data/ini/specialpower.ini"
    ].replace(
        b"SpecialPower SpecialAbilityFixtureGrace\n"
        b"  Enum = SPECIAL_ATHELAS\n",
        b"SpecialPower SpecialAbilityFixtureGrace\n"
        b"  Enum = SPECIAL_ELVEN_GRACE\n",
        1,
    )

    descriptor = compile_playable_unit_descriptor("AbilityHero", documents)

    validate_playable_unit_descriptor(descriptor)
    grace = _abilities_by_id(descriptor)["Command_FixtureGrace"]
    assert grace["implementation"]["status"] == "implemented"
    assert grace["effect"]["kind"] == "heal"
    assert grace["effect"]["amount"] == 500
    assert grace["effect"]["radius"] == 150
    assert grace["effect"]["healFxId"] == "FX_UnrelatedPresentationOnly"


def test_elven_grace_enum_refuses_ambiguous_button_triggered_heals() -> None:
    documents = _hero_ability_documents()
    object_path = "data/ini/object/units/test_units.ini"
    documents[object_path] = documents[object_path].replace(
        b"    TriggerFX = FX_FixtureGrace\n", b"", 1
    ).replace(
        b"  Behavior = AutoHealBehavior ModuleTag_GraceHealing\n",
        b"  Behavior = AutoHealBehavior ModuleTag_OtherButtonHeal\n"
        b"    ButtonTriggered = Yes\n"
        b"    HealingAmount = 1\n"
        b"    Radius = 1\n"
        b"    SingleBurst = Yes\n"
        b"  End\n"
        b"  Behavior = AutoHealBehavior ModuleTag_GraceHealing\n",
        1,
    )
    documents["data/ini/specialpower.ini"] = documents[
        "data/ini/specialpower.ini"
    ].replace(
        b"SpecialPower SpecialAbilityFixtureGrace\n"
        b"  Enum = SPECIAL_ATHELAS\n",
        b"SpecialPower SpecialAbilityFixtureGrace\n"
        b"  Enum = SPECIAL_ELVEN_GRACE\n",
        1,
    )

    descriptor = compile_playable_unit_descriptor("AbilityHero", documents)

    grace = _abilities_by_id(descriptor)["Command_FixtureGrace"]
    assert grace["implementation"]["status"] == "unimplemented"
    assert grace["effect"] == {"kind": "none"}


def test_hero_mode_without_modifier_binds_authored_hero_weapon_set() -> None:
    documents = _hero_ability_documents()
    object_path = "data/ini/object/units/test_units.ini"
    documents[object_path] = documents[object_path].replace(
        b"  WeaponSet\n"
        b"    Conditions = None\n"
        b"    Weapon = PRIMARY AbilityHeroSword\n"
        b"  End\n",
        b"  WeaponSet\n"
        b"    Conditions = None\n"
        b"    Weapon = PRIMARY AbilityHeroSword\n"
        b"  End\n"
        b"  WeaponSet\n"
        b"    Conditions = WEAPONSET_HERO_MODE\n"
        b"    Weapon = PRIMARY FixtureDeadeyeBow\n"
        b"  End\n",
        1,
    ).replace(
        b"    HeroAttributeModifier = FixtureRage\n", b"", 1
    )
    documents["data/ini/weapon.ini"] += (
        b"\nWeapon FixtureDeadeyeBow\n"
        b"  AttackRange = 400\n"
        b"  DelayBetweenShots = 500\n"
        b"  PreAttackDelay = 100\n"
        b"  FiringDuration = 100\n"
        b"  DamageNugget\n"
        b"    Damage = 250\n"
        b"    DamageType = HERO_RANGED\n"
        b"  End\n"
        b"End\n"
    )

    descriptor = compile_playable_unit_descriptor("AbilityHero", documents)

    validate_playable_unit_descriptor(descriptor)
    rage = _abilities_by_id(descriptor)["Command_FixtureRage"]
    assert rage["implementation"]["status"] == "implemented"
    assert rage["effect"] == {
        "kind": "weapon-mode-special-power",
        "specialPowerTemplateId": "SpecialAbilityFixtureRage",
        "durationMs": 20000,
        "startsPaused": False,
        "weaponSetFlags": ["WEAPONSET_HERO_MODE"],
        "sourceIni": object_path,
        "line": rage["effect"]["line"],
    }


def test_modifierless_hero_mode_without_authored_weapon_set_fails_closed() -> None:
    documents = _hero_ability_documents()
    object_path = "data/ini/object/units/test_units.ini"
    documents[object_path] = documents[object_path].replace(
        b"    HeroAttributeModifier = FixtureRage\n", b"", 1
    )

    descriptor = compile_playable_unit_descriptor("AbilityHero", documents)

    rage = _abilities_by_id(descriptor)["Command_FixtureRage"]
    assert rage["implementation"]["status"] == "unimplemented"
    assert "WEAPONSET_HERO_MODE" in rage["implementation"]["reason"]
    assert rage["effect"] == {"kind": "none"}


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
    assert toggle["implementation"]["status"] == "implemented"
    assert toggle["cooldownMs"] == 0
    assert toggle["effect"] == {
        "kind": "weapon-toggle",
        "toggleMode": "weaponset_toggle_1",
        "toggledWeaponId": "FixtureToggleBow",
        "sourceIni": "data/ini/commandbutton.ini",
    }
    evidence = toggle["weaponToggle"]
    assert evidence["toggleFlag"] == "WEAPONSET_TOGGLE_1"
    assert evidence["defaultWeaponId"] == "AbilityHeroSword"
    assert evidence["toggledWeaponId"] == "FixtureToggleBow"
    assert evidence["toggledWeapon"]["damage"] == 90
    assert evidence["toggledWeapon"]["attackRange"] == 320.0
    # The simulation contract publishes the toggled mode as a full runtime
    # weapon-mode profile keyed by the authored condition flag.
    modes = descriptor["gameplay"]["simulation"]["resolved"]["weaponModes"]
    profile = modes["weaponset_toggle_1"]
    assert profile["weaponId"] == "FixtureToggleBow"
    assert profile["attackRange"]["value"] == 320.0
    assert profile["damage"]["value"] == 90
    assert profile["delayBetweenShotsMs"]["value"] == 0
    assert profile["preAttackDelayMs"]["value"] == 0
    assert profile["firingDurationMs"]["value"] == 0
    assert profile["weaponSlot"] == "PRIMARY"


def _weapon_toggle_documents(weapon_ini: bytes, weapon_id: str) -> dict[str, bytes]:
    """Fixture hero with one TOGGLE_WEAPONSET button bound to ``weapon_id``."""

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
        f"    Weapon = PRIMARY {weapon_id}\n"
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
    documents["data/ini/weapon.ini"] += weapon_ini
    return documents


def test_weapon_mode_damage_resolves_through_the_authored_projectile_warhead() -> None:
    # Retail bow modes (HaldirBow) author no DamageNugget of their own: the
    # ProjectileNugget names a warhead Weapon and the warhead carries the
    # damage.  The mode profile must follow that authored hop.
    documents = _weapon_toggle_documents(
        b"\nWeapon FixtureWarheadBow\n"
        b"  AttackRange = 320.0\n"
        b"  ProjectileNugget\n"
        b"    ProjectileTemplateName = FixtureArrow\n"
        b"    WarheadTemplateName = FixtureWarheadBowWarhead\n"
        b"  End\n"
        b"End\n"
        b"\nWeapon FixtureWarheadBowWarhead\n"
        b"  DamageNugget\n"
        b"    Damage = 120\n"
        b"    Radius = 0.0\n"
        b"    DamageType = HERO_RANGED\n"
        b"  End\n"
        b"End\n",
        "FixtureWarheadBow",
    )

    descriptor = compile_playable_unit_descriptor("AbilityHero", documents)

    validate_playable_unit_descriptor(descriptor)
    toggle = _abilities_by_id(descriptor)["Command_FixtureToggle"]
    assert toggle["implementation"]["status"] == "implemented"
    profile = descriptor["gameplay"]["simulation"]["resolved"]["weaponModes"][
        "weaponset_toggle_1"
    ]
    assert profile["weaponId"] == "FixtureWarheadBow"
    assert profile["damage"]["value"] == 120
    assert profile["damageWarheadIds"] == ["FixtureWarheadBowWarhead"]


def test_weapon_mode_damage_skips_an_authored_empty_warhead() -> None:
    # RohanEntRockThrow fires a second ProjectileNugget purely to spawn a
    # shroud revealer, and retail authors its warhead as an empty Weapon so
    # the engine does not assert.  An empty warhead contributes nothing; it
    # is not an unresolved gap.
    documents = _weapon_toggle_documents(
        b"\nWeapon FixtureRockThrow\n"
        b"  AttackRange = 400.0\n"
        b"  ProjectileNugget\n"
        b"    ProjectileTemplateName = FixtureRock\n"
        b"    WarheadTemplateName = FixtureRockWarhead\n"
        b"  End\n"
        b"  ProjectileNugget\n"
        b"    ProjectileTemplateName = FixtureRevealer\n"
        b"    WarheadTemplateName = FixtureDummyWarhead\n"
        b"  End\n"
        b"End\n"
        b"\nWeapon FixtureRockWarhead\n"
        b"  DamageNugget\n"
        b"    Damage = 250\n"
        b"    Radius = 20.0\n"
        b"    DamageType = SIEGE\n"
        b"  End\n"
        b"End\n"
        b"\nWeapon FixtureDummyWarhead\n"
        b"End\n",
        "FixtureRockThrow",
    )

    descriptor = compile_playable_unit_descriptor("AbilityHero", documents)

    validate_playable_unit_descriptor(descriptor)
    toggle = _abilities_by_id(descriptor)["Command_FixtureToggle"]
    assert toggle["implementation"]["status"] == "implemented"
    profile = descriptor["gameplay"]["simulation"]["resolved"]["weaponModes"][
        "weaponset_toggle_1"
    ]
    assert profile["damage"]["value"] == 250
    assert profile["damageWarheadIds"] == ["FixtureRockWarhead"]
    assert profile["emptyWarheadIds"] == ["FixtureDummyWarhead"]


def test_weapon_mode_damage_fails_closed_when_a_warhead_is_unsupported() -> None:
    # A warhead that authors a payload we cannot convert is a real gap, and
    # must stay one -- only a warhead with no authored nuggets at all is
    # treated as the authored no-op.
    documents = _weapon_toggle_documents(
        b"\nWeapon FixtureFloodThrow\n"
        b"  AttackRange = 400.0\n"
        b"  ProjectileNugget\n"
        b"    ProjectileTemplateName = FixtureRock\n"
        b"    WarheadTemplateName = FixtureFloodWarhead\n"
        b"  End\n"
        b"End\n"
        b"\nWeapon FixtureFloodWarhead\n"
        b"  FireLogicNugget\n"
        b"    LogicType = DECREASE_BURN_RATE\n"
        b"    Radius = 40.0\n"
        b"    Damage = 10\n"
        b"  End\n"
        b"End\n",
        "FixtureFloodThrow",
    )

    descriptor = compile_playable_unit_descriptor("AbilityHero", documents)

    validate_playable_unit_descriptor(descriptor)
    toggle = _abilities_by_id(descriptor)["Command_FixtureToggle"]
    assert toggle["implementation"]["status"] == "unimplemented"
    assert "FixtureFloodWarhead" in toggle["implementation"]["reason"]


def test_lock_weapon_create_projects_permanent_primary_slot() -> None:
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
        "  Behavior = LockWeaponCreate ModuleTag_LockWeapon\n"
        "    SlotToLock = PRIMARY\n"
        "  End\n",
        1,
    ).encode()

    descriptor = compile_playable_unit_descriptor("AbilityHero", documents)

    validate_playable_unit_descriptor(descriptor)
    resolved = descriptor["gameplay"]["simulation"]["resolved"]
    assert resolved["combat"]["weaponSlot"] == "PRIMARY"
    assert resolved["permanentWeaponLocks"] == [
        {
            "slot": "PRIMARY",
            "state": "LOCKED_PERMANENTLY",
            "module": "LockWeaponCreate",
            "sourceIni": "data/ini/object/units/test_units.ini",
            "line": resolved["permanentWeaponLocks"][0]["line"],
        }
    ]
    lock_evidence = [
        row
        for row in descriptor["runtimeModuleEvidence"]
        if row["kind"] == "LockWeaponCreate"
    ]
    assert len(lock_evidence) == 1
    assert lock_evidence[0]["consumed"] is True
    assert "LockWeaponCreate" not in descriptor["specialCapabilities"]

    corrupted = deepcopy(descriptor)
    corrupted["gameplay"]["simulation"]["resolved"]["permanentWeaponLocks"][0][
        "sourceIni"
    ] = ""
    corrupted["descriptorSha256"] = hashlib.sha256(
        json.dumps(
            {
                key: value
                for key, value in corrupted.items()
                if key != "descriptorSha256"
            },
            sort_keys=True,
            separators=(",", ":"),
            ensure_ascii=False,
        ).encode("utf-8")
    ).hexdigest()
    with pytest.raises(
        PlayableUnitCompilerError,
        match="LockWeaponCreate policy evidence is invalid",
    ):
        validate_playable_unit_descriptor(corrupted)


def test_lock_weapon_create_rejects_slots_outside_retail_corpus() -> None:
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
        "  Behavior = LockWeaponCreate ModuleTag_LockWeapon\n"
        "    SlotToLock = SECONDARY\n"
        "  End\n",
        1,
    ).encode()

    with pytest.raises(
        PlayableUnitCompilerError, match="outside the retail corpus"
    ):
        compile_playable_unit_descriptor("AbilityHero", documents)


@pytest.mark.parametrize(
    ("body", "message"),
    (
        (
            "  Behavior = LockWeaponCreate ModuleTag_LockWeapon\n"
            "  End\n",
            "exactly one SlotToLock",
        ),
        (
            "  Behavior = LockWeaponCreate ModuleTag_LockWeapon\n"
            "    SlotToLock = PRIMARY\n"
            "    SlotToLock = PRIMARY\n"
            "  End\n",
            "exactly one SlotToLock",
        ),
        (
            "  Behavior = LockWeaponCreate ModuleTag_LockWeaponA\n"
            "    SlotToLock = PRIMARY\n"
            "  End\n"
            "  Behavior = LockWeaponCreate ModuleTag_LockWeaponB\n"
            "    SlotToLock = PRIMARY\n"
            "  End\n",
            "multiple effective LockWeaponCreate",
        ),
    ),
)
def test_lock_weapon_create_rejects_malformed_or_ambiguous_modules(
    body: str, message: str
) -> None:
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
        + body,
        1,
    ).encode()

    with pytest.raises(PlayableUnitCompilerError, match=message):
        compile_playable_unit_descriptor("AbilityHero", documents)


def test_lock_weapon_create_inherited_module_tag_replaces_exactly_once() -> None:
    documents = _hero_ability_documents()
    path = "data/ini/object/units/test_units.ini"
    text = documents[path].decode()
    text = text.replace(
        "  WeaponSet\n"
        "    Conditions = None\n"
        "    Weapon = PRIMARY AbilityHeroSword\n"
        "  End\n",
        "  WeaponSet\n"
        "    Conditions = None\n"
        "    Weapon = PRIMARY AbilityHeroSword\n"
        "  End\n"
        "  Behavior = LockWeaponCreate ModuleTag_LockWeapon\n"
        "    SlotToLock = PRIMARY\n"
        "  End\n",
        1,
    )
    text += (
        "\nChildObject AbilityHeroSummoned AbilityHero\n"
        "  Behavior = LockWeaponCreate ModuleTag_LockWeapon\n"
        "    SlotToLock = PRIMARY\n"
        "  End\n"
        "End\n"
    )
    documents[path] = text.encode()
    prepared = prepare_playable_unit_compiler(documents)
    lineage = _ancestry(
        prepared.objects, prepared.objects["abilityherosummoned"]
    )
    weapon_id = _default_set_target(lineage, "WeaponSet", "Weapon")

    locks = _permanent_weapon_locks(lineage, weapon_id)

    assert len(locks) == 1
    assert locks[0]["slot"] == "PRIMARY"
    child_line = (
        text[: text.index("ChildObject AbilityHeroSummoned")].count("\n") + 1
    )
    assert locks[0]["line"] > child_line


def test_weapon_toggle_with_unresolvable_weapon_stays_a_gap() -> None:
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
        "    Weapon = PRIMARY MissingToggleWeapon\n"
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

    descriptor = compile_playable_unit_descriptor("AbilityHero", documents)

    validate_playable_unit_descriptor(descriptor)
    toggle = _abilities_by_id(descriptor)["Command_FixtureToggle"]
    assert toggle["implementation"]["status"] == "unimplemented"
    assert "MissingToggleWeapon" in toggle["implementation"]["reason"]
    assert toggle["effect"] == {"kind": "none"}
    resolved = descriptor["gameplay"]["simulation"]["resolved"]
    assert "weaponModes" not in resolved
    gaps = resolved["weaponModeGaps"]
    assert gaps[0]["mode"] == "weaponset_toggle_1"
    assert "MissingToggleWeapon" in gaps[0]["reason"]


def _mounted_hero_documents() -> dict[str, bytes]:
    """Fixture hero extended with the authored mounted state (Theoden shape)."""

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
        "    Conditions = MOUNTED\n"
        "    Weapon = PRIMARY FixtureSwordMounted\n"
        "  End\n"
        "  ArmorSet\n"
        "    Conditions = MOUNTED\n"
        "    Armor = HeroArmorMounted\n"
        "  End\n"
        "  LocomotorSet\n"
        "    Locomotor = FixtureHorseLocomotor\n"
        "    Condition = SET_MOUNTED\n"
        "    Speed = 90\n"
        "  End\n",
        1,
    ).encode()
    documents["data/ini/weapon.ini"] += (
        b"\nWeapon FixtureSwordMounted\n"
        b"  MeleeWeapon = Yes\n"
        b"  AttackRange = 25.0\n"
        b"  DelayBetweenShots = 1400\n"
        b"  PreAttackDelay = 500\n"
        b"  FiringDuration = 500\n"
        b"  DamageNugget\n"
        b"    Damage = 150\n"
        b"    DamageType = HERO\n"
        b"  End\n"
        b"End\n"
    )
    return documents


def test_mount_toggle_compiles_from_authored_mounted_state() -> None:
    documents = _mounted_hero_documents()

    descriptor = compile_playable_unit_descriptor("AbilityHero", documents)

    validate_playable_unit_descriptor(descriptor)
    mount = _abilities_by_id(descriptor)["Command_FixtureMount"]
    assert mount["implementation"]["status"] == "implemented"
    effect = mount["effect"]
    assert effect["kind"] == "mount-toggle"
    assert effect["mountedSpeed"] == 90
    assert effect["mountedLocomotorId"] == "FixtureHorseLocomotor"
    assert effect["mountedWeaponModeKey"] == "mounted"
    assert effect["mountedWeaponId"] == "FixtureSwordMounted"
    assert effect["unpackMs"] == 1000
    assert effect["packMs"] == 0
    assert any(
        "MOUNTED ArmorSet is not applied" in item
        for item in mount["implementation"]["limitations"]
    )
    modes = descriptor["gameplay"]["simulation"]["resolved"]["weaponModes"]
    profile = modes["mounted"]
    assert profile["weaponId"] == "FixtureSwordMounted"
    assert profile["attackRange"]["value"] == 25.0
    assert profile["damage"]["value"] == 150
    assert profile["delayBetweenShotsMs"]["value"] == 1400


def test_mount_toggle_without_mounted_weapon_keeps_foot_weapon() -> None:
    documents = _mounted_hero_documents()
    text = documents["data/ini/object/units/test_units.ini"].decode()
    documents["data/ini/object/units/test_units.ini"] = text.replace(
        "  WeaponSet\n"
        "    Conditions = MOUNTED\n"
        "    Weapon = PRIMARY FixtureSwordMounted\n"
        "  End\n",
        "",
        1,
    ).encode()

    descriptor = compile_playable_unit_descriptor("AbilityHero", documents)

    validate_playable_unit_descriptor(descriptor)
    mount = _abilities_by_id(descriptor)["Command_FixtureMount"]
    assert mount["implementation"]["status"] == "implemented"
    effect = mount["effect"]
    assert effect["kind"] == "mount-toggle"
    assert effect["mountedSpeed"] == 90
    assert "mountedWeaponModeKey" not in effect
    assert any(
        "keeps the foot weapon" in item
        for item in mount["implementation"]["limitations"]
    )


def _capture_hero_documents() -> dict[str, bytes]:
    """Fixture hero extended with the retail CaptureBuilding.inc shape."""

    documents = _hero_ability_documents()
    _with_hero_modules(
        documents,
        "  Behavior = SpecialPowerModule ModuleTag_CaptureBuilding\n"
        "    SpecialPowerTemplate = SpecialAbilityCaptureBuilding\n"
        "    UpdateModuleStartsAttack = Yes\n"
        "    StartsPaused = No\n"
        "  End\n"
        "  Behavior = SpecialAbilityUpdate ModuleTag_CaptureBuildingUpdate\n"
        "    SpecialPowerTemplate = SpecialAbilityCaptureBuilding\n"
        "    StartAbilityRange = 15.0\n"
        "    UnpackTime = 1\n"
        "    PreparationTime = 15000\n"
        "    PackTime = 1\n"
        "    DoCaptureFX = Yes\n"
        "  End\n",
    )
    command_sets = documents["data/ini/commandset.ini"].decode()
    documents["data/ini/commandset.ini"] = command_sets.replace(
        "  9 = Command_FixtureBroken\nEnd",
        "  9 = Command_FixtureBroken\n  10 = Command_CaptureBuilding\nEnd",
        1,
    ).encode()
    documents["data/ini/commandbutton.ini"] += (
        b"\nCommandButton Command_CaptureBuilding\n"
        b"  Command = SPECIAL_POWER\n"
        b"  SpecialPower = SpecialAbilityCaptureBuilding\n"
        b"  Options = NEED_TARGET_ENEMY_OBJECT\n"
        b"  TextLabel = CONTROLBAR:CaptureBuilding\n"
        b"  DescriptLabel = CONTROLBAR:ToolTipCaptureBuilding\n"
        b"  ButtonImage = HSCaptureBuilding\n"
        b"End\n"
    )
    documents["data/ini/specialpower.ini"] += (
        b"\nSpecialPower SpecialAbilityCaptureBuilding\n"
        b"  Enum = SPECIAL_INFANTRY_CAPTURE_BUILDING\n"
        b"  ReloadTime = 0\n"
        b"End\n"
    )
    return documents


def test_capture_building_compiles_the_channel_envelope() -> None:
    documents = _capture_hero_documents()

    descriptor = compile_playable_unit_descriptor("AbilityHero", documents)

    validate_playable_unit_descriptor(descriptor)
    capture = _abilities_by_id(descriptor)["Command_CaptureBuilding"]
    assert capture["implementation"]["status"] == "implemented"
    assert capture["targeting"] == "enemy-object"
    assert capture["cooldownMs"] == 0
    effect = capture["effect"]
    assert effect["kind"] == "capture-building"
    assert effect["startAbilityRange"] == 15.0
    assert effect["unpackMs"] == 1
    assert effect["preparationMs"] == 15000
    assert effect["packMs"] == 1
    assert effect["doCaptureFx"] is True
    assert any(
        "tier-1" in item for item in capture["implementation"]["limitations"]
    )


def test_capture_building_binds_through_the_authored_include() -> None:
    # Retail authors capture via an object-body #include (CaptureBuilding.inc);
    # the compiler expands authored includes one level deep for module binding.
    documents = _capture_hero_documents()
    path = "data/ini/object/units/test_units.ini"
    text = documents[path].decode()
    modules = (
        "  Behavior = SpecialPowerModule ModuleTag_CaptureBuilding\n"
        "    SpecialPowerTemplate = SpecialAbilityCaptureBuilding\n"
        "    UpdateModuleStartsAttack = Yes\n"
        "    StartsPaused = No\n"
        "  End\n"
        "  Behavior = SpecialAbilityUpdate ModuleTag_CaptureBuildingUpdate\n"
        "    SpecialPowerTemplate = SpecialAbilityCaptureBuilding\n"
        "    StartAbilityRange = 15.0\n"
        "    UnpackTime = 1\n"
        "    PreparationTime = 15000\n"
        "    PackTime = 1\n"
        "    DoCaptureFX = Yes\n"
        "  End\n"
    )
    assert modules in text
    documents[path] = text.replace(
        modules,
        '  #include "..\\includes\\CaptureBuilding.inc"\n',
        1,
    ).encode()
    documents["data/ini/object/includes/capturebuilding.inc"] = modules.encode()

    descriptor = compile_playable_unit_descriptor("AbilityHero", documents)

    validate_playable_unit_descriptor(descriptor)
    capture = _abilities_by_id(descriptor)["Command_CaptureBuilding"]
    assert capture["implementation"]["status"] == "implemented"
    assert capture["effect"]["kind"] == "capture-building"
    assert capture["effect"]["preparationMs"] == 15000


def test_capture_building_without_channel_module_stays_a_gap() -> None:
    documents = _capture_hero_documents()
    path = "data/ini/object/units/test_units.ini"
    text = documents[path].decode()
    documents[path] = text.replace(
        "  Behavior = SpecialAbilityUpdate ModuleTag_CaptureBuildingUpdate\n"
        "    SpecialPowerTemplate = SpecialAbilityCaptureBuilding\n"
        "    StartAbilityRange = 15.0\n"
        "    UnpackTime = 1\n"
        "    PreparationTime = 15000\n"
        "    PackTime = 1\n"
        "    DoCaptureFX = Yes\n"
        "  End\n",
        "",
        1,
    ).encode()

    descriptor = compile_playable_unit_descriptor("AbilityHero", documents)

    validate_playable_unit_descriptor(descriptor)
    capture = _abilities_by_id(descriptor)["Command_CaptureBuilding"]
    assert capture["implementation"]["status"] == "unimplemented"
    assert "StartAbilityRange" in capture["implementation"]["reason"]
    assert capture["effect"] == {"kind": "none"}


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


def test_highlander_body_policy_tracks_effective_primary_body() -> None:
    path = "data/ini/object/units/test_units.ini"

    inherited = _documents()
    inherited[path] = inherited[path].replace(
        b"Object InfantryMember\n",
        b"Object InfantryMember\n"
        b"  Body = HighlanderBody ModuleTag_Body\n"
        b"    MaxHealth = 200\n"
        b"  End\n",
        1,
    ).replace(
        b"Object ReplacementMember\n",
        b"ChildObject ReplacementMember InfantryMember\n",
        1,
    )
    inherited_descriptor = compile_playable_unit_descriptor(
        "ChildHorde", inherited
    )
    validate_playable_unit_descriptor(inherited_descriptor)
    inherited_resolved = inherited_descriptor["gameplay"]["simulation"]["resolved"]
    assert inherited_resolved["highlanderBody"]["value"] is True
    assert inherited_resolved["highlanderBody"]["module"] == "HighlanderBody"
    assert "module" not in inherited_resolved["memberHealth"]

    replaced_with_active = deepcopy(inherited)
    replaced_with_active[path] = replaced_with_active[path].replace(
        b"ChildObject ReplacementMember InfantryMember\n",
        b"ChildObject ReplacementMember InfantryMember\n"
        b"  Body = ActiveBody ModuleTag_Body\n"
        b"    MaxHealth = 150\n"
        b"  End\n",
        1,
    )
    active_descriptor = compile_playable_unit_descriptor(
        "ChildHorde", replaced_with_active
    )
    active_resolved = active_descriptor["gameplay"]["simulation"]["resolved"]
    assert active_resolved["memberHealth"]["value"] == 150
    assert "highlanderBody" not in active_resolved

    replaced_with_highlander = _documents()
    replaced_with_highlander[path] = replaced_with_highlander[path].replace(
        b"Object InfantryMember\n",
        b"Object InfantryMember\n"
        b"  Body = ActiveBody ModuleTag_Body\n"
        b"    MaxHealth = 200\n"
        b"  End\n",
        1,
    ).replace(
        b"Object ReplacementMember\n",
        b"ChildObject ReplacementMember InfantryMember\n"
        b"  Body = HighlanderBody ModuleTag_Body\n"
        b"    MaxHealth = 125\n"
        b"  End\n",
        1,
    )
    highlander_descriptor = compile_playable_unit_descriptor(
        "ChildHorde", replaced_with_highlander
    )
    highlander_resolved = highlander_descriptor["gameplay"]["simulation"]["resolved"]
    assert highlander_resolved["memberHealth"]["value"] == 125
    assert highlander_resolved["highlanderBody"]["value"] is True
    assert highlander_resolved["highlanderBody"]["module"] == "HighlanderBody"

    corrupted = deepcopy(highlander_descriptor)
    corrupted["gameplay"]["simulation"]["resolved"]["highlanderBody"]["module"] = (
        "ActiveBody"
    )
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
    with pytest.raises(PlayableUnitCompilerError, match="HighlanderBody policy"):
        validate_playable_unit_descriptor(corrupted)


@pytest.mark.parametrize(
    ("authored_filter", "excluded"),
    [
        ("", []),
        ("    DeathTypes = ALL\n", []),
        ("    DeathTypes = ALL -TOPPLED\n", ["TOPPLED"]),
    ],
)
def test_destroy_die_compiles_measured_filters_and_consumes_module(
    authored_filter: str, excluded: list[str]
) -> None:
    documents = _documents()
    path = "data/ini/object/units/test_units.ini"
    documents[path] = documents[path].replace(
        b"Object HeroUnit\n",
        (
            "Object HeroUnit\n"
            "  Behavior = DestroyDie ModuleTag_ImmediateRemoval\n"
            f"{authored_filter}"
            "  End\n"
        ).encode("utf-8"),
        1,
    )

    descriptor = compile_playable_unit_descriptor("HeroUnit", documents)
    policies = descriptor["gameplay"]["simulation"]["resolved"]["destroyDie"]
    assert len(policies) == 1
    assert policies[0] == {
        "ownerRole": "object",
        "module": "DestroyDie",
        "deathTypes": "ALL",
        "excludedDeathTypes": excluded,
        "sourceIni": path,
        "line": policies[0]["line"],
    }
    assert policies[0]["line"] > 0
    evidence = [
        row
        for row in descriptor["runtimeModuleEvidence"]
        if row["kind"] == "DestroyDie"
    ]
    assert len(evidence) == 1
    assert evidence[0]["consumed"] is True
    assert "DestroyDie" not in descriptor["specialCapabilities"]
    validate_playable_unit_descriptor(descriptor)
    if excluded:
        # The two measured ALL -TOPPLED retail carriers are cinematic and are
        # not materialized by the playable-unit runtime. This assertion is
        # compiler-consumption evidence, not a runtime execution claim.
        assert policies[0]["ownerRole"] == "object"


@pytest.mark.parametrize(
    "unsupported_body",
    [
        "    DeathTypes = NONE +TOPPLED\n",
        "    VeterancyLevels = LEVEL_1\n",
        "    ExemptStatus = UNDER_CONSTRUCTION\n",
    ],
)
def test_destroy_die_refuses_unmeasured_diemux_shapes(
    unsupported_body: str,
) -> None:
    documents = _documents()
    path = "data/ini/object/units/test_units.ini"
    documents[path] = documents[path].replace(
        b"Object HeroUnit\n",
        (
            "Object HeroUnit\n"
            "  Behavior = DestroyDie ModuleTag_Unsupported\n"
            f"{unsupported_body}"
            "  End\n"
        ).encode("utf-8"),
        1,
    )
    with pytest.raises(
        PlayableUnitCompilerError,
        match=r"DestroyDie .* unsupported",
    ):
        compile_playable_unit_descriptor("HeroUnit", documents)


def test_auto_acquire_enemies_when_idle_compiles_exact_flags() -> None:
    documents = _documents()
    path = "data/ini/object/units/test_units.ini"
    documents[path] = documents[path].replace(
        b"Object InfantryHorde\n",
        (
            b"Object InfantryHorde\n"
            b"  Behavior = HordeAIUpdate ModuleTag_AI\n"
            b"    AutoAcquireEnemiesWhenIdle = Yes ATTACK_BUILDINGS STEALTHED\n"
            b"  End\n"
        ),
        1,
    )

    descriptor = compile_playable_unit_descriptor("InfantryHorde", documents)

    validate_playable_unit_descriptor(descriptor)
    contract = descriptor["gameplay"]["simulation"]["resolved"][
        "autoAcquireEnemiesWhenIdle"
    ]
    assert contract["enabled"]["value"] is True
    assert contract["attackBuildings"]["value"] is True
    assert contract["whileStealthed"]["value"] is True
    assert contract["sourceIni"] == path

    absent = compile_playable_unit_descriptor("RangedHorde", documents)
    assert (
        "autoAcquireEnemiesWhenIdle"
        not in absent["gameplay"]["simulation"]["resolved"]
    )

    # Aggregate ownership: a horde's HordeAIUpdate wins over its payload
    # member's different AIUpdateInterface policy.
    documents[path] = documents[path].replace(
        b"Object InfantryMember\n",
        (
            b"Object InfantryMember\n"
            b"  Behavior = AIUpdateInterface ModuleTag_MemberAI\n"
            b"    AutoAcquireEnemiesWhenIdle = No\n"
            b"  End\n"
        ),
        1,
    )
    descriptor = compile_playable_unit_descriptor("InfantryHorde", documents)
    contract = descriptor["gameplay"]["simulation"]["resolved"][
        "autoAcquireEnemiesWhenIdle"
    ]
    assert contract["enabled"]["value"] is True
    assert contract["attackBuildings"]["value"] is True
    assert contract["whileStealthed"]["value"] is True

    # A singleton porter-style concrete subclass owns the same interface
    # field; it must not fall back to legacy enabled behavior.
    documents = _documents()
    documents[path] = documents[path].replace(
        b"Object SiegeUnit\n",
        (
            b"Object SiegeUnit\n"
            b"  Behavior = DozerAIUpdate ModuleTag_DozerAI\n"
            b"    AutoAcquireEnemiesWhenIdle = No\n"
            b"  End\n"
        ),
        1,
    )
    descriptor = compile_playable_unit_descriptor("SiegeUnit", documents)
    contract = descriptor["gameplay"]["simulation"]["resolved"][
        "autoAcquireEnemiesWhenIdle"
    ]
    assert contract["enabled"]["value"] is False
    assert contract["attackBuildings"]["value"] is False
    assert contract["whileStealthed"]["value"] is False

    # Retail cinematic/dragon objects author No with modifier bits. Preserve
    # those bits even though enabled=false makes them inert at runtime.
    documents = _documents()
    documents[path] = documents[path].replace(
        b"Object MonsterUnit\n",
        (
            b"Object MonsterUnit\n"
            b"  Behavior = GiantBirdAIUpdate ModuleTag_BirdAI\n"
            b"    AutoAcquireEnemiesWhenIdle = No ATTACK_BUILDINGS\n"
            b"  End\n"
        ),
        1,
    )
    descriptor = compile_playable_unit_descriptor("MonsterUnit", documents)
    contract = descriptor["gameplay"]["simulation"]["resolved"][
        "autoAcquireEnemiesWhenIdle"
    ]
    assert contract["enabled"]["value"] is False
    assert contract["attackBuildings"]["value"] is True
    assert contract["whileStealthed"]["value"] is False


@pytest.mark.parametrize(
    "value",
    (
        "Yes ATTACK_BUILDINGS ATTACK_BUILDINGS",
        "Yes ATTACK_BUILDINGS UNKNOWN_FLAG",
        "Yes,ATTACK_BUILDINGS",
    ),
)
def test_auto_acquire_enemies_when_idle_rejects_invalid_tokens(value: str) -> None:
    documents = _documents()
    path = "data/ini/object/units/test_units.ini"
    documents[path] = documents[path].replace(
        b"Object InfantryHorde\n",
        (
            "Object InfantryHorde\n"
            "  Behavior = HordeAIUpdate ModuleTag_AI\n"
            f"    AutoAcquireEnemiesWhenIdle = {value}\n"
            "  End\n"
        ).encode("utf-8"),
        1,
    )

    with pytest.raises(PlayableUnitCompilerError, match="AutoAcquireEnemiesWhenIdle"):
        compile_playable_unit_descriptor("InfantryHorde", documents)


@pytest.mark.parametrize("milliseconds", (20, 250, 500, 2500, 5000))
def test_mood_attack_check_rate_compiles_exact_milliseconds(
    milliseconds: int,
) -> None:
    documents = _documents()
    path = "data/ini/object/units/test_units.ini"
    documents[path] = documents[path].replace(
        b"Object InfantryHorde\n",
        (
            "Object InfantryHorde\n"
            "  Behavior = HordeAIUpdate ModuleTag_AI\n"
            "    AutoAcquireEnemiesWhenIdle = Yes\n"
            f"    MoodAttackCheckRate = {milliseconds}\n"
            "  End\n"
        ).encode("utf-8"),
        1,
    )

    descriptor = compile_playable_unit_descriptor("InfantryHorde", documents)
    contract = descriptor["gameplay"]["simulation"]["resolved"][
        "moodAttackCheckRate"
    ]
    assert contract["milliseconds"]["value"] == milliseconds
    assert contract["sourceIni"] == path
    assert contract["semantic"] == "AIUpdateInterface.MoodAttackCheckRate"
    absent = compile_playable_unit_descriptor("RangedHorde", documents)
    assert "moodAttackCheckRate" not in absent["gameplay"]["simulation"]["resolved"]


@pytest.mark.parametrize("value", ("0", "-1", "1.5", "RATE_500", "500 ms"))
def test_mood_attack_check_rate_rejects_non_retail_forms(value: str) -> None:
    documents = _documents()
    path = "data/ini/object/units/test_units.ini"
    documents[path] = documents[path].replace(
        b"Object InfantryHorde\n",
        (
            "Object InfantryHorde\n"
            "  Behavior = HordeAIUpdate ModuleTag_AI\n"
            "    AutoAcquireEnemiesWhenIdle = Yes\n"
            f"    MoodAttackCheckRate = {value}\n"
            "  End\n"
        ).encode("utf-8"),
        1,
    )
    with pytest.raises(PlayableUnitCompilerError, match="MoodAttackCheckRate"):
        compile_playable_unit_descriptor("InfantryHorde", documents)


def test_mood_attack_check_rate_requires_policy_and_refuses_conflicts() -> None:
    documents = _documents()
    path = "data/ini/object/units/test_units.ini"
    documents[path] = documents[path].replace(
        b"Object InfantryHorde\n",
        (
            b"Object InfantryHorde\n"
            b"  Behavior = HordeAIUpdate ModuleTag_AI\n"
            b"    MoodAttackCheckRate = 500\n"
            b"  End\n"
        ),
        1,
    )
    with pytest.raises(
        PlayableUnitCompilerError,
        match="without AutoAcquireEnemiesWhenIdle",
    ):
        compile_playable_unit_descriptor("InfantryHorde", documents)

    documents[path] = documents[path].replace(
        b"    MoodAttackCheckRate = 500\n",
        (
            b"    AutoAcquireEnemiesWhenIdle = Yes\n"
            b"    MoodAttackCheckRate = 500\n"
            b"  End\n"
            b"  Behavior = HordeAIUpdate ModuleTag_ConflictingAI\n"
            b"    AutoAcquireEnemiesWhenIdle = Yes\n"
            b"    MoodAttackCheckRate = 250\n"
        ),
        1,
    )
    with pytest.raises(
        PlayableUnitCompilerError,
        match="disagree on MoodAttackCheckRate",
    ):
        compile_playable_unit_descriptor("InfantryHorde", documents)


# ---------------------------------------------------------------------------
# Batch-3 ability families: measured-field emission (experience-grant,
# arrow-storm, stealth-toggle, teleport, curse, leadership-strip).  Fixture
# magnitudes mirror the retail BFME2 corpus rows they pin (King's Favor,
# Legolas Arrow Storm, Thranduil Wild Walk / Move Unseen, Shelob Tunnel,
# Hour of the Witch-King, Horn of Gondor).
# ---------------------------------------------------------------------------

_BATCH3_BEHAVIORS = (
    "  Behavior = LevelGrantSpecialPower ModuleTag_KingsFavor\n"
    "    SpecialPowerTemplate = SpecialAbilityKingsFavor\n"
    "    UnpackingVariation = 2\n"
    "    StartAbilityRange = 200.0\n"
    "    LevelFX = FX_LevelUp\n"
    "    Experience = 50\n"
    "    RadiusEffect = 150\n"
    "    AcceptanceFilter = KINGSFAVOR_OBJECTFILTER\n"
    "    UnpackTime = 3000\n"
    "  End\n"
    "  Behavior = SpecialPowerModule ModuleTag_ArrowStormStarter\n"
    "    SpecialPowerTemplate = SpecialAbilityArrowStorm\n"
    "    UpdateModuleStartsAttack = Yes\n"
    "  End\n"
    "  Behavior = ArrowStormUpdate ModuleTag_ArrowStormUpdate\n"
    "    SpecialPowerTemplate = SpecialAbilityArrowStorm\n"
    "    StartAbilityRange = 320.0\n"
    "    PersistentPrepTime = 600\n"
    "    WeaponTemplate = FixtureBowArrowStorm\n"
    "    TargetRadius = 120\n"
    "    ShotsPerTarget = 1\n"
    "    ShotsPerBurst = 3\n"
    "    MaxShots = 50\n"
    "    CanShootEmptyGround = Yes\n"
    "  End\n"
    "  Behavior = SpecialPowerModule ModuleTag_WildWalkStarter\n"
    "    SpecialPowerTemplate = SpecialAbilityWildWalk\n"
    "    UpdateModuleStartsAttack = Yes\n"
    "  End\n"
    "  Behavior = ToggleHiddenSpecialAbilityUpdate ModuleTag_WildWalkUpdate\n"
    "    SpecialPowerTemplate = SpecialAbilityWildWalk\n"
    "    EffectDuration = 80000\n"
    "    ShowPalantirTimer = Yes\n"
    "  End\n"
    "  Behavior = InvisibilityUpdate ModuleTag_WildWalk\n"
    "    InvisibilityNugget\n"
    "      InvisibilityType = STEALTH\n"
    "      ForbiddenConditions = TAKING_DAMAGE USING_ABILITY\n"
    "    End\n"
    "    StartsActive = No\n"
    "    UpdatePeriod = 2000\n"
    "  End\n"
    "  Behavior = InvisibilitySpecialPower ModuleTag_MoveUnseen\n"
    "    SpecialPowerTemplate = SpecialAbilityMoveUnseen\n"
    "    BroadcastRadius = THRANDUIL_MOVEUNSEEN_EFFECT_RADIUS\n"
    "    ObjectFilter = ANY +HORDE +HERO +DOZER ALLIES\n"
    "    Duration = 30000\n"
    "    InvisibilityNugget\n"
    "      ForbiddenConditions = FIRING_ANY\n"
    "      InvisibilityType = CAMOUFLAGE\n"
    "    End\n"
    "  End\n"
    "  Behavior = SpecialPowerModule ModuleTag_TeleportStarter\n"
    "    SpecialPowerTemplate = SpecialAbilityFixtureTunnel\n"
    "    UpdateModuleStartsAttack = Yes\n"
    "  End\n"
    "  Behavior = TeleportSpecialAbilityUpdate ModuleTag_TeleportUpdate\n"
    "    SpecialPowerTemplate = SpecialAbilityFixtureTunnel\n"
    "    UnpackTime = 1800\n"
    "    PackTime = 1300\n"
    "    BusyForDuration = 1800\n"
    "    MaxDistance = WILD_SHELOB_TUNNEL_DISTANCE\n"
    "  End\n"
    "  Behavior = CurseSpecialPower ModuleTag_CurseUpdate\n"
    "    SpecialPowerTemplate = SpecialAbilityCurseEnemy\n"
    "    CursePercentage = 100.0%\n"
    "    StartAbilityRange = 200.0\n"
    "    CursedFX = FX_FixtureCursed\n"
    "  End\n"
    "  Behavior = SpecialPowerModule ModuleTag_HornStarter\n"
    "    SpecialPowerTemplate = SpecialAbilityHornOfGondor\n"
    "    UpdateModuleStartsAttack = Yes\n"
    "    AntiCategory = LEADERSHIP\n"
    "    AttributeModifier = FixtureHornAntiCategory\n"
    "    AttributeModifierRange = 70.0\n"
    "  End\n"
    "  Behavior = ModelConditionSpecialAbilityUpdate ModuleTag_HornUpdate\n"
    "    SpecialPowerTemplate = SpecialAbilityHornOfGondor\n"
    "    UnpackTime = 1700\n"
    "  End\n"
)


def _batch3_hero_documents() -> dict[str, bytes]:
    documents = _hero_ability_documents()
    _with_hero_modules(documents, _BATCH3_BEHAVIORS)
    command_sets = documents["data/ini/commandset.ini"].decode()
    documents["data/ini/commandset.ini"] = command_sets.replace(
        "  9 = Command_FixtureBroken\nEnd",
        "  9 = Command_FixtureBroken\n"
        "  10 = Command_FixtureKingsFavor\n"
        "  11 = Command_FixtureArrowStorm\n"
        "  12 = Command_FixtureWildWalk\n"
        "  13 = Command_FixtureMoveUnseen\n"
        "  14 = Command_FixtureTunnel\n"
        "  15 = Command_FixtureCurse\n"
        "  16 = Command_FixtureHorn\nEnd",
        1,
    ).encode()
    documents["data/ini/commandbutton.ini"] += (
        b"\nCommandButton Command_FixtureKingsFavor\n"
        b"  Command = SPECIAL_POWER\n"
        b"  SpecialPower = SpecialAbilityKingsFavor\n"
        b"  Options = NEED_TARGET_POS\n"
        b"  TextLabel = CONTROLBAR:FixtureKingsFavor\n"
        b"  DescriptLabel = CONTROLBAR:ToolTipFixtureKingsFavor\n"
        b"  ButtonImage = HSFixtureKingsFavor\n"
        b"End\n"
        b"\nCommandButton Command_FixtureArrowStorm\n"
        b"  Command = SPECIAL_POWER\n"
        b"  SpecialPower = SpecialAbilityArrowStorm\n"
        b"  Options = NEED_TARGET_POS\n"
        b"  TextLabel = CONTROLBAR:FixtureArrowStorm\n"
        b"  DescriptLabel = CONTROLBAR:ToolTipFixtureArrowStorm\n"
        b"  ButtonImage = HSFixtureArrowStorm\n"
        b"End\n"
        b"\nCommandButton Command_FixtureWildWalk\n"
        b"  Command = SPECIAL_POWER\n"
        b"  SpecialPower = SpecialAbilityWildWalk\n"
        b"  TextLabel = CONTROLBAR:FixtureWildWalk\n"
        b"  DescriptLabel = CONTROLBAR:ToolTipFixtureWildWalk\n"
        b"  ButtonImage = HSFixtureWildWalk\n"
        b"End\n"
        b"\nCommandButton Command_FixtureMoveUnseen\n"
        b"  Command = SPECIAL_POWER\n"
        b"  SpecialPower = SpecialAbilityMoveUnseen\n"
        b"  Options = NEED_TARGET_POS\n"
        b"  TextLabel = CONTROLBAR:FixtureMoveUnseen\n"
        b"  DescriptLabel = CONTROLBAR:ToolTipFixtureMoveUnseen\n"
        b"  ButtonImage = HSFixtureMoveUnseen\n"
        b"End\n"
        b"\nCommandButton Command_FixtureTunnel\n"
        b"  Command = SPECIAL_POWER\n"
        b"  SpecialPower = SpecialAbilityFixtureTunnel\n"
        b"  Options = NEED_TARGET_POS\n"
        b"  TextLabel = CONTROLBAR:FixtureTunnel\n"
        b"  DescriptLabel = CONTROLBAR:ToolTipFixtureTunnel\n"
        b"  ButtonImage = HSFixtureTunnel\n"
        b"End\n"
        b"\nCommandButton Command_FixtureCurse\n"
        b"  Command = SPECIAL_POWER\n"
        b"  SpecialPower = SpecialAbilityCurseEnemy\n"
        b"  Options = NEED_TARGET_ENEMY_OBJECT\n"
        b"  TextLabel = CONTROLBAR:FixtureCurse\n"
        b"  DescriptLabel = CONTROLBAR:ToolTipFixtureCurse\n"
        b"  ButtonImage = HSFixtureCurse\n"
        b"End\n"
        b"\nCommandButton Command_FixtureHorn\n"
        b"  Command = SPECIAL_POWER\n"
        b"  SpecialPower = SpecialAbilityHornOfGondor\n"
        b"  TextLabel = CONTROLBAR:FixtureHorn\n"
        b"  DescriptLabel = CONTROLBAR:ToolTipFixtureHorn\n"
        b"  ButtonImage = HSFixtureHorn\n"
        b"End\n"
    )
    documents["data/ini/specialpower.ini"] += (
        b"\nSpecialPower SpecialAbilityKingsFavor\n"
        b"  Enum = SPECIAL_KINGS_FAVOR\n"
        b"  ReloadTime = 180000\n"
        b"  RadiusCursorRadius = 100.0\n"
        b"  Flags = NEEDS_OBJECT_FILTER\n"
        b"  ObjectFilter = KINGSFAVOR_OBJECTFILTER\n"
        b"End\n"
        b"SpecialPower SpecialAbilityArrowStorm\n"
        b"  Enum = SPECIAL_ARROW_STORM\n"
        b"  ReloadTime = 60000\n"
        b"  RadiusCursorRadius = 120.0\n"
        b"End\n"
        b"SpecialPower SpecialAbilityWildWalk\n"
        b"  Enum = SPECIAL_GENERAL_TARGETLESS_TWO\n"
        b"  ReloadTime = 150000\n"
        b"End\n"
        b"SpecialPower SpecialAbilityMoveUnseen\n"
        b"  Enum = SPECIAL_ARROW_STORM\n"
        b"  ReloadTime = 60000\n"
        b"  RadiusCursorRadius = THRANDUIL_MOVEUNSEEN_EFFECT_RADIUS\n"
        b"End\n"
        b"SpecialPower SpecialAbilityFixtureTunnel\n"
        b"  Enum = SPECIAL_GENERAL_TARGETLESS\n"
        b"  ReloadTime = 90000\n"
        b"End\n"
        b"SpecialPower SpecialAbilityCurseEnemy\n"
        b"  Enum = SPECIAL_CURSE_ENEMY\n"
        b"  ReloadTime = 300000\n"
        b"  RadiusCursorRadius = 50.0\n"
        b"End\n"
        b"SpecialPower SpecialAbilityHornOfGondor\n"
        b"  Enum = SPECIAL_GENERAL_TARGETLESS\n"
        b"  ReloadTime = 90000\n"
        b"End\n"
    )
    documents["data/ini/attributemodifier.ini"] += (
        b"\nModifierList FixtureHornAntiCategory\n"
        b"  Duration = 5000\n"
        b"End\n"
    )
    documents["data/ini/weapon.ini"] += (
        b"\nWeapon FixtureBowArrowStorm\n"
        b"  AttackRange = 320.0\n"
        b"  HitPercentage = 100\n"
        b"  ProjectileNugget\n"
        b"    ProjectileTemplateName = FixtureBowArrowStormProjectile\n"
        b"    WarheadTemplateName = FixtureBowArrowStormWarhead\n"
        b"  End\n"
        b"End\n"
        b"Weapon FixtureBowArrowStormWarhead\n"
        b"  HitStoredTarget = Yes\n"
        b"  DamageNugget\n"
        b"    Damage = LEGOLAS_ARROWSTORM_DAMAGE\n"
        b"    Radius = 0.0\n"
        b"    DamageType = HERO_RANGED\n"
        b"  End\n"
        b"End\n"
    )
    documents["data/ini/gamedata.ini"] += (
        b"#define KINGSFAVOR_OBJECTFILTER ANY +CAVALRY +INFANTRY -STRUCTURE"
        b" -CASTLE_KEEP -BASE_FOUNDATION -HERO -MOVE_ONLY -DOZER ALLIES\n"
        b"#define THRANDUIL_MOVEUNSEEN_EFFECT_RADIUS 50\n"
        b"#define WILD_SHELOB_TUNNEL_DISTANCE 9999999\n"
        b"#define LEGOLAS_ARROWSTORM_DAMAGE 200\n"
    )
    return documents


def test_experience_grant_rows_emit_measured_fields() -> None:
    descriptor = compile_playable_unit_descriptor(
        "AbilityHero", _batch3_hero_documents()
    )
    validate_playable_unit_descriptor(descriptor)
    row = _abilities_by_id(descriptor)["Command_FixtureKingsFavor"]
    assert row["implementation"]["status"] == "implemented"
    assert row["cooldownMs"] == 180000
    assert row["targeting"] == "point"
    assert row["specialPowerContract"]["flags"] == ["NEEDS_OBJECT_FILTER"]
    assert row["specialPowerContract"]["objectFilter"] == [
        "KINGSFAVOR_OBJECTFILTER"
    ]
    effect = row["effect"]
    assert effect["kind"] == "experience-grant"
    assert effect["experience"] == 50
    assert effect["radiusEffect"] == 150
    assert effect["startAbilityRange"] == 200.0
    assert effect["levelFxId"] == "FX_LevelUp"
    assert effect["affects"] == (
        "ANY +CAVALRY +INFANTRY -STRUCTURE -CASTLE_KEEP -BASE_FOUNDATION"
        " -HERO -MOVE_ONLY -DOZER ALLIES"
    )


def test_arrow_storm_rows_emit_measured_fields() -> None:
    descriptor = compile_playable_unit_descriptor(
        "AbilityHero", _batch3_hero_documents()
    )
    validate_playable_unit_descriptor(descriptor)
    row = _abilities_by_id(descriptor)["Command_FixtureArrowStorm"]
    assert row["implementation"]["status"] == "implemented"
    assert row["cooldownMs"] == 60000
    effect = row["effect"]
    assert effect["kind"] == "arrow-storm"
    assert effect["weaponId"] == "FixtureBowArrowStorm"
    assert effect["warheadId"] == "FixtureBowArrowStormWarhead"
    assert effect["weaponDamage"] == 200
    assert effect["targetRadius"] == 120
    assert effect["maxShots"] == 50
    assert effect["shotsPerBurst"] == 3
    assert effect["persistentPrepMs"] == 600
    assert effect["canShootEmptyGround"] is True
    assert effect["startAbilityRange"] == 320.0


def test_stealth_toggle_rows_emit_measured_fields() -> None:
    descriptor = compile_playable_unit_descriptor(
        "AbilityHero", _batch3_hero_documents()
    )
    validate_playable_unit_descriptor(descriptor)
    abilities = _abilities_by_id(descriptor)
    wild_walk = abilities["Command_FixtureWildWalk"]
    assert wild_walk["implementation"]["status"] == "implemented"
    assert wild_walk["cooldownMs"] == 150000
    effect = wild_walk["effect"]
    assert effect["kind"] == "stealth-toggle"
    assert effect["effectDurationMs"] == 80000
    assert effect["forbiddenConditions"] == ["TAKING_DAMAGE", "USING_ABILITY"]
    assert "broadcastRadius" not in effect

    move_unseen = abilities["Command_FixtureMoveUnseen"]
    assert move_unseen["implementation"]["status"] == "implemented"
    effect = move_unseen["effect"]
    assert effect["kind"] == "stealth-toggle"
    assert effect["effectDurationMs"] == 30000
    assert effect["broadcastRadius"] == 50
    assert effect["forbiddenConditions"] == ["FIRING_ANY"]
    # HORDE drops from the projected filter (buffs proxy per member).
    assert effect["affects"] == "ANY +HERO +DOZER ALLIES"


def test_toggle_hidden_without_effect_duration_is_an_untimed_cloak() -> None:
    # RotWK authors EffectDuration on exactly one ToggleHiddenSpecialAbilityUpdate
    # (wormtongue.ini). Thranduil's Elven Cloak, the Elven horde's and the
    # create-a-hero cloak all omit it: the toggle holds until the player
    # recasts it or an authored ForbiddenCondition breaks it.
    documents = _batch3_hero_documents()
    documents["data/ini/object/units/test_units.ini"] = documents[
        "data/ini/object/units/test_units.ini"
    ].replace(b"    EffectDuration = 80000\n", b"", 1)

    descriptor = compile_playable_unit_descriptor("AbilityHero", documents)

    validate_playable_unit_descriptor(descriptor)
    row = _abilities_by_id(descriptor)["Command_FixtureWildWalk"]
    assert row["implementation"]["status"] == "implemented"
    effect = row["effect"]
    assert effect["kind"] == "stealth-toggle"
    assert effect["untimed"] is True
    assert "effectDurationMs" not in effect
    assert effect["forbiddenConditions"] == ["TAKING_DAMAGE", "USING_ABILITY"]


def test_toggle_hidden_with_an_unresolvable_duration_still_fails_closed() -> None:
    documents = _batch3_hero_documents()
    documents["data/ini/object/units/test_units.ini"] = documents[
        "data/ini/object/units/test_units.ini"
    ].replace(
        b"    EffectDuration = 80000\n",
        b"    EffectDuration = INVENTED_CLOAK_DURATION\n",
        1,
    )

    descriptor = compile_playable_unit_descriptor("AbilityHero", documents)

    row = _abilities_by_id(descriptor)["Command_FixtureWildWalk"]
    assert row["implementation"]["status"] == "unimplemented"
    assert "unresolvable EffectDuration" in row["implementation"]["reason"]
    assert row["effect"] == {"kind": "none"}


def test_teleport_rows_emit_measured_fields() -> None:
    descriptor = compile_playable_unit_descriptor(
        "AbilityHero", _batch3_hero_documents()
    )
    validate_playable_unit_descriptor(descriptor)
    row = _abilities_by_id(descriptor)["Command_FixtureTunnel"]
    assert row["implementation"]["status"] == "implemented"
    assert row["cooldownMs"] == 90000
    effect = row["effect"]
    assert effect["kind"] == "teleport"
    assert effect["maxDistance"] == 9999999
    assert effect["busyForDurationMs"] == 1800


def test_curse_rows_emit_measured_fields() -> None:
    descriptor = compile_playable_unit_descriptor(
        "AbilityHero", _batch3_hero_documents()
    )
    validate_playable_unit_descriptor(descriptor)
    row = _abilities_by_id(descriptor)["Command_FixtureCurse"]
    assert row["implementation"]["status"] == "implemented"
    assert row["cooldownMs"] == 300000
    effect = row["effect"]
    assert effect["kind"] == "curse"
    assert effect["cursePercentage"] == 100.0
    assert effect["radiusCursorRadius"] == 50.0
    assert effect["startAbilityRange"] == 200.0
    assert effect["cursedFxId"] == "FX_FixtureCursed"


def test_leadership_strip_rows_emit_measured_fields() -> None:
    descriptor = compile_playable_unit_descriptor(
        "AbilityHero", _batch3_hero_documents()
    )
    validate_playable_unit_descriptor(descriptor)
    row = _abilities_by_id(descriptor)["Command_FixtureHorn"]
    assert row["implementation"]["status"] == "implemented"
    assert row["cooldownMs"] == 90000
    effect = row["effect"]
    assert effect["kind"] == "leadership-strip"
    assert effect["antiCategory"] == "LEADERSHIP"
    assert effect["modifierId"] == "FixtureHornAntiCategory"
    assert effect["attributeModifierRange"] == 70.0
    assert effect["antiCategoryDurationMs"] == 5000


def test_batch3_rows_fail_closed_on_missing_magnitudes() -> None:
    # Experience grant without an authored Experience amount.
    documents = _batch3_hero_documents()
    path = "data/ini/object/units/test_units.ini"
    documents[path] = documents[path].replace(b"    Experience = 50\n", b"", 1)
    descriptor = compile_playable_unit_descriptor("AbilityHero", documents)
    validate_playable_unit_descriptor(descriptor)
    row = _abilities_by_id(descriptor)["Command_FixtureKingsFavor"]
    assert row["implementation"]["status"] == "unimplemented"
    assert "no resolvable Experience" in row["implementation"]["reason"]
    assert row["effect"] == {"kind": "none"}

    # Arrow storm without an authored MaxShots.
    documents = _batch3_hero_documents()
    documents[path] = documents[path].replace(b"    MaxShots = 50\n", b"", 1)
    descriptor = compile_playable_unit_descriptor("AbilityHero", documents)
    validate_playable_unit_descriptor(descriptor)
    row = _abilities_by_id(descriptor)["Command_FixtureArrowStorm"]
    assert row["implementation"]["status"] == "unimplemented"
    assert "no resolvable MaxShots" in row["implementation"]["reason"]
    assert row["effect"] == {"kind": "none"}

    # Toggle-hidden without the paired InvisibilityUpdate module.
    documents = _batch3_hero_documents()
    documents[path] = documents[path].replace(
        b"  Behavior = InvisibilityUpdate ModuleTag_WildWalk\n"
        b"    InvisibilityNugget\n"
        b"      InvisibilityType = STEALTH\n"
        b"      ForbiddenConditions = TAKING_DAMAGE USING_ABILITY\n"
        b"    End\n"
        b"    StartsActive = No\n"
        b"    UpdatePeriod = 2000\n"
        b"  End\n",
        b"",
        1,
    )
    descriptor = compile_playable_unit_descriptor("AbilityHero", documents)
    validate_playable_unit_descriptor(descriptor)
    row = _abilities_by_id(descriptor)["Command_FixtureWildWalk"]
    assert row["implementation"]["status"] == "unimplemented"
    assert "InvisibilityUpdate" in row["implementation"]["reason"]
    assert row["effect"] == {"kind": "none"}

    # Teleport without an authored MaxDistance uses the engine's unlimited
    # default (Karsh Blink); omission is not a missing magnitude.
    documents = _batch3_hero_documents()
    documents[path] = documents[path].replace(
        b"    MaxDistance = WILD_SHELOB_TUNNEL_DISTANCE\n", b"", 1
    )
    descriptor = compile_playable_unit_descriptor("AbilityHero", documents)
    validate_playable_unit_descriptor(descriptor)
    row = _abilities_by_id(descriptor)["Command_FixtureTunnel"]
    assert row["implementation"]["status"] == "implemented"
    assert row["effect"]["kind"] == "teleport"
    assert "maxDistance" not in row["effect"]

    # Curse whose power authors no RadiusCursorRadius target circle.
    documents = _batch3_hero_documents()
    documents["data/ini/specialpower.ini"] = documents[
        "data/ini/specialpower.ini"
    ].replace(
        b"SpecialPower SpecialAbilityCurseEnemy\n"
        b"  Enum = SPECIAL_CURSE_ENEMY\n"
        b"  ReloadTime = 300000\n"
        b"  RadiusCursorRadius = 50.0\n"
        b"End\n",
        b"SpecialPower SpecialAbilityCurseEnemy\n"
        b"  Enum = SPECIAL_CURSE_ENEMY\n"
        b"  ReloadTime = 300000\n"
        b"End\n",
        1,
    )
    descriptor = compile_playable_unit_descriptor("AbilityHero", documents)
    validate_playable_unit_descriptor(descriptor)
    row = _abilities_by_id(descriptor)["Command_FixtureCurse"]
    assert row["implementation"]["status"] == "unimplemented"
    assert "RadiusCursorRadius" in row["implementation"]["reason"]
    assert row["effect"] == {"kind": "none"}

    # Anti-category strips other than LEADERSHIP stay recorded gaps.
    documents = _batch3_hero_documents()
    documents[path] = documents[path].replace(
        b"    AntiCategory = LEADERSHIP\n", b"    AntiCategory = SPELL\n", 1
    )
    descriptor = compile_playable_unit_descriptor("AbilityHero", documents)
    validate_playable_unit_descriptor(descriptor)
    row = _abilities_by_id(descriptor)["Command_FixtureHorn"]
    assert row["implementation"]["status"] == "unimplemented"
    assert "only a LEADERSHIP strip" in row["implementation"]["reason"]
    assert row["effect"] == {"kind": "none"}


def test_screech_rows_project_the_engine_hardcoded_terror_emotion() -> None:
    documents = _batch3_hero_documents()
    _with_hero_modules(
        documents,
        "  Behavior = SpecialPowerModule ModuleTag_ScreechStarter\n"
        "    SpecialPowerTemplate = SpecialAbilityFixtureScreech\n"
        "    UpdateModuleStartsAttack = Yes\n"
        "  End\n"
        "  Behavior = SpecialAbilityUpdate ModuleTag_ScreechUpdate\n"
        "    SpecialPowerTemplate = SpecialAbilityFixtureScreech\n"
        "    UnpackTime = 1\n"
        "    EffectRange = 180\n"
        "    PackTime = 3000\n"
        "  End\n",
    )
    command_sets = documents["data/ini/commandset.ini"].decode()
    documents["data/ini/commandset.ini"] = command_sets.replace(
        "  16 = Command_FixtureHorn\nEnd",
        "  16 = Command_FixtureHorn\n  17 = Command_FixtureScreech\nEnd",
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
        b"SpecialPower SpecialAbilityFixtureScreech\n"
        b"  Enum = SPECIAL_SCREECH\n"
        b"  ReloadTime = 180000\n"
        b"End\n"
    )
    documents["data/ini/emotions.ini"] = _FIXTURE_EMOTIONS

    descriptor = compile_playable_unit_descriptor("AbilityHero", documents)

    validate_playable_unit_descriptor(descriptor)
    row = _abilities_by_id(descriptor)["Command_FixtureScreech"]
    assert row["implementation"]["status"] == "implemented"
    effect = row["effect"]
    assert effect["kind"] == "terror"
    assert effect["radius"] == 180
    assert effect["durationMs"] == 9000
    assert effect["emotionNuggetId"] == "Terror_Base"
    assert effect["engineEnum"] == "SPECIAL_SCREECH"
    assert effect["modifiers"][0]["kind"] == "DAMAGE_MULT"
    assert effect["modifiers"][0]["value"] == 0.0
    assert any(
        "engine SPECIAL_SCREECH" in item
        for item in row["implementation"]["limitations"]
    )


def test_screech_accepts_duplicate_modules_that_author_the_same_range() -> None:
    # MordorWitchKingOnFellBeast authors SpecialAbilityScreech twice: the
    # fell-beast module tag and its own, both EffectRange = 180 with different
    # trigger sounds. Duplicate tags that agree on the range are not an
    # ambiguity, so the row compiles at the one range retail authored.
    documents = _batch3_hero_documents()
    _with_hero_modules(
        documents,
        "  Behavior = SpecialPowerModule ModuleTag_ScreechStarter\n"
        "    SpecialPowerTemplate = SpecialAbilityFixtureScreech\n"
        "    UpdateModuleStartsAttack = Yes\n"
        "  End\n"
        "  Behavior = SpecialAbilityUpdate ModuleTag_ScreechUpdate\n"
        "    SpecialPowerTemplate = SpecialAbilityFixtureScreech\n"
        "    UnpackTime = 1\n"
        "    EffectRange = 180\n"
        "    TriggerSound = FixtureScreechA\n"
        "    PackTime = 3000\n"
        "  End\n"
        "  Behavior = SpecialAbilityUpdate ModuleTag_MountScreechUpdate\n"
        "    SpecialPowerTemplate = SpecialAbilityFixtureScreech\n"
        "    UnpackTime = 1\n"
        "    EffectRange = 180\n"
        "    TriggerSound = FixtureScreechB\n"
        "    PackTime = 3000\n"
        "  End\n",
    )
    command_sets = documents["data/ini/commandset.ini"].decode()
    documents["data/ini/commandset.ini"] = command_sets.replace(
        "  16 = Command_FixtureHorn\nEnd",
        "  16 = Command_FixtureHorn\n  17 = Command_FixtureScreech\nEnd",
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
        b"SpecialPower SpecialAbilityFixtureScreech\n"
        b"  Enum = SPECIAL_SCREECH\n"
        b"  ReloadTime = 180000\n"
        b"End\n"
    )
    documents["data/ini/emotions.ini"] = _FIXTURE_EMOTIONS

    descriptor = compile_playable_unit_descriptor("AbilityHero", documents)

    validate_playable_unit_descriptor(descriptor)
    row = _abilities_by_id(descriptor)["Command_FixtureScreech"]
    assert row["implementation"]["status"] == "implemented"
    assert row["effect"]["kind"] == "terror"
    assert row["effect"]["radius"] == 180


def test_screech_still_fails_closed_when_duplicate_modules_disagree() -> None:
    documents = _batch3_hero_documents()
    _with_hero_modules(
        documents,
        "  Behavior = SpecialPowerModule ModuleTag_ScreechStarter\n"
        "    SpecialPowerTemplate = SpecialAbilityFixtureScreech\n"
        "    UpdateModuleStartsAttack = Yes\n"
        "  End\n"
        "  Behavior = SpecialAbilityUpdate ModuleTag_ScreechUpdate\n"
        "    SpecialPowerTemplate = SpecialAbilityFixtureScreech\n"
        "    UnpackTime = 1\n"
        "    EffectRange = 180\n"
        "  End\n"
        "  Behavior = SpecialAbilityUpdate ModuleTag_MountScreechUpdate\n"
        "    SpecialPowerTemplate = SpecialAbilityFixtureScreech\n"
        "    UnpackTime = 1\n"
        "    EffectRange = 240\n"
        "  End\n",
    )
    command_sets = documents["data/ini/commandset.ini"].decode()
    documents["data/ini/commandset.ini"] = command_sets.replace(
        "  16 = Command_FixtureHorn\nEnd",
        "  16 = Command_FixtureHorn\n  17 = Command_FixtureScreech\nEnd",
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
        b"SpecialPower SpecialAbilityFixtureScreech\n"
        b"  Enum = SPECIAL_SCREECH\n"
        b"  ReloadTime = 180000\n"
        b"End\n"
    )
    documents["data/ini/emotions.ini"] = _FIXTURE_EMOTIONS

    descriptor = compile_playable_unit_descriptor("AbilityHero", documents)

    row = _abilities_by_id(descriptor)["Command_FixtureScreech"]
    assert row["implementation"]["status"] == "unimplemented"
    assert "disagreeing EffectRange" in row["implementation"]["reason"]
    assert row["effect"] == {"kind": "none"}


def test_missing_reload_time_defaults_to_zero_cooldown() -> None:
    # Retail may omit ReloadTime entirely (Dain's Stubborn Pride): the engine
    # default is zero milliseconds, not an unresolvable expression.
    documents = _batch3_hero_documents()
    documents["data/ini/specialpower.ini"] = documents[
        "data/ini/specialpower.ini"
    ].replace(b"  ReloadTime = 60000\n", b"", 1)

    descriptor = compile_playable_unit_descriptor("AbilityHero", documents)

    validate_playable_unit_descriptor(descriptor)
    row = _abilities_by_id(descriptor)["Command_FixtureBlast"]
    assert row["implementation"]["status"] == "implemented"
    assert row["cooldownMs"] == 0


def test_special_power_contract_rejects_unknown_fields_fail_closed() -> None:
    documents = _batch3_hero_documents()
    documents["data/ini/specialpower.ini"] = documents[
        "data/ini/specialpower.ini"
    ].replace(
        b"  ReloadTime = 60000\n",
        b"  ReloadTime = 60000\n  InventedPowerGate = Yes\n",
        1,
    )

    descriptor = compile_playable_unit_descriptor("AbilityHero", documents)

    row = _abilities_by_id(descriptor)["Command_FixtureBlast"]
    assert row["implementation"]["status"] == "unimplemented"
    assert "inventedpowergate" in row["implementation"]["reason"]


@pytest.mark.parametrize(
    ("flags", "expected_reason"),
    [
        ("INVENTED_FLAG", "unsupported Flags"),
        ("LIMIT_DISTANCE", "has no MaxCastRange"),
        ("NEEDS_OBJECT_FILTER", "has no ObjectFilter"),
        ("NO_FORBIDDEN_OBJECTS", "has no complete ForbiddenObjectFilter"),
    ],
)
def test_special_power_flags_fail_closed_without_a_complete_runtime_gate(
    flags: str, expected_reason: str
) -> None:
    documents = _hero_ability_documents()
    documents["data/ini/specialpower.ini"] = documents[
        "data/ini/specialpower.ini"
    ].replace(
        b"  Flags = LIMIT_DISTANCE NO_FORBIDDEN_OBJECTS\n"
        b"  MaxCastRange = 200\n"
        b"  ForbiddenObjectFilter = NO_SUMMON_NEAR_OBJECT_FILTER\n"
        b"  ForbiddenObjectRange = 60\n",
        b"",
        1,
    ).replace(
        b"  ReloadTime = 120000\n",
        f"  ReloadTime = 120000\n  Flags = {flags}\n".encode(),
        1,
    )

    descriptor = compile_playable_unit_descriptor("AbilityHero", documents)

    row = _abilities_by_id(descriptor)["Command_FixtureSummon"]
    assert row["implementation"]["status"] == "unimplemented"
    assert expected_reason in row["implementation"]["reason"]
    assert row["effect"] == {"kind": "none"}


def test_pathable_only_special_power_is_preserved_for_runtime_target_validation() -> None:
    documents = _hero_ability_documents()
    documents["data/ini/specialpower.ini"] = documents[
        "data/ini/specialpower.ini"
    ].replace(
        b"  Flags = LIMIT_DISTANCE NO_FORBIDDEN_OBJECTS\n"
        b"  MaxCastRange = 200\n"
        b"  ForbiddenObjectFilter = NO_SUMMON_NEAR_OBJECT_FILTER\n"
        b"  ForbiddenObjectRange = 60\n",
        b"  Flags = PATHABLE_ONLY\n",
        1,
    )

    descriptor = compile_playable_unit_descriptor("AbilityHero", documents)

    row = _abilities_by_id(descriptor)["Command_FixtureSummon"]
    assert row["implementation"]["status"] == "implemented"
    assert row["specialPowerContract"]["flags"] == ["PATHABLE_ONLY"]


def test_water_ok_special_power_is_preserved_for_runtime_target_validation() -> None:
    # WATER_OK is PATHABLE_ONLY's complement: retail authors it on the point
    # powers whose target location may be a water cell (Drogoth's Incinerate,
    # and the spellbook powers dropped across rivers). The flag is a target
    # admission rule the runtime honors, not an unmodelled effect.
    documents = _hero_ability_documents()
    documents["data/ini/specialpower.ini"] = documents[
        "data/ini/specialpower.ini"
    ].replace(
        b"  Flags = LIMIT_DISTANCE NO_FORBIDDEN_OBJECTS\n"
        b"  MaxCastRange = 200\n"
        b"  ForbiddenObjectFilter = NO_SUMMON_NEAR_OBJECT_FILTER\n"
        b"  ForbiddenObjectRange = 60\n",
        b"  Flags = WATER_OK\n",
        1,
    )

    descriptor = compile_playable_unit_descriptor("AbilityHero", documents)

    row = _abilities_by_id(descriptor)["Command_FixtureSummon"]
    assert row["implementation"]["status"] == "implemented"
    assert row["specialPowerContract"]["flags"] == ["WATER_OK"]


def test_multi_warhead_launchers_combine_every_authored_warhead() -> None:
    # Saruman Fireball shape: the launcher authors two ProjectileNuggets and
    # retail fires both per shot, so both warheads' base damage combines.
    documents = _batch3_hero_documents()
    documents["data/ini/weapon.ini"] = documents["data/ini/weapon.ini"].replace(
        b"Weapon FixtureHeroBlast\n"
        b"  AttackRange = 110.0\n"
        b"  DamageNugget\n"
        b"    Damage = 350\n"
        b"    Radius = 40.0\n"
        b"    DamageType = MAGIC\n"
        b"  End\n"
        b"End\n",
        b"Weapon FixtureHeroBlast\n"
        b"  AttackRange = 110.0\n"
        b"  ProjectileNugget\n"
        b"    ProjectileTemplateName = FixtureBlastProjectile\n"
        b"    WarheadTemplateName = FixtureBlastWarheadA\n"
        b"  End\n"
        b"  ProjectileNugget\n"
        b"    ProjectileTemplateName = FixtureBlastProjectile\n"
        b"    WarheadTemplateName = FixtureBlastWarheadB\n"
        b"  End\n"
        b"End\n"
        b"Weapon FixtureBlastWarheadA\n"
        b"  DamageNugget\n"
        b"    Damage = 400\n"
        b"    Radius = 30.0\n"
        b"    DamageType = FLAME\n"
        b"  End\n"
        b"End\n"
        b"Weapon FixtureBlastWarheadB\n"
        b"  DamageNugget\n"
        b"    Damage = 60\n"
        b"    Radius = 4.0\n"
        b"    DamageType = SIEGE\n"
        b"  End\n"
        b"End\n",
        1,
    )

    descriptor = compile_playable_unit_descriptor("AbilityHero", documents)

    validate_playable_unit_descriptor(descriptor)
    row = _abilities_by_id(descriptor)["Command_FixtureBlast"]
    assert row["implementation"]["status"] == "implemented"
    effect = row["effect"]
    assert effect["kind"] == "weapon-blast"
    assert effect["damage"] == 460
    assert effect["damageRadius"] == 30.0
    assert effect["warheadIds"] == [
        "FixtureBlastWarheadA",
        "FixtureBlastWarheadB",
    ]
    assert "warheadId" not in effect


def test_unsupported_damage_nuggets_record_annotated_reasons() -> None:
    documents = _batch3_hero_documents()
    documents["data/ini/weapon.ini"] = documents["data/ini/weapon.ini"].replace(
        b"Weapon FixtureHeroBlast\n"
        b"  AttackRange = 110.0\n"
        b"  DamageNugget\n"
        b"    Damage = 350\n"
        b"    Radius = 40.0\n"
        b"    DamageType = MAGIC\n"
        b"  End\n"
        b"End\n",
        b"Weapon FixtureHeroBlast\n"
        b"  AttackRange = 110.0\n"
        b"  DOTNugget\n"
        b"    Damage = 5\n"
        b"    DamageInterval = 1000\n"
        b"  End\n"
        b"  ParalyzeNugget\n"
        b"    Radius = 20\n"
        b"  End\n"
        b"End\n",
        1,
    )

    descriptor = compile_playable_unit_descriptor("AbilityHero", documents)

    validate_playable_unit_descriptor(descriptor)
    row = _abilities_by_id(descriptor)["Command_FixtureBlast"]
    assert row["implementation"]["status"] == "unimplemented"
    reason = row["implementation"]["reason"]
    assert "DOTNugget (needs damage-over-time)" in reason
    assert "ParalyzeNugget (needs paralysis status)" in reason
    assert row["effect"] == {"kind": "none"}


def test_weapon_toggle_prefers_the_exact_conditioned_set() -> None:
    # Lurtz carbine shape: many WeaponSets mention WEAPONSET_TOGGLE_1, but
    # exactly one is conditioned on that flag alone — the toggled base state.
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
        "    Conditions = CONTAINED WEAPONSET_TOGGLE_1\n"
        "    Weapon = PRIMARY AbilityHeroSword\n"
        "  End\n"
        "  WeaponSet\n"
        "    Conditions = WEAPONSET_TOGGLE_1 CLOSE_RANGE\n"
        "    Weapon = PRIMARY FixtureToggleBow\n"
        "  End\n"
        "  WeaponSet\n"
        "    Conditions = WEAPONSET_TOGGLE_1\n"
        "    Weapon = PRIMARY FixtureToggleBow\n"
        "  End\n"
        "  WeaponSet\n"
        "    Conditions = WEAPONSET_HERO_MODE WEAPONSET_TOGGLE_1\n"
        "    Weapon = PRIMARY AbilityHeroSword\n"
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
    assert toggle["implementation"]["status"] == "implemented"
    assert toggle["effect"]["kind"] == "weapon-toggle"
    assert toggle["effect"]["toggledWeaponId"] == "FixtureToggleBow"


def test_passive_button_without_reload_time_binds_its_gated_aura() -> None:
    # Dain's Stubborn Pride shape: a NONPRESSABLE button whose power authors
    # no ReloadTime, with a level-gated StartsActive=No aura bound through
    # the shared TriggeredBy upgrade.
    documents = _batch3_hero_documents()
    _with_hero_modules(
        documents,
        "  Behavior = UnpauseSpecialPowerUpgrade ModuleTag_PrideUnpause\n"
        "    SpecialPowerTemplate = SpecialAbilityFixturePride\n"
        "    TriggeredBy = Upgrade_FixturePride\n"
        "  End\n"
        "  Behavior = SpecialPowerModule ModuleTag_PrideStarter\n"
        "    SpecialPowerTemplate = SpecialAbilityFixturePride\n"
        "    UpdateModuleStartsAttack = No\n"
        "    StartsPaused = Yes\n"
        "  End\n"
        "  Behavior = AttributeModifierAuraUpdate ModuleTag_PrideUpdate\n"
        "    StartsActive = No\n"
        "    BonusName = FixturePride\n"
        "    TriggeredBy = Upgrade_FixturePride\n"
        "    RefreshDelay = 2000\n"
        "    Range = 200\n"
        "    ObjectFilter = ANY +INFANTRY +CAVALRY -HERO ALLIES\n"
        "  End\n",
    )
    command_sets = documents["data/ini/commandset.ini"].decode()
    documents["data/ini/commandset.ini"] = command_sets.replace(
        "  16 = Command_FixtureHorn\nEnd",
        "  16 = Command_FixtureHorn\n  17 = Command_FixturePride\nEnd",
        1,
    ).encode()
    documents["data/ini/commandbutton.ini"] += (
        b"\nCommandButton Command_FixturePride\n"
        b"  Command = SPECIAL_POWER\n"
        b"  SpecialPower = SpecialAbilityFixturePride\n"
        b"  Options = NONPRESSABLE\n"
        b"  TextLabel = CONTROLBAR:FixturePride\n"
        b"  DescriptLabel = CONTROLBAR:ToolTipFixturePride\n"
        b"  ButtonImage = HSFixturePride\n"
        b"End\n"
    )
    documents["data/ini/specialpower.ini"] += (
        b"SpecialPower SpecialAbilityFixturePride\n"
        b"  Enum = SPECIAL_GENERAL_TARGETLESS\n"
        b"End\n"
    )
    documents["data/ini/attributemodifier.ini"] += (
        b"\nModifierList FixturePride\n"
        b"  Category = SPELL\n"
        b"  Modifier = RESIST_FEAR 100%\n"
        b"  Duration = 3000\n"
        b"End\n"
    )
    documents["data/ini/experiencelevels.ini"] += (
        b"ExperienceLevel FixtureHeroLevel3\n"
        b"  TargetNames = FIXTUREHERO\n"
        b"  RequiredExperience = 300\n"
        b"  ExperienceAward = 30\n"
        b"  Rank = 3\n"
        b"  Upgrades = Upgrade_FixturePride\n"
        b"  SelectionDecal\n"
        b"    Texture = decal_hero_good\n"
        b"  End\n"
        b"End\n"
    )

    descriptor = compile_playable_unit_descriptor("AbilityHero", documents)

    validate_playable_unit_descriptor(descriptor)
    row = _abilities_by_id(descriptor)["Command_FixturePride"]
    assert row["implementation"]["status"] == "passive"
    assert row["cooldownMs"] == 0
    assert row["levelGate"]["requiredLevel"] == 3
    effect = row["effect"]
    assert effect["kind"] == "leadership-aura"
    assert effect["bonusName"] == "FixturePride"
    assert effect["range"] == 200
    assert effect["startsActive"] is True
    assert effect["modifiers"] == [
        {"kind": "RESIST_FEAR", "value": 1.0, "application": "multiplicative"}
    ]


# ---------------------------------------------------------------------------
# Portrait socket binding.
#
# The RotWK 2.01 Angmar corpus authors every unit SelectPortrait as a
# 192x192 "KU*Portrait" mapped image while Object-level ButtonImage fields
# carry 64x64 button icons.  The portrait socket must bind ONLY the authored
# SelectPortrait and fail closed to an empty socket when retail authors none
# or only button-class art — never borrow a 64-class icon (the playtest
# regression: BIWargSentry_Warg / KUSnowTrollIcon landing in the 191/192
# portrait slot).
# ---------------------------------------------------------------------------

from openbfme_importer.playable_unit_compiler import (  # noqa: E402
    _mapped_image_size_index,
    _portrait_image_ids,
)


# Measured from the RotWK 2.01 layered install (layer-0-rotwk/ini.big):
# data/ini/mappedimages/aptimages/buildingradialbuttons.ini and
# data/ini/mappedimages/aptimages/expansion1icons.ini.
_ROTWK_ANGMAR_MAPPED_IMAGE_ROWS = [
    {
        "id": "BIWargSentry_Warg",
        "texture": "BuildingRadialButtons_182.tga",
        "compiledTextureVirtualPath": "art/compiledtextures/bu/buildingradialbuttons_182.dds",
        "coords": {"left": 192, "top": 0, "right": 256, "bottom": 64},
    },
    {
        "id": "KUSnowTrollIcon",
        "texture": "Expansion1Icons_020.tga",
        "compiledTextureVirtualPath": "art/compiledtextures/ex/expansion1icons_020.dds",
        "coords": {"left": 325, "top": 260, "right": 389, "bottom": 324},
    },
    {
        "id": "KUDireWolfPortrait",
        "texture": "Expansion1Icons_007.tga",
        "compiledTextureVirtualPath": "art/compiledtextures/ex/expansion1icons_007.dds",
        "coords": {"left": 193, "top": 0, "right": 385, "bottom": 192},
    },
    {
        "id": "KUSnowTrollPortrait",
        "texture": "Expansion1Icons_012.tga",
        "compiledTextureVirtualPath": "art/compiledtextures/ex/expansion1icons_012.dds",
        "coords": {"left": 0, "top": 193, "right": 192, "bottom": 385},
    },
]

_ROTWK_ANGMAR_SIZE_INDEX = _mapped_image_size_index(
    {"resolvedLeaves": {"mappedImages": _ROTWK_ANGMAR_MAPPED_IMAGE_ROWS}}
)


def _portrait_probe_lineage(body: str) -> tuple:
    documents = _documents()
    documents["data/ini/object/units/portrait_probe.ini"] = (
        f"Object PortraitProbe\n  KindOf = PRELOAD SELECTABLE INFANTRY\n{body}End\n"
    ).encode("utf-8")
    prepared = prepare_playable_unit_compiler(documents)
    return (prepared.objects["portraitprobe"],)


def test_mapped_image_size_index_pins_rotwk_angmar_corpus_rows() -> None:
    assert _ROTWK_ANGMAR_SIZE_INDEX == {
        "biwargsentry_warg": (64, 64),
        "kusnowtrollicon": (64, 64),
        "kudirewolfportrait": (192, 192),
        "kusnowtrollportrait": (192, 192),
    }
    assert _mapped_image_size_index(None) is None
    assert _mapped_image_size_index({}) is None
    assert _mapped_image_size_index({"resolvedLeaves": {}}) is None


def test_portrait_socket_binds_authored_select_portrait_for_dire_wolf_row() -> None:
    # AngmarDireWolfHorde: angmarhordes.ini authors the container
    # SelectPortrait = KUDireWolfPortrait; angmardirewolf.ini authors the
    # member SelectPortrait = KUDireWolfPortrait AND ButtonImage =
    # BIWargSentry_Warg (64x64).  The button icon must never reach the
    # portrait socket.
    container = _portrait_probe_lineage(
        "  SelectPortrait = KUDireWolfPortrait\n"
    )
    member = _portrait_probe_lineage(
        "  SelectPortrait = KUDireWolfPortrait\n  ButtonImage = BIWargSentry_Warg\n"
    )

    assert _portrait_image_ids(container, member, _ROTWK_ANGMAR_SIZE_INDEX) == [
        "KUDireWolfPortrait"
    ]


def test_portrait_socket_binds_authored_select_portrait_for_snow_troll_row() -> None:
    # AngmarSnowTrollHorde: angmarhordes.ini authors the container
    # SelectPortrait = KUSnowTrollPortrait; angmarsnowtroll.ini authors the
    # member SelectPortrait = KUSnowTrollPortrait AND ButtonImage =
    # KUSnowTrollIcon (64x64).
    container = _portrait_probe_lineage(
        "  SelectPortrait = KUSnowTrollPortrait\n"
    )
    member = _portrait_probe_lineage(
        "  SelectPortrait = KUSnowTrollPortrait\n  ButtonImage = KUSnowTrollIcon\n"
    )

    assert _portrait_image_ids(container, member, _ROTWK_ANGMAR_SIZE_INDEX) == [
        "KUSnowTrollPortrait"
    ]


def test_portrait_socket_fails_closed_when_retail_authors_no_select_portrait() -> None:
    lineage = _portrait_probe_lineage("  ButtonImage = BIWargSentry_Warg\n")

    assert _portrait_image_ids(lineage, lineage, _ROTWK_ANGMAR_SIZE_INDEX) == []
    assert _portrait_image_ids(lineage, lineage, None) == []


def test_portrait_socket_fails_closed_on_button_class_select_portrait() -> None:
    lineage = _portrait_probe_lineage("  SelectPortrait = KUSnowTrollIcon\n")

    assert _portrait_image_ids(lineage, lineage, _ROTWK_ANGMAR_SIZE_INDEX) == []


def test_portrait_socket_fails_closed_on_unmeasured_select_portrait() -> None:
    lineage = _portrait_probe_lineage("  SelectPortrait = KUNeverMappedPortrait\n")

    assert _portrait_image_ids(lineage, lineage, _ROTWK_ANGMAR_SIZE_INDEX) == []


def test_portrait_socket_filters_authored_none_placeholder() -> None:
    lineage = _portrait_probe_lineage("  SelectPortrait = None\n")

    assert _portrait_image_ids(lineage, lineage, None) == []


def test_portrait_socket_without_size_oracle_keeps_only_select_portrait() -> None:
    # No faction census (documents-only compile): the authored SelectPortrait
    # is kept verbatim but ButtonImage still never spills into the socket.
    lineage = _portrait_probe_lineage(
        "  SelectPortrait = UPProbe\n  ButtonImage = BIProbe\n"
    )

    assert _portrait_image_ids(lineage, lineage, None) == ["UPProbe"]


def test_descriptor_portraits_are_select_portraits_only() -> None:
    documents = _documents()
    result = compile_playable_unit_descriptor("InfantryHorde", documents)

    assert result["presentation"]["ui"]["portraitImageIds"] == [
        "UPInfantryHorde",
        "UPInfantryMember",
    ]


def _nugget_typed_combat(components: list[dict[str, object]]) -> dict[str, object]:
    combat: dict[str, object] = {
        "damage": {
            "value": sum(float(row.get("value", 0)) for row in components),
            "components": components,
        }
    }
    _apply_nugget_damage_types(combat)
    return combat


def test_multi_nugget_weapon_keeps_each_authored_damage_type() -> None:
    # Retail ArwenSword authors no flat DamageType: HERO ARWEN_DAMAGE plus
    # SLASH 20. Summing them into one untyped lump resolved the whole hit
    # against the victim's DEFAULT armor column.
    combat = _nugget_typed_combat(
        [
            {"damageType": "HERO", "value": 180},
            {"damageType": "SLASH", "value": 20},
        ]
    )

    assert "damageType" not in combat
    assert combat["damageComponents"] == [
        {"damageType": "HERO", "value": 180},
        {"damageType": "SLASH", "value": 20},
    ]


def test_multi_nugget_weapon_of_one_type_publishes_that_type() -> None:
    combat = _nugget_typed_combat(
        [
            {"damageType": "SLASH", "value": 10},
            {"damageType": "SLASH", "value": 5},
        ]
    )

    assert combat["damageType"] == "SLASH"
    assert "damageComponents" not in combat


def test_untyped_nuggets_never_invent_a_damage_type() -> None:
    combat = _nugget_typed_combat([{"damageType": "", "value": 10}])

    assert "damageType" not in combat
    assert "damageComponents" not in combat


def test_partially_typed_nuggets_do_not_spread_the_authored_type() -> None:
    # One authored type plus an untyped nugget is not a single-type weapon:
    # claiming HERO for the untyped component would invent authorship.
    combat = _nugget_typed_combat(
        [
            {"damageType": "HERO", "value": 10},
            {"value": 5},
        ]
    )

    assert "damageType" not in combat
    assert combat["damageComponents"] == [
        {"damageType": "HERO", "value": 10},
        {"damageType": "", "value": 5},
    ]


def test_damage_nugget_components_keep_retail_radius_and_fx_semantics() -> None:
    documents = {
        "data/ini/weapon.ini": b"""
Weapon TestSiegeWarhead
  DamageNugget
    Damage = 200
    Radius = 20
    DamageTaperOff = 0
    DamageType = SIEGE
    DamageFXType = BIG_ROCK
    DeathType = EXPLODED
  End
  DamageNugget
    Damage = 200
    Radius = 100
    DamageTaperOff = 50
    DamageType = SIEGE
    DamageFXType = BIG_ROCK
    DeathType = EXPLODED
  End
End
""",
    }

    damage = _base_weapon_damage(documents, "TestSiegeWarhead", {})

    assert damage is not None
    assert damage["components"] == [
        {
            "value": 200,
            "expression": "200",
            "sourceIni": "data/ini/weapon.ini",
            "line": 4,
            "constantSourceIni": None,
            "damageType": "SIEGE",
            "radius": 20,
            "damageTaperOff": 0,
            "deathType": "EXPLODED",
            "damageFXType": "BIG_ROCK",
        },
        {
            "value": 200,
            "expression": "200",
            "sourceIni": "data/ini/weapon.ini",
            "line": 12,
            "constantSourceIni": None,
            "damageType": "SIEGE",
            "radius": 100,
            "damageTaperOff": 50,
            "deathType": "EXPLODED",
            "damageFXType": "BIG_ROCK",
        },
    ]


def test_zero_radius_melee_keeps_direct_damage_and_no_projectile_semantic() -> None:
    documents = {
        "data/ini/weapon.ini": b"""
Weapon TestMelee
  MeleeWeapon = Yes
  DamageNugget
    Damage = 75
    Radius = 0
    DamageTaperOff = 0
    DamageType = CAVALRY
    DamageFXType = SWORD_SLASH
    DeathType = NORMAL
  End
End
""",
    }

    damage = _base_weapon_damage(documents, "TestMelee", {})

    assert damage is not None
    assert damage["value"] == 75
    assert damage["components"][0]["radius"] == 0
    assert damage["components"][0]["damageTaperOff"] == 0


def test_sub_object_upgrade_compiles_fire_plane_show_token() -> None:
    command_row, button_row = _combat_command("SiegeEngine", 8, "SiegeEngine")
    documents = _combat_documents(
        _combat_object(
            "SiegeEngine",
            "SIEGEENGINE MACHINE",
            "  WeaponSet\n"
            "    Conditions = None\n"
            "    Weapon = PRIMARY SiegeRock\n"
            "  End\n"
            "  Behavior = SubObjectsUpgrade ModuleTag_FlamingRockUpgrade\n"
            "    TriggeredBy = Upgrade_GondorFireStones\n"
            "    ShowSubObjects = FirePlane\n"
            "  End\n",
        ),
        "Weapon SiegeRock\n"
        "  AttackRange = 400.0\n"
        "  DelayBetweenShots = 8000\n"
        "  DamageNugget\n"
        "    Damage = 200\n"
        "    DamageType = SIEGE\n"
        "  End\n"
        "End\n",
        command_row,
        button_row,
    )

    descriptor = compile_playable_unit_descriptor("SiegeEngine", documents)
    validate_playable_unit_descriptor(descriptor)
    upgrades = descriptor["gameplay"]["simulation"]["resolved"]["subObjectUpgrades"]
    assert upgrades == [
        {
            "upgradeId": "Upgrade_GondorFireStones",
            "show": ["FirePlane"],
            "hide": [],
            "sourceIni": "data/ini/object/units/test_units.ini",
            "line": upgrades[0]["line"],
        }
    ]
    assert isinstance(upgrades[0]["line"], int) and upgrades[0]["line"] > 0


@pytest.mark.parametrize(
    ("label", "game", "grace_radius", "grace_line", "deadeye_level"),
    [
        ("bfme2-retail", "bfme2", 1, 540, 4),
        ("rotwk-retail", "rotwk", 200, 529, 7),
    ],
)
def test_canonical_retail_elven_grace_and_deadeye_effects_are_implemented(
    label: str,
    game: str,
    grace_radius: int,
    grace_line: int,
    deadeye_level: int,
) -> None:
    catalog_path = _RETAIL_CATALOGS[label]
    if not catalog_path.is_file():
        pytest.skip(f"{label} retail catalog unavailable")
    documents = dict(read_catalog_documents(InstallCatalog.load(catalog_path)))
    prepared = prepare_playable_unit_compiler(documents)

    elrond = compile_playable_unit_descriptor(
        "ElvenElrond",
        documents,
        prepared=prepared,
        game=game,
        scenario_admission={"role": "scenario-only", "surfaces": ["script-spawn"]},
    )
    validate_playable_unit_descriptor(elrond)
    grace = next(
        row for row in elrond["abilities"]
        if row["id"] == "Command_SpecialAbilityElrondElvenGrace"
    )
    assert grace["implementation"] == {
        "status": "implemented",
        "reason": "",
        "limitations": [],
    }
    assert grace["targeting"] == "self"
    assert grace["cooldownMs"] == 90000
    assert grace["effect"] == {
        "kind": "heal",
        "amount": 600,
        "amountKind": "flat",
        "radius": grace_radius,
        "affects": "HERO",
        "onlyOthers": False,
        "healFxId": "FX_AragornAthelas",
        "module": "AutoHealBehavior",
        "sourceIni": "data/ini/object/goodfaction/units/elven/elrond.ini",
        "line": grace_line,
    }

    thranduil = compile_playable_unit_descriptor(
        "ElvenThranduil",
        documents,
        prepared=prepared,
        game=game,
        scenario_admission={"role": "scenario-only", "surfaces": ["script-spawn"]},
    )
    validate_playable_unit_descriptor(thranduil)
    deadeye = next(
        row for row in thranduil["abilities"]
        if row["id"] == "Command_SpecialAbilityDeadEye"
    )
    assert deadeye["implementation"] == {
        "status": "implemented",
        "reason": "",
        "limitations": [],
    }
    assert deadeye["targeting"] == "self"
    assert deadeye["cooldownMs"] == 120000
    assert deadeye["levelGate"] == {
        "upgradeIds": ["Upgrade_ThranduilDeadeye"],
        "requiredLevel": deadeye_level,
        "sourceIni": "data/ini/experiencelevels.ini",
    }
    assert deadeye["effect"] == {
        "kind": "weapon-mode-special-power",
        "specialPowerTemplateId": "SpecialAbilityThranduilDeadeye",
        "durationMs": 20000,
        "startsPaused": True,
        "weaponSetFlags": ["WEAPONSET_HERO_MODE"],
        "sourceIni": "data/ini/object/goodfaction/units/elven/thranduil.ini",
        "line": 489,
    }


@pytest.mark.parametrize(
    ("label", "game", "toggle_line", "style_line"),
    [
        ("bfme2-retail", "bfme2", 421, 380),
        ("rotwk-retail", "rotwk", 422, 381),
    ],
)
def test_retail_dwarven_demolisher_emits_exact_nonhero_toggle_ability(
    label: str, game: str, toggle_line: int, style_line: int
) -> None:
    catalog_path = _RETAIL_CATALOGS[label]
    if not catalog_path.is_file():
        pytest.skip(f"{label} retail catalog unavailable")
    documents = dict(
        read_catalog_documents(InstallCatalog.load(catalog_path))
    )
    descriptor = compile_playable_unit_descriptor(
        "DwarvenDemolisher", documents, game=game
    )
    validate_playable_unit_descriptor(descriptor)
    assert descriptor["category"] == "siege"
    assert len(descriptor["abilities"]) == 1
    ability = descriptor["abilities"][0]
    assert ability["id"] == "Command_SpecialAbilityDwarvenDemolisherDeploy"
    assert ability["slot"] == 2
    assert ability["targeting"] == "self"
    assert ability["cooldownMs"] == 0
    assert ability["button"]["options"] == [
        "OK_FOR_MULTI_EXECUTE",
        "OK_FOR_MULTI_SELECT",
    ]
    assert ability["effect"] == {
        "kind": "toggle-deploy",
        "autoAcquireEnabled": True,
        "autoAcquireModes": ["ATTACK_BUILDINGS"],
        "moodAttackCheckRateMs": 2500,
        "mustDeployToAttack": False,
        "unpackTimeMs": 2000,
        "packTimeMs": 2000,
        "deployedAttributeModifierId": "DwarvenDemolisherDeployModifier",
        "sourceIni": "data/ini/object/goodfaction/units/dwarven/dwarvenram.ini",
        "line": toggle_line,
        "specialPowerTemplateId": "SpecialAbilityDwarvenDemolisherDeploy",
        "targetMode": "SELF",
        "ignoreFacingCheck": True,
        "soundDeployId": "DwarfDemolisherDeployMS",
        "soundUndeployId": "DwarfDemolisherUndeployMS",
        "deployStyle": {
            "tag": "ModuleTag_03",
            "sourceIni": "data/ini/object/goodfaction/units/dwarven/dwarvenram.ini",
            "line": style_line,
        },
        "deployedAttributeModifier": {
            "id": "DwarvenDemolisherDeployModifier",
            "modifiers": [
                {"kind": "ARMOR", "value": 1.0, "application": "additive"}
            ],
            "sourceIni": "data/ini/attributemodifier.ini",
            "category": "SPELL",
            "durationMs": 0,
        },
        "autoAbility": True,
        "triggerWhenReady": True,
        "autoAbilityBlockedModelConditions": [
            "UNPACKING",
            "DEPLOYED",
            "PACKING",
            "MOVING",
        ],
    }


@pytest.mark.parametrize(
    ("label", "game", "module_line"),
    [
        ("bfme2-retail", "bfme2", 1056),
        ("rotwk-retail", "rotwk", 1045),
    ],
)
def test_canonical_retail_eowyn_emits_binary_closed_disguise_ability(
    label: str, game: str, module_line: int
) -> None:
    catalog_path = _RETAIL_CATALOGS[label]
    if not catalog_path.is_file():
        pytest.skip(f"{label} retail catalog unavailable")
    documents = dict(read_catalog_documents(InstallCatalog.load(catalog_path)))
    descriptor = compile_playable_unit_descriptor(
        "RohanEowyn", documents, game=game,
        scenario_admission={"role": "scenario-only", "surfaces": ["script-spawn"]},
    )
    validate_playable_unit_descriptor(descriptor)
    ability = next(
        row for row in descriptor["abilities"]
        if row["specialPowerId"] == "SpecialAbilityDisguise"
    )

    assert ability["implementation"] == {
        "status": "implemented",
        "reason": "",
        "limitations": [
            "special-disguise-viewer-perspective-deferred",
            "special-disguise-death-reset-ordering-deferred",
            "special-disguise-critical-hit-ordering-deferred",
            "special-disguise-user1-stealth-ordering-deferred",
        ],
    }
    assert ability["levelGate"]["requiredLevel"] == 4
    assert ability["effect"] == {
        "kind": "special-disguise",
        "specialPowerTemplateId": "SpecialAbilityDisguise",
        "targetMode": "SELF",
        "unpackTimeMs": 1000,
        "preparationTimeMs": 1,
        "persistentPrepTimeMs": 250,
        "packTimeMs": 1000,
        "opacityTarget": pytest.approx(0.3),
        "ownerObjectId": "RohanEowyn",
        "ownerDisguiseTemplateId": "RohanEowynDisguised",
        "hostileDisguiseTemplateId": "RohanRohirrimHorde",
        "disguiseFxId": "FX_DisguiseExit",
        "forceMountedWhenDisguising": True,
        "deferredBoundaries": [
            "critical-hit-ordering", "death-reset-ordering",
            "user1-stealth-ordering", "viewer-perspective",
        ],
        "sourceIni": "data/ini/object/goodfaction/units/men/eowyn.ini",
        "line": module_line,
    }
