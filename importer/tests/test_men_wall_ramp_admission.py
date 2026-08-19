"""Lane L6: MenWallRamp is admitted through WallHubBehavior HubCapTemplateName.

GondorCastleWallHub OPTION_TWO authors ``HubCapTemplateName = MenWallRamp``.
The faction census harvested SegmentTemplateName / gate / tower templates but
not hub caps, so MenWallRamp never entered the men pack (no cooked GLB).
That is our exclusion, not a converter gap.
"""

from __future__ import annotations

import tempfile
from pathlib import Path

from openbfme_importer.faction_census import _OBJECT_EDGE_FIELDS, census_men_faction
from openbfme_importer.faction_import import (
    _wall_template_roots,
    build_faction_import_plan,
)
from importer.tests.test_faction_census import _catalog
from importer.tests.test_playable_structure_compiler import _structure_documents


def test_hub_cap_template_is_a_census_object_edge() -> None:
    assert _OBJECT_EDGE_FIELDS["hubcaptemplatename"] == "wall-hub-cap-template"


def test_wall_template_roots_include_hub_caps() -> None:
    graph = {
        "definitions": {
            "objects": [
                {
                    "id": "GondorCastleWallHub",
                    "edges": [
                        {
                            "field": "HubCapTemplateName",
                            "targetKind": "wall-hub-cap-template",
                            "targetId": "MenWallRamp",
                        },
                        {
                            "field": "SegmentTemplateName",
                            "targetKind": "wall-segment-template",
                            "targetId": "GondorCastleWallSegment",
                        },
                    ],
                }
            ]
        }
    }
    assert _wall_template_roots(graph) == (
        "GondorCastleWallSegment",
        "MenWallRamp",
    )


def test_census_admits_men_wall_ramp_from_hub_cap() -> None:
    with tempfile.TemporaryDirectory() as raw:
        catalog = _catalog(Path(raw), wall_hub_ramp=True)
        graph = census_men_faction(catalog)
    object_ids = {item["id"] for item in graph["definitions"]["objects"]}
    assert "MenWallRamp" in object_ids
    fortress = next(
        item
        for item in graph["definitions"]["objects"]
        if item["id"] == "MenFortress"
    )
    assert {
        "field": "HubCapTemplateName",
        "targetKind": "wall-hub-cap-template",
        "targetId": "MenWallRamp",
    } in fortress["edges"]


def test_import_plan_admits_men_wall_ramp_as_wall_template() -> None:
    documents = _structure_documents()
    objects_path = "data/ini/object/units/test_units.ini"
    documents[objects_path] = documents[objects_path] + (
        b"\nObject MenWallRamp\n"
        b"  KindOf = STRUCTURE IMMOBILE WALK_ON_TOP_OF_WALL SCALEABLE_WALL\n"
        b"  Draw = W3DScriptedModelDraw ModuleTag_Draw\n"
        b"    DefaultModelConditionState\n"
        b"      Model = GBWallRamp\n"
        b"    End\n"
        b"    WallBoundsMesh = P1\n"
        b"    RampMesh1 = R1\n"
        b"    RampMesh2 = R2\n"
        b"  End\n"
        b"  Body = StructureBody ModuleTag_Body\n"
        b"    MaxHealth = 1500.0\n"
        b"  End\n"
        b"End\n"
    )
    graph = {
        "target": {"playerTemplate": "FactionMen", "faction": "Men"},
        "summary": {"unresolvedCount": 0},
        "inputSetSha256": "1" * 64,
        "definitions": {
            "objects": [
                {
                    "id": "TestKeep",
                    "edges": [
                        {
                            "field": "HubCapTemplateName",
                            "targetKind": "wall-hub-cap-template",
                            "targetId": "MenWallRamp",
                        }
                    ],
                },
                {"id": "MenWallRamp", "edges": []},
            ],
            "commandButtons": [],
        },
        "roots": [],
    }
    plan = build_faction_import_plan(
        graph, documents, catalog_identity_sha256="2" * 64
    )
    by_id = {row["id"]: row for row in plan["objects"]}
    assert "MenWallRamp" in by_id
    row = by_id["MenWallRamp"]
    assert row["status"] == "descriptor-ready"
    assert row["family"] == "structure"
