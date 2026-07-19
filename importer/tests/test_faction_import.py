from copy import deepcopy
from pathlib import Path
from unittest import mock

import pytest

from openbfme_importer.faction_import import (
    build_faction_conversion,
    build_faction_import_plan,
)
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


def test_plan_rejects_mismatched_template_and_faction() -> None:
    documents, graph = _fixture()
    graph["target"]["faction"] = "Mordor"

    with pytest.raises(ValueError, match="identity pair"):
        build_faction_import_plan(
            graph, documents, catalog_identity_sha256="2" * 64
        )


def _structure_success_patches() -> tuple[mock._patch, ...]:
    descriptor = {
        "objectId": "UniversalFactory",
        "descriptorSha256": "5" * 64,
        "production": {"evidence": "authored-construct-command", "routes": [{}]},
    }
    recipe = {"recipeSha256": "6" * 64, "resources": [{}, {}]}
    runtime = {"runtimeSha256": "7" * 64}
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
    ), structure_patches[1], structure_patches[2]:
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
    assert first == second
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
Object TestSpellBook
  KindOf = SPELL_BOOK
End
"""
    ).encode("utf-8")
    graph["definitions"]["objects"].append({"id": "TestSpellBook", "edges": []})
    unit_patches = _unit_conversion_patches()
    with unit_patches[0], unit_patches[1], unit_patches[2]:
        coverage = build_faction_conversion(
            graph,
            documents,
            Path("unused-effective-root"),
            catalog_identity_sha256="2" * 64,
        )

    row = next(r for r in coverage["objects"] if r["id"] == "TestSpellBook")
    assert row["status"] == "excluded"
    assert "spell book" in row["reason"]


def test_foundation_without_visuals_is_excluded_with_descriptor_evidence() -> None:
    documents, graph = _fixture()
    from openbfme_importer.playable_structure_pack_compiler import (
        PlayableStructurePackCompilerError,
    )

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
