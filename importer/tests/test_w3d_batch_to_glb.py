from __future__ import annotations

from contextlib import redirect_stdout
import importlib.util
import io
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import types
import unittest
from unittest import mock


def load_batch_module():
    path = Path(__file__).parents[1] / "blender" / "w3d_batch_to_glb.py"
    name = "openbfme_test_w3d_batch_to_glb"
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError("could not load W3D batch adapter")
    module = importlib.util.module_from_spec(spec)
    previous = sys.modules.get(name)
    sys.modules[name] = module
    try:
        spec.loader.exec_module(module)
    finally:
        if previous is None:
            sys.modules.pop(name, None)
        else:
            sys.modules[name] = previous
    return module


BATCH = load_batch_module()


def static_job(job_id: str = "static-a", output: str = "glb/static-a.glb"):
    return {
        "job_id": job_id,
        "model": f"models/{job_id}.w3d",
        "asset_kind": "static",
        "animations": [],
        "required_equipment": [],
        "excluded_optional_meshes": [],
        "proven_root_rigid_bake": False,
        "output": output,
    }


def animated_job(job_id: str = "animated-a", output: str = "glb/animated-a.glb"):
    return {
        "job_id": job_id,
        "model": f"models/{job_id}.w3d",
        "asset_kind": "animated",
        "animations": [f"animations/{job_id}-idle.w3d"],
        "required_equipment": ["right-hand-weapon"],
        "excluded_optional_meshes": ["unused_mesh"],
        "proven_root_rigid_bake": False,
        "output": output,
    }


def embedded_animated_job(
    job_id: str = "embedded-a", output: str = "glb/embedded-a.glb"
):
    row = animated_job(job_id, output)
    row["animations"] = [row["model"]]
    return row


def manifest_document(jobs):
    return {
        "manifest_schema": BATCH.BATCH_MANIFEST_SCHEMA,
        "manifest_version": BATCH.BATCH_MANIFEST_VERSION,
        "jobs": jobs,
    }


def adapter_report(
    asset_kind: str,
    animations: int = 0,
    required_equipment: list[str] | None = None,
    *,
    embedded_model_animation: bool = False,
):
    return {
        "report_schema": BATCH.ADAPTER_REPORT_SCHEMA,
        "report_version": BATCH.ADAPTER_REPORT_VERSION,
        "asset_kind": asset_kind,
        "meshes": 1,
        "mesh_inventory": [{"name": "authored-private-name"}],
        "required_equipment": sorted(required_equipment or []),
        "equipment": [],
        "animations": animations,
        "embedded_model_animation": embedded_model_animation,
        "animation_curves": animations,
        "animation_keys": animations,
        "bones": int(asset_kind != "static"),
        "skeletons": int(asset_kind != "static"),
        "vertices": 3,
        "triangles": 1,
        "skinned_meshes": int(asset_kind != "static"),
        "materials": 1,
        "images": 1,
        "generated_images": 0,
        "shader_material_compatibility": {},
        "root_rigid_bake": {},
        "filtered_non_render_geometry": {},
        "excluded_optional_meshes": {},
        "remaining_non_render_geometry": 0,
        "remaining_ambiguous_box_geometry": 0,
        "equipment_attachments_canonicalized_restored_and_revalidated": False,
        "fps": 24,
    }


