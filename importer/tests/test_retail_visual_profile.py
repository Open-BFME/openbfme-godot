from __future__ import annotations

import copy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from openbfme_importer.profile import ImportProfile
from openbfme_importer.retail_visual_profile import (
    STATIC_PROP_PLAN_SCHEMA,
    build_retail_static_prop_plan,
    load_retail_static_prop_plan_inputs,
    write_retail_static_prop_plan,
)


def canonical_sha256(value: object) -> str:
    return hashlib.sha256(
        json.dumps(
            value,
            sort_keys=True,
            ensure_ascii=False,
            separators=(",", ":"),
            allow_nan=False,
        ).encode("utf-8")
    ).hexdigest()


def manifest_aggregate(files: list[dict]) -> str:
    digest = hashlib.sha256()
    for item in files:
        digest.update(item["path"].encode("utf-8"))
        digest.update(b"\0")
        digest.update(str(item["size"]).encode("ascii"))
        digest.update(b"\0")
        digest.update(item["sha256"].encode("ascii"))
        digest.update(b"\n")
    return digest.hexdigest()


def source(
    path: str,
    digest: str,
    size: int,
    *,
    archive: str = "art.big",
    offset: int = 1,
) -> dict:
    return {
        "archive": archive,
        "offset": offset,
        "path": path,
        "precedence": 1000,
        "sha256": digest,
        "size": size,
    }


def make_manifest() -> dict:
    files = [
        source("art/w3d/pt/shared-tree.w3d", "a" * 64, 100, offset=4),
        source("art/compiledtextures/pt/shared-tree.dds", "b" * 64, 20, offset=8),
    ]
    return {
        "schema": "openbfme.effective-assets-manifest",
        "schema_version": 0,
        "catalog": {
            "archive_count": 1,
            "entry_count": 2,
            "format": 4,
            "identity_sha256": "c" * 64,
        },
        "install": {"identity_sha256": "d" * 64, "root": "private"},
        "totals": {"bytes": 120, "files": 2},
        "aggregate_sha256": manifest_aggregate(files),
        "files": files,
    }


def model_leaf(target: str) -> dict:
    return {
        "targetObject": target,
        "kind": "model",
        "usage": "model",
        "identifier": "SharedTree",
        "status": "resolved",
        "physicalVirtualPaths": ["art/w3d/pt/shared-tree.w3d"],
        "conditions": [],
        "lifecyclePhases": ["intact"],
        "evidence": ["exact-stem:shared-tree"],
        "provenance": {
            "virtualPath": "data/ini/object/tree.ini",
            "line": 10,
            "definingObject": target,
            "inheritanceDistance": 0,
            "scopePath": ["W3DDefaultDraw Draw", "DefaultModelConditionState"],
        },
    }


def object_summary(target: str) -> dict:
    return {
        "name": target,
        "objectKind": "Object",
        "source": {"virtualPath": "data/ini/object/tree.ini", "line": 1},
        "ancestry": [target],
        "inheritanceComplete": True,
        "lifecycleCoverage": ["intact"],
        "drawModuleCount": 1,
        "drawModules": [],
        "referenceSummary": {
            "resolved": 1,
            "semantic": 0,
            "missing": 0,
            "ambiguous": 0,
            "invalid": 0,
        },
    }


def seal_report(report: dict) -> dict:
    dependency = report["w3dDependencyClosure"]
    dependency.pop("aggregateSha256", None)
    dependency["aggregateSha256"] = canonical_sha256(dependency)
    report.pop("aggregateSha256", None)
    report["aggregateSha256"] = canonical_sha256(report)
    return report


