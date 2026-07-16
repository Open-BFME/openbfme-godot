from __future__ import annotations

import copy
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

from openbfme_importer.paths import repo_root_from_module
from openbfme_importer.profile import ImportProfile
from openbfme_importer.retail_animated_prop_profile import (
    ANIMATED_PROP_PLAN_SCHEMA,
    ANIMATED_PROP_PLAN_SCHEMA_VERSION,
)
from openbfme_importer.retail_hierarchical_profile import (
    HIERARCHICAL_PROP_PLAN_SCHEMA,
    HIERARCHICAL_PROP_PLAN_SCHEMA_VERSION,
)
from openbfme_importer.retail_no_motion_prop_profile import (
    MODEL_VIRTUAL_PATH,
    NO_MOTION_PROP_PLAN_SCHEMA,
    TARGET_OBJECT,
    TEXTURE_VIRTUAL_PATH,
    _validate_upstream_plans,
    build_retail_no_motion_prop_plan,
    generated_import_profile,
)
from openbfme_importer.retail_visual_profile import (
    STATIC_PROP_PLAN_SCHEMA,
    STATIC_PROP_PLAN_SCHEMA_VERSION,
    _canonical_sha256,
)


CONTAINER = 0x80000000


def _fixed(value: str, size: int = 16) -> bytes:
    encoded = value.encode("ascii")
    return encoded + b"\0" * (size - len(encoded))


def _chunk(kind: int, payload: bytes, *, container: bool = False) -> bytes:
    return (
        struct.pack("<II", kind, len(payload) | (CONTAINER if container else 0))
        + payload
    )


def _model(*, frame_count: int = 101) -> bytes:
    hierarchy_header = struct.pack(
        "<I16sI3f", 0x00040001, _fixed("PMMEATRACK01"), 1, 0.0, 0.0, 0.0
    )
    pivot = struct.pack("<16si10f", _fixed("ROOTTRANSFORM"), -1, *([0.0] * 9), 1.0)
    hierarchy = _chunk(
        0x00000100,
        _chunk(0x00000101, hierarchy_header) + _chunk(0x00000102, pivot),
        container=True,
    )
    animation_header = struct.pack(
        "<I16s16sII",
        0x00040001,
        _fixed("PMMEATRACK01"),
        _fixed("PMMEATRACK01"),
        frame_count,
        30,
    )
    animation = _chunk(
        0x00000200,
        _chunk(0x00000201, animation_header),
        container=True,
    )
    mesh_header = struct.pack(
        "<II16s16s9I10f",
        0x00040002,
        0,
        _fixed("PMMEATRACK01"),
        _fixed("PMMEATRACK01"),
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        3,
        1,
        *([0.0] * 10),
    )
    mesh = _chunk(0x00000000, _chunk(0x0000001F, mesh_header), container=True)
    hlod_header = struct.pack(
        "<II16s16s",
        0x00010000,
        1,
        _fixed("PMMEATRACK01"),
        _fixed("PMMEATRACK01"),
    )
    lod = _chunk(0x00000703, struct.pack("<If", 1, 1.0)) + _chunk(
        0x00000704,
        struct.pack("<I32s", 0, _fixed("PMMEATRACK01.PMMEATRACK01", 32)),
    )
    hlod = _chunk(
        0x00000700,
        _chunk(0x00000701, hlod_header) + _chunk(0x00000702, lod, container=True),
        container=True,
    )
    return hierarchy + animation + mesh + hlod


def _seal(value: dict) -> dict:
    result = copy.deepcopy(value)
    result["aggregateSha256"] = _canonical_sha256(result)
    return result


def _source(path: str, payload: bytes) -> dict:
    return {
        "virtualPath": path,
        "byteLength": len(payload),
        "sha256": hashlib.sha256(payload).hexdigest(),
        "source": {"archive": "fixture.big", "offset": 0, "precedence": 1},
    }


def _evidence() -> tuple[dict, dict]:
    manifest = {
        "aggregateSha256": "1" * 64,
        "catalogIdentitySha256": "2" * 64,
        "installIdentitySha256": "3" * 64,
        "fileCount": 2,
        "byteLength": 1,
    }
    map_evidence = {
        "schema": "openbfme.sage-map-objects",
        "schemaVersion": 0,
        "documentAggregateSha256": "4" * 64,
        "objectRecordCount": 1,
        "nonRoadObjectRecordCount": 1,
        "roadControlPointRecordCount": 0,
    }
    return manifest, map_evidence


