from copy import deepcopy
from pathlib import Path
from unittest import mock

import pytest

from openbfme_importer.faction_import import (
    build_faction_conversion,
    build_faction_import_plan,
    convert_faction_import,
)
from openbfme_importer.playable_unit_compiler import PlayableUnitCompilerError
from importer.tests.test_playable_unit_compiler import _hero_roster_fixture


def _fixture() -> tuple[dict[str, bytes], dict[str, object]]:
    documents, graph = _hero_roster_fixture()
    graph["target"]["faction"] = "Men"
    graph["summary"] = {"unresolvedCount": 0}
    graph["inputSetSha256"] = "1" * 64
    return documents, graph


def test_plan_accounts_for_each_object_once_and_is_deterministic() -> None:
    documents, graph = _fixture()

    first = build_faction_import_plan(
        graph, documents, catalog_identity_sha256="2" * 64
    )
    second = build_faction_import_plan(
        graph, documents, catalog_identity_sha256="2" * 64
    )

    assert first == second
    assert [row["id"] for row in first["objects"]] == [
        "HeroEight",
        "HeroSeven",
        "UniversalFactory",
    ]
    assert first["summary"] == {
        "ready": False,
        "publicationReady": False,
        "descriptorCoverageComplete": False,
        "objectCount": 3,
        "descriptorReadyCount": 2,
        "excludedCount": 0,
        "converterGapCount": 1,
        "unresolvedLeafCount": 0,
        "unsupportedFamilies": ["structure"],
        "blockingReason": "plan-only schema has no audited pack/runtime receipt",
    }
    assert first["objects"][2]["family"] == "structure"


def test_unresolved_leaf_prevents_false_ready_state() -> None:
    documents, graph = _fixture()
    graph["definitions"]["objects"] = graph["definitions"]["objects"][1:]
    graph["summary"]["unresolvedCount"] = 1

    plan = build_faction_import_plan(
        graph, documents, catalog_identity_sha256="2" * 64
    )

    assert plan["summary"]["ready"] is False
    assert plan["summary"]["unresolvedLeafCount"] == 1


def test_duplicate_object_identity_is_rejected() -> None:
    documents, graph = _fixture()
    graph["definitions"]["objects"].append(
        deepcopy(graph["definitions"]["objects"][0])
    )

    with pytest.raises(ValueError, match="duplicate Object"):
        build_faction_import_plan(
            graph, documents, catalog_identity_sha256="2" * 64
        )


def test_missing_effective_object_is_counted_as_a_gap() -> None:
    documents, graph = _fixture()
    graph["definitions"]["objects"].append({"id": "AbsentRetailObject", "edges": []})

    plan = build_faction_import_plan(
        graph, documents, catalog_identity_sha256="2" * 64
    )

    row = next(item for item in plan["objects"] if item["id"] == "AbsentRetailObject")
    assert row["family"] == "missing-object"
    assert row["status"] == "converter-gap"


def test_compiler_initialization_failure_accounts_for_every_object_as_gap() -> None:
    documents, graph = _fixture()
    with mock.patch(
        "openbfme_importer.faction_import.prepare_playable_unit_compiler",
        side_effect=PlayableUnitCompilerError(
            "ambiguous effective Object definition: ExpansionOnly"
        ),
    ):
        plan = build_faction_import_plan(
            graph, documents, catalog_identity_sha256="2" * 64
        )

    assert plan["summary"]["objectCount"] == 3
    assert plan["summary"]["converterGapCount"] == 3
    assert {row["status"] for row in plan["objects"]} == {"converter-gap"}
    assert {row["family"] for row in plan["objects"]} == {"retail-object-parser"}


def test_conversion_rejects_bfme2_catalog_without_106_policy() -> None:
    catalog = mock.Mock()
    catalog.source_policy = None

    with pytest.raises(ValueError, match="BFME2 1.06 policy-bound"):
        convert_faction_import(
            catalog,
            Path("unused"),
            "men",
            game="bfme2",
        )


def test_conversion_rejects_unknown_game() -> None:
    catalog = mock.Mock()
    catalog.source_policy = None

    with pytest.raises(ValueError, match="does not support game"):
        convert_faction_import(
            catalog,
            Path("unused"),
            "men",
            game="tiberium",
        )