def make_report() -> dict:
    targets = ["TreeA", "TreeB"]
    embedded = [
        {
            "sourceW3dVirtualPath": "art/w3d/pt/shared-tree.w3d",
            "identifier": "SharedTree.tga",
            "status": "resolved",
            "physicalVirtualPaths": ["art/compiledtextures/pt/shared-tree.dds"],
            "evidence": [
                "w3d-compiled-texture:exact-tga-stem-to-dds",
                "texture:case-insensitive-exact-basename-extensionless-stem",
            ],
            "provenance": {
                "virtualPath": "art/w3d/pt/shared-tree.w3d",
                "valueOffset": 40,
                "valueSize": 15,
                "chunkHeaderOffset": 32,
                "chunkPayloadOffset": 40,
                "chunkPayloadSize": 15,
                "chunkId": 50,
                "chunkIdHex": "0x00000032",
                "chunkName": "texture-name",
                "parentChunkHeaderOffset": 24,
            },
            "textureChunkHeaderOffset": 24,
        }
    ]
    dependency = {
        "readBoundary": {
            "policy": "resolved-target-w3d-leaves-plus-exact-unresolved-candidates",
            "uniqueVirtualPaths": ["art/w3d/pt/shared-tree.w3d"],
            "uniqueReadCount": 1,
            "byteLength": 100,
        },
        "embeddedTextures": embedded,
        "summary": {
            "fileCount": 1,
            "embeddedTextureReferenceCount": 1,
            "resolvedEmbeddedTextureCount": 1,
            "missingEmbeddedTextureCount": 0,
            "ambiguousEmbeddedTextureCount": 0,
            "invalidEmbeddedTextureCount": 0,
        },
    }
    report = {
        "schema": "openbfme.retail-visual-closure",
        "schemaVersion": 1,
        "targets": [
            {"requestedName": target, "status": "resolved"} for target in targets
        ],
        "definitionClosure": [],
        "missingDefinitions": [],
        "sourceClosure": {"paths": [], "includes": []},
        "catalog": {
            "assetPathCount": 2,
            "sageSourcePathCount": 1,
            "w3dPathCount": 1,
            "visualPathCount": 2,
            "w3dCatalogMode": "path-only-plus-targeted-headers",
        },
        "objects": [object_summary(target) for target in targets],
        "exactLeaves": [model_leaf(target) for target in targets],
        "semanticLeaves": [],
        "unresolved": {"graphDiagnostics": [], "references": []},
        "scannedW3d": [
            {
                "virtualPath": "art/w3d/pt/shared-tree.w3d",
                "byteLength": 100,
                "sha256": "a" * 64,
                "headerIds": {
                    "virtualPath": "art/w3d/pt/shared-tree.w3d",
                    "modelIds": ["SHAREDTREE"],
                    "hierarchyIds": [],
                    "animationIds": [],
                },
                "modelReferences": [],
                "warnings": [],
            }
        ],
        "w3dDependencyClosure": dependency,
        "summary": {
            "targetCount": 2,
            "resolvedTargetCount": 2,
            "definitionClosureCount": 0,
            "sourceClosureCount": 0,
            "exactLeafCount": 2,
            "semanticLeafCount": 0,
            "unresolvedReferenceCount": 0,
            "graphDiagnosticCount": 0,
            "scannedW3dCount": 1,
            "scannedW3dByteLength": 100,
            "embeddedTextureReferenceCount": 1,
            "resolvedEmbeddedTextureCount": 1,
            "unresolvedEmbeddedTextureCount": 0,
            "ready": True,
        },
    }
    return seal_report(report)


def reseal_manifest(manifest: dict) -> dict:
    manifest["totals"] = {
        "files": len(manifest["files"]),
        "bytes": sum(item["size"] for item in manifest["files"]),
    }
    manifest["aggregate_sha256"] = manifest_aggregate(manifest["files"])
    return manifest


