from __future__ import annotations

import copy
import hashlib
import json
from pathlib import Path
import struct
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from openbfme_importer.profile import ImportProfile
from openbfme_importer.retail_animated_prop_profile import (
    ANIMATED_PROP_PLAN_SCHEMA,
    build_retail_animated_prop_plan,
    generated_import_profile,
    load_retail_animated_prop_plan_inputs,
    write_generated_import_profile,
    write_retail_animated_prop_plan,
)
from openbfme_importer.retail_hierarchical_profile import (
    build_retail_hierarchical_prop_plan,
)
from openbfme_importer.retail_visual_profile import build_retail_static_prop_plan
from openbfme_importer.w3d_metadata import scan_w3d_metadata


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


def _fixed(value: str, size: int) -> bytes:
    encoded = value.encode("cp1252")
    return encoded + b"\0" * (size - len(encoded))


def _chunk(chunk_id: int, payload: bytes, *, children: bool = False) -> bytes:
    size = len(payload) | (0x80000000 if children else 0)
    return struct.pack("<II", chunk_id, size) + payload


def _mesh_header(mesh: str, container: str) -> bytes:
    return struct.pack(
        "<II16s16s9I10f",
        0x00040002,
        0x0002A000,
        _fixed(mesh, 16),
        _fixed(container, 16),
        12,
        8,
        1,
        0,
        0,
        0,
        0,
        0x13,
        1,
        *([0.0] * 10),
    )


def model_w3d(
    *,
    model_id: str = "CRITTER_SKN",
    secondary_skin: bool = False,
) -> bytes:
    # The mesh header declares 8 vertices, so valid dual-bone streams must
    # carry exactly 8 float32-triple records: the scanner decodes them
    # fail-closed instead of warning about them.
    secondary_streams = (
        _chunk(0x0C00, b"\0" * 96) + _chunk(0x0C01, b"\0" * 96)
        if secondary_skin
        else b""
    )
    mesh = _chunk(
        0x00000000,
        _chunk(0x1F, _mesh_header("BODY", model_id))
        + _chunk(0x02, b"\0" * 12)
        + _chunk(0x20, b"\0" * 12)
        + secondary_streams,
        children=True,
    )
    lod_array = _chunk(
        0x702,
        _chunk(0x703, struct.pack("<If", 1, 1000.0))
        + _chunk(
            0x704,
            struct.pack("<I32s", 0, _fixed(f"{model_id}.BODY", 32)),
        ),
        children=True,
    )
    hlod = _chunk(
        0x700,
        _chunk(
            0x701,
            struct.pack(
                "<II16s16s",
                0x00010000,
                1,
                _fixed(model_id, 16),
                _fixed("CRITTER_SKL", 16),
            ),
        )
        + lod_array,
        children=False,
    )
    return mesh + hlod


def hierarchy_w3d() -> bytes:
    pivot = struct.pack("<16si10f", _fixed("ROOTTRANSFORM", 16), -1, *([0.0] * 10))
    return _chunk(
        0x100,
        _chunk(
            0x101,
            struct.pack(
                "<I16sI3f",
                0x00040001,
                _fixed("CRITTER_SKL", 16),
                1,
                0.0,
                0.0,
                0.0,
            ),
        )
        + _chunk(0x102, pivot),
        children=False,
    )


def animation_w3d() -> bytes:
    return _chunk(
        0x280,
        _chunk(
            0x281,
            struct.pack(
                "<I16s16sIHH",
                0x00000001,
                _fixed("CRITTER_IDLE", 16),
                _fixed("CRITTER_SKL", 16),
                20,
                20,
                1,
            ),
        )
        + _chunk(0x282, b"\0" * 8),
        children=True,
    )


def embedded_model_w3d() -> bytes:
    return model_w3d() + hierarchy_w3d() + animation_w3d()


def _source(path: str, payload: bytes, offset: int) -> dict:
    return {
        "archive": "fixture.big",
        "offset": offset,
        "path": path,
        "precedence": 1000,
        "sha256": hashlib.sha256(payload).hexdigest(),
        "size": len(payload),
    }


