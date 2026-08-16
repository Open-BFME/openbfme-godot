from __future__ import annotations

import importlib.util
import csv
import io
from pathlib import Path
import sys

from openbfme_importer.retail_ini_coverage import (
    classify_feature_categories,
    collect_document_surface,
    evidence_status,
    is_hero_family_object,
    is_neutral_mob_object,
)


def test_collect_document_surface_keeps_every_top_level_and_nested_field() -> None:
    source = b"""
#define BUILD_SECONDS 30
Object NeutralShip
  Side = Neutral
  KindOf = SHIP SELECTABLE
  BuildTime = BUILD_SECONDS
  HoleHealthRegen%PerSecond = 0.5%
  Behavior = ProductionUpdate ModuleTag_Production
    GiveNoXP = Yes
  End
  Draw = W3DScriptedModelDraw ModuleTag_Draw
    DefaultModelConditionState
      Model = NShip_SKN
    End
  End
End
Weapon ShipCannon
  AttackRange = 500
  ProjectileNugget
    ProjectileTemplateName = ShipCannonball
  End
End
CommandSet ShipCommandSet
  1 = Command_Attack
End
"""

    surface = collect_document_surface("data/ini/fixture.ini", source)

    assert [(row.kind, row.name) for row in surface.definitions] == [
        ("Object", "NeutralShip"),
        ("Weapon", "ShipCannon"),
        ("CommandSet", "ShipCommandSet"),
    ]
    assert surface.directives["define"] == 1
    assert surface.fields[("object", "buildtime")].sites == 1
    assert surface.fields[("object", "holehealthregen%persecond")].sites == 1
    assert surface.fields[("object", "givenoxp")].sites == 1
    assert surface.fields[("object", "model")].sites == 1
    assert surface.fields[("weapon", "projectiletemplatename")].sites == 1
    assert surface.fields[("commandset", "1")].sites == 1
    assert surface.nested_blocks[("object", "defaultmodelconditionstate")] == 1
    assert surface.nested_blocks[("weapon", "projectilenugget")] == 1


def test_script_terminator_is_accounted_as_syntax_not_a_nested_feature() -> None:
    surface = collect_document_surface(
        "data/ini/object/fixture.ini",
        b"""
Object ScriptedUnit
  Draw = W3DScriptedModelDraw ModuleTag_Draw
    AnimationState = USER_1
      BeginScript
        CurDrawableHideSubObject(\"HIDDEN\")
      EndScript
    End
  End
End
""",
    )

    assert surface.nested_blocks[("object", "beginscript")] == 1
    assert ("object", "endscript") not in surface.nested_blocks
    assert any(
        "curdrawablehidesubobject" in text
        for _root, text in surface.non_assignment_lines
    )


def test_feature_categories_cover_requested_high_risk_lanes() -> None:
    assert "timers" in classify_feature_categories("field", "Object.BuildTime", "")
    assert "ships" in classify_feature_categories("object", "NeutralShip", "SHIP Neutral")
    assert "neutral-mobs" in classify_feature_categories(
        "object", "CaveTroll", "data/ini/object/neutral/cavetroll.ini"
    )
    assert "spellbooks" in classify_feature_categories(
        "definition", "SpecialPower.SpellBookHeal", ""
    )
    assert "combat-effects" in classify_feature_categories(
        "field", "Weapon.Damage", ""
    )
    assert "assets" in classify_feature_categories("field", "Draw.Model", "")


def test_evidence_status_never_promotes_mentions_to_functional_parity() -> None:
    assert evidence_status(False, False, False, False) == "unmapped"
    assert evidence_status(True, False, False, False) == "importer-mentioned"
    assert evidence_status(True, True, False, False) == "descriptor-emitted"
    assert evidence_status(True, True, True, False) == "runtime-mentioned"
    assert evidence_status(True, True, True, True) == "runtime-tested"
    assert evidence_status(False, False, False, True) == "unmapped"
    assert evidence_status(True, False, True, True) == "importer-mentioned"