class RetailVisualProfileTests(unittest.TestCase):
    def test_shared_static_w3d_is_converted_once_with_exact_bindings(self) -> None:
        first = build_retail_static_prop_plan(make_report(), make_manifest())
        second = build_retail_static_prop_plan(make_report(), make_manifest())

        self.assertEqual(first, second)
        self.assertEqual(first["schema"], STATIC_PROP_PLAN_SCHEMA)
        self.assertEqual(first["summary"]["eligibleTargetTypeCount"], 2)
        self.assertEqual(first["summary"]["ineligibleTargetTypeCount"], 0)
        self.assertEqual(first["summary"]["conversionGroupCount"], 1)
        self.assertEqual(first["summary"]["uniqueTextureSourceCount"], 1)
        self.assertTrue(first["summary"]["placementIndependent"])
        self.assertFalse(first["policy"]["placementDataConsumed"])
        self.assertFalse(first["policy"]["substitutesAllowed"])

        group = first["conversionGroups"][0]
        self.assertEqual(group["targetObjects"], ["TreeA", "TreeB"])
        self.assertEqual(group["modelSource"]["sha256"], "a" * 64)
        self.assertEqual(group["textureSources"][0]["sha256"], "b" * 64)
        self.assertEqual(group["modelSource"]["source"]["archive"], "art.big")

        bindings = first["profileFragment"]["objectBindings"]["models"]
        self.assertEqual([item["typeName"] for item in bindings], ["TreeA", "TreeB"])
        self.assertTrue(
            all(item["matchMethod"] == "exact-type-name" for item in bindings)
        )
        self.assertEqual(bindings[0]["glb"], bindings[1]["glb"])
        self.assertEqual(
            bindings[0]["sourceVirtualModel"],
            "art/w3d/pt/shared-tree.w3d",
        )

        calculated = copy.deepcopy(first)
        declared = calculated.pop("aggregateSha256")
        self.assertEqual(declared, canonical_sha256(calculated))

    def test_profile_fragment_satisfies_import_profile_contract(self) -> None:
        plan = build_retail_static_prop_plan(make_report(), make_manifest())
        payload = {
            "format": 1,
            "id": "static-prop-fragment-fixture",
            "pack": {"id": "static-prop-fragment-pack"},
            "resources": plan["profileFragment"]["resources"],
        }
        with tempfile.TemporaryDirectory() as raw:
            path = Path(raw) / "profile.json"
            path.write_text(json.dumps(payload), encoding="utf-8")
            profile = ImportProfile.load(path)
        self.assertEqual(len(profile.resources), 2)
        model = next(item for item in profile.resources if item.kind == "model")
        texture = next(item for item in profile.resources if item.kind == "texture")
        self.assertEqual(model.converter, "w3d-static")
        self.assertEqual(model.options["inputResourceIds"], [texture.id])

    def test_unrelated_animation_gap_only_excludes_affected_target(self) -> None:
        report = make_report()
        report["unresolved"]["references"].append(
            {
                "targetObject": "TreeA",
                "kind": "animation",
                "usage": "animation",
                "identifier": "TREE_SKL.TREE_IDLE",
                "status": "missing",
                "candidateCount": 1,
                "candidates": ["art/w3d/pt/tree-nearby.w3d"],
                "provenance": {
                    "virtualPath": "data/ini/object/tree.ini",
                    "line": 20,
                },
            }
        )
        report["summary"]["unresolvedReferenceCount"] = 1
        report["summary"]["ready"] = False
        seal_report(report)

        plan = build_retail_static_prop_plan(report, make_manifest())
        self.assertEqual(
            [item["targetObject"] for item in plan["eligibleTargets"]], ["TreeB"]
        )
        excluded = plan["ineligibleTargets"][0]
        reason_codes = {item["code"] for item in excluded["reasons"]}
        self.assertIn("requires-animation", reason_codes)
        self.assertIn("unresolved-visual-reference", reason_codes)
        self.assertEqual(plan["summary"]["conversionGroupCount"], 1)

    def test_unresolved_embedded_texture_candidate_is_never_substituted(self) -> None:
        report = make_report()
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

        plan = build_retail_static_prop_plan(report, make_manifest())
        self.assertEqual(plan["eligibleTargets"], [])
        self.assertEqual(plan["conversionGroups"], [])
        self.assertEqual(plan["profileFragment"]["resources"], [])
        for target in plan["ineligibleTargets"]:
            self.assertIn(
                "embedded-texture-unresolved",
                {item["code"] for item in target["reasons"]},
            )

    def test_scanner_warning_and_headers_fail_closed_with_reasons(self) -> None:
        report = make_report()
        scanned = report["scannedW3d"][0]
        scanned["warnings"] = [{"code": "unsupported-chunk", "offset": 12}]
        scanned["headerIds"]["hierarchyIds"] = ["TREE_SKL"]
        scanned["headerIds"]["animationIds"] = ["TREE_SKL.TREE_IDLE"]
        seal_report(report)

        plan = build_retail_static_prop_plan(report, make_manifest())
        codes = {
            reason["code"]
            for target in plan["ineligibleTargets"]
            for reason in target["reasons"]
        }
        self.assertIn("model-w3d-scanner-warnings", codes)
        self.assertIn("model-w3d-contains-hierarchy-headers", codes)
        self.assertIn("model-w3d-contains-animation-headers", codes)

    def test_rejects_report_and_nested_dependency_digest_mismatches(self) -> None:
        report = make_report()
        report["targets"][0]["requestedName"] = "Tampered"
        with self.assertRaisesRegex(ValueError, "visual closure digest mismatch"):
            build_retail_static_prop_plan(report, make_manifest())

        report = make_report()
        report["w3dDependencyClosure"]["summary"]["fileCount"] = 2
        report.pop("aggregateSha256")
        report["aggregateSha256"] = canonical_sha256(report)
        with self.assertRaisesRegex(
            ValueError, "W3DDependencyClosure|w3dDependencyClosure|digest mismatch"
        ):
            build_retail_static_prop_plan(report, make_manifest())

    def test_rejects_schema_and_recalculated_summary_inconsistencies(self) -> None:
        report = make_report()
        report["schemaVersion"] = 2
        seal_report(report)
        with self.assertRaisesRegex(ValueError, "schema version"):
            build_retail_static_prop_plan(report, make_manifest())

        report = make_report()
        report["summary"]["ready"] = False
        seal_report(report)
        with self.assertRaisesRegex(ValueError, "ready status mismatch"):
            build_retail_static_prop_plan(report, make_manifest())

    def test_rejects_duplicate_case_ambiguous_ids_and_paths(self) -> None:
        report = make_report()
        report["targets"].append(
            {"requestedName": "treea", "status": "missing-definition"}
        )
        report["summary"]["targetCount"] = 3
        seal_report(report)
        with self.assertRaisesRegex(ValueError, "case-ambiguous target Object id"):
            build_retail_static_prop_plan(report, make_manifest())

        manifest = make_manifest()
        manifest["files"].append(
            source("ART/W3D/PT/SHARED-TREE.W3D", "e" * 64, 1, offset=12)
        )
        reseal_manifest(manifest)
        with self.assertRaisesRegex(ValueError, "case-ambiguous effective-assets"):
            build_retail_static_prop_plan(make_report(), manifest)

    def test_rejects_unsafe_paths_and_source_hash_disagreement(self) -> None:
        report = make_report()
        report["exactLeaves"][0]["physicalVirtualPaths"] = ["../shared-tree.w3d"]
        seal_report(report)
        with self.assertRaisesRegex(ValueError, "unsafe leaf physical path"):
            build_retail_static_prop_plan(report, make_manifest())

        manifest = make_manifest()
        manifest["files"][0]["sha256"] = "e" * 64
        reseal_manifest(manifest)
        with self.assertRaisesRegex(ValueError, "source SHA-256 mismatch"):
            build_retail_static_prop_plan(make_report(), manifest)

    def test_file_helpers_load_build_and_write_verified_plan(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            report_path = root / "closure.json"
            manifest_path = root / "manifest.json"
            plan_path = root / "plan.json"
            report_path.write_text(json.dumps(make_report()), encoding="utf-8")
            manifest_path.write_text(json.dumps(make_manifest()), encoding="utf-8")
            report, manifest = load_retail_static_prop_plan_inputs(
                report_path, manifest_path
            )
            plan = build_retail_static_prop_plan(report, manifest)
            write_retail_static_prop_plan(plan_path, plan)
            written = json.loads(plan_path.read_text(encoding="utf-8"))
        self.assertEqual(written, plan)


if __name__ == "__main__":
    unittest.main()