def _manifest(files: list[tuple[str, bytes]]) -> dict:
    rows = [
        _source(path, payload, index * 100)
        for index, (path, payload) in enumerate(
            sorted(files, key=lambda item: item[0].casefold()), start=1
        )
    ]
    aggregate = hashlib.sha256()
    for item in rows:
        aggregate.update(item["path"].encode("utf-8"))
        aggregate.update(b"\0")
        aggregate.update(str(item["size"]).encode("ascii"))
        aggregate.update(b"\0")
        aggregate.update(item["sha256"].encode("ascii"))
        aggregate.update(b"\n")
    return {
        "schema": "openbfme.effective-assets-manifest",
        "schema_version": 0,
        "catalog": {
            "archive_count": 1,
            "entry_count": len(rows),
            "format": 4,
            "identity_sha256": "c" * 64,
        },
        "install": {"identity_sha256": "d" * 64, "root": "private"},
        "totals": {
            "bytes": sum(item["size"] for item in rows),
            "files": len(rows),
        },
        "aggregate_sha256": aggregate.hexdigest(),
        "files": rows,
    }


def _model_leaf(target: str) -> dict:
    return {
        "targetObject": target,
        "kind": "model",
        "usage": "model",
        "identifier": "CRITTER_SKN",
        "status": "resolved",
        "physicalVirtualPaths": ["art/w3d/cu/critter_skn.w3d"],
        "conditions": [],
        "lifecyclePhases": ["intact"],
        "evidence": ["fixture-exact"],
        "provenance": {
            "virtualPath": "data/ini/object/fixture.ini",
            "line": 10,
            "definingObject": target,
            "inheritanceDistance": 0,
            "scopePath": ["W3DScriptedModelDraw Draw"],
        },
    }


def _animation_leaf(target: str) -> dict:
    return {
        "targetObject": target,
        "kind": "animation",
        "usage": "animation",
        "identifier": "CRITTER_SKL.CRITTER_IDLE",
        "status": "resolved",
        "physicalVirtualPaths": ["art/w3d/cu/critter_idle.w3d"],
        "conditions": [],
        "lifecyclePhases": ["intact"],
        "evidence": ["fixture-exact"],
        "provenance": {
            "virtualPath": "data/ini/object/fixture.ini",
            "line": 11,
            "definingObject": target,
            "inheritanceDistance": 0,
            "scopePath": ["W3DScriptedModelDraw Draw", "IdleAnimationState"],
        },
    }


def _missing_transition_leaf(
    target: str,
    identifier: str,
    condition: str,
) -> dict:
    return {
        "targetObject": target,
        "kind": "animation",
        "usage": "animation",
        "identifier": identifier,
        "status": "missing",
        "conditions": [condition],
        "lifecyclePhases": ["intact"],
        "reason": "missing W3D animation reference",
        "provenance": {
            "virtualPath": "data/ini/object/fixture.ini",
            "line": 13,
            "definingObject": target,
            "inheritanceDistance": 0,
            "scopePath": [
                "W3DScriptedModelDraw Draw",
                f"TransitionState {condition}",
                "Animation MISSING",
            ],
        },
    }


def _texture_leaf(target: str) -> dict:
    return {
        "targetObject": target,
        "kind": "texture",
        "usage": "texture",
        "identifier": "Critter.tga",
        "status": "resolved",
        "physicalVirtualPaths": ["art/compiledtextures/cu/critter.dds"],
        "conditions": [],
        "lifecyclePhases": ["intact"],
        "evidence": ["fixture-exact"],
        "provenance": {
            "virtualPath": "data/ini/object/fixture.ini",
            "line": 12,
            "definingObject": target,
            "inheritanceDistance": 0,
            "scopePath": ["W3DScriptedModelDraw Draw"],
        },
    }


