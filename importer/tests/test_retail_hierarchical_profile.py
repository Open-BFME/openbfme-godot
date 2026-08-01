from __future__ import annotations

import copy
import json
from pathlib import Path
import tempfile
import unittest

from openbfme_importer.profile import ImportProfile
from openbfme_importer.retail_hierarchical_profile import (
    HIERARCHICAL_PROP_PLAN_SCHEMA,
    build_retail_hierarchical_prop_plan,
    load_retail_hierarchical_prop_plan_inputs,
    write_retail_hierarchical_prop_plan,
)
from openbfme_importer.retail_visual_profile import build_retail_static_prop_plan
from tests.test_retail_visual_profile import (
    canonical_sha256,
    make_manifest,
    make_report,
    reseal_manifest,
    seal_report,
)


def make_hierarchical_report() -> dict:
    report = make_report()
    scanned = report["scannedW3d"][0]
    scanned["headerIds"]["hierarchyIds"] = ["SHAREDTREE"]
    scanned["headerIds"]["modelIds"] = ["SHAREDTREE.BODY", "SHAREDTREE"]
    scanned["modelReferences"] = [
        {
            "identifier": "SHAREDTREE.BODY",
            "boneIndex": 0,
            "role": "lod",
            "provenance": {
                "virtualPath": "art/w3d/pt/shared-tree.w3d",
                "offset": 64,
            },
        }
    ]
    return seal_report(report)


def make_map_objects(*, wrong_case: bool = False) -> dict:
    names_and_roads = [
        ("treea" if wrong_case else "TreeA", 0),
        ("TreeA", 0),
        ("TreeB", 0),
        ("TreeA", 2),
        ("*Waypoints/Waypoint", 0),
    ]
    objects = [
        {"index": index, "typeName": name, "roadType": road_type}
        for index, (name, road_type) in enumerate(names_and_roads)
    ]
    return {
        "schema": "openbfme.sage-map-objects",
        "schemaVersion": 0,
        "count": len(objects),
        "coordinateTransform": "godot=(sage.x,sage.z,-sage.y)",
        "positionZMeaning": "fixture",
        "objects": objects,
    }


def make_inputs() -> tuple[dict, dict, dict, dict]:
    report = make_hierarchical_report()
    manifest = make_manifest()
    static_plan = build_retail_static_prop_plan(report, manifest)
    return report, static_plan, manifest, make_map_objects()


def reseal_plan(plan: dict) -> dict:
    plan.pop("aggregateSha256", None)
    plan["aggregateSha256"] = canonical_sha256(plan)
    return plan