def test_object_ledger_does_not_call_neutral_props_mobs() -> None:
    tool_path = Path(__file__).resolve().parents[2] / "tools" / "retail-ini-coverage.py"
    spec = importlib.util.spec_from_file_location("retail_ini_coverage_tool", tool_path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)

    rows = module._object_rows(
        "data/ini/object/neutral/fixture.ini",
        b"""
Object NeutralTree
 Side = Neutral
 KindOf = IMMOBILE INERT
End
Object NeutralWolf
 Side = Neutral
 KindOf = MONSTER CREEP CAN_ATTACK
End
""",
    )

    indexed = {row["objectId"]: row for row in rows}
    assert "neutral-mobs" not in indexed["NeutralTree"]["categories"]
    assert "neutral-mobs" in indexed["NeutralWolf"]["categories"]


def test_object_family_categories_follow_inherited_side_and_kindof() -> None:
    tool_path = Path(__file__).resolve().parents[2] / "tools" / "retail-ini-coverage.py"
    spec = importlib.util.spec_from_file_location(
        "retail_ini_coverage_tool_inherited_categories", tool_path
    )
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)

    parent_rows = module._object_rows(
        "data/ini/object/neutral/fixture.ini",
        b"""
Object NeutralCreatureBase
 Side = Creeps
 KindOf = MONSTER CREEP CAN_ATTACK
End
Object NavalHeroBase
 KindOf = HERO SHIP SELECTABLE
End
""",
    )
    child_rows = module._object_rows(
        "data/ini/object/faction/children.ini",
        b"""
ChildObject InheritedCreature NeutralCreatureBase
 MaxHealth = 100
End
ChildObject InheritedNavalHero NavalHeroBase
 MaxHealth = 500
End
""",
    )
    rows = [*parent_rows, *child_rows]
    module._resolve_object_family_categories(rows)

    indexed = {row["objectId"]: row for row in rows}
    assert "neutral-mobs" in indexed["InheritedCreature"]["categories"]
    assert "hero-abilities" in indexed["InheritedNavalHero"]["categories"]
    assert "ships" in indexed["InheritedNavalHero"]["categories"]
    assert indexed["InheritedCreature"]["resolvedSides"] == ["Creeps"]
    assert indexed["InheritedNavalHero"]["resolvedKindOf"] == [
        "HERO",
        "SELECTABLE",
        "SHIP",
    ]
    union = module._object_union(rows)
    summary = module._object_slice_summary(union)
    assert summary["authoredKindOfShip"]["objects"] == 1
    assert summary["effectiveKindOfShip"]["objects"] == 2


def test_gdscript_string_index_does_not_lose_literals_after_comment_quote(
    tmp_path: Path,
) -> None:
    tool_path = Path(__file__).resolve().parents[2] / "tools" / "retail-ini-coverage.py"
    spec = importlib.util.spec_from_file_location("retail_ini_coverage_tool_strings", tool_path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)

    source = tmp_path / "field_consumer.gd"
    source.write_text(
        '# an unmatched " in a comment must not swallow later evidence\n'
        'var normalized := "allow_neutral"\n'
        'var authored := "AllowNeutralInside"\n',
        encoding="utf-8",
    )
    # gdscript_strings reports repository-relative evidence, so use its exact
    # literal matcher here to isolate the lexer contract from path ownership.
    values = [
        match.group(1) if match.group(1) is not None else match.group(2)
        for match in module._GDSCRIPT_STRING.finditer(source.read_text(encoding="utf-8"))
    ]

    assert "AllowNeutralInside" in values
    assert values.count("AllowNeutralInside") == 1
    assert "allow_neutral" in values
    assert "allow_neutral".casefold() != "AllowNeutralInside".casefold()


def test_neutral_mob_predicate_is_shared_and_fail_closed_for_side_only() -> None:
    common = {
        "parent_id": None,
        "source_ini": "data/ini/object/neutral/fixture.ini",
        "side": "Neutral",
    }
    assert not is_neutral_mob_object(
        object_id="NeutralBridge",
        kind_of=["IMMOBILE", "STRUCTURE"],
        **common,
    )
    assert is_neutral_mob_object(
        object_id="NeutralWolf",
        kind_of=["MONSTER", "CREEP"],
        **common,
    )
    assert is_neutral_mob_object(
        object_id="WargLairHole",
        parent_id="NeutralStructureHole",
        source_ini="data/ini/object/civilian/hole.ini",
        side=None,
        kind_of=["CREEP"],
    )
    assert not is_neutral_mob_object(
        object_id="CivilianInfantry",
        parent_id=None,
        source_ini="data/ini/object/civilian/civilian.ini",
        side="Civilian",
        kind_of=["INFANTRY"],
    )


