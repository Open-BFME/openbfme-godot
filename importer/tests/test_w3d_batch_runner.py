from __future__ import annotations

from dataclasses import FrozenInstanceError, replace
import hashlib
import json
import os
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

import openbfme_importer.w3d_batch_runner as batch_runner
from openbfme_importer.w3d_batch_runner import (
    W3D_BATCH_CORPUS_MANIFEST,
    W3DBatchConversionError,
    W3DBatchReuseError,
    W3DBatchRunnerError,
    W3DProcessResult,
    hash_w3d_tool_tree,
    run_w3d_job_plan,
)
from openbfme_importer.w3d_job_planner import (
    SECONDARY_SKIN_PREPARATION,
    W3DJobBatch,
    W3DJobPlan,
    W3DPlannedJob,
    W3DTerminal,
)
from openbfme_importer.w3d_prepared_root import (
    W3DPreparedRootBinding,
    W3DPreparedRootError,
)


def _canonical(value: object) -> bytes:
    return (
        json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
        + "\n"
    ).encode("utf-8")


def _sha_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def _sha_file(path: Path) -> str:
    return _sha_bytes(path.read_bytes())


def _success_marker_row(job: W3DPlannedJob) -> dict[str, object]:
    return {
        "job_id": job.job_id,
        "status": "succeeded",
        "output_sha256": "1" * 64,
        "report_schema": "openbfme.w3d-adapter-report",
        "report_version": 2,
        "asset_kind": job.asset_kind,
        "adapter_report_sha256": "2" * 64,
        "meshes": 1,
        "animations": len(job.animations),
        "bones": 0,
        "skeletons": 0,
        "vertices": 3,
        "triangles": 1,
        "skinned_meshes": 0,
        "materials": 1,
        "images": 1,
    }


def _failure_marker_row(
    job: W3DPlannedJob,
    phase: str,
    kind: str,
) -> dict[str, object]:
    return {
        "job_id": job.job_id,
        "status": "failed",
        "failure_code": "conversion-error",
        "failure_phase": phase,
        "failure_kind": kind,
    }


def _marker_result(
    batch: W3DJobBatch,
    rows: tuple[dict[str, object], ...],
    *,
    returncode: int,
    done_changes: dict[str, object] | None = None,
) -> W3DProcessResult:
    succeeded = sum(row.get("status") == "succeeded" for row in rows)
    failed = sum(row.get("status") == "failed" for row in rows)
    done: dict[str, object] = {
        "report_schema": "openbfme.w3d-batch-report",
        "report_version": 1,
        "manifest_sha256": batch.manifest_sha256,
        "jobs": len(batch.jobs),
        "succeeded": succeeded,
        "failed": failed,
        "complete": failed == 0,
    }
    done.update(done_changes or {})
    lines = [
        "OPENBFME_W3D_BATCH_JOB " + _canonical(row).decode("utf-8").rstrip("\n")
        for row in rows
    ]
    lines.append(
        "OPENBFME_W3D_BATCH_DONE " + _canonical(done).decode("utf-8").rstrip("\n")
    )
    return W3DProcessResult(
        returncode,
        ("\n".join(lines) + "\n").encode("utf-8"),
    )