def test_conversion_admits_rotwk_data_driven_catalog() -> None:
    # RotWK is discovered data-driven: the catalog carries no fixed source
    # policy (source_policy is None). The conversion path must admit it and
    # thread game="rotwk" into census + conversion (never the bfme2 curations).
    catalog = mock.Mock()
    catalog.source_policy = None
    sentinel = {"admitted": True}

    with mock.patch(
        "openbfme_importer.faction_import._faction_spec",
        return_value=("FactionAngmar", "FactionAngmar", "Angmar"),
    ), mock.patch(
        "openbfme_importer.faction_import.census_playable_faction",
        return_value={"graph": True},
    ) as census, mock.patch(
        "openbfme_importer.faction_import.spellbook_source_documents",
        return_value={},
    ), mock.patch(
        "openbfme_importer.faction_import.build_faction_conversion",
        return_value=sentinel,
    ) as build:
        result = convert_faction_import(
            catalog,
            Path("unused"),
            "FactionAngmar",
            game="rotwk",
        )

    assert result is sentinel
    assert census.call_args.kwargs["game"] == "rotwk"
    assert build.call_args.kwargs["game"] == "rotwk"


def test_census_resolved_but_unparseable_object_is_a_parser_gap() -> None:
    documents, graph = _fixture()
    source_path = "data/ini/object/units/unparseable.ini"
    documents[source_path] = b"Object CensusResolved\n  KindOf = INFANTRY\n"
    graph["definitions"]["objects"].append(
        {
            "id": "CensusResolved",
            "edges": [],
            "source": {"virtualPath": source_path},
        }
    )

    plan = build_faction_import_plan(
        graph, documents, catalog_identity_sha256="2" * 64
    )

    row = next(item for item in plan["objects"] if item["id"] == "CensusResolved")
    assert row["family"] == "retail-object-parser"
    assert row["sourceVirtualPath"] == source_path
    assert "unterminated Object" in row["parserError"]


def test_descriptor_coverage_alone_cannot_claim_publication_readiness() -> None:
    documents, graph = _fixture()
    graph["definitions"]["objects"] = [graph["definitions"]["objects"][0]]

    with (
        mock.patch(
            "openbfme_importer.faction_import.playable_object_kind_of",
            return_value=("INFANTRY",),
        ),
        mock.patch(
            "openbfme_importer.faction_import.compile_playable_unit_descriptor",
            return_value={"category": "infantry", "descriptorSha256": "3" * 64},
        ),
    ):
        plan = build_faction_import_plan(
            graph, documents, catalog_identity_sha256="2" * 64
        )

    assert plan["summary"]["descriptorCoverageComplete"] is True
    assert plan["summary"]["publicationReady"] is False
    assert plan["summary"]["ready"] is False


@pytest.mark.parametrize(
    ("field", "value", "message"),
    (
        ("inputSetSha256", None, "factionGraphInputSetSha256"),
        ("inputSetSha256", "A" * 64, "factionGraphInputSetSha256"),
    ),
)
def test_plan_rejects_invalid_graph_identity(
    field: str, value: object, message: str
) -> None:
    documents, graph = _fixture()
    graph[field] = value

    with pytest.raises(ValueError, match=message):
        build_faction_import_plan(
            graph, documents, catalog_identity_sha256="2" * 64
        )


def test_plan_rejects_negative_unresolved_count() -> None:
    documents, graph = _fixture()
    graph["summary"]["unresolvedCount"] = -1

    with pytest.raises(ValueError, match="unresolvedCount"):
        build_faction_import_plan(
            graph, documents, catalog_identity_sha256="2" * 64
        )


def test_plan_rejects_invalid_catalog_and_target_identities() -> None:
    documents, graph = _fixture()
    with pytest.raises(ValueError, match="catalogIdentitySha256"):
        build_faction_import_plan(
            graph, documents, catalog_identity_sha256="not-a-hash"
        )

    graph["target"]["faction"] = ""
    with pytest.raises(ValueError, match="faction"):
        build_faction_import_plan(
            graph, documents, catalog_identity_sha256="2" * 64
        )


def test_plan_does_not_infer_side_from_player_template_spelling() -> None:
    documents, graph = _fixture()
    graph["target"]["playerTemplate"] = "PlayableMen"

    plan = build_faction_import_plan(
        graph, documents, catalog_identity_sha256="2" * 64
    )

    assert plan["target"] == {
        "playerTemplate": "PlayableMen",
        "faction": "Men",
    }