def test_neutral_mob_category_is_bound_to_exact_object_sites_not_neutral_words() -> None:
    tool_path = Path(__file__).resolve().parents[2] / "tools" / "retail-ini-coverage.py"
    spec = importlib.util.spec_from_file_location(
        "retail_ini_coverage_tool_neutral_categories", tool_path
    )
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)

    exact = {
        ("data/ini/object/neutral/warg.ini", "neutralwarg"): frozenset(
            {"neutral-mobs"}
        )
    }
    assert "neutral-mobs" in module._site_categories(
        "field",
        "Object.MaxHealth",
        "data/ini/object/neutral/warg.ini",
        "Object",
        "NeutralWarg",
        exact,
    )
    for root_kind, root_name, field in (
        ("Weapon", "AllegianceWeapon", "SendToNeutral"),
        ("LivingWorldMapInfo", "DefaultLivingWorld", "NeutralBanner"),
        ("MiscAudio", "DefaultMiscAudio", "StealthNeutralizedSound"),
        ("ChildObject", "CivilianBuilding", "Civilian"),
    ):
        assert "neutral-mobs" not in module._site_categories(
            "field",
            f"{root_kind}.{field}",
            "data/ini/fixture.ini",
            root_kind,
            root_name,
            exact,
        )

    aggregate = module.Aggregate()
    aggregate.add(
        "MaxHealth",
        "data/ini/object/neutral/warg.ini",
        7,
        categories=("combat-effects", "neutral-mobs"),
    )
    aggregate.add(
        "MaxHealth",
        "data/ini/object/goodfaction/soldier.ini",
        9,
        categories=("combat-effects",),
    )
    assert aggregate.sites == 2
    assert aggregate.category_sites == {
        "combat-effects": 2,
        "neutral-mobs": 1,
    }


def test_object_hero_and_ship_categories_are_family_bound_not_field_words() -> None:
    tool_path = Path(__file__).resolve().parents[2] / "tools" / "retail-ini-coverage.py"
    spec = importlib.util.spec_from_file_location(
        "retail_ini_coverage_tool_object_family_categories", tool_path
    )
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)

    exact = {
        ("data/ini/object/fixture.ini", "ordinaryunit"): frozenset(),
        ("data/ini/object/fixture.ini", "navalhero"): frozenset(
            {"hero-abilities", "ships"}
        ),
    }
    ordinary = module._site_categories(
        "field",
        "Object.SpecialPowerTemplate",
        "data/ini/object/fixture.ini",
        "Object",
        "OrdinaryUnit",
        exact,
    )
    assert "hero-abilities" not in ordinary
    assert "ships" not in ordinary
    assert "spellbooks" not in ordinary
    naval = module._site_categories(
        "field",
        "Object.MaxHealth",
        "data/ini/object/fixture.ini",
        "ChildObject",
        "NavalHero",
        exact,
    )
    assert "hero-abilities" in naval
    assert "ships" in naval
    # Non-object definitions retain lexical classification because their
    # ownership graph is not an Object inheritance relationship.
    assert "hero-abilities" in module._site_categories(
        "field",
        "SpecialPower.SpecialPowerTemplate",
        "data/ini/specialpower.ini",
        "SpecialPower",
        "Power",
        exact,
    )


def test_hero_family_predicate_matches_kind_and_hero_owned_context() -> None:
    assert is_hero_family_object(
        object_id="OrdinaryName",
        source_ini="data/ini/object/units/fixture.ini",
        side="Men",
        kind_of=["HERO", "INFANTRY"],
    )
    assert is_hero_family_object(
        object_id="HeroOwnedHelper",
        source_ini="data/ini/object/units/fixture.ini",
        side="Men",
        kind_of=["INFANTRY"],
    )
    assert not is_hero_family_object(
        object_id="OrdinarySoldier",
        source_ini="data/ini/object/units/fixture.ini",
        side="Men",
        kind_of=["INFANTRY"],
    )