def _semantic_leaf(target: str) -> dict:
    return {
        "targetObject": target,
        "kind": "model",
        "usage": "model",
        "identifier": "None",
        "status": "semantic",
        "physicalVirtualPaths": [],
        "conditions": [],
        "lifecyclePhases": ["intact"],
        "evidence": ["semantic-none"],
        "provenance": {
            "virtualPath": "data/ini/object/fixture.ini",
            "line": 20,
            "definingObject": target,
            "inheritanceDistance": 0,
            "scopePath": ["W3DDefaultDraw Draw"],
        },
    }


def _object_summary(target: str) -> dict:
    return {
        "name": target,
        "objectKind": "Object",
        "source": {"virtualPath": "data/ini/object/fixture.ini", "line": 1},
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


def _scan_record(path: str, payload: bytes) -> dict:
    metadata = scan_w3d_metadata(payload, path)
    headers = metadata.file_headers()
    return {
        "virtualPath": path,
        "byteLength": len(payload),
        "sha256": hashlib.sha256(payload).hexdigest(),
        "headerIds": {
            "virtualPath": path,
            "modelIds": list(headers.model_ids),
            "hierarchyIds": list(headers.hierarchy_ids),
            "animationIds": list(headers.animation_ids),
        },
        "modelReferences": [item.neutral() for item in metadata.model_references],
        "warnings": [item.neutral() for item in metadata.warnings],
    }


def _seal_report(report: dict) -> dict:
    dependency = report["w3dDependencyClosure"]
    dependency.pop("aggregateSha256", None)
    dependency["aggregateSha256"] = canonical_sha256(dependency)
    report.pop("aggregateSha256", None)
    report["aggregateSha256"] = canonical_sha256(report)
    return report


def _report(files: dict[str, bytes], *, embedded: bool = False) -> dict:
    targets = ["Critter", "LogicalEmitter"]
    scanned_paths = (
        ["art/w3d/cu/critter_skn.w3d"]
        if embedded
        else [
            "art/w3d/cu/critter_idle.w3d",
            "art/w3d/cu/critter_skn.w3d",
        ]
    )
    scanned = [_scan_record(path, files[path]) for path in scanned_paths]
    dependency = {
        "readBoundary": {
            "policy": "resolved-target-w3d-leaves-plus-exact-unresolved-candidates",
            "uniqueVirtualPaths": scanned_paths,
            "uniqueReadCount": len(scanned),
            "byteLength": sum(len(files[path]) for path in scanned_paths),
        },
        "embeddedTextures": [],
        "summary": {
            "fileCount": len(scanned),
            "embeddedTextureReferenceCount": 0,
            "resolvedEmbeddedTextureCount": 0,
            "missingEmbeddedTextureCount": 0,
            "ambiguousEmbeddedTextureCount": 0,
            "invalidEmbeddedTextureCount": 0,
        },
    }
    exact = [_model_leaf("Critter"), _texture_leaf("Critter")]
    if not embedded:
        exact.insert(1, _animation_leaf("Critter"))
    semantic = [_semantic_leaf("LogicalEmitter")]
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
            "assetPathCount": len(files),
            "sageSourcePathCount": 1,
            "w3dPathCount": sum(path.endswith(".w3d") for path in files),
            "visualPathCount": len(files),
            "w3dCatalogMode": "path-only-plus-targeted-headers",
        },
        "objects": [_object_summary(target) for target in targets],
        "exactLeaves": exact,
        "semanticLeaves": semantic,
        "unresolved": {"graphDiagnostics": [], "references": []},
        "scannedW3d": scanned,
        "w3dDependencyClosure": dependency,
        "summary": {
            "targetCount": len(targets),
            "resolvedTargetCount": len(targets),
            "definitionClosureCount": 0,
            "sourceClosureCount": 0,
            "exactLeafCount": len(exact),
            "semanticLeafCount": len(semantic),
            "unresolvedReferenceCount": 0,
            "graphDiagnosticCount": 0,
            "scannedW3dCount": len(scanned),
            "scannedW3dByteLength": sum(len(files[path]) for path in scanned_paths),
            "embeddedTextureReferenceCount": 0,
            "resolvedEmbeddedTextureCount": 0,
            "unresolvedEmbeddedTextureCount": 0,
            "ready": True,
        },
    }
    return _seal_report(report)