def _structure_success_patches() -> tuple[mock._patch, ...]:
    descriptor = {
        "objectId": "UniversalFactory",
        "descriptorSha256": "5" * 64,
        "production": {"evidence": "authored-construct-command", "routes": [{}]},
    }
    recipe = {"recipeSha256": "6" * 64, "resources": [{}, {}]}
    runtime = {"runtimeSha256": "7" * 64}
    evidence = {"evidenceSha256": "a" * 64}
    return (
        mock.patch(
            "openbfme_importer.faction_import."
            "compile_playable_structure_descriptor",
            return_value=descriptor,
        ),
        mock.patch(
            "openbfme_importer.faction_import.compile_structure_visual_recipe",
            return_value=recipe,
        ),
        mock.patch(
            "openbfme_importer.faction_import."
            "compose_structure_runtime_document",
            return_value=runtime,
        ),
        mock.patch(
            "openbfme_importer.faction_import."
            "compile_structure_lifecycle_evidence",
            return_value=evidence,
        ),
    )


def _unit_conversion_patches() -> tuple[mock._patch, ...]:
    return (
        mock.patch(
            "openbfme_importer.faction_import.build_retail_visual_closure",
            return_value={"aggregateSha256": "8" * 64},
        ),
        mock.patch(
            "openbfme_importer.faction_import.compile_playable_unit_pack_recipe",
            return_value={"recipeSha256": "9" * 64, "resources": [{}]},
        ),
        mock.patch(
            "openbfme_importer.faction_import._resolved_media",
            return_value=({}, {}),
        ),
    )


def test_conversion_converts_units_and_structures_and_is_deterministic() -> None:
    documents, graph = _fixture()
    results = []
    artifacts: list[tuple[str, str]] = []
    unit_patches = _unit_conversion_patches()
    structure_patches = _structure_success_patches()
    with unit_patches[0], unit_patches[1], unit_patches[2], (
        structure_patches[0]
    ), structure_patches[1], structure_patches[2], structure_patches[3]:
        for _ in range(2):
            results.append(
                build_faction_conversion(
                    graph,
                    documents,
                    Path("unused-effective-root"),
                    catalog_identity_sha256="2" * 64,
                    artifact_writer=lambda o, k, d: artifacts.append((o, k)),
                )
            )

    first, second = results
    # Content identity must match; wall-clock timing fields (convertElapsedMs,
    # convertLoopMs, …) are ephemeral and intentionally excluded from the digest.
    from openbfme_importer.faction_import import coverage_digest_payload

    assert first["aggregateSha256"] == second["aggregateSha256"]
    assert coverage_digest_payload(first) == coverage_digest_payload(second)
    rows = {row["id"]: row for row in first["objects"]}
    assert rows["HeroSeven"]["status"] == "converted"
    assert rows["HeroSeven"]["converter"] == "playable-unit"
    assert rows["UniversalFactory"]["status"] == "converted"
    assert rows["UniversalFactory"]["converter"] == "playable-structure"
    assert rows["UniversalFactory"]["runtimeSha256"] == "7" * 64
    assert first["summary"]["convertedCount"] == 3
    assert first["summary"]["converterGapCount"] == 0
    assert first["summary"]["publicationReady"] is False
    assert first["summary"]["conversionComplete"] is True
    assert ("UniversalFactory", "runtime") in artifacts
    assert ("HeroSeven", "pack-recipe") in artifacts
    assert len(first["aggregateSha256"]) == 64


def test_conversion_records_per_object_failures_and_continues() -> None:
    documents, graph = _fixture()
    unit_patches = _unit_conversion_patches()
    with unit_patches[0], unit_patches[1], unit_patches[2]:
        coverage = build_faction_conversion(
            graph,
            documents,
            Path("unused-effective-root"),
            catalog_identity_sha256="2" * 64,
        )

    rows = {row["id"]: row for row in coverage["objects"]}
    assert rows["UniversalFactory"]["status"] == "converter-gap"
    assert "construct command" in rows["UniversalFactory"]["reason"]
    assert rows["HeroSeven"]["status"] == "converted"
    assert rows["HeroEight"]["status"] == "converted"
    assert coverage["summary"]["converterGapCount"] == 1
    assert coverage["summary"]["conversionComplete"] is False