def write_minimal_glb(path: Path) -> None:
    document = b'{"asset":{"version":"2.0"}}'
    document += b" " * ((-len(document)) % 4)
    size = 12 + 8 + len(document)
    payload = (
        struct.pack("<4sII", b"glTF", 2, size)
        + struct.pack("<II", len(document), 0x4E4F534A)
        + document
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(payload)


class W3dBatchManifestTests(unittest.TestCase):
    def test_accepts_exact_bounded_job_contracts(self) -> None:
        jobs = BATCH.parse_manifest_document(
            manifest_document(
                [
                    static_job(),
                    animated_job(),
                    {
                        **static_job("hierarchy-a", "glb/hierarchy-a.glb"),
                        "asset_kind": "hierarchical",
                        "proven_root_rigid_bake": True,
                    },
                ]
            )
        )
        self.assertEqual(len(jobs), 3)
        self.assertEqual(jobs[1].asset_kind, "animated")
        self.assertEqual(
            jobs[1].animations[0].as_posix(), "animations/animated-a-idle.w3d"
        )
        self.assertTrue(jobs[2].proven_root_rigid_bake)

    def test_only_exact_single_animated_model_overlap_is_admitted(self) -> None:
        exact = embedded_animated_job()
        parsed = BATCH.parse_manifest_document(manifest_document([exact]))[0]
        self.assertTrue(parsed.embedded_model_animation)
        self.assertEqual(parsed.animations, (parsed.model,))

        case_alias = embedded_animated_job("case-alias")
        case_alias["animations"] = [case_alias["model"].upper()]
        extra_clip = embedded_animated_job("extra-clip")
        extra_clip["animations"].append("animations/extra-clip-idle.w3d")
        non_animated = static_job("non-animated")
        non_animated["animations"] = [non_animated["model"]]

        for row in (case_alias, extra_clip, non_animated):
            with self.subTest(row=row):
                with self.assertRaisesRegex(BATCH.BatchManifestError, "overlap"):
                    BATCH.parse_manifest_document(manifest_document([row]))

    def test_rejects_unknown_fields_bad_paths_duplicates_and_overlaps(self) -> None:
        invalid_documents = []
        row = static_job()
        row["unexpected"] = True
        invalid_documents.append(manifest_document([row]))

        for key, value in (
            ("model", "../escape.w3d"),
            ("model", "C:/escape.w3d"),
            ("output", "glb\\escape.glb"),
            ("output", "glb/not-a-w3d.w3d"),
            ("job_id", "Authored Name"),
            ("output", ".openbfme-w3d-batch-staging/collision.glb"),
        ):
            row = static_job()
            row[key] = value
            invalid_documents.append(manifest_document([row]))

        row = animated_job()
        row["excluded_optional_meshes"] = ["Not_Canonical"]
        invalid_documents.append(manifest_document([row]))

        invalid_documents.append(
            manifest_document(
                [
                    static_job("first", "glb/SAME.glb"),
                    static_job("second", "glb/same.glb"),
                ]
            )
        )
        invalid_documents.append(manifest_document([]))
        boolean_version = manifest_document([static_job()])
        boolean_version["manifest_version"] = True
        invalid_documents.append(boolean_version)
        non_string_kind = static_job()
        non_string_kind["asset_kind"] = ["static"]
        invalid_documents.append(manifest_document([non_string_kind]))
        invalid_documents.append(
            manifest_document(
                [
                    static_job(f"job-{index}")
                    for index in range(BATCH.MAX_BATCH_JOBS + 1)
                ]
            )
        )

        for document in invalid_documents:
            with self.subTest(document=document):
                with self.assertRaises(BATCH.BatchManifestError):
                    BATCH.parse_manifest_document(document)

    def test_asset_kind_animation_equipment_and_bake_rules_fail_closed(self) -> None:
        animated_without_clips = animated_job()
        animated_without_clips["animations"] = []
        static_with_clip = static_job()
        static_with_clip["animations"] = ["animations/idle.w3d"]
        static_with_equipment = static_job()
        static_with_equipment["required_equipment"] = ["left-hand-shield"]
        animated_with_bake = animated_job()
        animated_with_bake["proven_root_rigid_bake"] = True
        unsupported_equipment = animated_job()
        unsupported_equipment["required_equipment"] = ["generic-weapon"]

        for row in (
            animated_without_clips,
            static_with_clip,
            static_with_equipment,
            animated_with_bake,
            unsupported_equipment,
        ):
            with self.subTest(row=row):
                with self.assertRaises(BATCH.BatchManifestError):
                    BATCH.parse_manifest_document(manifest_document([row]))

    def test_loader_requires_exact_canonical_utf8_json(self) -> None:
        document = manifest_document([static_job()])
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            canonical = root / "canonical.json"
            canonical.write_bytes(BATCH.canonical_json_bytes(document))
            jobs, digest = BATCH.load_canonical_manifest(canonical)
            self.assertEqual(len(jobs), 1)
            self.assertEqual(len(digest), 64)

            pretty = root / "pretty.json"
            pretty.write_text(json.dumps(document, indent=2), encoding="utf-8")
            with self.assertRaisesRegex(BATCH.BatchManifestError, "canonical"):
                BATCH.load_canonical_manifest(pretty)

            oversized = root / "oversized.json"
            oversized.write_bytes(b" " * (BATCH.MAX_MANIFEST_BYTES + 1))
            with self.assertRaisesRegex(BATCH.BatchManifestError, "size"):
                BATCH.load_canonical_manifest(oversized)


class W3dBatchExecutionTests(unittest.TestCase):
    def make_roots(self, root: Path):
        plugin = root / "plugin"
        jobs = root / "jobs"
        outputs = root / "outputs"
        for path in (plugin, jobs, outputs):
            path.mkdir()
        return plugin, jobs, outputs

    def write_manifest(self, root: Path, jobs):
        path = root / "manifest.json"
        path.write_bytes(BATCH.canonical_json_bytes(manifest_document(jobs)))
        return path

    def write_inputs(self, job_root: Path, rows) -> None:
        for row in rows:
            for relative in (row["model"], *row["animations"]):
                path = job_root.joinpath(*Path(relative).parts)
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"private fixture")

    def test_runs_all_jobs_continues_after_failure_and_publishes_atomically(
        self,
    ) -> None:
        rows = [
            static_job("first", "glb/first.glb"),
            static_job("failing", "glb/failing.glb"),
            animated_job("last", "glb/last.glb"),
        ]
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            plugin, jobs, outputs = self.make_roots(root)
            self.write_inputs(jobs, rows)
            manifest = self.write_manifest(root, rows)
            calls = []

            def converter(**kwargs):
                calls.append(kwargs["model"].name)
                write_minimal_glb(kwargs["output"])
                if kwargs["model"].stem == "failing":
                    raise RuntimeError(
                        f"do not disclose {kwargs['model']} or AuthoredRetailName"
                    )
                return adapter_report(
                    kwargs["asset_kind"],
                    len(kwargs["animations"]),
                    kwargs["required_equipment"],
                )

            markers = []
            report = BATCH.run_batch(
                manifest=manifest,
                plugin_root=plugin,
                job_root=jobs,
                output_root=outputs,
                converter=converter,
                emit=markers.append,
            )

            self.assertEqual(len(calls), 3)
            self.assertEqual(report["succeeded"], 2)
            self.assertEqual(report["failed"], 1)
            self.assertFalse(report["complete"])
            self.assertTrue((outputs / "glb" / "first.glb").is_file())
            self.assertFalse((outputs / "glb" / "failing.glb").exists())
            self.assertTrue((outputs / "glb" / "last.glb").is_file())
            self.assertEqual(
                [item["status"] for item in report["results"]],
                ["succeeded", "failed", "succeeded"],
            )
            self.assertEqual(len(markers), 4)
            self.assertTrue(markers[-1].startswith("OPENBFME_W3D_BATCH_DONE "))
            for item in report["results"]:
                self.assertNotIn("embedded_model_animation", item)
            for marker in markers[:-1]:
                marker_row = json.loads(marker.split(" ", 1)[1])
                self.assertNotIn("embedded_model_animation", marker_row)
            rendered = "\n".join(markers)
            self.assertNotIn(str(root), rendered)
            self.assertNotIn("AuthoredRetailName", rendered)
            self.assertNotIn("authored-private-name", rendered)
            self.assertNotIn("first.w3d", rendered)
            self.assertNotIn("failing.w3d", rendered)
            self.assertNotIn("last.w3d", rendered)

    def test_failure_kind_classifier_uses_only_fixed_categories(self) -> None:
        class ApplicationFailure(Exception):
            pass

        secret = "AuthoredRetailName C:/private/model.w3d"
        cases = (
            (AssertionError(secret), "assertion"),
            (MemoryError(secret), "memory"),
            (TimeoutError(secret), "timeout"),
            (OSError(secret), "os"),
            (KeyError(secret), "key"),
            (TypeError(secret), "type"),
            (ValueError(secret), "value"),
            (RuntimeError(secret), "runtime"),
            (ApplicationFailure(secret), "application"),
            (BaseException(secret), "control-flow"),
        )
        self.assertEqual(
            {expected for _error, expected in cases},
            {
                "assertion",
                "memory",
                "timeout",
                "os",
                "key",
                "type",
                "value",
                "runtime",
                "application",
                "control-flow",
            },
        )
        for error, expected in cases:
            with self.subTest(expected=expected):
                self.assertEqual(BATCH._safe_failure_kind(error), expected)

    def test_converter_initialization_base_exception_reports_every_trusted_job(
        self,
    ) -> None:
        rows = [
            static_job("startup-first", "glb/startup-first.glb"),
            static_job("startup-last", "glb/startup-last.glb"),
        ]
        secret = "AuthoredRetailName C:/private/startup.w3d"
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            plugin, jobs, outputs = self.make_roots(root)
            manifest = self.write_manifest(root, rows)
            markers: list[str] = []

            with mock.patch.object(
                BATCH,
                "_default_converter",
                side_effect=KeyboardInterrupt(secret),
            ):
                report = BATCH.run_batch(
                    manifest=manifest,
                    plugin_root=plugin,
                    job_root=jobs,
                    output_root=outputs,
                    emit=markers.append,
                )

            self.assertEqual(
                report,
                {
                    "report_schema": BATCH.BATCH_REPORT_SCHEMA,
                    "report_version": BATCH.BATCH_REPORT_VERSION,
                    "manifest_sha256": report["manifest_sha256"],
                    "jobs": 2,
                    "succeeded": 0,
                    "failed": 2,
                    "complete": False,
                    "results": [
                        {
                            "job_id": row["job_id"],
                            "status": "failed",
                            "failure_code": "conversion-error",
                            "failure_phase": "converter-initialization",
                            "failure_kind": "control-flow",
                        }
                        for row in rows
                    ],
                },
            )
            self.assertEqual(len(markers), 3)
            self.assertEqual(
                [
                    json.loads(marker.split(" ", 1)[1])["job_id"]
                    for marker in markers[:-1]
                ],
                [row["job_id"] for row in rows],
            )
            done = json.loads(markers[-1].split(" ", 1)[1])
            self.assertEqual(
                {key: done[key] for key in ("jobs", "succeeded", "failed", "complete")},
                {"jobs": 2, "succeeded": 0, "failed": 2, "complete": False},
            )
            rendered = "\n".join(markers)
            for forbidden in (secret, str(root), *(row["model"] for row in rows)):
                self.assertNotIn(forbidden, rendered)

    def test_converter_control_flow_failure_is_bounded_per_job(self) -> None:
        rows = [
            static_job("control-flow", "glb/control-flow.glb"),
            static_job("survivor", "glb/survivor.glb"),
        ]
        secret = "AuthoredRetailName C:/private/control-flow.w3d"
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            plugin, jobs, outputs = self.make_roots(root)
            self.write_inputs(jobs, rows)
            manifest = self.write_manifest(root, rows)
            markers: list[str] = []

            def converter(**kwargs):
                if kwargs["model"].stem == "control-flow":
                    raise KeyboardInterrupt(secret)
                write_minimal_glb(kwargs["output"])
                return adapter_report("static")

            report = BATCH.run_batch(
                manifest=manifest,
                plugin_root=plugin,
                job_root=jobs,
                output_root=outputs,
                converter=converter,
                emit=markers.append,
            )

            self.assertEqual(report["succeeded"], 1)
            self.assertEqual(report["failed"], 1)
            self.assertEqual(
                report["results"][0],
                {
                    "job_id": "control-flow",
                    "status": "failed",
                    "failure_code": "conversion-error",
                    "failure_phase": "converter-execution",
                    "failure_kind": "control-flow",
                },
            )
            self.assertEqual(report["results"][1]["status"], "succeeded")
            self.assertTrue((outputs / "glb" / "survivor.glb").is_file())
            self.assertNotIn(secret, "\n".join(markers))

    def test_only_exact_bound_phase_error_can_refine_converter_failure(self) -> None:
        class W3DConversionPhaseError(RuntimeError):
            __slots__ = ("_failure_kind", "_failure_phase")

            def __init__(self, phase: str, kind: str, secret: str) -> None:
                super().__init__(secret)
                self._failure_phase = phase
                self._failure_kind = kind

            @property
            def failure_phase(self) -> str:
                return self._failure_phase

            @property
            def failure_kind(self) -> str:
                return self._failure_kind

        class ForgedSubclass(W3DConversionPhaseError):
            pass

        class UntrustedPhaseError(RuntimeError):
            failure_phase = "animation-import"
            failure_kind = "value"

        rows = [
            static_job("trusted", "glb/trusted.glb"),
            static_job("subclass", "glb/subclass.glb"),
            static_job("untrusted", "glb/untrusted.glb"),
            static_job("invalid", "glb/invalid.glb"),
            static_job("success", "glb/success.glb"),
        ]
        secret = "AuthoredRetailName C:/private/phase.w3d"
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            plugin, jobs, outputs = self.make_roots(root)
            self.write_inputs(jobs, rows)
            manifest = self.write_manifest(root, rows)
            markers: list[str] = []

            def converter(**kwargs):
                stem = kwargs["model"].stem
                if stem == "trusted":
                    raise W3DConversionPhaseError("model-import", "key", secret)
                if stem == "subclass":
                    raise ForgedSubclass("animation-import", "value", secret)
                if stem == "untrusted":
                    raise UntrustedPhaseError(secret)
                if stem == "invalid":
                    raise W3DConversionPhaseError(secret, secret, secret)
                write_minimal_glb(kwargs["output"])
                return adapter_report("static")

            with mock.patch.object(
                BATCH,
                "_default_converter",
                return_value=(converter, W3DConversionPhaseError),
            ):
                report = BATCH.run_batch(
                    manifest=manifest,
                    plugin_root=plugin,
                    job_root=jobs,
                    output_root=outputs,
                    emit=markers.append,
                )

            evidence = {
                row["job_id"]: (row["failure_phase"], row["failure_kind"])
                for row in report["results"]
                if row["status"] == "failed"
            }
            self.assertEqual(
                evidence,
                {
                    "trusted": ("model-import", "key"),
                    "subclass": ("converter-execution", "runtime"),
                    "untrusted": ("converter-execution", "runtime"),
                    "invalid": ("converter-execution", "runtime"),
                },
            )
            success = report["results"][-1]
            expected_success_fields = {
                "job_id",
                "status",
                "output_sha256",
                "report_schema",
                "report_version",
                "asset_kind",
                "adapter_report_sha256",
                *BATCH.COUNT_REPORT_FIELDS,
            }
            self.assertEqual(set(success), expected_success_fields)
            self.assertNotIn("embedded_model_animation", success)
            self.assertEqual(
                markers[-2],
                "OPENBFME_W3D_BATCH_JOB "
                + BATCH.canonical_json_bytes(success).decode("utf-8").rstrip("\n"),
            )
            rendered = "\n".join(markers)
            self.assertNotIn(secret, rendered)
            self.assertNotIn(str(root), rendered)

        class HostileEvidenceError(RuntimeError):
            @property
            def failure_phase(self) -> str:
                raise KeyboardInterrupt(secret)

            @property
            def failure_kind(self) -> str:
                raise KeyboardInterrupt(secret)

        self.assertEqual(
            BATCH._failure_evidence(
                HostileEvidenceError(secret),
                coarse_phase="converter-execution",
                phase_error_type=HostileEvidenceError,
            ),
            ("converter-execution", "runtime"),
        )

    def test_all_refined_converter_phases_require_exact_bound_error(self) -> None:
        phases = (
            "scene-reset",
            "embedded-model-import",
            "model-import-validation",
            "request-validation",
            "rig-resolution",
            "rig-validation",
            "action-validation",
            "geometry-validation",
            "material-validation",
            "presentation-validation",
            "skin-validation",
            "attachment-validation",
            "attachment-canonicalization",
            "mesh-object-type-validation",
            "mesh-helper-filter-validation",
            "mesh-box-ambiguity-validation",
            "mesh-equipment-classification",
            "required-equipment-validation",
            "model-animation-setup",
            "model-animation-channel-processing",
            "model-animation-bone-resolution",
            "model-animation-channel-decode",
            "model-animation-keyframe-write",
            "model-animation-action-finalization",
            "model-animation-frame-reset",
            "model-load-complete",
            "model-direct-load-dispatch",
            "model-direct-load-result",
            "model-operator-dispatch",
            "model-operator-result",
            "animation-output-capture-setup",
            "animation-output-capture-restore",
            "animation-output-capture-accounting",
            "additive-material-discovery",
            "additive-material-graph-validation",
            "additive-material-pixel-read",
            "additive-material-alpha-derivation",
            "additive-material-image-duplication",
            "additive-material-pixel-write",
            "additive-material-round-trip",
            "additive-material-alpha-link",
            "generated-image-validation",
            "shader-material-validation",
            "render-proof",
            "post-animation-validation",
            "attachment-restoration",
            "render-revalidation",
            "animation-export-preparation",
        )

        class W3DConversionPhaseError(RuntimeError):
            __slots__ = ("_failure_kind", "_failure_phase")

            def __init__(self, phase: str, kind: str) -> None:
                super().__init__("private authored failure")
                self._failure_phase = phase
                self._failure_kind = kind

            @property
            def failure_phase(self) -> str:
                return self._failure_phase

            @property
            def failure_kind(self) -> str:
                return self._failure_kind

        class ForgedSubclass(W3DConversionPhaseError):
            pass

        for phase in phases:
            with self.subTest(phase=phase, evidence="exact"):
                self.assertEqual(
                    BATCH._failure_evidence(
                        W3DConversionPhaseError(phase, "value"),
                        coarse_phase="converter-execution",
                        phase_error_type=W3DConversionPhaseError,
                    ),
                    (phase, "value"),
                )
            with self.subTest(phase=phase, evidence="forged-subclass"):
                self.assertEqual(
                    BATCH._failure_evidence(
                        ForgedSubclass(phase, "value"),
                        coarse_phase="converter-execution",
                        phase_error_type=W3DConversionPhaseError,
                    ),
                    ("converter-execution", "runtime"),
                )

        self.assertEqual(
            BATCH._failure_evidence(
                W3DConversionPhaseError("unknown-refined-checkpoint", "value"),
                coarse_phase="converter-execution",
                phase_error_type=W3DConversionPhaseError,
            ),
            ("converter-execution", "runtime"),
        )

    def test_failure_markers_seal_phase_kind_and_aggregate_counts(self) -> None:
        rows = [
            static_job("input-phase", "glb/input-phase.glb"),
            static_job("converter-phase", "glb/converter-phase.glb"),
            static_job("glb-phase", "glb/glb-phase.glb"),
            static_job("report-phase", "glb/report-phase.glb"),
            static_job("publication-phase", "glb/publication-phase.glb"),
        ]
        expected = {
            "input-phase": ("input-validation", "value"),
            "converter-phase": ("converter-execution", "assertion"),
            "glb-phase": ("glb-validation", "runtime"),
            "report-phase": ("report-validation", "runtime"),
            "publication-phase": ("publication", "os"),
        }
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            plugin, jobs, outputs = self.make_roots(root)
            self.write_inputs(jobs, rows[1:])
            manifest = self.write_manifest(root, rows)

            def converter(**kwargs):
                stem = kwargs["model"].stem
                if stem == "converter-phase":
                    raise AssertionError(
                        f"AuthoredRetailName must not leak: {kwargs['model']}"
                    )
                if stem == "glb-phase":
                    kwargs["output"].write_bytes(b"private malformed glb")
                    return adapter_report("static")
                write_minimal_glb(kwargs["output"])
                report = adapter_report("static")
                if stem == "report-phase":
                    report["report_version"] = 99
                return report

            markers = []
            with mock.patch.object(
                BATCH.os,
                "replace",
                side_effect=OSError(
                    f"AuthoredRetailName publication failure below {root}"
                ),
            ):
                report = BATCH.run_batch(
                    manifest=manifest,
                    plugin_root=plugin,
                    job_root=jobs,
                    output_root=outputs,
                    converter=converter,
                    emit=markers.append,
                )

            failure_fields = {
                "job_id",
                "status",
                "failure_code",
                "failure_phase",
                "failure_kind",
            }
            self.assertEqual(report["jobs"], 5)
            self.assertEqual(report["succeeded"], 0)
            self.assertEqual(report["failed"], 5)
            self.assertFalse(report["complete"])
            self.assertEqual(len(markers), 6)
            for result, marker in zip(report["results"], markers[:-1], strict=True):
                self.assertEqual(set(result), failure_fields)
                self.assertEqual(result["status"], "failed")
                self.assertEqual(result["failure_code"], "conversion-error")
                self.assertEqual(
                    (result["failure_phase"], result["failure_kind"]),
                    expected[result["job_id"]],
                )
                self.assertEqual(json.loads(marker.split(" ", 1)[1]), result)
            done = json.loads(markers[-1].split(" ", 1)[1])
            self.assertEqual(
                {key: done[key] for key in ("jobs", "succeeded", "failed", "complete")},
                {"jobs": 5, "succeeded": 0, "failed": 5, "complete": False},
            )
            rendered = "\n".join(markers)
            self.assertNotIn(str(root), rendered)
            self.assertNotIn("AuthoredRetailName", rendered)
            self.assertNotIn("private malformed glb", rendered)
            for row in rows:
                self.assertNotIn(row["model"], rendered)

    def test_embedded_animation_report_flag_must_exactly_match_request(self) -> None:
        rows = [
            embedded_animated_job("embedded-good", "glb/embedded-good.glb"),
            embedded_animated_job("embedded-false", "glb/embedded-false.glb"),
            animated_job("external-true", "glb/external-true.glb"),
            embedded_animated_job("embedded-integer", "glb/embedded-integer.glb"),
        ]
        reported_flags = {
            "embedded-good": True,
            "embedded-false": False,
            "external-true": True,
            "embedded-integer": 1,
        }
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            plugin, jobs, outputs = self.make_roots(root)
            self.write_inputs(jobs, rows)
            manifest = self.write_manifest(root, rows)

            def converter(**kwargs):
                write_minimal_glb(kwargs["output"])
                report = adapter_report(
                    kwargs["asset_kind"],
                    len(kwargs["animations"]),
                    kwargs["required_equipment"],
                )
                report["embedded_model_animation"] = reported_flags[
                    kwargs["model"].stem
                ]
                if kwargs["model"].stem.startswith("embedded"):
                    self.assertEqual(kwargs["animations"], [kwargs["model"]])
                return report

            report = BATCH.run_batch(
                manifest=manifest,
                plugin_root=plugin,
                job_root=jobs,
                output_root=outputs,
                converter=converter,
                emit=lambda _line: None,
            )

            self.assertEqual(report["succeeded"], 1)
            self.assertEqual(report["failed"], 3)
            self.assertTrue((outputs / "glb" / "embedded-good.glb").is_file())
            for name in ("embedded-false", "external-true", "embedded-integer"):
                with self.subTest(name=name):
                    self.assertFalse((outputs / "glb" / f"{name}.glb").exists())

    def test_invalid_glb_or_report_is_a_failure_and_does_not_publish(self) -> None:
        rows = [static_job("bad-glb"), static_job("bad-report", "glb/bad-report.glb")]
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            plugin, jobs, outputs = self.make_roots(root)
            self.write_inputs(jobs, rows)
            manifest = self.write_manifest(root, rows)

            def converter(**kwargs):
                if kwargs["model"].stem == "bad-glb":
                    kwargs["output"].write_bytes(b"not glb")
                    return adapter_report("static")
                write_minimal_glb(kwargs["output"])
                report = adapter_report("static")
                report["report_version"] = 99
                return report

            report = BATCH.run_batch(
                manifest=manifest,
                plugin_root=plugin,
                job_root=jobs,
                output_root=outputs,
                converter=converter,
                emit=lambda _line: None,
            )
            self.assertEqual(report["failed"], 2)
            self.assertFalse((outputs / "glb" / "static-a.glb").exists())
            self.assertFalse((outputs / "glb" / "bad-report.glb").exists())

    def test_missing_input_fails_one_job_without_invoking_converter(self) -> None:
        rows = [static_job("missing"), static_job("present", "glb/present.glb")]
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            plugin, jobs, outputs = self.make_roots(root)
            self.write_inputs(jobs, [rows[1]])
            manifest = self.write_manifest(root, rows)
            calls = []

            def converter(**kwargs):
                calls.append(kwargs["model"].name)
                write_minimal_glb(kwargs["output"])
                return adapter_report("static")

            report = BATCH.run_batch(
                manifest=manifest,
                plugin_root=plugin,
                job_root=jobs,
                output_root=outputs,
                converter=converter,
                emit=lambda _line: None,
            )
            self.assertEqual(calls, ["present.w3d"])
            self.assertEqual(report["failed"], 1)
            self.assertEqual(report["succeeded"], 1)

    def test_roots_must_be_separate_and_link_free(self) -> None:
        rows = [static_job()]
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            plugin, jobs, outputs = self.make_roots(root)
            self.write_inputs(jobs, rows)
            manifest = self.write_manifest(root, rows)

            with self.assertRaisesRegex(BATCH.BatchManifestError, "overlap"):
                BATCH.run_batch(
                    manifest=manifest,
                    plugin_root=plugin,
                    job_root=jobs,
                    output_root=jobs,
                    converter=lambda **_kwargs: adapter_report("static"),
                    emit=lambda _line: None,
                )

            linked = root / "linked-jobs"
            try:
                os.symlink(jobs, linked, target_is_directory=True)
            except (NotImplementedError, OSError):
                self.skipTest("directory symlink creation is unavailable")
            with self.assertRaisesRegex(BATCH.BatchManifestError, "link"):
                BATCH.run_batch(
                    manifest=manifest,
                    plugin_root=plugin,
                    job_root=linked,
                    output_root=outputs,
                    converter=lambda **_kwargs: adapter_report("static"),
                    emit=lambda _line: None,
                )