def test_module_runtime_symbols_require_consumer_action_and_exact_stem(tmp_path: Path) -> None:
    tool_path = Path(__file__).resolve().parents[2] / "tools" / "retail-ini-coverage.py"
    spec = importlib.util.spec_from_file_location("retail_ini_coverage_tool_symbols", tool_path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)

    # The index is deliberately limited to function declarations: comments,
    # variables, and quoted labels cannot fabricate a runtime consumer.
    source = tmp_path / "fixture.gd"
    source.write_text(
        "# PhysicsBehavior\n"
        "var physics_behavior = true\n"
        "func _attach_physics_behavior_contract(row):\n\tpass\n"
        "func describe_lifetime_update():\n\tpass\n",
        encoding="utf-8",
    )
    original_root = module.ROOT
    module.ROOT = tmp_path
    try:
        symbols = module.gdscript_function_symbols([source])
    finally:
        module.ROOT = original_root

    assert module.module_runtime_symbol_files("PhysicsBehavior", symbols) == ["fixture.gd"]
    assert module.module_runtime_symbol_files("LifetimeUpdate", symbols) == []
    assert module.module_runtime_symbol_files("UnrelatedBehavior", symbols) == []


def test_gdscript_unquote_preserves_unknown_escapes_and_unicode() -> None:
    tool_path = Path(__file__).resolve().parents[2] / "tools" / "retail-ini-coverage.py"
    spec = importlib.util.spec_from_file_location("retail_ini_coverage_tool_unquote", tool_path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)

    assert module._unquote_gd(r"PhysicsBehavior\n\q\\done é") == "PhysicsBehavior\n\\q\\done é"


def test_asset_denominator_counts_only_catalog_proven_physical_members() -> None:
    tool_path = Path(__file__).resolve().parents[2] / "tools" / "retail-ini-coverage.py"
    spec = importlib.util.spec_from_file_location("retail_ini_coverage_tool_assets", tool_path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)

    rows = [
        {"retailResolution": "retail-asset-stem", "selectedPackMapped": False},
        {"retailResolution": "ambiguous-retail-stem", "selectedPackMapped": False},
        {"retailResolution": "ini-definition", "selectedPackMapped": False},
        {"retailResolution": "unresolved-candidate", "selectedPackMapped": False},
        {"retailResolution": "retail-asset", "selectedPackMapped": True},
    ]
    statuses = module._asset_reference_statuses(rows)
    assert statuses == {
        "selected-pack-mapped": 1,
        "proven-retail-asset-not-selected-pack-mapped": 2,
        "ini-definition-not-selected-pack-mapped": 1,
        "unresolved-candidate-not-selected-pack-mapped": 1,
        "all-not-selected-pack-mapped": 4,
    }


def test_drawable_script_aliases_promote_only_executable_operations() -> None:
    tool_path = Path(__file__).resolve().parents[2] / "tools" / "retail-ini-coverage.py"
    spec = importlib.util.spec_from_file_location(
        "retail_ini_coverage_tool_script_aliases", tool_path
    )
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)

    selected = module.SelectedIndex()
    selected.values["hide-sub-object"] = ["pack/data/unit.json"]
    evidence = module._feature_evidence(
        "CurDrawableHideSubObject",
        {"curdrawablehidesubobject": ["typed_visual_graph.py"]},
        selected,
        {"hide-sub-object": ["asset_factory.gd"]},
        {"hide-sub-object": ["drawable_script_runtime_runner.gd"]},
        aliases=module._EXECUTABLE_SCRIPT_OPERATION_ALIASES[
            "curdrawablehidesubobject"
        ],
    )
    assert evidence["status"] == "runtime-tested"
    assert evidence["evidence"]["descriptors"] == ["pack/data/unit.json"]

    selected.values["play-sound"] = ["pack/data/unit.json"]
    sound = module._feature_evidence(
        "CurDrawablePlaySound",
        {"curdrawableplaysound": ["typed_visual_graph.py"]},
        selected,
        {"audio_intents": ["asset_factory.gd"]},
        {"audio_intents": ["drawable_script_runtime_runner.gd"]},
        aliases=module._EXECUTABLE_SCRIPT_OPERATION_ALIASES[
            "curdrawableplaysound"
        ],
    )
    assert sound["status"] == "runtime-tested"

    # Parsed but still semantically unsupported commands never receive an
    # alias merely because their normalized label occurs in diagnostics/tests.
    assert (
        "curdrawablesettransitionanimstate"
        not in module._EXECUTABLE_SCRIPT_OPERATION_ALIASES
    )
    unsupported = module._feature_evidence(
        "CurDrawableSetTransitionAnimState",
        {"curdrawablesettransitionanimstate": ["typed_visual_graph.py"]},
        selected,
        {"set-transition-animation-state": ["asset_factory.gd"]},
        {"set-transition-animation-state": ["drawable_script_runtime_runner.gd"]},
        aliases=module._EXECUTABLE_SCRIPT_OPERATION_ALIASES.get(
            "curdrawablesettransitionanimstate", ()
        ),
    )
    assert unsupported["status"] == "importer-mentioned"