def _map_objects() -> dict:
    objects = [
        {"index": 0, "typeName": "Critter", "roadType": 0},
        {"index": 1, "typeName": "Critter", "roadType": 0},
        {"index": 2, "typeName": "LogicalEmitter", "roadType": 0},
        {"index": 3, "typeName": "RoadControl", "roadType": 2},
    ]
    return {
        "schema": "openbfme.sage-map-objects",
        "schemaVersion": 0,
        "count": len(objects),
        "coordinateTransform": "fixture",
        "positionZMeaning": "fixture",
        "objects": objects,
    }


def _base_profile(*, logical_name: str = "LogicalEmitter") -> dict:
    return {
        "format": 1,
        "id": "animated-prop-fixture-base",
        "pack": {"id": "animated-prop-fixture-base-pack"},
        "resources": [
            {
                "id": "fixture-map",
                "kind": "map",
                "converter": "sage-map",
                "patterns": ["maps/fixture/map.map"],
                "required": True,
                "limit": 1,
                "expected_count": 1,
                "options": {
                    "objectBindings": {
                        "logical": [
                            {
                                "typeName": logical_name,
                                "classification": "ambient-audio-emitter",
                            }
                        ],
                        "models": [],
                    }
                },
            }
        ],
    }


class Fixture:
    def __init__(
        self,
        root: Path,
        *,
        embedded: bool = False,
        secondary_skin: bool = False,
    ) -> None:
        if embedded and secondary_skin:
            raise ValueError("fixture cannot combine embedded and secondary modes")
        self.root = root
        self.files = (
            {
                "art/w3d/cu/critter_skn.w3d": embedded_model_w3d(),
                "art/compiledtextures/cu/critter.dds": b"fixture-texture",
            }
            if embedded
            else {
                "art/w3d/cu/critter_skn.w3d": model_w3d(secondary_skin=secondary_skin),
                "art/w3d/cu/critter_skl.w3d": hierarchy_w3d(),
                "art/w3d/cu/critter_idle.w3d": animation_w3d(),
                "art/compiledtextures/cu/critter.dds": b"fixture-texture",
            }
        )
        for virtual_path, payload in self.files.items():
            path = root.joinpath(*virtual_path.split("/"))
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(payload)
        self.manifest = _manifest(list(self.files.items()))
        self.report = _report(self.files, embedded=embedded)
        self.objects = _map_objects()
        self.base_profile = _base_profile()
        self.static = build_retail_static_prop_plan(self.report, self.manifest)
        self.hierarchical = build_retail_hierarchical_prop_plan(
            self.report,
            self.static,
            self.manifest,
            self.objects,
        )

    def args(self) -> tuple[object, ...]:
        return (
            self.report,
            self.static,
            self.hierarchical,
            self.manifest,
            self.objects,
            self.base_profile,
            self.root,
        )

    def retarget_physical_object(self, target: str) -> None:
        old_target = "Critter"
        self.report["targets"][0]["requestedName"] = target
        for section in ("exactLeaves", "semanticLeaves"):
            for item in self.report[section]:
                if item["targetObject"] == old_target:
                    item["targetObject"] = target
                    item["provenance"]["definingObject"] = target
        object_summary = self.report["objects"][0]
        object_summary["name"] = target
        object_summary["ancestry"] = [target]
        for item in self.objects["objects"]:
            if item.get("typeName") == old_target:
                item["typeName"] = target
        _seal_report(self.report)
        self.static = build_retail_static_prop_plan(self.report, self.manifest)
        self.hierarchical = build_retail_hierarchical_prop_plan(
            self.report,
            self.static,
            self.manifest,
            self.objects,
        )

    def add_shared_animation_clone(self) -> None:
        target = "CritterClone"
        model_path = "art/w3d/cu/critter2_skn.w3d"
        payload = model_w3d(model_id="CRITTER2_SKN")
        self.files[model_path] = payload
        physical_path = self.root.joinpath(*model_path.split("/"))
        physical_path.parent.mkdir(parents=True, exist_ok=True)
        physical_path.write_bytes(payload)
        self.manifest = _manifest(list(self.files.items()))

        self.report["targets"].append({"requestedName": target, "status": "resolved"})
        self.report["objects"].append(_object_summary(target))
        model_leaf = _model_leaf(target)
        model_leaf["identifier"] = "CRITTER2_SKN"
        model_leaf["physicalVirtualPaths"] = [model_path]
        self.report["exactLeaves"].extend(
            [model_leaf, _animation_leaf(target), _texture_leaf(target)]
        )
        self.report["scannedW3d"].append(_scan_record(model_path, payload))
        self.report["scannedW3d"].sort(key=lambda item: item["virtualPath"].casefold())
        scanned_paths = [item["virtualPath"] for item in self.report["scannedW3d"]]
        boundary = self.report["w3dDependencyClosure"]["readBoundary"]
        boundary["uniqueVirtualPaths"] = scanned_paths
        boundary["uniqueReadCount"] = len(scanned_paths)
        boundary["byteLength"] = sum(
            item["byteLength"] for item in self.report["scannedW3d"]
        )
        self.report["w3dDependencyClosure"]["summary"]["fileCount"] = len(scanned_paths)
        self.report["catalog"].update(
            {
                "assetPathCount": len(self.files),
                "visualPathCount": len(self.files),
                "w3dPathCount": sum(path.endswith(".w3d") for path in self.files),
            }
        )
        self.report["summary"].update(
            {
                "targetCount": 3,
                "resolvedTargetCount": 3,
                "exactLeafCount": len(self.report["exactLeaves"]),
                "scannedW3dCount": len(scanned_paths),
                "scannedW3dByteLength": boundary["byteLength"],
            }
        )
        self.objects["objects"].append({"index": 4, "typeName": target, "roadType": 0})
        self.objects["count"] = len(self.objects["objects"])
        _seal_report(self.report)
        self.static = build_retail_static_prop_plan(self.report, self.manifest)
        self.hierarchical = build_retail_hierarchical_prop_plan(
            self.report,
            self.static,
            self.manifest,
            self.objects,
        )