class W3dAdapterInitializationTests(unittest.TestCase):
    def test_default_converter_load_suppresses_bytecode_and_restores_flag(self) -> None:
        observed: list[bool] = []

        class Loader:
            def exec_module(self, _module) -> None:
                observed.append(sys.dont_write_bytecode)

        previous = sys.dont_write_bytecode
        module = types.SimpleNamespace()
        loader = Loader()
        sys.dont_write_bytecode = False
        try:
            BATCH._exec_module_without_bytecode(loader, module)
            self.assertEqual(observed, [True])
            self.assertFalse(sys.dont_write_bytecode)
        finally:
            sys.dont_write_bytecode = previous

    def test_converter_bindings_require_exact_module_owned_phase_error_class(
        self,
    ) -> None:
        module = types.ModuleType("openbfme_test_converter_bindings")
        exec(
            "class W3DConversionPhaseError(RuntimeError):\n    pass\n",
            module.__dict__,
        )
        module.initialize_w3d_converter = lambda _root: None
        module.convert_w3d_job = lambda **_kwargs: {}

        initialize, convert, error_type = BATCH._validated_converter_bindings(module)
        self.assertIs(initialize, module.initialize_w3d_converter)
        self.assertIs(convert, module.convert_w3d_job)
        self.assertIs(error_type, module.W3DConversionPhaseError)

        foreign = types.ModuleType("openbfme_test_foreign_bindings")
        exec(
            "class W3DConversionPhaseError(RuntimeError):\n    pass\n",
            foreign.__dict__,
        )
        module.W3DConversionPhaseError = foreign.W3DConversionPhaseError
        with self.assertRaisesRegex(BATCH.BatchManifestError, "failure contract"):
            BATCH._validated_converter_bindings(module)

    def test_batch_and_single_job_report_contracts_are_identical(self) -> None:
        previous_bpy = sys.modules.get("bpy")
        sys.modules["bpy"] = types.SimpleNamespace()
        try:
            path = Path(__file__).parents[1] / "blender" / "w3d_to_glb.py"
            spec = importlib.util.spec_from_file_location(
                "openbfme_test_w3d_report_contract",
                path,
            )
            if spec is None or spec.loader is None:
                raise RuntimeError("could not load W3D converter contract")
            converter = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(converter)
        finally:
            if previous_bpy is None:
                sys.modules.pop("bpy", None)
            else:
                sys.modules["bpy"] = previous_bpy

        self.assertEqual(
            (BATCH.ADAPTER_REPORT_SCHEMA, BATCH.ADAPTER_REPORT_VERSION),
            (converter.ADAPTER_REPORT_SCHEMA, converter.ADAPTER_REPORT_VERSION),
        )

    def test_single_job_main_keeps_established_marker_and_report(self) -> None:
        fake_bpy = types.SimpleNamespace()
        previous_bpy = sys.modules.get("bpy")
        sys.modules["bpy"] = fake_bpy
        try:
            path = Path(__file__).parents[1] / "blender" / "w3d_to_glb.py"
            spec = importlib.util.spec_from_file_location(
                "openbfme_test_w3d_single_main", path
            )
            if spec is None or spec.loader is None:
                raise RuntimeError("could not load W3D adapter")
            adapter = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(adapter)

            with tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                model = root / "model.w3d"
                model.write_bytes(b"fixture")
                expected = adapter_report("static")
                calls = {"initialize": 0, "convert": 0}
                adapter.parse_args = lambda: types.SimpleNamespace(
                    plugin_root=root / "plugin",
                    model=model,
                    asset_kind="static",
                    animations=[],
                    required_equipment=[],
                    excluded_optional_meshes=[],
                    proven_root_rigid_bake=False,
                    proven_pivot_only_model=False,
                    retail_absent_textures=[],
                    output=root / "output.glb",
                )
                adapter.initialize_w3d_converter = lambda _root: calls.__setitem__(
                    "initialize", calls["initialize"] + 1
                )

                def convert(**_kwargs):
                    calls["convert"] += 1
                    return expected

                adapter.convert_w3d_job = convert
                output = io.StringIO()
                with redirect_stdout(output):
                    adapter.main()
                marker = output.getvalue().strip()
                self.assertTrue(marker.startswith("OPENBFME_W3D_OK "))
                self.assertEqual(json.loads(marker.split(" ", 1)[1]), expected)
                self.assertEqual(calls, {"initialize": 1, "convert": 1})
        finally:
            if previous_bpy is None:
                sys.modules.pop("bpy", None)
            else:
                sys.modules["bpy"] = previous_bpy

    def test_plugin_factory_reset_and_shim_are_installed_once(self) -> None:
        fake_bpy = types.SimpleNamespace(
            ops=types.SimpleNamespace(
                wm=types.SimpleNamespace(read_factory_settings=lambda **_kwargs: None)
            )
        )
        previous_bpy = sys.modules.get("bpy")
        previous_plugin = sys.modules.get("io_mesh_w3d")
        sys.modules["bpy"] = fake_bpy
        try:
            path = Path(__file__).parents[1] / "blender" / "w3d_to_glb.py"
            spec = importlib.util.spec_from_file_location(
                "openbfme_test_w3d_batch_initialization", path
            )
            if spec is None or spec.loader is None:
                raise RuntimeError("could not load W3D adapter")
            adapter = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(adapter)

            calls = {"factory": 0, "register": 0, "shim": 0}
            fake_bpy.ops.wm.read_factory_settings = lambda **_kwargs: calls.__setitem__(
                "factory", calls["factory"] + 1
            )
            plugin = types.SimpleNamespace(
                register=lambda: calls.__setitem__("register", calls["register"] + 1)
            )
            sys.modules["io_mesh_w3d"] = plugin
            adapter.install_shader_material_compatibility_shim = lambda: (
                calls.__setitem__("shim", calls["shim"] + 1)
            )

            with tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                plugin_root = root / "plugin"
                (plugin_root / "io_mesh_w3d").mkdir(parents=True)
                (plugin_root / "io_mesh_w3d" / "__init__.py").write_text(
                    "# fixture\n", encoding="utf-8"
                )
                other_root = root / "other"
                (other_root / "io_mesh_w3d").mkdir(parents=True)
                (other_root / "io_mesh_w3d" / "__init__.py").write_text(
                    "# fixture\n", encoding="utf-8"
                )

                adapter.initialize_w3d_converter(plugin_root)
                adapter.initialize_w3d_converter(plugin_root)
                self.assertEqual(calls, {"factory": 1, "register": 1, "shim": 1})
                with self.assertRaisesRegex(RuntimeError, "switch plugin roots"):
                    adapter.initialize_w3d_converter(other_root)
        finally:
            if previous_bpy is None:
                sys.modules.pop("bpy", None)
            else:
                sys.modules["bpy"] = previous_bpy
            if previous_plugin is None:
                sys.modules.pop("io_mesh_w3d", None)
            else:
                sys.modules["io_mesh_w3d"] = previous_plugin


if __name__ == "__main__":
    unittest.main()