def test_selected_descriptor_evidence_is_scoped_by_retail_edition() -> None:
    tool_path = Path(__file__).resolve().parents[2] / "tools" / "retail-ini-coverage.py"
    spec = importlib.util.spec_from_file_location(
        "retail_ini_coverage_tool_selection_scope", tool_path
    )
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)

    assert module._pack_applies_to_game("bfme2-men-vslice/digest", "bfme2")
    assert not module._pack_applies_to_game("rotwk-men-vslice/digest", "bfme2")
    # RotWK retail is layered on BFME2 and the live runtime intentionally
    # mounts both families, so both are valid evidence for that edition.
    assert module._pack_applies_to_game("bfme2-men-vslice/digest", "rotwk")
    assert module._pack_applies_to_game("rotwk-men-vslice/digest", "rotwk")
    assert module._pack_applies_to_game("custom-common-pack/digest", None)

    try:
        module._pack_applies_to_game("pack/digest", "unknown")
    except ValueError as exc:
        assert "unknown game selection scope" in str(exc)
    else:
        raise AssertionError("unknown edition must fail closed")


def test_descriptor_coverage_requires_every_exact_source_site() -> None:
    tool_path = Path(__file__).resolve().parents[2] / "tools" / "retail-ini-coverage.py"
    spec = importlib.util.spec_from_file_location(
        "retail_ini_coverage_tool_source_sites", tool_path
    )
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)

    selected = module.SelectedIndex()
    selected.walk(
        {
            "sourceIni": "Data\\INI\\Object\\Warg.ini",
            "line": 10,
            "nested": {
                "provenance": {
                    "virtualPath": "data/ini/object/warg.ini",
                    "line": 30,
                }
            },
        },
        "bfme2-pack/data/warg.json",
    )
    assert selected.source_sites[("data/ini/object/warg.ini", 30)] == [
        "bfme2-pack/data/warg.json"
    ]
    sites = [
        {
            "sourceIni": "Data\\INI\\Object\\Warg.ini",
            "line": 10,
            "categories": ["neutral-mobs", "combat-effects"],
        },
        {
            "sourceIni": "data/ini/object/warg.ini",
            "line": 20,
            "categories": ["neutral-mobs"],
        },
    ]
    exact = module._site_descriptor_evidence(sites, selected)
    assert exact["descriptorSiteCounts"] == {
        "mapped": 1,
        "unmapped": 1,
        "total": 2,
    }
    assert not exact["descriptorSitesComplete"]
    assert exact["descriptorCategorySiteCounts"]["neutral-mobs"] == {
        "mapped": 1,
        "unmapped": 1,
        "total": 2,
    }
    module._annotate_site_descriptor_evidence(sites, selected)
    assert sites[0]["descriptorSiteMapped"] is True
    assert sites[0]["descriptorSiteEvidence"] == [
        "bfme2-pack/data/warg.json"
    ]
    assert sites[1]["descriptorSiteMapped"] is False
    assert sites[1]["descriptorSiteEvidence"] == []
    module._annotate_exact_site_statuses(
        sites,
        {
            "importerMentioned": True,
            "runtimeMentioned": True,
            "runtimeTested": True,
        },
    )
    assert sites[0]["siteCoverageStatus"] == "runtime-tested"
    assert sites[1]["siteCoverageStatus"] == "importer-mentioned"

    broad = {
        "status": "runtime-tested",
        "importerMentioned": True,
        "descriptorEmitted": True,
        "runtimeMentioned": True,
        "runtimeTested": True,
        "evidence": {"importer": [], "descriptors": ["wrong.json"], "runtime": [], "tests": []},
    }
    tightened = module._with_site_descriptor_evidence(broad, exact)
    assert tightened["status"] == "importer-mentioned"
    assert tightened["evidence"]["descriptors"] == [
        "bfme2-pack/data/warg.json"
    ]
    row = {
        **tightened,
        "categorySites": {"neutral-mobs": 2},
    }
    assert module._category_gap_sites(row, "neutral-mobs") == 1