def test_conversion_excludes_accounted_support_families() -> None:
    documents, graph = _fixture()
    objects_path = "data/ini/object/units/test_units.ini"
    documents[objects_path] = (
        documents[objects_path].decode("utf-8")
        + """
Object TestBanner
  KindOf = INFANTRY BANNER
End
Object TestSpellBook
  KindOf = SPELL_BOOK
End
"""
    ).encode("utf-8")
    graph["definitions"]["objects"].append({"id": "TestBanner", "edges": []})
    graph["definitions"]["objects"].append({"id": "TestSpellBook", "edges": []})
    unit_patches = _unit_conversion_patches()
    with unit_patches[0], unit_patches[1], unit_patches[2]:
        coverage = build_faction_conversion(
            graph,
            documents,
            Path("unused-effective-root"),
            catalog_identity_sha256="2" * 64,
        )

    banner = next(r for r in coverage["objects"] if r["id"] == "TestBanner")
    assert banner["status"] == "excluded"
    assert "parent horde" in banner["reason"]
    # The spellbook lane converts the faction spell book now; a bare
    # SPELL_BOOK object with no authored store content fails closed instead.
    spellbook = next(r for r in coverage["objects"] if r["id"] == "TestSpellBook")
    assert spellbook["status"] == "converter-gap"
    assert spellbook["family"] == "spellbook"


def test_foundation_without_visuals_is_excluded_with_descriptor_evidence() -> None:
    documents, graph = _fixture()
    from openbfme_importer.playable_structure_pack_compiler import (
        PlayableStructurePackCompilerError,
    )

    # Non-CenterGeneric BASE_FOUNDATION still uses the legacy exception path
    # (only *FortressCenterGeneric early-exits before visual closure).
    descriptor = {
        "objectId": "UniversalFactory",
        "descriptorSha256": "5" * 64,
        "kindOf": ["STRUCTURE", "BASE_FOUNDATION"],
        "production": {"evidence": "engine-spawned-composite", "routes": []},
    }
    unit_patches = _unit_conversion_patches()
    with (
        unit_patches[0],
        unit_patches[1],
        unit_patches[2],
        mock.patch(
            "openbfme_importer.faction_import."
            "compile_playable_structure_descriptor",
            return_value=descriptor,
        ),
        mock.patch(
            "openbfme_importer.faction_import.compile_structure_visual_recipe",
            side_effect=PlayableStructurePackCompilerError(
                "structure has no resolved lifecycle model: UniversalFactory"
            ),
        ),
    ):
        coverage = build_faction_conversion(
            graph,
            documents,
            Path("unused-effective-root"),
            catalog_identity_sha256="2" * 64,
        )

    row = next(r for r in coverage["objects"] if r["id"] == "UniversalFactory")
    assert row["status"] == "excluded"
    assert "foundation composite" in row["reason"]
    assert row["descriptorSha256"] == "5" * 64


def test_fortress_center_generic_skips_visual_closure() -> None:
    """*FortressCenterGeneric exits before visual closure (perf)."""

    from openbfme_importer.faction_import import _convert_one_plan_object

    descriptor = {
        "objectId": "AngmarFortressCenterGeneric",
        "descriptorSha256": "5" * 64,
        "kindOf": ["STRUCTURE", "BASE_FOUNDATION"],
        "production": {"evidence": "engine-spawned-composite", "routes": []},
    }
    with (
        mock.patch(
            "openbfme_importer.faction_import."
            "compile_playable_structure_descriptor",
            return_value=descriptor,
        ),
        mock.patch(
            "openbfme_importer.faction_import.build_retail_visual_closure",
            side_effect=AssertionError("must not run visual closure"),
        ),
    ):
        row, artifacts = _convert_one_plan_object(
            {
                "id": "AngmarFortressCenterGeneric",
                "family": "structure",
                "status": "descriptor-ready",
                "descriptorSha256": "5" * 64,
            },
            documents={},
            prepared=None,  # type: ignore[arg-type]
            faction_graph={},
            effective_root=Path("unused"),
            catalog=None,
            spawned=(),
            wall_templates=(),
            source_null_sets=(),
            object_cache=None,
            documents_fp="d",
            catalog_identity_sha256="2" * 64,
            assets_fp="a",
            graph_input_set_sha256="g",
            plan_aggregate_sha256="p",
            policy_fp="y",
            compiler_token="c",
            game="rotwk",
        )
    assert row["status"] == "excluded"
    assert "foundation composite" in row["reason"]
    assert artifacts == {}