class RetailHierarchicalProfileTests(unittest.TestCase):
    def test_shared_hierarchical_w3d_is_grouped_once_with_exact_coverage(self) -> None:
        inputs = make_inputs()
        first = build_retail_hierarchical_prop_plan(*inputs)
        second = build_retail_hierarchical_prop_plan(*inputs)

        self.assertEqual(first, second)
        self.assertEqual(first["schema"], HIERARCHICAL_PROP_PLAN_SCHEMA)
        self.assertEqual(first["summary"]["eligibleTargetTypeCount"], 2)
        self.assertEqual(first["summary"]["conversionGroupCount"], 1)
        self.assertEqual(first["summary"]["uniqueTextureSourceCount"], 1)
        self.assertEqual(first["summary"]["objectBindingModelRowCount"], 2)
        self.assertEqual(
            first["summary"]["provenRootRigidConversionGroupCount"], 1
        )
        self.assertEqual(first["summary"]["hierarchicalBatchPlacementCount"], 3)
        self.assertTrue(
            first["policy"]["profileFragmentValidatedByImportProfile"]
        )
        self.assertFalse(first["policy"]["substitutesAllowed"])
        self.assertFalse(first["policy"]["roadsCountedAsPlacements"])

        group = first["conversionGroups"][0]
        self.assertEqual(group["targetObjects"], ["TreeA", "TreeB"])
        self.assertEqual(group["placementCount"], 3)
        self.assertEqual(group["hierarchyHeaderIds"], ["SHAREDTREE"])
        self.assertEqual(group["animationHeaderIds"], [])
        self.assertTrue(group["provenRootRigidBake"])
        self.assertEqual(
            group["modelReferences"],
            [
                {
                    "identifier": "SHAREDTREE.BODY",
                    "boneIndex": 0,
                    "role": "lod",
                }
            ],
        )
        self.assertEqual(group["modelSource"]["sha256"], "a" * 64)
        self.assertEqual(group["textureSources"][0]["sha256"], "b" * 64)

        bindings = first["profileFragment"]["objectBindings"]["models"]
        self.assertEqual([row["typeName"] for row in bindings], ["TreeA", "TreeB"])
        self.assertTrue(
            all(row["matchMethod"] == "exact-type-name" for row in bindings)
        )
        self.assertEqual(bindings[0]["glb"], bindings[1]["glb"])

        coverage = first["placementCoverage"]
        self.assertEqual(coverage["targetPlacementCount"], 3)
        self.assertEqual(coverage["hierarchicalBatchPlacementCount"], 3)
        self.assertEqual(coverage["uncoveredTargetPlacementCount"], 0)
        self.assertEqual(
            first["sourceEvidence"]["mapObjects"]["roadControlPointRecordCount"],
            1,
        )
        calculated = copy.deepcopy(first)
        declared = calculated.pop("aggregateSha256")
        self.assertEqual(declared, canonical_sha256(calculated))

    def test_fragment_satisfies_import_profile_hierarchical_contract(self) -> None:
        plan = build_retail_hierarchical_prop_plan(*make_inputs())
        payload = {
            "format": 1,
            "id": "hierarchical-prop-fixture",
            "pack": {"id": "hierarchical-prop-pack"},
            "resources": plan["profileFragment"]["resources"],
        }
        with tempfile.TemporaryDirectory() as raw:
            path = Path(raw) / "profile.json"
            path.write_text(json.dumps(payload), encoding="utf-8")
            profile = ImportProfile.load(path)

        self.assertEqual(len(profile.resources), 2)
        model = next(resource for resource in profile.resources if resource.kind == "model")
        texture = next(
            resource for resource in profile.resources if resource.kind == "texture"
        )
        self.assertEqual(model.converter, "w3d-hierarchical")
        self.assertEqual(model.options["animations"], [])
        self.assertEqual(model.options["required_equipment"], [])
        self.assertEqual(model.options["inputResourceIds"], [texture.id])
        self.assertTrue(model.options["provenRootRigidBake"])

    def test_meshless_hlod_is_rejected_before_profile_generation(self) -> None:
        report = make_hierarchical_report()
        report["scannedW3d"][0]["modelReferences"] = []
        seal_report(report)
        manifest = make_manifest()
        static_plan = build_retail_static_prop_plan(report, manifest)

        plan = build_retail_hierarchical_prop_plan(
            report, static_plan, manifest, make_map_objects()
        )

        self.assertEqual(plan["eligibleTargets"], [])
        self.assertEqual(plan["conversionGroups"], [])
        self.assertEqual(plan["profileFragment"]["resources"], [])
        for target in plan["rejectedTargets"]:
            self.assertIn(
                "model-w3d-has-no-supported-render-subobject",
                {reason["code"] for reason in target["reasons"]},
            )

    def test_multi_pivot_hierarchy_remains_a_normal_skeletal_conversion(self) -> None:
        report = make_hierarchical_report()
        scanned = report["scannedW3d"][0]
        scanned["headerIds"]["modelIds"].insert(1, "SHAREDTREE.BRANCH")
        scanned["modelReferences"].append(
            {
                "identifier": "SHAREDTREE.BRANCH",
                "boneIndex": 1,
                "role": "lod",
                "provenance": {
                    "virtualPath": "art/w3d/pt/shared-tree.w3d",
                    "offset": 100,
                },
            }
        )
        seal_report(report)
        manifest = make_manifest()
        static_plan = build_retail_static_prop_plan(report, manifest)

        plan = build_retail_hierarchical_prop_plan(
            report, static_plan, manifest, make_map_objects()
        )

        self.assertEqual(plan["summary"]["eligibleTargetTypeCount"], 2)
        self.assertEqual(
            plan["summary"]["provenRootRigidConversionGroupCount"], 0
        )
        model = next(
            resource
            for resource in plan["profileFragment"]["resources"]
            if resource["converter"] == "w3d-hierarchical"
        )
        self.assertFalse(model["options"]["provenRootRigidBake"])

    def test_animation_header_excludes_orc_style_zero_clip_impostor(self) -> None:
        report = make_hierarchical_report()
        report["scannedW3d"][0]["headerIds"]["animationIds"] = [
            "SHAREDTREE.IDLE"
        ]
        seal_report(report)
        manifest = make_manifest()
        static_plan = build_retail_static_prop_plan(report, manifest)
        plan = build_retail_hierarchical_prop_plan(
            report, static_plan, manifest, make_map_objects()
        )

        self.assertEqual(plan["eligibleTargets"], [])
        self.assertEqual(plan["conversionGroups"], [])
        for target in plan["rejectedTargets"]:
            self.assertIn(
                "model-w3d-contains-animation-headers",
                {reason["code"] for reason in target["reasons"]},
            )

    def test_authored_animation_gap_only_excludes_affected_target(self) -> None:
        report = make_hierarchical_report()
        report["unresolved"]["references"].append(
            {
                "targetObject": "TreeA",
                "kind": "animation",
                "usage": "animation",
                "identifier": "TREE_SKL.TREE_IDLE",
                "status": "missing",
                "candidateCount": 0,
                "candidates": [],
                "provenance": {
                    "virtualPath": "data/ini/object/tree.ini",
                    "line": 20,
                },
            }
        )
        report["summary"]["unresolvedReferenceCount"] = 1
        report["summary"]["ready"] = False
        seal_report(report)
        manifest = make_manifest()
        static_plan = build_retail_static_prop_plan(report, manifest)
        plan = build_retail_hierarchical_prop_plan(
            report, static_plan, manifest, make_map_objects()
        )

        self.assertEqual(
            [target["targetObject"] for target in plan["eligibleTargets"]],
            ["TreeB"],
        )
        excluded = next(
            target
            for target in plan["rejectedTargets"]
            if target["targetObject"] == "TreeA"
        )
        codes = {reason["code"] for reason in excluded["reasons"]}
        self.assertIn("requires-animation", codes)
        self.assertIn("unresolved-visual-reference", codes)

    def test_unresolved_embedded_texture_and_scanner_warning_fail_closed(self) -> None:
        report = make_hierarchical_report()
        dependency = report["w3dDependencyClosure"]
        texture = dependency["embeddedTextures"][0]
        texture["status"] = "missing"
        texture.pop("physicalVirtualPaths")
        texture["candidateCount"] = 1
        texture["candidates"] = ["art/compiledtextures/pt/shared-tree.dds"]
        dependency["summary"].update(
            {
                "resolvedEmbeddedTextureCount": 0,
                "missingEmbeddedTextureCount": 1,
            }
        )
        report["summary"].update(
            {
                "resolvedEmbeddedTextureCount": 0,
                "unresolvedEmbeddedTextureCount": 1,
                "ready": False,
            }
        )
        seal_report(report)
        manifest = make_manifest()
        static_plan = build_retail_static_prop_plan(report, manifest)
        plan = build_retail_hierarchical_prop_plan(
            report, static_plan, manifest, make_map_objects()
        )
        for target in plan["rejectedTargets"]:
            self.assertIn(
                "embedded-texture-unresolved",
                {reason["code"] for reason in target["reasons"]},
            )

        report = make_hierarchical_report()
        report["scannedW3d"][0]["warnings"] = [
            {"code": "unsupported-chunk", "offset": 12}
        ]
        seal_report(report)
        static_plan = build_retail_static_prop_plan(report, manifest)
        plan = build_retail_hierarchical_prop_plan(
            report, static_plan, manifest, make_map_objects()
        )
        for target in plan["rejectedTargets"]:
            self.assertIn(
                "model-w3d-scanner-warnings",
                {reason["code"] for reason in target["reasons"]},
            )

    def test_static_eligible_targets_are_retained_as_explicit_rejections(self) -> None:
        report = make_report()
        manifest = make_manifest()
        static_plan = build_retail_static_prop_plan(report, manifest)
        plan = build_retail_hierarchical_prop_plan(
            report, static_plan, manifest, make_map_objects()
        )
        self.assertEqual(plan["eligibleTargets"], [])
        self.assertFalse(
            plan["policy"]["profileFragmentValidatedByImportProfile"]
        )
        for target in plan["rejectedTargets"]:
            self.assertEqual(
                target["reasons"],
                [{"code": "already-covered-by-static-prop-plan"}],
            )

    def test_rejects_tampered_or_resealed_nonmatching_static_plan(self) -> None:
        report, static_plan, manifest, objects = make_inputs()
        static_plan["summary"]["eligibleTargetTypeCount"] = 99
        with self.assertRaisesRegex(ValueError, "static-prop plan digest mismatch"):
            build_retail_hierarchical_prop_plan(
                report, static_plan, manifest, objects
            )

        report, static_plan, manifest, objects = make_inputs()
        static_plan["policy"]["substitutesAllowed"] = True
        reseal_plan(static_plan)
        with self.assertRaisesRegex(ValueError, "does not exactly match"):
            build_retail_hierarchical_prop_plan(
                report, static_plan, manifest, objects
            )

    def test_rejects_case_drift_unsafe_closure_and_manifest_hash_drift(self) -> None:
        report, static_plan, manifest, _ = make_inputs()
        with self.assertRaisesRegex(ValueError, "target case does not match"):
            build_retail_hierarchical_prop_plan(
                report, static_plan, manifest, make_map_objects(wrong_case=True)
            )

        report = make_hierarchical_report()
        report["exactLeaves"][0]["physicalVirtualPaths"] = ["../shared-tree.w3d"]
        seal_report(report)
        with self.assertRaisesRegex(ValueError, "unsafe leaf physical path"):
            build_retail_hierarchical_prop_plan(
                report,
                {},
                make_manifest(),
                make_map_objects(),
            )

        report = make_hierarchical_report()
        manifest = make_manifest()
        static_plan = build_retail_static_prop_plan(report, manifest)
        manifest["files"][0]["sha256"] = "e" * 64
        reseal_manifest(manifest)
        with self.assertRaisesRegex(ValueError, "source SHA-256 mismatch"):
            build_retail_hierarchical_prop_plan(
                report, static_plan, manifest, make_map_objects()
            )

    def test_file_helpers_load_build_and_write_verified_plan(self) -> None:
        report, static_plan, manifest, objects = make_inputs()
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            input_values = [report, static_plan, manifest, objects]
            input_paths = [
                root / "closure.json",
                root / "static.json",
                root / "manifest.json",
                root / "objects.json",
            ]
            for path, value in zip(input_paths, input_values, strict=True):
                path.write_text(json.dumps(value), encoding="utf-8")
            loaded = load_retail_hierarchical_prop_plan_inputs(*input_paths)
            plan = build_retail_hierarchical_prop_plan(*loaded)
            output = root / "hierarchical-plan.json"
            write_retail_hierarchical_prop_plan(output, plan)
            written = json.loads(output.read_text(encoding="utf-8"))
        self.assertEqual(written, plan)


if __name__ == "__main__":
    unittest.main()