def test_unmapped_csv_exposes_exact_site_and_evidence_columns() -> None:
    tool_path = Path(__file__).resolve().parents[2] / "tools" / "retail-ini-coverage.py"
    spec = importlib.util.spec_from_file_location(
        "retail_ini_coverage_tool_csv_contract", tool_path
    )
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)

    output = module.csv_text(
        [
            "descriptorMappedSites",
            "descriptorUnmappedSites",
            "importerMentioned",
            "runtimeMentioned",
            "runtimeTested",
        ],
        [[1, 2, True, False, False]],
    )
    row = next(csv.DictReader(io.StringIO(output)))
    assert row == {
        "descriptorMappedSites": "1",
        "descriptorUnmappedSites": "2",
        "importerMentioned": "True",
        "runtimeMentioned": "False",
        "runtimeTested": "False",
    }


def test_completion_summary_counts_exact_sites_not_only_signatures() -> None:
    tool_path = Path(__file__).resolve().parents[2] / "tools" / "retail-ini-coverage.py"
    spec = importlib.util.spec_from_file_location(
        "retail_ini_coverage_tool_completion", tool_path
    )
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)

    feature_rows = {
        "bfme2": [{"status": "runtime-tested", "sites": 3}],
        "rotwk": [{"status": "importer-mentioned", "sites": 2}],
    }
    site_rows = {
        "bfme2": [
            {"siteCoverageStatus": "runtime-tested"},
            {"siteCoverageStatus": "runtime-tested"},
            {"siteCoverageStatus": "descriptor-emitted"},
        ],
        "rotwk": [{"siteCoverageStatus": "unmapped"}],
    }
    result = module._completion_summary(
        feature_rows,
        site_rows,
        [{"coverageStatus": "runtime-tested"}, {"coverageStatus": "deferred-descriptor"}],
        {"bfme2": [{"descriptorEmitted": True}], "rotwk": [{"descriptorEmitted": False}]},
        {"bfme2": 1, "rotwk": 2},
        {"bfme2": 0, "rotwk": 1},
        dishonest_pack_addresses=1,
    )
    assert result["featureSignatures"] == {"total": 2, "incomplete": 1}
    assert result["authoredSites"] == {"total": 4, "incomplete": 2}
    assert result["modules"] == {"total": 2, "incomplete": 1}
    assert result["objects"] == {"total": 2, "notEmitted": 1}
    assert result["provenRetailAssetsNotSelected"] == 3
    assert result["unknownSemanticLines"] == 1
    assert result["dishonestPackAddresses"] == 1
    assert result["staticCoverageComplete"] is False


def test_completion_markdown_exposes_every_non_conflated_denominator() -> None:
    tool_path = Path(__file__).resolve().parents[2] / "tools" / "retail-ini-coverage.py"
    spec = importlib.util.spec_from_file_location(
        "retail_ini_coverage_tool_completion_markdown", tool_path
    )
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)

    markdown = module._completion_markdown(
        {
            "featureSignatures": {"total": 101, "incomplete": 11},
            "authoredSites": {"total": 202, "incomplete": 22},
            "modules": {"total": 303, "incomplete": 33},
            "objects": {"total": 404, "notEmitted": 44},
            "provenRetailAssetsNotSelected": 55,
            "unknownSemanticLines": 66,
            "dishonestPackAddresses": 77,
            "staticCoverageComplete": False,
        }
    )
    for expected in (
        "| Feature signatures | 101 | 11 |",
        "| Authored semantic sites | 202 | 22 |",
        "| Object module kinds | 303 | 33 |",
        "| Effective objects | 404 | 44 |",
        "| Catalog-proven retail assets | - | 55 |",
        "| Unknown semantic lines | - | 66 |",
        "| Selected pack addresses | - | 77 |",
        "Static coverage complete: **no**",
    ):
        assert expected in markdown


def test_summary_labels_authored_and_effective_ship_denominators() -> None:
    tool_path = Path(__file__).resolve().parents[2] / "tools" / "retail-ini-coverage.py"
    source = tool_path.read_text(encoding="utf-8")
    assert '"Authored KindOf SHIP"' in source
    assert '"Effective KindOf SHIP"' in source
    assert '["effectiveKindOfShip"]["objects"]' in source
    assert '["effectiveKindOfShip"]["objectNotEmitted"]' in source