def _construct_fixture() -> tuple[dict[str, bytes], dict[str, object]]:
    documents, graph = _fixture()
    objects_path = "data/ini/object/units/test_units.ini"
    documents[objects_path] += b"""
Object ConstructKeep
  KindOf = PRELOAD SELECTABLE STRUCTURE
  Body = StructureBody ModuleTag_Body
    MaxHealth = 1500
  End
End

Object ConstructPorter
  KindOf = PRELOAD SELECTABLE INFANTRY DOZER
  CommandSet = ConstructPorterCommandSet
End

Object ConstructBanner
  KindOf = PRELOAD SELECTABLE INFANTRY BANNER
End

Object ReskinBanner
  KindOf = PRELOAD SELECTABLE INFANTRY
End
"""
    documents["data/ini/commandset.ini"] += b"""
CommandSet ConstructPorterCommandSet
  1 = Command_ConstructKeep
End
"""
    documents["data/ini/commandbutton.ini"] += b"""
CommandButton Command_ConstructKeep
  Command = PORTER_CONSTRUCT
  Object = ConstructKeep
End
"""
    graph["definitions"]["objects"][0]["edges"] = [
        {"field": "BannerCarriersAllowed", "targetKind": "horde-banner", "targetId": "ReskinBanner"}
    ]
    graph["definitions"]["objects"].extend(
        [
            {"id": "ConstructKeep", "edges": []},
            {"id": "ConstructBanner", "edges": []},
            {"id": "ReskinBanner", "edges": []},
        ]
    )
    return documents, graph


def test_plan_routes_structures_and_excludes_banner_members() -> None:
    documents, graph = _construct_fixture()

    plan = build_faction_import_plan(graph, documents, catalog_identity_sha256="2" * 64)

    rows = {row["id"]: row for row in plan["objects"]}
    keep = rows["ConstructKeep"]
    assert keep["status"] == "descriptor-ready"
    assert keep["family"] == "structure"
    assert keep["category"] == "structure"
    assert len(keep["descriptorSha256"]) == 64
    for banner_id in ("ConstructBanner", "ReskinBanner"):
        banner = rows[banner_id]
        assert banner["status"] == "excluded"
        assert banner["family"] == "banner-member"
        assert "parent horde" in banner["reason"]
    summary = plan["summary"]
    assert summary["objectCount"] == 6
    assert summary["descriptorReadyCount"] == 3
    assert summary["excludedCount"] == 2
    assert summary["converterGapCount"] == 1
    assert summary["unsupportedFamilies"] == ["structure"]
    assert summary["descriptorCoverageComplete"] is False


def test_plan_routes_spellbook_through_the_spellbook_lane() -> None:
    documents, graph = _construct_fixture()
    objects_path = "data/ini/object/units/test_units.ini"
    documents[objects_path] += b"""
Object FixtureSpellBook
  KindOf = SPELL_BOOK
End
"""
    graph["definitions"]["objects"].append({"id": "FixtureSpellBook", "edges": []})
    descriptor = {
        "spellBook": {"objectId": "FixtureSpellBook"},
        "descriptorSha256": "4" * 64,
    }
    with mock.patch(
        "openbfme_importer.faction_import.compile_spellbook_descriptor",
        return_value=descriptor,
    ) as spellbook_compile:
        plan = build_faction_import_plan(
            graph, documents, catalog_identity_sha256="2" * 64
        )

    row = next(r for r in plan["objects"] if r["id"] == "FixtureSpellBook")
    assert row["status"] == "descriptor-ready"
    assert row["family"] == "spellbook"
    assert row["category"] == "spellbook"
    assert row["descriptorSha256"] == "4" * 64
    assert spellbook_compile.call_count == 1


def test_plan_rejects_a_spellbook_descriptor_with_a_foreign_identity() -> None:
    documents, graph = _construct_fixture()
    objects_path = "data/ini/object/units/test_units.ini"
    documents[objects_path] += b"""
Object FixtureSpellBook
  KindOf = SPELL_BOOK
End
"""
    graph["definitions"]["objects"].append({"id": "FixtureSpellBook", "edges": []})
    descriptor = {
        "spellBook": {"objectId": "OtherSpellBook"},
        "descriptorSha256": "4" * 64,
    }
    with mock.patch(
        "openbfme_importer.faction_import.compile_spellbook_descriptor",
        return_value=descriptor,
    ):
        plan = build_faction_import_plan(
            graph, documents, catalog_identity_sha256="2" * 64
        )

    row = next(r for r in plan["objects"] if r["id"] == "FixtureSpellBook")
    assert row["status"] == "converter-gap"
    assert row["family"] == "spellbook"
    assert "identity" in row["reason"]