def _glb(animation_count: int = 0) -> bytes:
    positions = struct.pack(
        "<9f",
        0.0,
        0.0,
        0.0,
        1.0,
        0.0,
        0.0,
        0.0,
        1.0,
        0.0,
    )
    payload = bytearray(positions + struct.pack("<3H", 0, 1, 2) + b"\0\0PNG!")
    views: list[dict[str, int]] = [
        {"buffer": 0, "byteOffset": 0, "byteLength": 36, "target": 34962},
        {"buffer": 0, "byteOffset": 36, "byteLength": 6, "target": 34963},
        {"buffer": 0, "byteOffset": 44, "byteLength": 4},
    ]
    accessors: list[dict[str, object]] = [
        {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"},
        {"bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR"},
    ]
    animations: list[dict[str, object]] = []
    for _ in range(animation_count):
        time_offset = len(payload)
        payload.extend(struct.pack("<2f", 0.0, 1.0))
        translation_offset = len(payload)
        payload.extend(struct.pack("<6f", 0.0, 0.0, 0.0, 1.0, 0.0, 0.0))
        time_view = len(views)
        views.append({"buffer": 0, "byteOffset": time_offset, "byteLength": 8})
        translation_view = len(views)
        views.append({"buffer": 0, "byteOffset": translation_offset, "byteLength": 24})
        time_accessor = len(accessors)
        accessors.append(
            {
                "bufferView": time_view,
                "componentType": 5126,
                "count": 2,
                "type": "SCALAR",
            }
        )
        translation_accessor = len(accessors)
        accessors.append(
            {
                "bufferView": translation_view,
                "componentType": 5126,
                "count": 2,
                "type": "VEC3",
            }
        )
        animations.append(
            {
                "samplers": [{"input": time_accessor, "output": translation_accessor}],
                "channels": [
                    {
                        "sampler": 0,
                        "target": {"node": 1, "path": "translation"},
                    }
                ],
            }
        )
    document: dict[str, object] = {
        "asset": {"version": "2.0"},
        "buffers": [{"byteLength": len(payload)}],
        "bufferViews": views,
        "accessors": accessors,
        "images": [{"bufferView": 2, "mimeType": "image/png"}],
        "textures": [{"source": 0}],
        "materials": [{"pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}}],
        "meshes": [
            {
                "primitives": [
                    {"attributes": {"POSITION": 0}, "indices": 1, "material": 0}
                ]
            }
        ],
        "nodes": [{"mesh": 0}, {}],
        "scenes": [{"nodes": [0, 1]}],
        "scene": 0,
    }
    if animations:
        document["animations"] = animations
    encoded = _canonical(document).rstrip(b"\n")
    encoded += b" " * (-len(encoded) % 4)
    binary = bytes(payload) + b"\0" * (-len(payload) % 4)
    chunks = struct.pack("<II", len(encoded), 0x4E4F534A) + encoded
    chunks += struct.pack("<II", len(binary), 0x004E4942) + binary
    return struct.pack("<4sII", b"glTF", 2, 12 + len(chunks)) + chunks


def _asset_only_glb() -> bytes:
    document = _canonical({"asset": {"version": "2.0"}}).rstrip(b"\n")
    document += b" " * (-len(document) % 4)
    chunk = struct.pack("<II", len(document), 0x4E4F534A) + document
    return struct.pack("<4sII", b"glTF", 2, 12 + len(chunk)) + chunk


def _make_plan(models: list[tuple[str, bytes]], *, batch_size: int = 256) -> W3DJobPlan:
    jobs: list[W3DPlannedJob] = []
    for index, (model, payload) in enumerate(models):
        definition = _sha_bytes(f"definition-{index}".encode("ascii"))
        job_id = f"w3d-{definition[:40]}"
        jobs.append(
            W3DPlannedJob(
                job_id=job_id,
                asset_kind="static",
                model=model,
                animations=(),
                output=f"glb/{job_id}.glb",
                model_source_id=f"src-{index:032x}",
                model_source_sha256=_sha_bytes(payload),
                hierarchy_source_id=None,
                animation_source_ids=(),
                definition_sha256=definition,
            )
        )
    batches: list[W3DJobBatch] = []
    for offset in range(0, len(jobs), batch_size):
        selected = tuple(jobs[offset : offset + batch_size])
        document = {
            "manifest_schema": "openbfme.w3d-batch-jobs",
            "manifest_version": 1,
            "jobs": [job.manifest_row() for job in selected],
        }
        digest = _sha_bytes(_canonical(document))
        batches.append(
            W3DJobBatch(
                batch_id=f"batch-{digest[:32]}",
                jobs=selected,
                manifest_sha256=digest,
            )
        )
    provisional = W3DJobPlan(
        jobs=tuple(jobs),
        batches=tuple(batches),
        terminals=(),
        catalog_input_sha256=_sha_bytes(b"catalog-input"),
        catalog_metadata_sha256=_sha_bytes(b"catalog-metadata"),
        source_count=len(jobs),
        consumed_source_count=len(jobs),
        private_plan_sha256=_sha_bytes(b"private-plan"),
        evidence_sha256="",
    )
    return replace(
        provisional,
        evidence_sha256=_sha_bytes(_canonical(provisional.evidence_hash_basis())),
    )


def _reseal_plan(plan: W3DJobPlan, **changes: object) -> W3DJobPlan:
    provisional = replace(plan, **changes, evidence_sha256="")
    return replace(
        provisional,
        evidence_sha256=_sha_bytes(_canonical(provisional.evidence_hash_basis())),
    )


def _replace_plan_jobs(
    plan: W3DJobPlan,
    jobs: tuple[W3DPlannedJob, ...],
    *,
    private_plan_sha256: str | None = None,
    preserve_manifests: bool = True,
) -> W3DJobPlan:
    batches: list[W3DJobBatch] = []
    cursor = 0
    for batch in plan.batches:
        selected = jobs[cursor : cursor + len(batch.jobs)]
        cursor += len(batch.jobs)
        document = {
            "manifest_schema": "openbfme.w3d-batch-jobs",
            "manifest_version": 1,
            "jobs": [job.manifest_row() for job in selected],
        }
        digest = _sha_bytes(_canonical(document))
        replacement = W3DJobBatch(
            batch_id=f"batch-{digest[:32]}",
            jobs=selected,
            manifest_sha256=digest,
        )
        if (
            preserve_manifests
            and replacement.manifest_bytes() != batch.manifest_bytes()
        ):
            raise AssertionError("fixture changed the adapter manifest")
        batches.append(replacement)
    if cursor != len(jobs):
        raise AssertionError("fixture batch coverage is invalid")
    provisional = replace(
        plan,
        jobs=jobs,
        batches=tuple(batches),
        private_plan_sha256=(
            plan.private_plan_sha256
            if private_plan_sha256 is None
            else private_plan_sha256
        ),
        evidence_sha256="",
    )
    return replace(
        provisional,
        evidence_sha256=_sha_bytes(_canonical(provisional.evidence_hash_basis())),
    )


class _AdapterRunner:
    def __init__(self, mode: str = "success") -> None:
        self.mode = mode
        self.commands: list[tuple[str, ...]] = []
        self.created_hardlink = False

    def __call__(
        self,
        command: tuple[str, ...],
        *,
        timeout_seconds: float,
        max_stdout_bytes: int,
        max_stderr_bytes: int,
    ) -> W3DProcessResult:
        del timeout_seconds, max_stdout_bytes, max_stderr_bytes
        self.commands.append(command)
        if self.mode == "timeout":
            return W3DProcessResult(-9, timed_out=True)
        if self.mode == "process-failure":
            return W3DProcessResult(3, b"", b"conversion failed")

        manifest = Path(command[command.index("--manifest") + 1])
        output_root = Path(command[command.index("--output-root") + 1])
        raw_manifest = manifest.read_bytes()
        document = json.loads(raw_manifest)
        lines: list[str] = []
        for index, job in enumerate(document["jobs"]):
            output = output_root.joinpath(*job["output"].split("/"))
            output.parent.mkdir(parents=True, exist_ok=True)
            payload = (
                _asset_only_glb()
                if self.mode == "empty-semantic"
                else _glb(len(job["animations"]))
            )
            digest = _sha_bytes(payload)
            if self.mode != "partial-output":
                if self.mode == "symlink-output":
                    target = output_root / f"outside-{index}.glb"
                    target.write_bytes(payload)
                    os.symlink(target, output)
                elif self.mode == "hardlink-output":
                    target = output_root.parent / f".external-{job['job_id']}.glb"
                    target.write_bytes(payload)
                    os.link(target, output)
                    self.created_hardlink = True
                else:
                    output.write_bytes(payload)
            result: dict[str, object] = {
                "job_id": job["job_id"],
                "status": "succeeded",
                "output_sha256": digest,
                "report_schema": "openbfme.w3d-adapter-report",
                "report_version": 2,
                "asset_kind": job["asset_kind"],
                "adapter_report_sha256": _sha_bytes(
                    f"adapter-{job['job_id']}".encode("ascii")
                ),
                "meshes": 1,
                "animations": len(job["animations"]),
                "bones": 0,
                "skeletons": 0,
                "vertices": 3,
                "triangles": 1,
                "skinned_meshes": 0,
                "materials": 1,
                "images": 1,
            }
            if self.mode == "hash-mismatch":
                result["output_sha256"] = "0" * 64
            if self.mode == "leaky-marker":
                result["source_path"] = "private/SecretModel.w3d"
            marker = _canonical(result).decode("utf-8").rstrip("\n")
            lines.append(f"OPENBFME_W3D_BATCH_JOB {marker}")
            if self.mode == "duplicate-marker":
                lines.append(f"OPENBFME_W3D_BATCH_JOB {marker}")
        done = {
            "report_schema": "openbfme.w3d-batch-report",
            "report_version": 1,
            "manifest_sha256": _sha_bytes(raw_manifest),
            "jobs": len(document["jobs"]),
            "succeeded": len(document["jobs"]),
            "failed": 0,
            "complete": True,
        }
        done_marker = _canonical(done).decode("utf-8").rstrip("\n")
        lines.append(f"OPENBFME_W3D_BATCH_DONE {done_marker}")
        if self.mode == "malformed-marker":
            lines[0] = "OPENBFME_W3D_BATCH_JOB {"
        if self.mode == "unknown-marker":
            lines.insert(0, "OPENBFME_W3D_BATCH_SECRET {}")
        return W3DProcessResult(0, ("\n".join(lines) + "\n").encode("utf-8"))


class W3DBatchRunnerTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.blender = self.root / "tools" / "blender.exe"
        self.adapter = self.root / "adapter" / "batch.py"
        self.converter = self.adapter.with_name("w3d_to_glb.py")
        self.plugin = self.root / "plugin"
        self.jobs = self.root / "jobs"
        self.destination = self.root / "private-output" / "w3d"
        self.blender.parent.mkdir()
        self.adapter.parent.mkdir()
        self.plugin.mkdir()
        self.jobs.mkdir()
        self.blender.write_bytes(b"pinned blender")
        self.adapter.write_bytes(b"pinned adapter")
        self.converter.write_bytes(b"pinned converter")
        (self.plugin / "plugin.py").write_bytes(b"pinned plugin")

    def _plan(
        self,
        count: int = 1,
        *,
        batch_size: int = 256,
        authored_paths: bool = False,
    ) -> W3DJobPlan:
        values: list[tuple[str, bytes]] = []
        for index in range(count):
            relative = (
                f"private/SecretModel{index:04d}.w3d"
                if authored_paths
                else f"staged/{index:04d}.w3d"
            )
            payload = f"model-{index}".encode("ascii")
            path = self.jobs.joinpath(*relative.split("/"))
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(payload)
            values.append((relative, payload))
        return _make_plan(values, batch_size=batch_size)

    def _partial_plan(self, count: int = 1, *, terminal_count: int = 1) -> W3DJobPlan:
        plan = self._plan(count)
        terminals = tuple(
            W3DTerminal(
                source_id=f"terminal-{index:032x}",
                source_sha256=_sha_bytes(f"terminal-{index}".encode("ascii")),
                reason_codes=("unsupported-source",),
            )
            for index in range(terminal_count)
        )
        return _reseal_plan(
            plan,
            terminals=terminals,
            source_count=plan.consumed_source_count + len(terminals),
            private_plan_sha256=_sha_bytes(
                f"partial-private-plan-{terminal_count}".encode("ascii")
            ),
        )

    def _run(
        self,
        plan: W3DJobPlan,
        runner: _AdapterRunner,
        **kwargs: object,
    ):
        if kwargs.get("execute_accounted_jobs") is True:
            kwargs.setdefault(
                "blender_runtime_tree_sha256",
                hash_w3d_tool_tree(self.blender.parent).sha256,
            )
            kwargs.setdefault(
                "adapter_bundle_tree_sha256",
                batch_runner.hash_w3d_adapter_bundle(self.adapter).sha256,
            )

        def execute():
            return run_w3d_job_plan(
                plan,
                self.blender,
                self.plugin,
                self.jobs,
                self.adapter,
                self.destination,
                blender_executable_sha256=_sha_file(self.blender),
                adapter_sha256=_sha_file(self.adapter),
                plugin_tree_sha256=hash_w3d_tool_tree(self.plugin).sha256,
                subprocess_runner=runner,
                **kwargs,
            )

        if (
            kwargs.get("execute_accounted_jobs") is True
            and "prepared_root_report" not in kwargs
        ):
            kwargs["prepared_root_report"] = object()
            binding = self._accounted_binding()

            class _Validated:
                @staticmethod
                def runner_binding() -> W3DPreparedRootBinding:
                    return binding

            with patch.object(
                batch_runner,
                "validate_w3d_prepared_root",
                return_value=_Validated(),
            ):
                return execute()
        return execute()

    def _accounted_binding(self) -> W3DPreparedRootBinding:
        digest = _sha_bytes(b"prepared-root-binding")
        job_files, _ = batch_runner._scan_tree(self.jobs.resolve(), "fixture job tree")
        return W3DPreparedRootBinding(
            execute_accounted_jobs=True,
            source_identity_sha256=digest,
            source_manifest_sha256=digest,
            source_request_sha256=digest,
            source_tree_sha256=digest,
            input_private_plan_sha256=digest,
            input_plan_evidence_sha256=digest,
            prepared_private_plan_sha256=digest,
            prepared_plan_evidence_sha256=digest,
            inventory_sha256=digest,
            payload_tree_sha256=digest,
            runner_tree_sha256=batch_runner._tree_identity(job_files).sha256,
            request_sha256=digest,
            identity_sha256=digest,
            manifest_sha256=digest,
        )

    def _prepared_plan(
        self,
        *,
        embedded_hierarchy: bool = False,
    ) -> tuple[W3DJobPlan, Path, bytes, Path | None, bytes]:
        plan = self._plan()
        job = plan.jobs[0]
        model = self.jobs.joinpath(*job.model.split("/"))
        raw_model = model.read_bytes()
        prepared_model = b"proof-bound prepared model"
        model.write_bytes(prepared_model)
        if embedded_hierarchy:
            hierarchy_path = job.model
            hierarchy_source_id = job.model_source_id
            hierarchy_source_sha256 = job.model_source_sha256
            hierarchy = None
            hierarchy_bytes = raw_model
        else:
            hierarchy_path = "staged/shared-hierarchy.w3d"
            hierarchy_source_id = "src-hierarchy"
            hierarchy_bytes = b"source-bound hierarchy"
            hierarchy = self.jobs.joinpath(*hierarchy_path.split("/"))
            hierarchy.parent.mkdir(parents=True, exist_ok=True)
            hierarchy.write_bytes(hierarchy_bytes)
            hierarchy_source_sha256 = _sha_bytes(hierarchy_bytes)
        planned_job = replace(
            job,
            asset_kind="hierarchical",
            hierarchy=hierarchy_path,
            hierarchy_source_id=hierarchy_source_id,
            hierarchy_source_sha256=hierarchy_source_sha256,
            model_preparation=SECONDARY_SKIN_PREPARATION,
            hierarchy_resolution_mode=(
                "same-source" if embedded_hierarchy else "sibling-path"
            ),
        )
        raw_plan = _replace_plan_jobs(
            plan,
            (planned_job,),
            preserve_manifests=False,
        )
        attested_job = replace(
            planned_job,
            prepared_model_sha256=_sha_bytes(prepared_model),
            model_preparation_evidence_sha256=_sha_bytes(b"secondary-skin-proof"),
        )
        attested = _replace_plan_jobs(
            raw_plan,
            (attested_job,),
            private_plan_sha256=_sha_bytes(b"attested-private-plan"),
        )
        return attested, model, prepared_model, hierarchy, hierarchy_bytes

    def test_executes_one_exact_command_per_batch_and_publishes_canonical_evidence(
        self,
    ) -> None:
        plan = self._plan(2, batch_size=1, authored_paths=True)
        runner = _AdapterRunner()

        report = self._run(plan, runner)

        self.assertEqual(len(runner.commands), 2)
        for command in runner.commands:
            self.assertEqual(
                command[:6],
                (
                    str(self.blender.resolve()),
                    "--background",
                    "--factory-startup",
                    "--python",
                    str(self.adapter.resolve()),
                    "--",
                ),
            )
            self.assertEqual(
                [command[index] for index in (6, 8, 10, 12)],
                ["--manifest", "--plugin-root", "--job-root", "--output-root"],
            )
        self.assertTrue(report.conversion_complete)
        self.assertFalse(report.reused)
        manifest = report.manifest_path.read_bytes()
        self.assertEqual(report.manifest_path.name, W3D_BATCH_CORPUS_MANIFEST)
        self.assertEqual(manifest, _canonical(json.loads(manifest)))
        self.assertEqual(_sha_bytes(manifest), report.manifest_sha256)
        self.assertEqual(
            sorted(
                path.relative_to(self.destination).as_posix()
                for path in self.destination.rglob("*.glb")
            ),
            sorted(job.output for job in plan.jobs),
        )
        neutral = json.dumps(report.neutral(), sort_keys=True)
        for secret in (
            str(self.root),
            "private/SecretModel0000.w3d",
            "SecretModel",
        ):
            self.assertNotIn(secret, neutral)
        self.assertEqual(report.neutral()["schemaVersion"], 0)
        self.assertNotIn("executionPolicy", report.neutral())
        self.assertNotIn("parentPlanEvidence", report.neutral())
        self.assertNotIn("terminals", report.neutral())
        self.assertTrue(report.neutral()["summary"]["conversionComplete"])
        self.assertTrue(report.neutral()["summary"]["jobConversionComplete"])

    def test_committed_force_publish_survives_best_effort_backup_cleanup(self) -> None:
        first_plan = self._plan()
        self._run(first_plan, _AdapterRunner())
        second_plan = self._plan(2)
        remove_owned_tree = batch_runner._remove_owned_tree

        def fail_backup_cleanup(path: Path, parent: Path, prefix: str) -> None:
            if ".backup-" in path.name:
                raise OSError("injected backup cleanup failure")
            remove_owned_tree(path, parent, prefix)

        with patch.object(
            batch_runner,
            "_remove_owned_tree",
            side_effect=fail_backup_cleanup,
        ):
            replaced = self._run(second_plan, _AdapterRunner(), force=True)

        self.assertFalse(replaced.reused)
        self.assertTrue(replaced.job_conversion_complete)
        self.assertEqual(len(replaced.outputs), 2)
        self.assertEqual(
            json.loads(replaced.manifest_path.read_bytes())["summary"]["jobCount"],
            2,
        )

    def test_unattested_preparation_is_rejected_before_process_launch(self) -> None:
        plan = self._plan()
        hierarchy_path = "staged/shared-hierarchy.w3d"
        hierarchy_bytes = b"source-bound hierarchy"
        hierarchy = self.jobs.joinpath(*hierarchy_path.split("/"))
        hierarchy.write_bytes(hierarchy_bytes)
        job = replace(
            plan.jobs[0],
            asset_kind="hierarchical",
            hierarchy=hierarchy_path,
            hierarchy_source_id="src-hierarchy",
            hierarchy_source_sha256=_sha_bytes(hierarchy_bytes),
            model_preparation=SECONDARY_SKIN_PREPARATION,
            hierarchy_resolution_mode="sibling-path",
        )
        raw_preparation = _replace_plan_jobs(
            plan,
            (job,),
            preserve_manifests=False,
        )
        runner = _AdapterRunner()

        with self.assertRaisesRegex(W3DBatchRunnerError, "fully attested"):
            self._run(raw_preparation, runner)

        self.assertFalse(runner.commands)
        self.assertFalse(self.destination.exists())

    def test_attested_preparation_executes_with_stable_adapter_manifest(self) -> None:
        plan, model, prepared_model, hierarchy, hierarchy_bytes = self._prepared_plan()
        assert hierarchy is not None
        manifest_before = plan.batches[0].manifest_bytes()
        runner = _AdapterRunner()

        report = self._run(plan, runner)

        self.assertTrue(report.conversion_complete)
        self.assertEqual(len(runner.commands), 1)
        self.assertEqual(model.read_bytes(), prepared_model)
        self.assertEqual(hierarchy.read_bytes(), hierarchy_bytes)
        manifest_path = Path(
            runner.commands[0][runner.commands[0].index("--manifest") + 1]
        )
        self.assertFalse(manifest_path.exists())
        self.assertEqual(plan.batches[0].manifest_bytes(), manifest_before)
        self.assertEqual(
            plan.jobs[0].prepared_model_sha256,
            _sha_bytes(prepared_model),
        )

    def test_embedded_hierarchy_attestation_uses_prepared_model_hash(self) -> None:
        plan, model, prepared_model, hierarchy, raw_model = self._prepared_plan(
            embedded_hierarchy=True
        )
        self.assertIsNone(hierarchy)
        self.assertEqual(
            plan.jobs[0].hierarchy_source_sha256,
            _sha_bytes(raw_model),
        )
        self.assertNotEqual(_sha_bytes(prepared_model), _sha_bytes(raw_model))

        report = self._run(plan, _AdapterRunner())

        self.assertTrue(report.conversion_complete)
        self.assertEqual(model.read_bytes(), prepared_model)

    def test_prepared_model_and_external_hierarchy_tamper_are_rejected(self) -> None:
        plan, model, prepared_model, hierarchy, hierarchy_bytes = self._prepared_plan()
        assert hierarchy is not None

        model.write_bytes(prepared_model + b"tamper")
        model_runner = _AdapterRunner()
        with self.assertRaisesRegex(W3DBatchRunnerError, "planned model"):
            self._run(plan, model_runner)
        self.assertFalse(model_runner.commands)
        model.write_bytes(prepared_model)

        hierarchy.write_bytes(hierarchy_bytes + b"tamper")
        hierarchy_runner = _AdapterRunner()
        with self.assertRaisesRegex(W3DBatchRunnerError, "planned hierarchy"):
            self._run(plan, hierarchy_runner)
        self.assertFalse(hierarchy_runner.commands)
        self.assertFalse(self.destination.exists())

    def test_hierarchy_triplet_and_preparation_kind_are_fail_closed(self) -> None:
        plan = self._plan()
        incomplete = replace(plan.jobs[0], hierarchy="staged/missing.w3d")
        unsupported = replace(
            plan.jobs[0],
            hierarchy="staged/0000.w3d",
            hierarchy_source_id=plan.jobs[0].model_source_id,
            hierarchy_source_sha256=plan.jobs[0].model_source_sha256,
            model_preparation="unproved-rewrite",
            prepared_model_sha256=_sha_bytes(b"prepared"),
            model_preparation_evidence_sha256=_sha_bytes(b"proof"),
            hierarchy_resolution_mode="same-source",
        )
        for job, error in (
            (incomplete, "hierarchy evidence is incomplete"),
            (unsupported, "preparation kind is unsupported"),
        ):
            with self.subTest(error=error):
                runner = _AdapterRunner()
                with self.assertRaisesRegex(W3DBatchRunnerError, error):
                    self._run(_replace_plan_jobs(plan, (job,)), runner)
                self.assertFalse(runner.commands)

    def test_resolution_contract_forgery_matrix_fails_before_execution_or_reuse(
        self,
    ) -> None:
        plan = self._plan()
        job = plan.jobs[0]
        published = self._run(plan, _AdapterRunner())
        manifest_before = published.manifest_path.read_bytes()
        sibling = replace(
            job,
            asset_kind="hierarchical",
            hierarchy="staged/hierarchy.w3d",
            hierarchy_source_id="src-hierarchy",
            hierarchy_source_sha256=_sha_bytes(b"hierarchy"),
            hierarchy_resolution_mode="sibling-path",
        )
        malformed = {
            "missing-hierarchy-mode": replace(sibling, hierarchy_resolution_mode=None),
            "false-same-source-hierarchy": replace(
                sibling, hierarchy_resolution_mode="same-source"
            ),
            "case-alias-hierarchy": replace(sibling, hierarchy=job.model.swapcase()),
            "shared-sibling-id": replace(
                sibling, hierarchy_source_id=job.model_source_id
            ),
            "mode-without-hierarchy": replace(
                job, hierarchy_resolution_mode="sibling-path"
            ),
            "extra-animation-mode": replace(
                job,
                animation_hierarchy_resolution_modes=("sibling-path",),
            ),
            "wrong-hierarchy-mode-type": replace(sibling, hierarchy_resolution_mode=1),
            "wrong-animation-mode-type": replace(
                job, animation_hierarchy_resolution_modes=["sibling-path"]
            ),
        }
        for label, forged_job in malformed.items():
            with self.subTest(label=label):
                forged = _replace_plan_jobs(
                    plan,
                    (forged_job,),
                    preserve_manifests=False,
                )
                runner = _AdapterRunner()
                with self.assertRaisesRegex(
                    W3DBatchRunnerError, "resolution contract"
                ) as caught:
                    self._run(forged, runner)
                self.assertFalse(runner.commands)
                self.assertNotIn(job.model, str(caught.exception))
                self.assertEqual(published.manifest_path.read_bytes(), manifest_before)

    def test_ordinary_job_uses_raw_hash_and_remains_byte_identical(self) -> None:
        plan = self._plan()
        model = self.jobs.joinpath(*plan.jobs[0].model.split("/"))
        before = model.read_bytes()

        report = self._run(plan, _AdapterRunner())

        self.assertTrue(report.conversion_complete)
        self.assertEqual(model.read_bytes(), before)
        self.assertIsNone(plan.jobs[0].model_preparation)
        self.assertIsNone(plan.jobs[0].prepared_model_sha256)

    def test_animation_mutation_is_rejected_before_process_launch(self) -> None:
        plan = self._plan()
        animation_path = "staged/idle.w3d"
        animation = b"source-bound animation"
        animation_file = self.jobs.joinpath(*animation_path.split("/"))
        animation_file.write_bytes(animation)
        hierarchy_path = "staged/rig.w3d"
        hierarchy = b"source-bound hierarchy"
        hierarchy_file = self.jobs.joinpath(*hierarchy_path.split("/"))
        hierarchy_file.write_bytes(hierarchy)
        job = replace(
            plan.jobs[0],
            asset_kind="animated",
            animations=(animation_path,),
            animation_source_ids=("src-animation",),
            animation_source_sha256s=(_sha_bytes(animation),),
            hierarchy=hierarchy_path,
            hierarchy_source_id="src-hierarchy",
            hierarchy_source_sha256=_sha_bytes(hierarchy),
            hierarchy_resolution_mode="sibling-path",
            animation_hierarchy_resolution_modes=("sibling-path",),
        )
        animated_plan = _replace_plan_jobs(
            plan,
            (job,),
            preserve_manifests=False,
        )
        animation_file.write_bytes(b"mutated animation")
        runner = _AdapterRunner()

        with self.assertRaisesRegex(
            W3DBatchRunnerError,
            "animation is missing or has changed",
        ):
            self._run(animated_plan, runner)

        self.assertFalse(runner.commands)
        self.assertFalse(self.destination.exists())

    def test_strict_schema_v0_default_and_false_have_fixed_reuse_hashes(self) -> None:
        plan = self._plan()
        implicit = self._run(plan, _AdapterRunner())
        forbidden = _AdapterRunner("process-failure")
        explicit = self._run(
            plan,
            forbidden,
            execute_accounted_jobs=False,
        )

        self.assertTrue(explicit.reused)
        self.assertFalse(forbidden.commands)
        self.assertEqual(implicit.request_sha256, explicit.request_sha256)
        self.assertEqual(implicit.identity_sha256, explicit.identity_sha256)
        self.assertEqual(implicit.manifest_sha256, explicit.manifest_sha256)
        self.assertEqual(
            implicit.request_sha256,
            "9cd372c5339acce921c4df77431710b655dfaee7d5c1d68b86944b11b7a052fe",
        )
        self.assertEqual(
            implicit.identity_sha256,
            "db286b0ab8720873a58d0100066ec2edc25d27390817445c66e96cb213fdc5bd",
        )
        self.assertEqual(
            implicit.manifest_sha256,
            "af1a5849c4be993eac3c3d3fb340faa7535b04c70b32139f7a7ceffb402c57c8",
        )
        self.assertEqual(implicit.neutral()["schemaVersion"], 0)
        self.assertNotIn("executionPolicy", implicit.neutral())
        self.assertNotIn("parentPlanEvidence", implicit.neutral())
        self.assertNotIn("terminals", implicit.neutral())

    def test_valid_destination_is_fully_revalidated_and_reused_without_launch(
        self,
    ) -> None:
        plan = self._plan()
        first = self._run(plan, _AdapterRunner())
        forbidden = _AdapterRunner("process-failure")

        reused = self._run(plan, forbidden)

        self.assertTrue(reused.reused)
        self.assertEqual(reused.identity_sha256, first.identity_sha256)
        self.assertFalse(forbidden.commands)
        output = self.destination.joinpath(*plan.jobs[0].output.split("/"))
        output.write_bytes(b"tampered")
        with self.assertRaises(W3DBatchReuseError):
            self._run(plan, _AdapterRunner())

    def test_force_failure_preserves_previous_verified_destination(self) -> None:
        plan = self._plan()
        first = self._run(plan, _AdapterRunner())
        before = first.manifest_path.read_bytes()

        with self.assertRaises(W3DBatchRunnerError):
            self._run(plan, _AdapterRunner("process-failure"), force=True)

        self.assertEqual(first.manifest_path.read_bytes(), before)
        reused = self._run(plan, _AdapterRunner())
        self.assertTrue(reused.reused)

    def test_terminals_and_bad_pins_are_rejected_before_process_launch(self) -> None:
        plan = self._plan()
        terminal = W3DTerminal("src-opaque", "0" * 64, ("unsupported-source",))
        bad = replace(
            plan, terminals=(terminal,), consumed_source_count=0, evidence_sha256=""
        )
        bad = replace(
            bad, evidence_sha256=_sha_bytes(_canonical(bad.evidence_hash_basis()))
        )
        runner = _AdapterRunner()
        with self.assertRaises(W3DBatchRunnerError):
            self._run(bad, runner)
        self.assertFalse(runner.commands)

        with self.assertRaises(W3DBatchRunnerError):
            run_w3d_job_plan(
                plan,
                self.blender,
                self.plugin,
                self.jobs,
                self.adapter,
                self.destination,
                blender_executable_sha256="0" * 64,
                adapter_sha256=_sha_file(self.adapter),
                plugin_tree_sha256=hash_w3d_tool_tree(self.plugin).sha256,
                subprocess_runner=runner,
            )
        self.assertFalse(runner.commands)

    def test_accounted_jobs_publish_transparent_incomplete_corpus_and_reuse(
        self,
    ) -> None:
        plan = self._partial_plan(2, terminal_count=2)
        first_runner = _AdapterRunner()

        first = self._run(
            plan,
            first_runner,
            execute_accounted_jobs=True,
        )

        self.assertEqual(len(first_runner.commands), len(plan.batches))
        self.assertTrue(first.job_conversion_complete)
        self.assertFalse(first.conversion_complete)
        self.assertFalse(first.corpus_complete)
        self.assertFalse(first.plan.source_accounting_complete)
        evidence = first.neutral()
        self.assertEqual(evidence["schemaVersion"], 2)
        self.assertEqual(
            evidence["executionPolicy"],
            {"executeAccountedJobs": True},
        )
        self.assertEqual(evidence["parentPlanEvidence"], plan.neutral())
        self.assertEqual(
            evidence["terminals"],
            [terminal.neutral() for terminal in plan.terminals],
        )
        self.assertEqual(
            evidence["preparedRootAttestation"],
            self._accounted_binding().neutral(),
        )
        self.assertEqual(
            evidence["toolchainAttestation"],
            {
                "blenderRuntimeTree": hash_w3d_tool_tree(self.blender.parent).neutral(),
                "adapterBundleTree": batch_runner.hash_w3d_adapter_bundle(
                    self.adapter
                ).neutral(),
            },
        )
        self.assertEqual(
            evidence["summary"],
            {
                "sourceCount": 4,
                "consumedSourceCount": 2,
                "jobCount": 2,
                "batchCount": 1,
                "terminalCount": 2,
                "outputCount": 2,
                "sourceAccountingComplete": False,
                "jobConversionComplete": True,
                "conversionComplete": False,
                "corpusComplete": False,
                "published": True,
            },
        )
        manifest = json.loads(first.manifest_path.read_bytes())
        self.assertEqual(manifest, evidence)

        forbidden = _AdapterRunner("process-failure")
        reused = self._run(
            plan,
            forbidden,
            execute_accounted_jobs=True,
        )
        self.assertTrue(reused.reused)
        self.assertEqual(reused.request_sha256, first.request_sha256)
        self.assertEqual(reused.identity_sha256, first.identity_sha256)
        self.assertEqual(reused.manifest_sha256, first.manifest_sha256)
        self.assertFalse(forbidden.commands)

    def test_accounted_jobs_require_current_prepared_attestation_before_process(
        self,
    ) -> None:
        plan = self._partial_plan()
        missing_runner = _AdapterRunner()

        with self.assertRaisesRegex(
            W3DBatchRunnerError,
            "requires a prepared-root attestation",
        ):
            self._run(
                plan,
                missing_runner,
                execute_accounted_jobs=True,
                prepared_root_report=None,
            )
        self.assertFalse(missing_runner.commands)

        stale_runner = _AdapterRunner()
        with patch.object(
            batch_runner,
            "validate_w3d_prepared_root",
            side_effect=W3DPreparedRootError("texture tree changed"),
        ):
            with self.assertRaisesRegex(
                W3DBatchRunnerError,
                "failed attestation",
            ):
                self._run(
                    plan,
                    stale_runner,
                    execute_accounted_jobs=True,
                    prepared_root_report=object(),
                )
        self.assertFalse(stale_runner.commands)

    def test_strict_mode_rejects_irrelevant_prepared_attestation_without_launch(
        self,
    ) -> None:
        plan = self._plan()
        runner = _AdapterRunner()

        with self.assertRaisesRegex(
            W3DBatchRunnerError,
            "strict W3D execution rejects",
        ):
            self._run(plan, runner, prepared_root_report=object())

        self.assertFalse(runner.commands)

    def test_accounted_job_policy_is_bound_into_reuse_request(self) -> None:
        plan = self._plan()
        strict = self._run(plan, _AdapterRunner())
        runner = _AdapterRunner()

        with self.assertRaisesRegex(W3DBatchReuseError, "complete verification"):
            self._run(
                plan,
                runner,
                execute_accounted_jobs=True,
            )

        self.assertFalse(runner.commands)
        self.assertFalse(strict.execute_accounted_jobs)
        self.assertNotIn("executionPolicy", strict.neutral())
        self.assertNotIn("parentPlanEvidence", strict.neutral())
        self.assertNotIn("terminals", strict.neutral())

    def test_accounted_job_terminal_evidence_is_bound_into_reuse_request(
        self,
    ) -> None:
        plan = self._partial_plan()
        first = self._run(plan, _AdapterRunner(), execute_accounted_jobs=True)
        changed_terminal = replace(
            plan.terminals[0],
            reason_codes=("different-terminal-reason",),
        )
        changed = _reseal_plan(plan, terminals=(changed_terminal,))
        runner = _AdapterRunner()

        with self.assertRaisesRegex(W3DBatchReuseError, "complete verification"):
            self._run(
                changed,
                runner,
                execute_accounted_jobs=True,
            )

        self.assertFalse(runner.commands)
        self.assertEqual(
            first.neutral()["terminals"],
            [plan.terminals[0].neutral()],
        )

    def test_accounted_toolchain_changes_invalidate_reuse_and_midrun_mutation(
        self,
    ) -> None:
        plan = self._partial_plan()
        runtime = self.blender.parent / "runtime.dll"
        runtime.write_bytes(b"runtime-a")
        self._run(plan, _AdapterRunner(), execute_accounted_jobs=True)

        self.converter.write_bytes(b"changed converter")
        with self.assertRaisesRegex(W3DBatchReuseError, "complete verification"):
            self._run(plan, _AdapterRunner(), execute_accounted_jobs=True)
        self.converter.write_bytes(b"pinned converter")

        runtime.write_bytes(b"runtime-b")
        with self.assertRaisesRegex(W3DBatchReuseError, "complete verification"):
            self._run(plan, _AdapterRunner(), execute_accounted_jobs=True)

        class _MutatingRunner(_AdapterRunner):
            def __call__(
                inner_self, *args: object, **kwargs: object
            ) -> W3DProcessResult:
                result = super(_MutatingRunner, inner_self).__call__(*args, **kwargs)
                self.converter.write_bytes(b"mid-run converter mutation")
                return result

        with self.assertRaisesRegex(
            W3DBatchRunnerError,
            "adapter bundle changed during conversion",
        ):
            self._run(
                plan,
                _MutatingRunner(),
                execute_accounted_jobs=True,
                force=True,
            )

    def test_accounted_job_plan_rejects_removed_bad_or_overlapping_accounting(
        self,
    ) -> None:
        plan = self._partial_plan()
        removed = _reseal_plan(plan, terminals=())
        bad_count = _reseal_plan(plan, source_count=plan.source_count + 1)
        overlap_terminal = replace(
            plan.terminals[0],
            source_id=plan.jobs[0].model_source_id,
        )
        overlap = _reseal_plan(plan, terminals=(overlap_terminal,))
        duplicate_base = self._partial_plan(terminal_count=2)
        duplicate_terminal = replace(
            duplicate_base.terminals[1],
            source_id=duplicate_base.terminals[0].source_id,
        )
        duplicate = _reseal_plan(
            duplicate_base,
            terminals=(duplicate_base.terminals[0], duplicate_terminal),
        )

        cases = (
            (removed, "source accounting is not exact"),
            (bad_count, "source accounting is not exact"),
            (overlap, "overlap the terminal inventory"),
            (duplicate, "duplicate terminal source IDs"),
        )
        for candidate, error in cases:
            with self.subTest(error=error):
                runner = _AdapterRunner()
                with self.assertRaisesRegex(W3DBatchRunnerError, error):
                    self._run(
                        candidate,
                        runner,
                        execute_accounted_jobs=True,
                    )
                self.assertFalse(runner.commands)
        self.assertFalse(self.destination.exists())

    def test_accounted_job_plan_rejects_zero_jobs_and_process_failure(self) -> None:
        empty = self._plan(0)
        empty_runner = _AdapterRunner()
        with self.assertRaisesRegex(W3DBatchRunnerError, "at least one planned job"):
            self._run(
                empty,
                empty_runner,
                execute_accounted_jobs=True,
            )
        self.assertFalse(empty_runner.commands)

        plan = self._partial_plan()
        failed_runner = _AdapterRunner("process-failure")
        with self.assertRaises(W3DBatchRunnerError):
            self._run(
                plan,
                failed_runner,
                execute_accounted_jobs=True,
            )
        self.assertEqual(len(failed_runner.commands), 1)
        self.assertFalse(self.destination.exists())

    def test_run_process_retains_bounded_rc1_and_rejects_other_failures(
        self,
    ) -> None:
        accepted = W3DProcessResult(1, b"bounded stdout", b"bounded stderr")
        actual = batch_runner._run_process(
            lambda *_args, **_kwargs: accepted,
            ("blender",),
            timeout_seconds=1,
            stdout_limit_bytes=64,
            stderr_limit_bytes=64,
        )
        self.assertEqual(actual, accepted)

        rejected = (
            W3DProcessResult(2),
            W3DProcessResult(1, timed_out=True),
            W3DProcessResult(1, stdout_overflow=True),
            W3DProcessResult(1, stderr_overflow=True),
            W3DProcessResult(1, stdout=b"x" * 65),
            W3DProcessResult(1, stderr=b"x" * 65),
        )
        for result in rejected:
            with self.subTest(result=result):
                with self.assertRaises(W3DBatchRunnerError):
                    batch_runner._run_process(
                        lambda *_args, **_kwargs: result,
                        ("blender",),
                        timeout_seconds=1,
                        stdout_limit_bytes=64,
                        stderr_limit_bytes=64,
                    )

    def test_valid_rc1_failure_markers_raise_sanitized_canonical_aggregate(
        self,
    ) -> None:
        plan = self._plan(4, authored_paths=True)
        batch = plan.batches[0]
        rows = (
            _success_marker_row(batch.jobs[0]),
            _failure_marker_row(
                batch.jobs[1],
                "converter-execution",
                "runtime",
            ),
            _failure_marker_row(
                batch.jobs[2],
                "input-validation",
                "value",
            ),
            _failure_marker_row(
                batch.jobs[3],
                "converter-execution",
                "runtime",
            ),
        )
        result = _marker_result(batch, rows, returncode=1)

        class FailureRunner(_AdapterRunner):
            def __call__(
                self, command: tuple[str, ...], **kwargs: object
            ) -> W3DProcessResult:
                del command, kwargs
                return result

        with self.assertRaises(W3DBatchConversionError) as raised:
            self._run(plan, FailureRunner())

        error = raised.exception
        self.assertEqual(str(error), "W3D batch conversion failed")
        self.assertEqual(error.args, ("W3D batch conversion failed",))
        self.assertEqual(error.failed_job_count, 3)
        self.assertEqual(error.job_count, 4)
        self.assertEqual(
            error.failure_counts,
            (
                ("converter-execution", "runtime", 2),
                ("input-validation", "value", 1),
            ),
        )
        with self.assertRaises(FrozenInstanceError):
            error.failed_job_count = 9  # type: ignore[misc]
        encoded = repr(error) + str(error) + repr(error.args)
        for forbidden in ("private/", "SecretModel", str(self.root)):
            self.assertNotIn(forbidden, encoded)
        self.assertFalse(self.destination.exists())

    def test_all_fixed_converter_phases_are_accepted_and_aggregated(self) -> None:
        phases = (
            "converter-initialization",
            "scene-reset",
            "model-import",
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
            "scene-validation",
            "animation-import",
            "post-animation-validation",
            "animation-sidecar-mesh-strip",
            "attachment-restoration",
            "render-revalidation",
            "animation-export-preparation",
            "export",
        )
        plan = self._plan(len(phases), authored_paths=True)
        batch = plan.batches[0]
        rows = tuple(
            _failure_marker_row(job, phase, "control-flow")
            for job, phase in zip(batch.jobs, phases, strict=True)
        )
        result = _marker_result(batch, rows, returncode=1)

        with self.assertRaises(W3DBatchConversionError) as raised:
            batch_runner._parse_markers(result, batch)

        error = raised.exception
        self.assertEqual(error.failed_job_count, len(phases))
        self.assertEqual(error.job_count, len(phases))
        self.assertEqual(
            error.failure_counts,
            tuple((phase, "control-flow", 1) for phase in sorted(phases)),
        )
        self.assertEqual(
            sum(count for _phase, _kind, count in error.failure_counts),
            error.failed_job_count,
        )
        rendered = repr(error) + str(error) + repr(error.args)
        self.assertNotIn("private/", rendered)
        self.assertNotIn("SecretModel", rendered)

    def test_failure_marker_field_enum_count_order_and_rc_forgery_fail_closed(
        self,
    ) -> None:
        plan = self._plan(2, authored_paths=True)
        batch = plan.batches[0]
        success = _success_marker_row(batch.jobs[0])
        failure = _failure_marker_row(
            batch.jobs[1],
            "converter-execution",
            "runtime",
        )

        extra = dict(failure)
        extra["detail"] = "private/SecretModel.w3d"
        missing = dict(failure)
        del missing["failure_kind"]
        bad_phase = dict(failure)
        bad_phase["failure_phase"] = "private/SecretModel.w3d"
        bad_kind = dict(failure)
        bad_kind["failure_kind"] = "traceback"
        bad_code = dict(failure)
        bad_code["failure_code"] = "private-error"
        bad_status = dict(failure)
        bad_status["status"] = "succeeded"

        cases = {
            "extra-field": _marker_result(batch, (success, extra), returncode=1),
            "missing-field": _marker_result(batch, (success, missing), returncode=1),
            "phase-enum": _marker_result(batch, (success, bad_phase), returncode=1),
            "kind-enum": _marker_result(batch, (success, bad_kind), returncode=1),
            "failure-code": _marker_result(batch, (success, bad_code), returncode=1),
            "failure-status": _marker_result(
                batch, (success, bad_status), returncode=1
            ),
            "job-order": _marker_result(batch, (failure, success), returncode=1),
            "done-jobs": _marker_result(
                batch,
                (success, failure),
                returncode=1,
                done_changes={"jobs": 1},
            ),
            "done-succeeded": _marker_result(
                batch,
                (success, failure),
                returncode=1,
                done_changes={"succeeded": 2},
            ),
            "done-failed": _marker_result(
                batch,
                (success, failure),
                returncode=1,
                done_changes={"failed": 0},
            ),
            "done-complete": _marker_result(
                batch,
                (success, failure),
                returncode=1,
                done_changes={"complete": True},
            ),
            "failure-rc": _marker_result(batch, (success, failure), returncode=0),
            "success-rc": _marker_result(
                batch,
                tuple(_success_marker_row(job) for job in batch.jobs),
                returncode=1,
            ),
        }
        for label, result in cases.items():
            with self.subTest(label=label):
                with self.assertRaises(W3DBatchRunnerError) as raised:
                    batch_runner._parse_markers(result, batch)
                message = str(raised.exception)
                self.assertNotIn("private/", message)
                self.assertNotIn("SecretModel", message)
                self.assertNotIsInstance(
                    raised.exception,
                    W3DBatchConversionError,
                )

    def test_timeout_partial_output_and_hash_mismatch_never_publish(self) -> None:
        for mode in ("timeout", "partial-output", "hash-mismatch"):
            with self.subTest(mode=mode):
                plan = self._plan()
                with self.assertRaises(W3DBatchRunnerError):
                    self._run(plan, _AdapterRunner(mode))
                self.assertFalse(self.destination.exists())

    def test_asset_only_glb_with_fabricated_counts_never_publishes(self) -> None:
        plan = self._plan()
        with self.assertRaisesRegex(W3DBatchRunnerError, "semantic evidence"):
            self._run(plan, _AdapterRunner("empty-semantic"))
        self.assertFalse(self.destination.exists())

    def test_malformed_duplicate_unknown_and_leaky_markers_fail_closed(self) -> None:
        for mode in (
            "malformed-marker",
            "duplicate-marker",
            "unknown-marker",
            "leaky-marker",
        ):
            with self.subTest(mode=mode):
                plan = self._plan()
                with self.assertRaises(W3DBatchRunnerError):
                    self._run(plan, _AdapterRunner(mode))
                self.assertFalse(self.destination.exists())

    def test_linked_output_is_rejected(self) -> None:
        plan = self._plan()
        original = batch_runner._is_link_like

        def mark_glb_as_link(path: Path) -> bool:
            return path.suffix.casefold() == ".glb" or original(path)

        with patch.object(batch_runner, "_is_link_like", mark_glb_as_link):
            with self.assertRaises(W3DBatchRunnerError):
                self._run(plan, _AdapterRunner())

        self.assertFalse(self.destination.exists())

    def test_hard_linked_output_is_rejected(self) -> None:
        plan = self._plan()
        runner = _AdapterRunner("hardlink-output")

        with self.assertRaises(W3DBatchRunnerError):
            self._run(plan, runner)

        self.assertTrue(runner.created_hardlink)
        self.assertFalse(self.destination.exists())

    def test_job_root_mutation_during_process_aborts_transaction(self) -> None:
        plan = self._plan()

        class MutatingRunner(_AdapterRunner):
            def __call__(
                self, command: tuple[str, ...], **kwargs: object
            ) -> W3DProcessResult:
                result = super().__call__(command, **kwargs)
                model = self_jobs.joinpath(*plan.jobs[0].model.split("/"))
                model.write_bytes(b"mutated")
                return result

        self_jobs = self.jobs
        with self.assertRaises(W3DBatchRunnerError):
            self._run(plan, MutatingRunner())
        self.assertFalse(self.destination.exists())

    def test_extra_files_and_invalid_glb_chunks_are_rejected(self) -> None:
        plan = self._plan()

        class ExtraRunner(_AdapterRunner):
            def __call__(
                self, command: tuple[str, ...], **kwargs: object
            ) -> W3DProcessResult:
                result = super().__call__(command, **kwargs)
                output_root = Path(command[command.index("--output-root") + 1])
                (output_root / "undeclared.bin").write_bytes(b"extra")
                return result

        with self.assertRaises(W3DBatchRunnerError):
            self._run(plan, ExtraRunner())
        self.assertFalse(self.destination.exists())

        class InvalidGlbRunner(_AdapterRunner):
            def __call__(
                self, command: tuple[str, ...], **kwargs: object
            ) -> W3DProcessResult:
                result = super().__call__(command, **kwargs)
                output_root = Path(command[command.index("--output-root") + 1])
                output = output_root.joinpath(*plan.jobs[0].output.split("/"))
                payload = bytearray(output.read_bytes())
                struct.pack_into("<I", payload, 8, len(payload) + 4)
                output.write_bytes(payload)
                marker = result.stdout.decode("utf-8").splitlines()
                row = json.loads(marker[0].split(" ", 1)[1])
                row["output_sha256"] = _sha_bytes(payload)
                marker[0] = "OPENBFME_W3D_BATCH_JOB " + _canonical(row).decode(
                    "utf-8"
                ).rstrip("\n")
                return W3DProcessResult(0, ("\n".join(marker) + "\n").encode("utf-8"))

        with self.assertRaises(W3DBatchRunnerError):
            self._run(plan, InvalidGlbRunner())
        self.assertFalse(self.destination.exists())


if __name__ == "__main__":
    unittest.main()