def _upstream_plans() -> tuple[dict, dict, dict, dict, dict]:
    visual_digest = "5" * 64
    dependency_digest = "6" * 64
    manifest, map_evidence = _evidence()
    static = _seal(
        {
            "schema": STATIC_PROP_PLAN_SCHEMA,
            "schemaVersion": STATIC_PROP_PLAN_SCHEMA_VERSION,
            "sourceEvidence": {
                "visualClosureAggregateSha256": visual_digest,
                "w3dDependencyAggregateSha256": dependency_digest,
                "effectiveAssets": manifest,
            },
            "eligibleTargets": [],
            "ineligibleTargets": [
                {
                    "targetObject": TARGET_OBJECT,
                    "reasons": [
                        {
                            "code": "model-w3d-contains-animation-headers",
                            "headerCount": 2,
                        },
                        {
                            "code": "model-w3d-contains-hierarchy-headers",
                            "headerCount": 1,
                        },
                    ],
                }
            ],
        }
    )
    hierarchical = _seal(
        {
            "schema": HIERARCHICAL_PROP_PLAN_SCHEMA,
            "schemaVersion": HIERARCHICAL_PROP_PLAN_SCHEMA_VERSION,
            "sourceEvidence": {
                "visualClosureAggregateSha256": visual_digest,
                "w3dDependencyAggregateSha256": dependency_digest,
                "effectiveAssets": manifest,
                "mapObjects": map_evidence,
                "staticPropPlanAggregateSha256": static["aggregateSha256"],
            },
            "eligibleTargets": [],
            "rejectedTargets": [
                {
                    "targetObject": TARGET_OBJECT,
                    "placementCount": 1,
                    "reasons": [
                        {
                            "code": "model-w3d-contains-animation-headers",
                            "headerCount": 2,
                        }
                    ],
                    "staticPlanReasons": static["ineligibleTargets"][0]["reasons"],
                }
            ],
        }
    )
    animated = _seal(
        {
            "schema": ANIMATED_PROP_PLAN_SCHEMA,
            "schemaVersion": ANIMATED_PROP_PLAN_SCHEMA_VERSION,
            "sourceEvidence": {
                "visualClosureAggregateSha256": visual_digest,
                "w3dDependencyAggregateSha256": dependency_digest,
                "effectiveAssets": manifest,
                "mapObjects": map_evidence,
                "staticPropPlanAggregateSha256": static["aggregateSha256"],
                "hierarchicalPropPlanAggregateSha256": hierarchical["aggregateSha256"],
            },
            "candidateTargets": [TARGET_OBJECT],
            "eligibleTargets": [],
            "rejectedTargets": [
                {
                    "targetObject": TARGET_OBJECT,
                    "placementCount": 1,
                    "reasons": [{"code": "embedded-animation-has-no-key-channel"}],
                    "hierarchicalPlanReasons": hierarchical["rejectedTargets"][0][
                        "reasons"
                    ],
                }
            ],
            "profileFragment": {
                "resources": [],
                "objectBindings": {"models": []},
            },
        }
    )
    return static, hierarchical, animated, manifest, map_evidence