def test_resolved_structure_images_bind_construct_icon_and_record_gaps() -> None:
    from openbfme_importer.faction_import import _resolved_structure_images

    descriptor = {
        "production": {
            "evidence": "authored-construct-command",
            "routes": [
                {
                    "surface": "construct",
                    "commandId": "Command_ConstructTestKeep",
                    "buttonImageId": "BITestKeep",
                },
                {"surface": "construct", "commandId": "Command_ConstructAlias"},
            ],
        },
        "presentation": {
            "ui": {"SelectPortrait": {"expression": "UPTestKeep"}}
        },
    }
    graph = {
        "resolvedLeaves": {
            "mappedImages": [
                {
                    "id": "BITestKeep",
                    "texture": "bibuttons.tga",
                    "textureWidth": 256,
                    "textureHeight": 256,
                    "coords": {"left": 0, "top": 0, "right": 64, "bottom": 64},
                    "compiledTextureVirtualPath": (
                        "art/compiledtextures/bi/bibuttons.tga"
                    ),
                },
                {
                    "id": "UPTestKeep",
                    "texture": "upportraits.tga",
                    "textureWidth": 256,
                    "textureHeight": 256,
                    "coords": {"left": 0, "top": 0, "right": 191, "bottom": 191},
                    "compiledTextureResolution": "missing",
                },
            ]
        }
    }

    images, gaps = _resolved_structure_images(graph, descriptor)

    assert sorted(images) == ["BITestKeep"]
    assert images["BITestKeep"]["compiledTextureVirtualPath"] == (
        "art/compiledtextures/bi/bibuttons.tga"
    )
    # The portrait's atlas is retail-absent: explicit gap, never silence.
    assert {
        "usage": "select-portrait",
        "imageId": "UPTestKeep",
        "reason": "unresolved-mapped-image-texture",
    } in gaps
    assert len(gaps) == 1


def test_resolved_structure_images_record_absent_evidence_gaps() -> None:
    from openbfme_importer.faction_import import _resolved_structure_images

    descriptor = {
        "production": {"evidence": "wall-template", "routes": []},
        "presentation": {"ui": {}},
    }
    graph = {"resolvedLeaves": {"mappedImages": []}}

    images, gaps = _resolved_structure_images(graph, descriptor)

    assert images == {}
    assert gaps == [
        {
            "usage": "construct-button",
            "imageId": "",
            "reason": "no-authored-construct-command",
        },
        {
            "usage": "select-portrait",
            "imageId": "",
            "reason": "no-authored-select-portrait",
        },
    ]


def test_resolved_structure_images_without_media_closure_record_gaps() -> None:
    from openbfme_importer.faction_import import _resolved_structure_images

    descriptor = {
        "production": {
            "evidence": "authored-construct-command",
            "routes": [
                {"surface": "construct", "buttonImageId": "BITestKeep"}
            ],
        },
        "presentation": {"ui": {"SelectPortrait": {"expression": "UPTestKeep"}}},
    }

    images, gaps = _resolved_structure_images({}, descriptor)

    assert images == {}
    assert {row["reason"] for row in gaps} == {
        "faction-graph-has-no-mapped-image-closure"
    }
    assert {row["imageId"] for row in gaps} == {"BITestKeep", "UPTestKeep"}


def test_unresolved_construct_image_is_an_explicit_gap_row() -> None:
    from openbfme_importer.faction_import import _resolved_structure_images

    descriptor = {
        "production": {
            "evidence": "authored-construct-command",
            "routes": [
                {"surface": "construct", "buttonImageId": "BIMissingKeep"}
            ],
        },
        "presentation": {"ui": {"SelectPortrait": {"expression": "None"}}},
    }
    graph = {"resolvedLeaves": {"mappedImages": []}}

    images, gaps = _resolved_structure_images(graph, descriptor)

    assert images == {}
    assert gaps == [
        {
            "usage": "construct-button",
            "imageId": "BIMissingKeep",
            "reason": "unresolved-mapped-image",
        },
        {
            "usage": "select-portrait",
            "imageId": "",
            "reason": "no-authored-select-portrait",
        },
    ]