class RetailAnimatedPropProfileTests(unittest.TestCase):
    def test_combined_planners_rebuild_deterministically_and_close_fixture(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as raw:
            fixture = Fixture(Path(raw))
            first = build_retail_animated_prop_plan(*fixture.args())
            second = build_retail_animated_prop_plan(*fixture.args())

        self.assertEqual(first, second)
        self.assertEqual(first["schema"], ANIMATED_PROP_PLAN_SCHEMA)
        self.assertEqual(first["candidateTargets"], ["Critter"])
        self.assertEqual(first["summary"]["animatedCandidateTargetTypeCount"], 1)
        self.assertEqual(first["summary"]["eligibleTargetTypeCount"], 1)
        self.assertEqual(first["summary"]["animatedCandidatePlacementCount"], 2)
        self.assertEqual(first["summary"]["animatedBatchPlacementCount"], 2)
        self.assertEqual(first["summary"]["profileResourceCount"], 2)
        self.assertEqual(
            first["logicalExclusions"],
            [
                {
                    "targetObject": "LogicalEmitter",
                    "classification": "ambient-audio-emitter",
                    "placementCount": 1,
                }
            ],
        )
        model = next(
            item
            for item in first["profileFragment"]["resources"]
            if item["kind"] == "model"
        )
        self.assertEqual(model["converter"], "w3d-bundle")
        self.assertEqual(
            model["patterns"],
            [
                "art/w3d/cu/critter_idle.w3d",
                "art/w3d/cu/critter_skl.w3d",
                "art/w3d/cu/critter_skn.w3d",
            ],
        )
        self.assertEqual(model["options"]["model"], "critter_skn.w3d")
        self.assertEqual(model["options"]["animations"], ["critter_idle.w3d"])
        self.assertTrue(first["policy"]["profileFragmentValidatedByImportProfile"])
        calculated = copy.deepcopy(first)
        declared = calculated.pop("aggregateSha256")
        self.assertEqual(declared, canonical_sha256(calculated))

    def test_generated_standalone_profile_satisfies_import_profile(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            plan = build_retail_animated_prop_plan(*Fixture(root).args())
            profile = generated_import_profile(plan)
            path = root / "generated.json"
            write_generated_import_profile(path, profile)
            parsed = ImportProfile.load(path)
        self.assertEqual(len(parsed.resources), 2)
        self.assertEqual(
            next(item for item in parsed.resources if item.kind == "model").converter,
            "w3d-bundle",
        )

    def test_reused_hierarchy_and_actions_have_one_shared_source_owner(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            fixture = Fixture(Path(raw))
            fixture.add_shared_animation_clone()
            plan = build_retail_animated_prop_plan(*fixture.args())

        self.assertEqual(plan["summary"]["eligibleTargetTypeCount"], 2)
        self.assertEqual(plan["summary"]["sharedW3dInputResourceCount"], 1)
        self.assertEqual(plan["summary"]["sharedW3dInputSourceCount"], 2)
        self.assertEqual(plan["summary"]["profileResourceCount"], 4)
        shared = plan["sharedW3dInputs"][0]
        shared_resource = next(
            item
            for item in plan["profileFragment"]["resources"]
            if item["id"] == shared["resourceId"]
        )
        self.assertEqual(shared_resource["kind"], "data")
        self.assertEqual(shared_resource["converter"], "hash-only")
        self.assertEqual(
            shared_resource["patterns"],
            [
                "art/w3d/cu/critter_idle.w3d",
                "art/w3d/cu/critter_skl.w3d",
            ],
        )
        model_resources = [
            item
            for item in plan["profileFragment"]["resources"]
            if item["kind"] == "model"
        ]
        self.assertEqual(
            {tuple(item["patterns"]) for item in model_resources},
            {
                ("art/w3d/cu/critter_skn.w3d",),
                ("art/w3d/cu/critter2_skn.w3d",),
            },
        )
        for item in model_resources:
            self.assertEqual(
                item["options"]["inputResourceIds"][0], shared_resource["id"]
            )
            self.assertEqual(item["options"]["animations"], ["critter_idle.w3d"])
        patterns = [
            pattern
            for item in plan["profileFragment"]["resources"]
            for pattern in item["patterns"]
        ]
        self.assertEqual(len(patterns), len({path.casefold() for path in patterns}))

    def test_unresolved_authored_animation_remains_uncovered(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            fixture = Fixture(Path(raw))
            report = copy.deepcopy(fixture.report)
            animation = next(
                item for item in report["exactLeaves"] if item["kind"] == "animation"
            )
            report["exactLeaves"].remove(animation)
            animation.pop("physicalVirtualPaths")
            animation.update(
                {"status": "missing", "candidateCount": 0, "candidates": []}
            )
            report["unresolved"]["references"].append(animation)
            report["scannedW3d"] = [
                item
                for item in report["scannedW3d"]
                if item["virtualPath"] != "art/w3d/cu/critter_idle.w3d"
            ]
            boundary = report["w3dDependencyClosure"]["readBoundary"]
            boundary["uniqueVirtualPaths"] = ["art/w3d/cu/critter_skn.w3d"]
            boundary["uniqueReadCount"] = 1
            boundary["byteLength"] = len(fixture.files["art/w3d/cu/critter_skn.w3d"])
            report["w3dDependencyClosure"]["summary"]["fileCount"] = 1
            report["summary"].update(
                {
                    "exactLeafCount": 2,
                    "unresolvedReferenceCount": 1,
                    "scannedW3dCount": 1,
                    "scannedW3dByteLength": boundary["byteLength"],
                    "ready": False,
                }
            )
            _seal_report(report)
            static = build_retail_static_prop_plan(report, fixture.manifest)
            hierarchical = build_retail_hierarchical_prop_plan(
                report, static, fixture.manifest, fixture.objects
            )
            plan = build_retail_animated_prop_plan(
                report,
                static,
                hierarchical,
                fixture.manifest,
                fixture.objects,
                fixture.base_profile,
                fixture.root,
            )

        self.assertEqual(plan["eligibleTargets"], [])
        self.assertEqual(plan["profileFragment"]["resources"], [])
        codes = {item["code"] for item in plan["rejectedTargets"][0]["reasons"]}
        self.assertIn("unresolved-visual-reference", codes)
        self.assertIn("no-authored-animation-w3d", codes)

    def test_exact_source_native_missing_transitions_are_no_clip_with_corpus_proof(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as raw:
            fixture = Fixture(Path(raw))
            fixture.retarget_physical_object("Bear")
            fixture.report["unresolved"]["references"].extend(
                [
                    _missing_transition_leaf(
                        "Bear",
                        "CUBear_SKL.CUBear_IDLE",
                        "TRANS_AlertToIdle",
                    ),
                    _missing_transition_leaf(
                        "Bear",
                        "CUBear_SKL.CUBear_IDLC",
                        "TRANS_IdleToAlert",
                    ),
                ]
            )
            fixture.report["summary"].update(
                {"unresolvedReferenceCount": 2, "ready": False}
            )
            _seal_report(fixture.report)
            fixture.static = build_retail_static_prop_plan(
                fixture.report, fixture.manifest
            )
            fixture.hierarchical = build_retail_hierarchical_prop_plan(
                fixture.report,
                fixture.static,
                fixture.manifest,
                fixture.objects,
            )
            plan = build_retail_animated_prop_plan(*fixture.args())

        self.assertEqual(plan["rejectedTargets"], [])
        self.assertEqual(plan["summary"]["sourceNativeNoClipTransitionCount"], 2)
        proof = plan["sourceEvidence"]["sourceNativeNoClipAbsenceProof"]
        self.assertEqual(proof["w3dFileCount"], 3)
        self.assertEqual(proof["hitCount"], 0)
        target = plan["eligibleTargets"][0]
        self.assertEqual(
            [
                item["headerIdentifier"]
                for item in target["sourceNativeNoClipTransitions"]
            ],
            ["CUBear_IDLC", "CUBear_IDLE"],
        )
        model = next(
            item
            for item in plan["profileFragment"]["resources"]
            if item["kind"] == "model"
        )
        self.assertEqual(len(model["options"]["sourceNativeNoClipTransitions"]), 2)

    def test_near_name_missing_transition_is_not_admitted(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            fixture = Fixture(Path(raw))
            fixture.retarget_physical_object("Bear")
            fixture.report["unresolved"]["references"].extend(
                [
                    _missing_transition_leaf(
                        "Bear",
                        "CUBear_SKL.CUBear_IDLE_NEAR",
                        "TRANS_AlertToIdle",
                    ),
                    _missing_transition_leaf(
                        "Bear",
                        "CUBear_SKL.CUBear_IDLC",
                        "TRANS_IdleToAlert",
                    ),
                ]
            )
            fixture.report["summary"].update(
                {"unresolvedReferenceCount": 2, "ready": False}
            )
            _seal_report(fixture.report)
            fixture.static = build_retail_static_prop_plan(
                fixture.report, fixture.manifest
            )
            fixture.hierarchical = build_retail_hierarchical_prop_plan(
                fixture.report,
                fixture.static,
                fixture.manifest,
                fixture.objects,
            )
            plan = build_retail_animated_prop_plan(*fixture.args())

        self.assertEqual(plan["eligibleTargets"], [])
        self.assertNotIn("sourceNativeNoClipAbsenceProof", plan["sourceEvidence"])
        codes = {item["code"] for item in plan["rejectedTargets"][0]["reasons"]}
        self.assertIn("unresolved-visual-reference", codes)

    def test_single_model_embedded_animation_is_admitted_as_one_bundle_input(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as raw:
            plan = build_retail_animated_prop_plan(
                *Fixture(Path(raw), embedded=True).args()
            )

        self.assertEqual(plan["rejectedTargets"], [])
        self.assertEqual(plan["summary"]["embeddedAnimationTargetTypeCount"], 1)
        target = plan["eligibleTargets"][0]
        self.assertEqual(target["animationSourceMode"], "model-embedded")
        self.assertEqual(
            target["animationVirtualPaths"],
            ["art/w3d/cu/critter_skn.w3d"],
        )
        model = next(
            item
            for item in plan["profileFragment"]["resources"]
            if item["kind"] == "model"
        )
        self.assertEqual(model["patterns"], ["art/w3d/cu/critter_skn.w3d"])
        self.assertEqual(model["options"]["model"], "critter_skn.w3d")
        self.assertEqual(model["options"]["animations"], ["critter_skn.w3d"])

    def test_secondary_skin_streams_require_and_record_semantic_proof(
        self,
    ) -> None:
        neutral_proof = {
            "schema": "openbfme.w3d-secondary-skin-proof",
            "schemaVersion": 0,
            "proofSha256": "e" * 64,
        }
        proof_result = SimpleNamespace(
            proof=SimpleNamespace(neutral=lambda: neutral_proof)
        )
        with tempfile.TemporaryDirectory() as raw:
            fixture = Fixture(Path(raw), secondary_skin=True)
            with patch(
                "openbfme_importer.retail_animated_prop_profile."
                "strip_proven_redundant_secondary_skin_streams",
                return_value=proof_result,
            ) as semantic_proof:
                plan = build_retail_animated_prop_plan(*fixture.args())

        semantic_proof.assert_called_once()
        self.assertEqual(plan["rejectedTargets"], [])
        self.assertEqual(plan["summary"]["secondarySkinProofTargetTypeCount"], 1)
        self.assertEqual(
            plan["eligibleTargets"][0]["secondarySkinProof"], neutral_proof
        )
        self.assertEqual(
            plan["conversionGroups"][0]["secondarySkinProof"], neutral_proof
        )

    def test_private_source_hash_drift_and_upstream_plan_tamper_fail_closed(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as raw:
            fixture = Fixture(Path(raw))
            skeleton = fixture.root / "art/w3d/cu/critter_skl.w3d"
            skeleton.write_bytes(skeleton.read_bytes() + b"tampered")
            with self.assertRaisesRegex(ValueError, "byte length drift"):
                build_retail_animated_prop_plan(*fixture.args())

        with tempfile.TemporaryDirectory() as raw:
            fixture = Fixture(Path(raw))
            fixture.hierarchical["summary"]["eligibleTargetTypeCount"] = 9
            with self.assertRaisesRegex(
                ValueError, "hierarchical-prop plan digest mismatch"
            ):
                build_retail_animated_prop_plan(*fixture.args())

    def test_logical_case_drift_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            fixture = Fixture(Path(raw))
            fixture.base_profile = _base_profile(logical_name="logicalemitter")
            with self.assertRaisesRegex(ValueError, "logical target case"):
                build_retail_animated_prop_plan(*fixture.args())

    def test_file_helpers_load_build_and_write_verified_documents(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            fixture = Fixture(root / "assets")
            values = [
                fixture.report,
                fixture.static,
                fixture.hierarchical,
                fixture.manifest,
                fixture.objects,
                fixture.base_profile,
            ]
            paths = [root / f"input-{index}.json" for index in range(len(values))]
            for path, value in zip(paths, values, strict=True):
                path.write_text(json.dumps(value), encoding="utf-8")
            loaded = load_retail_animated_prop_plan_inputs(*paths)
            plan = build_retail_animated_prop_plan(*loaded, fixture.root)
            output = root / "animated-plan.json"
            write_retail_animated_prop_plan(output, plan)
            written = json.loads(output.read_text(encoding="utf-8"))
        self.assertEqual(written, plan)


if __name__ == "__main__":
    unittest.main()