class RetailNoMotionPropProfileTests(unittest.TestCase):
    def test_upstream_handoff_is_exact_and_rejects_reclassified_target(self) -> None:
        static, hierarchical, animated, manifest, map_evidence = _upstream_plans()
        result = _validate_upstream_plans(
            static,
            hierarchical,
            animated,
            visual_digest="5" * 64,
            dependency_digest="6" * 64,
            manifest_evidence=manifest,
            map_evidence=map_evidence,
        )
        self.assertEqual(
            result["animatedPropPlanAggregateSha256"],
            animated["aggregateSha256"],
        )

        changed = copy.deepcopy(animated)
        changed["rejectedTargets"][0]["reasons"] = [
            {"code": "embedded-animation-has-key-channel"}
        ]
        changed.pop("aggregateSha256")
        changed = _seal(changed)
        with self.assertRaisesRegex(ValueError, "rejection evidence changed"):
            _validate_upstream_plans(
                static,
                hierarchical,
                changed,
                visual_digest="5" * 64,
                dependency_digest="6" * 64,
                manifest_evidence=manifest,
                map_evidence=map_evidence,
            )

    def _build_with_patched_evidence(
        self, root: Path, model: bytes, texture: bytes, *, bad_digest: bool = False
    ) -> dict:
        model_path = root.joinpath(*MODEL_VIRTUAL_PATH.split("/"))
        texture_path = root.joinpath(*TEXTURE_VIRTUAL_PATH.split("/"))
        model_path.parent.mkdir(parents=True, exist_ok=True)
        texture_path.parent.mkdir(parents=True, exist_ok=True)
        model_path.write_bytes(model)
        texture_path.write_bytes(texture)
        model_source = _source(MODEL_VIRTUAL_PATH, model)
        texture_source = _source(TEXTURE_VIRTUAL_PATH, texture)
        if bad_digest:
            model_source["sha256"] = "0" * 64
        manifest, map_evidence = _evidence()
        closure = {
            "targets": [TARGET_OBJECT],
            "reportDigest": "5" * 64,
            "dependencyDigest": "6" * 64,
        }
        with (
            patch(
                "openbfme_importer.retail_no_motion_prop_profile._validate_effective_manifest",
                return_value=({}, manifest),
            ),
            patch(
                "openbfme_importer.retail_no_motion_prop_profile._validate_visual_closure",
                return_value=closure,
            ),
            patch(
                "openbfme_importer.retail_no_motion_prop_profile._validate_visual_target",
                return_value={
                    "modelSource": model_source,
                    "textureSource": texture_source,
                    "embeddedTextureEvidence": {},
                },
            ),
            patch(
                "openbfme_importer.retail_no_motion_prop_profile._validate_placement_document",
                return_value=({TARGET_OBJECT: 1}, map_evidence),
            ),
            patch(
                "openbfme_importer.retail_no_motion_prop_profile._validate_upstream_plans",
                return_value={
                    "staticPropPlanAggregateSha256": "7" * 64,
                    "hierarchicalPropPlanAggregateSha256": "8" * 64,
                    "animatedPropPlanAggregateSha256": "9" * 64,
                },
            ),
            patch(
                "openbfme_importer.retail_no_motion_prop_profile._validate_census",
                return_value={"aggregateSha256": "a" * 64},
            ),
        ):
            return build_retail_no_motion_prop_plan({}, {}, {}, {}, {}, {}, {}, root)

    def test_exact_header_only_model_builds_two_resource_fragment(self) -> None:
        scratch = repo_root_from_module() / ".private" / "scratch"
        scratch.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as raw:
            root = Path(raw)
            plan = self._build_with_patched_evidence(root, _model(), b"texture")
            self.assertEqual(plan["schema"], NO_MOTION_PROP_PLAN_SCHEMA)
            self.assertEqual(plan["summary"]["profileResourceCount"], 2)
            self.assertEqual(plan["summary"]["placementCount"], 1)
            resources = plan["profileFragment"]["resources"]
            model = next(item for item in resources if item["kind"] == "model")
            self.assertEqual(model["converter"], "w3d-hierarchical")
            self.assertEqual(model["options"]["animations"], [])
            self.assertIs(model["options"]["provenRootRigidBake"], True)
            self.assertEqual(
                model["options"]["provenNoMotionAnimations"],
                [
                    {
                        "identifier": "PMMEATRACK01",
                        "hierarchyIdentifier": "PMMEATRACK01",
                        "frameCount": 101,
                        "frameRate": 30,
                        "compressed": False,
                        "modelIdentifier": "PMMEATRACK01",
                    }
                ],
            )
            self.assertEqual(
                plan["profileFragment"]["objectBindings"]["models"][0]["typeName"],
                TARGET_OBJECT,
            )
            profile = generated_import_profile(plan)
            path = root / "profile.json"
            path.write_text(json.dumps(profile), encoding="utf-8")
            self.assertEqual(len(ImportProfile.load(path).resources), 2)

    def test_build_is_deterministic_for_identical_source_evidence(self) -> None:
        scratch = repo_root_from_module() / ".private" / "scratch"
        scratch.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as raw:
            root = Path(raw)
            first = self._build_with_patched_evidence(root, _model(), b"texture")
            second = self._build_with_patched_evidence(root, _model(), b"texture")
            self.assertEqual(first, second)
            basis = dict(first)
            declared = basis.pop("aggregateSha256")
            self.assertEqual(declared, _canonical_sha256(basis))

    def test_source_digest_mismatch_fails_before_transform(self) -> None:
        scratch = repo_root_from_module() / ".private" / "scratch"
        scratch.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as raw:
            with self.assertRaisesRegex(ValueError, "digest mismatch"):
                self._build_with_patched_evidence(
                    Path(raw), _model(), b"texture", bad_digest=True
                )

    def test_wrong_header_timing_cannot_be_admitted(self) -> None:
        scratch = repo_root_from_module() / ".private" / "scratch"
        scratch.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as raw:
            with self.assertRaisesRegex(ValueError, "does not exactly match"):
                self._build_with_patched_evidence(
                    Path(raw), _model(frame_count=100), b"texture"
                )

    def test_generated_profile_rejects_resealed_wrong_resource_count(self) -> None:
        plan = {
            "schema": NO_MOTION_PROP_PLAN_SCHEMA,
            "schemaVersion": 0,
            "profileFragment": {"resources": []},
        }
        plan["aggregateSha256"] = _canonical_sha256(plan)
        with self.assertRaisesRegex(ValueError, "exactly two resources"):
            generated_import_profile(plan)


if __name__ == "__main__":
    unittest.main()
