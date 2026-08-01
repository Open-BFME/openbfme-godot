from __future__ import annotations

from dataclasses import replace
import hashlib
import json
import os
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

import openbfme_importer.w3d_job_preparation as preparation
from openbfme_importer.w3d_job_planner import (
    SECONDARY_SKIN_PREPARATION,
    W3DJobBatch,
    W3DJobPlan,
    W3DPlannedJob,
    W3DTerminal,
)
from openbfme_importer.w3d_job_preparation import (
    MODEL_PREPARATION_PROOF_REJECTED,
    W3D_JOB_PREPARATION_FORCED_TERMINAL_SCHEMA,
    W3D_JOB_PREPARATION_FORCED_TERMINAL_VERSION,
    W3D_JOB_PREPARATION_FIXED_POINT_SCHEMA,
    W3D_JOB_PREPARATION_FIXED_POINT_VERSION,
    W3D_JOB_PREPARATION_PREFLIGHT_SCHEMA,
    W3D_JOB_PREPARATION_PREFLIGHT_VERSION,
    W3DJobPreparationError,
    attest_w3d_job_preparations,
    preflight_w3d_job_preparations,
    seal_w3d_job_preparation_fixed_point,
    validate_w3d_job_preparation_fixed_point,
)
from openbfme_importer.w3d_skin_safety import (
    HIERARCHY_PIVOT_FIXUP_UNSUPPORTED,
    SKIN_ROOT_PIVOT_INFLUENCE_UNSUPPORTED,
)


CONTAINER = 0x80000000
MESH = 0x00000000
VERTICES = 0x00000002
NORMALS = 0x00000003
INFLUENCES = 0x0000000E
MESH_HEADER = 0x0000001F
HIERARCHY = 0x00000100
HIERARCHY_HEADER = 0x00000101
PIVOTS = 0x00000102
PIVOT_FIXUPS = 0x00000103
HLOD = 0x00000700
HLOD_HEADER = 0x00000701
HLOD_LOD_ARRAY = 0x00000702
HLOD_LOD_ARRAY_HEADER = 0x00000703
HLOD_SUB_OBJECT = 0x00000704
VERTICES_2 = 0x00000C00
NORMALS_2 = 0x00000C01


def _canonical(value: object) -> bytes:
    return (
        json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
        + "\n"
    ).encode("utf-8")


def _sha(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def _source_id(kind: str, ordinal: int) -> str:
    suffix = _sha(f"{kind}-{ordinal}".encode("ascii"))[:32]
    return f"src-{suffix}"


def _fixed(value: str, size: int) -> bytes:
    encoded = value.encode("ascii")
    return encoded + b"\x00" * (size - len(encoded))


def _chunk(kind: int, payload: bytes, *, container: bool = False) -> bytes:
    size = len(payload) | (CONTAINER if container else 0)
    return struct.pack("<II", kind, size) + payload


def _pivot(
    name: str,
    parent: int,
    translation: tuple[float, float, float],
) -> bytes:
    return struct.pack(
        "<16si10f",
        _fixed(name, 16),
        parent,
        *translation,
        0.0,
        0.0,
        0.0,
        0.0,
        0.0,
        0.0,
        1.0,
    )


def _hierarchy(*, include_fixups: bool = False) -> bytes:
    pivots = b"".join(
        (
            _pivot("ROOTTRANSFORM", -1, (0.0, 0.0, 0.0)),
            _pivot("BONE1", 0, (1.0, 0.0, 0.0)),
            _pivot("BONE2", 0, (0.0, 2.0, 0.0)),
        )
    )
    header = struct.pack(
        "<I16sI3f",
        0x00040001,
        _fixed("TEST_SKL", 16),
        3,
        0.0,
        0.0,
        0.0,
    )
    children = _chunk(HIERARCHY_HEADER, header) + _chunk(PIVOTS, pivots)
    if include_fixups:
        children += _chunk(PIVOT_FIXUPS, b"")
    return _chunk(
        HIERARCHY,
        children,
        container=True,
    )


def _mesh_header(*, skin: bool = True, vertex_count: int = 2) -> bytes:
    return struct.pack(
        "<II16s16s9I10f",
        0x00040002,
        0x00020000 if skin else 0,
        _fixed("DUAL", 16),
        _fixed("TEST_SKIN", 16),
        0,
        vertex_count,
        0,
        0,
        0,
        0,
        0,
        0x13 if skin else 0x03,
        1,
        *([0.0] * 10),
    )


def _vec3(values: tuple[tuple[float, float, float], ...]) -> bytes:
    return b"".join(struct.pack("<3f", *value) for value in values)


def _model(
    *,
    secondary_vertices: tuple[tuple[float, float, float], ...] = (
        (1.0, 3.0, 4.0),
        (3.0, 3.0, 7.0),
    ),
    influences: tuple[tuple[int, int, int, int], ...] = (
        (1, 0, 100, 0),
        (1, 2, 60, 40),
    ),
) -> bytes:
    primary_vertices = ((1.0, 3.0, 4.0), (2.0, 5.0, 7.0))
    normals = ((0.0, 0.0, 1.0), (0.0, 1.0, 0.0))
    mesh = _chunk(
        MESH,
        b"".join(
            (
                _chunk(MESH_HEADER, _mesh_header()),
                _chunk(VERTICES, _vec3(primary_vertices)),
                _chunk(VERTICES_2, _vec3(secondary_vertices)),
                _chunk(NORMALS, _vec3(normals)),
                _chunk(NORMALS_2, _vec3(normals)),
                _chunk(
                    INFLUENCES,
                    b"".join(struct.pack("<4H", *value) for value in influences),
                ),
            )
        ),
        container=True,
    )
    hlod_header = struct.pack(
        "<II16s16s",
        0x00010000,
        1,
        _fixed("TEST_SKIN", 16),
        _fixed("TEST_SKL", 16),
    )
    sub_object = _chunk(
        HLOD_SUB_OBJECT,
        struct.pack("<I32s", 0, _fixed("TEST_SKIN.DUAL", 32)),
    )
    lod = _chunk(
        HLOD_LOD_ARRAY,
        _chunk(HLOD_LOD_ARRAY_HEADER, struct.pack("<If", 1, 1.0)) + sub_object,
        container=True,
    )
    return mesh + _chunk(
        HLOD,
        _chunk(HLOD_HEADER, hlod_header) + lod,
        container=True,
    )


def _static_model() -> bytes:
    return _chunk(
        MESH,
        _chunk(MESH_HEADER, _mesh_header(skin=False, vertex_count=0)),
        container=True,
    )


def _preparation_rejected_model(*, displaced_x: float = 30.0) -> bytes:
    return _model(secondary_vertices=((1.0, 3.0, 4.0), (displaced_x, 3.0, 7.0)))


def _planned_job(
    ordinal: int,
    model_path: str,
    model_bytes: bytes,
    *,
    preparation_kind: str | None = SECONDARY_SKIN_PREPARATION,
    hierarchy_path: str | None = "inputs/shared-skeleton.w3d",
    hierarchy_bytes: bytes | None = None,
) -> W3DPlannedJob:
    definition = _sha(f"definition-{ordinal}".encode("ascii"))
    job_id = f"w3d-{definition[:40]}"
    if hierarchy_path is None:
        hierarchy_source_id = None
        hierarchy_source_sha256 = None
    elif hierarchy_path == model_path:
        hierarchy_source_id = _source_id("model", ordinal)
        hierarchy_source_sha256 = _sha(model_bytes)
    else:
        assert hierarchy_bytes is not None
        hierarchy_source_id = _source_id("hierarchy", ordinal)
        hierarchy_source_sha256 = _sha(hierarchy_bytes)
    return W3DPlannedJob(
        job_id=job_id,
        asset_kind="hierarchical" if hierarchy_path is not None else "static",
        model=model_path,
        animations=(),
        output=f"glb/{job_id}.glb",
        model_source_id=_source_id("model", ordinal),
        model_source_sha256=_sha(model_bytes),
        hierarchy_source_id=hierarchy_source_id,
        animation_source_ids=(),
        definition_sha256=definition,
        hierarchy=hierarchy_path,
        hierarchy_source_sha256=hierarchy_source_sha256,
        model_preparation=preparation_kind,
        hierarchy_resolution_mode=(
            None
            if hierarchy_path is None
            else "same-source"
            if hierarchy_path == model_path
            else "sibling-path"
        ),
    )


def _plan(jobs: tuple[W3DPlannedJob, ...]) -> W3DJobPlan:
    document = {
        "manifest_schema": "openbfme.w3d-batch-jobs",
        "manifest_version": 1,
        "jobs": [job.manifest_row() for job in jobs],
    }
    manifest_sha256 = _sha(_canonical(document))
    batches = (
        (
            W3DJobBatch(
                batch_id=f"batch-{manifest_sha256[:32]}",
                jobs=jobs,
                manifest_sha256=manifest_sha256,
            ),
        )
        if jobs
        else ()
    )
    provisional = W3DJobPlan(
        jobs=jobs,
        batches=batches,
        terminals=(),
        catalog_input_sha256=_sha(b"catalog-input"),
        catalog_metadata_sha256=_sha(b"catalog-metadata"),
        source_count=len(jobs),
        consumed_source_count=len(jobs),
        private_plan_sha256=_sha(b"private-plan"),
        evidence_sha256="",
    )
    return replace(
        provisional,
        evidence_sha256=_sha(_canonical(provisional.evidence_hash_basis())),
    )


def _reseal_plan(plan: W3DJobPlan, **changes: object) -> W3DJobPlan:
    provisional = replace(plan, evidence_sha256="", **changes)
    return replace(
        provisional,
        evidence_sha256=_sha(_canonical(provisional.evidence_hash_basis())),
    )


def _accounted_plan(
    jobs: tuple[W3DPlannedJob, ...],
    terminals: tuple[W3DTerminal, ...],
) -> W3DJobPlan:
    consumed_source_ids: set[str] = set()
    for job in jobs:
        consumed_source_ids.add(job.model_source_id)
        consumed_source_ids.update(job.animation_source_ids)
        if job.hierarchy_source_id is not None:
            consumed_source_ids.add(job.hierarchy_source_id)
    plan = _plan(jobs)
    return _reseal_plan(
        plan,
        terminals=terminals,
        source_count=len(consumed_source_ids) + len(terminals),
        consumed_source_count=len(consumed_source_ids),
        private_plan_sha256=_sha(b"accounted-private-plan"),
    )


def _terminal(ordinal: int) -> W3DTerminal:
    return W3DTerminal(
        source_id=_source_id("terminal", ordinal),
        source_sha256=_sha(f"terminal-{ordinal}".encode("ascii")),
        reason_codes=("unsupported-source",),
    )


class W3DJobPreparationTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name) / "job-root"
        self.root.mkdir()

    def _write(self, relative_path: str, payload: bytes) -> Path:
        path = self.root.joinpath(*relative_path.split("/"))
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(payload)
        return path

    def _tree_snapshot(self) -> tuple[tuple[object, ...], ...]:
        rows: list[tuple[object, ...]] = [
            (".", "directory", self.root.stat().st_mtime_ns)
        ]
        for path in sorted(
            self.root.rglob("*"),
            key=lambda item: item.relative_to(self.root).as_posix(),
        ):
            relative = path.relative_to(self.root).as_posix()
            metadata = path.stat(follow_symlinks=False)
            if path.is_dir():
                rows.append((relative, "directory", metadata.st_mtime_ns))
            else:
                rows.append((relative, "file", metadata.st_mtime_ns, path.read_bytes()))
        return tuple(rows)

    def test_success_is_source_bound_and_preserves_adapter_manifest_bytes(self) -> None:
        model = _model()
        hierarchy = _hierarchy()
        model_path = "inputs/model.w3d"
        hierarchy_path = "inputs/shared-skeleton.w3d"
        model_file = self._write(model_path, model)
        hierarchy_file = self._write(hierarchy_path, hierarchy)
        no_op_path = "inputs/no-op.w3d"
        no_op = _static_model()
        no_op_file = self._write(no_op_path, no_op)
        jobs = (
            _planned_job(
                0,
                model_path,
                model,
                hierarchy_path=hierarchy_path,
                hierarchy_bytes=hierarchy,
            ),
            _planned_job(
                1,
                no_op_path,
                no_op,
                preparation_kind=None,
                hierarchy_path=None,
            ),
        )
        plan = _plan(jobs)
        manifests_before = tuple(batch.manifest_bytes() for batch in plan.batches)

        attested = attest_w3d_job_preparations(plan, self.root)

        self.assertNotEqual(model_file.read_bytes(), model)
        self.assertEqual(no_op_file.read_bytes(), no_op)
        self.assertEqual(hierarchy_file.read_bytes(), hierarchy)
        prepared = attested.jobs[0]
        self.assertEqual(prepared.prepared_model_sha256, _sha(model_file.read_bytes()))
        self.assertRegex(
            prepared.model_preparation_evidence_sha256 or "",
            r"^[0-9a-f]{64}$",
        )
        self.assertNotEqual(attested.private_plan_sha256, plan.private_plan_sha256)
        self.assertNotEqual(attested.evidence_sha256, plan.evidence_sha256)
        self.assertEqual(attested.jobs[1], plan.jobs[1])
        self.assertEqual(
            tuple(batch.manifest_bytes() for batch in attested.batches),
            manifests_before,
        )
        self.assertEqual(
            tuple(batch.manifest_sha256 for batch in attested.batches),
            tuple(batch.manifest_sha256 for batch in plan.batches),
        )
        self.assertEqual(
            tuple(job.job_id for job in attested.jobs),
            tuple(job.job_id for job in plan.jobs),
        )

    def test_no_preparation_plan_is_fully_verified_and_byte_identical(self) -> None:
        payload = _static_model()
        path = "inputs/static.w3d"
        model_file = self._write(path, payload)
        plan = _plan(
            (
                _planned_job(
                    0,
                    path,
                    payload,
                    preparation_kind=None,
                    hierarchy_path=None,
                ),
            )
        )
        before = model_file.read_bytes()

        returned = attest_w3d_job_preparations(plan, self.root)

        self.assertIs(returned, plan)
        self.assertEqual(model_file.read_bytes(), before)

    def test_preflight_all_preparations_are_provable_and_read_only(self) -> None:
        model = _model()
        hierarchy = _hierarchy()
        hierarchy_path = "inputs/shared-skeleton.w3d"
        self._write(hierarchy_path, hierarchy)
        jobs = tuple(
            _planned_job(
                ordinal,
                f"inputs/model-{ordinal}.w3d",
                model,
                hierarchy_path=hierarchy_path,
                hierarchy_bytes=hierarchy,
            )
            for ordinal in range(2)
        )
        for job in jobs:
            self._write(job.model, model)
        self._write("inputs/unplanned-extra.bin", b"ordinary extra root file")
        plan = _accounted_plan(jobs, ())
        before = self._tree_snapshot()

        with (
            patch.object(preparation, "_owned_sibling") as owned_sibling,
            patch.object(preparation, "_write_staged_file") as write_staged,
            patch.object(preparation, "_commit_replacements") as commit,
        ):
            report = preflight_w3d_job_preparations(
                plan,
                self.root,
                execute_accounted_jobs=True,
            )

        owned_sibling.assert_not_called()
        write_staged.assert_not_called()
        commit.assert_not_called()
        self.assertEqual(self._tree_snapshot(), before)
        self.assertEqual(report.schema, W3D_JOB_PREPARATION_PREFLIGHT_SCHEMA)
        self.assertEqual(
            report.schema_version,
            W3D_JOB_PREPARATION_PREFLIGHT_VERSION,
        )
        self.assertEqual(report.catalog_input_sha256, plan.catalog_input_sha256)
        self.assertEqual(
            report.catalog_metadata_sha256,
            plan.catalog_metadata_sha256,
        )
        self.assertEqual(report.source_count, plan.source_count)
        self.assertEqual(report.input_private_plan_sha256, plan.private_plan_sha256)
        self.assertEqual(report.input_plan_evidence_sha256, plan.evidence_sha256)
        self.assertEqual(report.declared_preparation_count, 2)
        self.assertEqual(report.provable_preparation_count, 2)
        self.assertEqual(report.rejected_preparation_count, 0)
        self.assertEqual(report.forced_terminal_rows, ())
        self.assertEqual(report.planner_forced_terminal_rows, ())
        self.assertEqual(
            report.evidence_sha256,
            _sha(_canonical(report.evidence_hash_basis())),
        )

        fixed_point = seal_w3d_job_preparation_fixed_point((report,), plan)
        validate_w3d_job_preparation_fixed_point(fixed_point, plan)
        self.assertEqual(fixed_point.schema, W3D_JOB_PREPARATION_FIXED_POINT_SCHEMA)
        self.assertEqual(
            fixed_point.schema_version,
            W3D_JOB_PREPARATION_FIXED_POINT_VERSION,
        )
        self.assertEqual(fixed_point.rejecting_iteration_count, 0)
        self.assertEqual(fixed_point.accumulated_rejection_count, 0)
        self.assertEqual(fixed_point.final_declared_preparation_count, 2)
        self.assertEqual(
            fixed_point.evidence_sha256,
            _sha(_canonical(fixed_point.evidence_hash_basis())),
        )

    def test_preflight_fixed_point_rejects_then_binds_exact_final_plan(self) -> None:
        hierarchy = _hierarchy()
        valid = _model()
        rejected = _preparation_rejected_model()
        jobs = (
            _planned_job(
                0,
                "inputs/provable.w3d",
                valid,
                hierarchy_path="inputs/provable-skeleton.w3d",
                hierarchy_bytes=hierarchy,
            ),
            _planned_job(
                1,
                "inputs/rejected.w3d",
                rejected,
                hierarchy_path="inputs/rejected-skeleton.w3d",
                hierarchy_bytes=hierarchy,
            ),
        )
        for job, payload in zip(jobs, (valid, rejected), strict=True):
            self._write(job.model, payload)
            assert job.hierarchy is not None
            self._write(job.hierarchy, hierarchy)
        initial_plan = _accounted_plan(jobs, ())
        rejecting = preflight_w3d_job_preparations(
            initial_plan,
            self.root,
            execute_accounted_jobs=True,
        )
        forced_reasons, forced_seal = rejecting.merged_forced_terminals()
        rejected_job = jobs[1]
        assert rejected_job.hierarchy_source_id is not None
        assert rejected_job.hierarchy_source_sha256 is not None
        terminals = tuple(
            sorted(
                (
                    W3DTerminal(
                        source_id=rejected_job.model_source_id,
                        source_sha256=rejected_job.model_source_sha256,
                        reason_codes=(MODEL_PREPARATION_PROOF_REJECTED,),
                    ),
                    W3DTerminal(
                        source_id=rejected_job.hierarchy_source_id,
                        source_sha256=rejected_job.hierarchy_source_sha256,
                        reason_codes=("unowned-hierarchy-dependency",),
                    ),
                ),
                key=lambda item: item.source_id,
            )
        )
        final_plan = _reseal_plan(
            _accounted_plan((jobs[0],), terminals),
            forced_terminal_rows=tuple(sorted(forced_reasons.items())),
            forced_terminal_evidence_sha256=forced_seal,
        )
        final = preflight_w3d_job_preparations(
            final_plan,
            self.root,
            execute_accounted_jobs=True,
        )

        fixed_point = seal_w3d_job_preparation_fixed_point(
            (rejecting, final),
            final_plan,
        )
        validate_w3d_job_preparation_fixed_point(fixed_point, final_plan)

        self.assertEqual(fixed_point.rejecting_iteration_count, 1)
        self.assertEqual(fixed_point.accumulated_rejection_count, 1)
        self.assertEqual(fixed_point.final_declared_preparation_count, 1)
        self.assertEqual(final.rejected_preparation_count, 0)
        self.assertEqual(final.input_plan_evidence_sha256, final_plan.evidence_sha256)
        self.assertEqual(
            final.upstream_forced_terminal_evidence_sha256,
            final_plan.forced_terminal_evidence_sha256,
        )

        cases = (
            ((final, rejecting), final_plan),
            ((rejecting,), initial_plan),
            ((rejecting, rejecting, final), final_plan),
            ((rejecting, final), initial_plan),
        )
        for reports, candidate_plan in cases:
            with self.subTest(
                report_count=len(reports), plan=candidate_plan is final_plan
            ):
                with self.assertRaises(W3DJobPreparationError):
                    seal_w3d_job_preparation_fixed_point(reports, candidate_plan)

        provisional = replace(
            fixed_point,
            final_declared_preparation_count=2,
            evidence_sha256="",
        )
        tampered = replace(
            provisional,
            evidence_sha256=_sha(_canonical(provisional.evidence_hash_basis())),
        )
        with self.assertRaisesRegex(W3DJobPreparationError, "final-plan binding"):
            validate_w3d_job_preparation_fixed_point(tampered, final_plan)

    def test_preflight_mixed_proofs_force_only_rejected_model_without_writes(
        self,
    ) -> None:
        hierarchy = _hierarchy()
        hierarchy_path = "inputs/shared-skeleton.w3d"
        self._write(hierarchy_path, hierarchy)
        valid = _model()
        rejected = _preparation_rejected_model()
        jobs = (
            _planned_job(
                0,
                "inputs/provable.w3d",
                valid,
                hierarchy_path=hierarchy_path,
                hierarchy_bytes=hierarchy,
            ),
            _planned_job(
                1,
                "inputs/rejected.w3d",
                rejected,
                hierarchy_path=hierarchy_path,
                hierarchy_bytes=hierarchy,
            ),
        )
        self._write(jobs[0].model, valid)
        self._write(jobs[1].model, rejected)
        plan = _accounted_plan(jobs, ())
        before = self._tree_snapshot()

        with (
            patch.object(preparation, "_owned_sibling") as owned_sibling,
            patch.object(preparation, "_write_staged_file") as write_staged,
            patch.object(preparation, "_commit_replacements") as commit,
        ):
            report = preflight_w3d_job_preparations(
                plan,
                self.root,
                execute_accounted_jobs=True,
            )

        owned_sibling.assert_not_called()
        write_staged.assert_not_called()
        commit.assert_not_called()
        self.assertEqual(self._tree_snapshot(), before)
        self.assertEqual(report.declared_preparation_count, 2)
        self.assertEqual(report.provable_preparation_count, 1)
        self.assertEqual(report.rejected_preparation_count, 1)
        row = report.forced_terminal_rows[0]
        self.assertEqual(row.source_id, jobs[1].model_source_id)
        self.assertEqual(row.source_sha256, jobs[1].model_source_sha256)
        self.assertEqual(row.reason_codes, (MODEL_PREPARATION_PROOF_REJECTED,))
        self.assertEqual(
            report.planner_forced_terminal_rows,
            ((jobs[1].model_source_id, (MODEL_PREPARATION_PROOF_REJECTED,)),),
        )

    def test_preflight_all_rejected_is_canonical_and_deterministic(self) -> None:
        hierarchy = _hierarchy()
        hierarchy_path = "inputs/shared-skeleton.w3d"
        self._write(hierarchy_path, hierarchy)
        payloads = {
            1: _preparation_rejected_model(displaced_x=31.0),
            0: _preparation_rejected_model(displaced_x=30.0),
        }
        jobs = tuple(
            _planned_job(
                ordinal,
                f"inputs/rejected-{ordinal}.w3d",
                payload,
                hierarchy_path=hierarchy_path,
                hierarchy_bytes=hierarchy,
            )
            for ordinal, payload in payloads.items()
        )
        for job, payload in zip(jobs, payloads.values(), strict=True):
            self._write(job.model, payload)
        plan = _accounted_plan(jobs, ())
        before = self._tree_snapshot()

        first = preflight_w3d_job_preparations(
            plan,
            self.root,
            execute_accounted_jobs=True,
        )
        second = preflight_w3d_job_preparations(
            plan,
            self.root,
            execute_accounted_jobs=True,
        )

        self.assertEqual(first, second)
        self.assertEqual(first.neutral(), second.neutral())
        self.assertEqual(first.declared_preparation_count, 2)
        self.assertEqual(first.provable_preparation_count, 0)
        self.assertEqual(first.rejected_preparation_count, 2)
        self.assertEqual(
            tuple(row.source_id for row in first.forced_terminal_rows),
            tuple(sorted(job.model_source_id for job in jobs)),
        )
        self.assertEqual(
            first.evidence_sha256,
            _sha(_canonical(first.evidence_hash_basis())),
        )
        self.assertEqual(self._tree_snapshot(), before)

    def test_preflight_no_preparation_has_zero_canonical_report(self) -> None:
        payload = _static_model()
        path = "inputs/static.w3d"
        self._write(path, payload)
        job = _planned_job(
            0,
            path,
            payload,
            preparation_kind=None,
            hierarchy_path=None,
        )
        plan = _accounted_plan((job,), ())
        before = self._tree_snapshot()

        report = preflight_w3d_job_preparations(
            plan,
            self.root,
            execute_accounted_jobs=True,
        )

        self.assertEqual(report.declared_preparation_count, 0)
        self.assertEqual(report.provable_preparation_count, 0)
        self.assertEqual(report.rejected_preparation_count, 0)
        self.assertEqual(report.forced_terminal_rows, ())
        self.assertEqual(
            report.evidence_sha256,
            _sha(_canonical(report.evidence_hash_basis())),
        )
        self.assertEqual(self._tree_snapshot(), before)
        with self.assertRaises(TypeError):
            preflight_w3d_job_preparations(plan, self.root)  # type: ignore[call-arg]

    def test_mode_none_active_primary_root_is_terminalized_before_writes(
        self,
    ) -> None:
        model = _model(
            influences=((0, 1, 100, 0), (1, 2, 60, 40)),
        )
        hierarchy = _hierarchy()
        model_path = "inputs/root-influenced-model.w3d"
        hierarchy_path = "inputs/root-influenced-hierarchy.w3d"
        self._write(model_path, model)
        self._write(hierarchy_path, hierarchy)
        job = _planned_job(
            0,
            model_path,
            model,
            preparation_kind=None,
            hierarchy_path=hierarchy_path,
            hierarchy_bytes=hierarchy,
        )
        plan = _accounted_plan((job,), ())
        before = self._tree_snapshot()

        with (
            patch.object(preparation, "_owned_sibling") as owned_sibling,
            patch.object(preparation, "_write_staged_file") as write_staged,
            patch.object(preparation, "_commit_replacements") as commit,
        ):
            report = preflight_w3d_job_preparations(
                plan,
                self.root,
                execute_accounted_jobs=True,
            )
            with self.assertRaisesRegex(
                W3DJobPreparationError,
                "skin-safety proof rejected",
            ):
                attest_w3d_job_preparations(
                    plan,
                    self.root,
                    execute_accounted_jobs=True,
                )

        owned_sibling.assert_not_called()
        write_staged.assert_not_called()
        commit.assert_not_called()
        self.assertEqual(self._tree_snapshot(), before)
        self.assertEqual(report.declared_preparation_count, 0)
        self.assertEqual(report.rejected_preparation_count, 0)
        self.assertEqual(len(report.forced_terminal_rows), 1)
        forced = report.forced_terminal_rows[0]
        self.assertEqual(forced.source_id, job.model_source_id)
        self.assertEqual(forced.source_sha256, job.model_source_sha256)
        self.assertEqual(
            forced.reason_codes,
            (SKIN_ROOT_PIVOT_INFLUENCE_UNSUPPORTED,),
        )
        safety = report.skin_safety_report
        self.assertIsNotNone(safety)
        assert safety is not None
        self.assertEqual(safety.checked_job_count, 1)
        self.assertEqual(safety.safe_job_count, 0)
        self.assertEqual(safety.rejected_job_count, 1)
        self.assertEqual(safety.active_primary_root_count, 1)
        self.assertEqual(safety.active_secondary_root_count, 0)
        self.assertEqual(
            safety.rejections[0].reason_code,
            SKIN_ROOT_PIVOT_INFLUENCE_UNSUPPORTED,
        )

    def test_shared_hierarchy_fixup_has_one_terminal_covering_dependents(
        self,
    ) -> None:
        model = _static_model()
        hierarchy = _hierarchy(include_fixups=True)
        hierarchy_path = "inputs/shared-fixup-hierarchy.w3d"
        self._write(hierarchy_path, hierarchy)
        first = _planned_job(
            0,
            "inputs/fixup-dependent-0.w3d",
            model,
            preparation_kind=None,
            hierarchy_path=hierarchy_path,
            hierarchy_bytes=hierarchy,
        )
        second = replace(
            _planned_job(
                1,
                "inputs/fixup-dependent-1.w3d",
                model,
                preparation_kind=None,
                hierarchy_path=hierarchy_path,
                hierarchy_bytes=hierarchy,
            ),
            hierarchy_source_id=first.hierarchy_source_id,
        )
        for job in (first, second):
            self._write(job.model, model)
        plan = _accounted_plan((first, second), ())

        report = preflight_w3d_job_preparations(
            plan,
            self.root,
            execute_accounted_jobs=True,
        )

        safety = report.skin_safety_report
        self.assertIsNotNone(safety)
        assert safety is not None
        self.assertEqual(safety.checked_job_count, 2)
        self.assertEqual(safety.safe_job_count, 0)
        self.assertEqual(safety.rejected_job_count, 2)
        self.assertEqual(
            {row.job_id for row in safety.rejections},
            {first.job_id, second.job_id},
        )
        self.assertEqual(
            {row.owner_source_id for row in safety.rejections},
            {first.hierarchy_source_id},
        )
        self.assertTrue(
            all(
                row.reason_code == HIERARCHY_PIVOT_FIXUP_UNSUPPORTED
                for row in safety.rejections
            )
        )
        self.assertEqual(len(safety.forced_terminal_rows), 1)
        self.assertEqual(len(report.forced_terminal_rows), 1)
        forced = report.forced_terminal_rows[0]
        self.assertEqual(forced.source_id, first.hierarchy_source_id)
        self.assertEqual(forced.source_sha256, _sha(hierarchy))
        self.assertEqual(
            forced.reason_codes,
            (HIERARCHY_PIVOT_FIXUP_UNSUPPORTED,),
        )

    def test_safe_skinned_mode_none_job_passes_without_mutation(self) -> None:
        model = _model()
        hierarchy = _hierarchy()
        model_path = "inputs/safe-mode-none-model.w3d"
        hierarchy_path = "inputs/safe-mode-none-hierarchy.w3d"
        self._write(model_path, model)
        self._write(hierarchy_path, hierarchy)
        job = _planned_job(
            0,
            model_path,
            model,
            preparation_kind=None,
            hierarchy_path=hierarchy_path,
            hierarchy_bytes=hierarchy,
        )
        plan = _accounted_plan((job,), ())
        before = self._tree_snapshot()

        report = preflight_w3d_job_preparations(
            plan,
            self.root,
            execute_accounted_jobs=True,
        )
        returned = attest_w3d_job_preparations(
            plan,
            self.root,
            execute_accounted_jobs=True,
        )

        self.assertIs(returned, plan)
        self.assertEqual(self._tree_snapshot(), before)
        self.assertEqual(report.forced_terminal_rows, ())
        safety = report.skin_safety_report
        self.assertIsNotNone(safety)
        assert safety is not None
        self.assertEqual(safety.checked_job_count, 1)
        self.assertEqual(safety.safe_job_count, 1)
        self.assertEqual(safety.rejected_job_count, 0)
        self.assertEqual(safety.skin_mesh_count, 1)
        self.assertEqual(safety.influence_record_count, 2)
        self.assertEqual(safety.active_primary_root_count, 0)
        self.assertEqual(safety.active_secondary_root_count, 0)
        self.assertEqual(safety.forced_terminal_rows, ())

    def test_skin_safety_evidence_is_deterministic_and_path_free(self) -> None:
        model = _model(
            influences=((0, 1, 100, 0), (1, 2, 60, 40)),
        )
        hierarchy = _hierarchy()
        model_path = "inputs/AuthoredModelName.w3d"
        hierarchy_path = "inputs/AuthoredHierarchyName.w3d"
        self._write(model_path, model)
        self._write(hierarchy_path, hierarchy)
        job = _planned_job(
            0,
            model_path,
            model,
            preparation_kind=None,
            hierarchy_path=hierarchy_path,
            hierarchy_bytes=hierarchy,
        )
        plan = _accounted_plan((job,), ())

        first = preflight_w3d_job_preparations(
            plan,
            self.root,
            execute_accounted_jobs=True,
        )
        second = preflight_w3d_job_preparations(
            plan,
            self.root,
            execute_accounted_jobs=True,
        )

        first_safety = first.skin_safety_report
        second_safety = second.skin_safety_report
        self.assertIsNotNone(first_safety)
        self.assertEqual(first_safety, second_safety)
        assert first_safety is not None
        encoded = json.dumps(first_safety.neutral(), sort_keys=True)
        self.assertNotIn(str(self.root), encoded)
        self.assertNotIn(model_path, encoded)
        self.assertNotIn(hierarchy_path, encoded)
        self.assertNotIn("AuthoredModelName", encoded)
        self.assertNotIn("AuthoredHierarchyName", encoded)
        self.assertEqual(
            first_safety.evidence_sha256,
            _sha(_canonical(first_safety.evidence_hash_basis())),
        )

    def test_preflight_malformed_attested_and_unbound_inputs_fail_closed(self) -> None:
        model = _model()
        hierarchy = _hierarchy()
        model_path = "inputs/model.w3d"
        hierarchy_path = "inputs/skeleton.w3d"
        model_file = self._write(model_path, model)
        self._write(hierarchy_path, hierarchy)
        job = _planned_job(
            0,
            model_path,
            model,
            hierarchy_path=hierarchy_path,
            hierarchy_bytes=hierarchy,
        )
        plan = _accounted_plan((job,), ())
        malformed = replace(plan, evidence_sha256="0" * 64)
        bad_upstream = _reseal_plan(
            plan,
            forced_terminal_rows=(("authored-name", ("valid-reason",)),),
            forced_terminal_evidence_sha256=_sha(b"upstream"),
        )
        attested_job = replace(
            job,
            prepared_model_sha256=_sha(b"prepared-model"),
            model_preparation_evidence_sha256=_sha(b"preparation-proof"),
        )
        attested = _plan((attested_job,))

        for candidate in (malformed, bad_upstream, attested):
            with self.subTest(candidate=candidate.private_plan_sha256):
                with patch.object(preparation, "_owned_sibling") as owned_sibling:
                    with self.assertRaises(W3DJobPreparationError):
                        preflight_w3d_job_preparations(
                            candidate,
                            self.root,
                            execute_accounted_jobs=False,
                        )
                owned_sibling.assert_not_called()

        model_file.write_bytes(b"tampered model binding")
        with patch.object(preparation, "_owned_sibling") as owned_sibling:
            with self.assertRaisesRegex(
                W3DJobPreparationError,
                "model source does not match",
            ):
                preflight_w3d_job_preparations(
                    plan,
                    self.root,
                    execute_accounted_jobs=True,
                )
        owned_sibling.assert_not_called()

    def test_preflight_neutral_evidence_never_leaks_paths_names_or_exceptions(
        self,
    ) -> None:
        model = _model()
        hierarchy = _hierarchy()
        model_path = "inputs/SecretRetailModel.w3d"
        hierarchy_path = "inputs/SecretRetailSkeleton.w3d"
        self._write(model_path, model)
        self._write(hierarchy_path, hierarchy)
        job = _planned_job(
            0,
            model_path,
            model,
            hierarchy_path=hierarchy_path,
            hierarchy_bytes=hierarchy,
        )
        plan = _accounted_plan((job,), ())
        raw_error = f"raw failure SecretRetail at {self.root}"
        before = self._tree_snapshot()

        with patch.object(
            preparation,
            "strip_proven_redundant_secondary_skin_streams",
            side_effect=RuntimeError(raw_error),
        ):
            report = preflight_w3d_job_preparations(
                plan,
                self.root,
                execute_accounted_jobs=True,
            )

        encoded = json.dumps(report.neutral(), sort_keys=True)
        self.assertNotIn("SecretRetail", encoded)
        self.assertNotIn(str(self.root), encoded)
        self.assertNotIn(raw_error, encoded)
        self.assertNotIn(model_path, encoded)
        self.assertNotIn(hierarchy_path, encoded)
        self.assertEqual(report.rejected_preparation_count, 1)
        self.assertEqual(self._tree_snapshot(), before)

    def test_resolution_mode_forgery_is_rejected_before_preparation(self) -> None:
        model = _model()
        hierarchy = _hierarchy()
        model_path = "inputs/model.w3d"
        hierarchy_path = "inputs/skeleton.w3d"
        animation_path = "inputs/animation.w3d"
        animation = b"animation"
        self._write(model_path, model)
        self._write(hierarchy_path, hierarchy)
        self._write(animation_path, animation)
        base = _planned_job(
            0,
            model_path,
            model,
            preparation_kind=None,
            hierarchy_path=hierarchy_path,
            hierarchy_bytes=hierarchy,
        )
        animated = replace(
            base,
            asset_kind="animated",
            animations=(animation_path,),
            animation_source_ids=(_source_id("animation", 0),),
            animation_source_sha256s=(_sha(animation),),
            animation_hierarchy_resolution_modes=("sibling-path",),
        )
        same_bound_animation = replace(
            animated,
            animations=(hierarchy_path,),
            animation_source_ids=(base.hierarchy_source_id,),
            animation_source_sha256s=(base.hierarchy_source_sha256,),
            animation_hierarchy_resolution_modes=("sibling-path",),
        )
        partial_model_hierarchy = replace(
            animated,
            hierarchy=animated.model,
            hierarchy_source_id=animated.model_source_id,
            hierarchy_source_sha256=animated.model_source_sha256,
            hierarchy_resolution_mode="same-source",
        )
        partial_model_animation = replace(
            animated,
            animations=(animated.model,),
            animation_source_ids=(animated.model_source_id,),
            animation_source_sha256s=(animated.model_source_sha256,),
        )
        embedded_payload = hierarchy + model
        embedded_path = "inputs/embedded.w3d"
        self._write(embedded_path, embedded_payload)
        embedded = _planned_job(
            1,
            embedded_path,
            embedded_payload,
            preparation_kind=None,
            hierarchy_path=embedded_path,
        )
        cases = (
            replace(base, hierarchy_resolution_mode=None),
            replace(base, hierarchy_resolution_mode="same-source"),
            replace(embedded, hierarchy_resolution_mode="sibling-path"),
            replace(animated, animation_hierarchy_resolution_modes=()),
            replace(
                animated,
                animation_hierarchy_resolution_modes=("invented",),
            ),
            replace(
                animated,
                animation_hierarchy_resolution_modes=("same-source",),
            ),
            same_bound_animation,
            partial_model_hierarchy,
            partial_model_animation,
        )

        for candidate in cases:
            with self.subTest(candidate=candidate):
                with patch.object(preparation, "_owned_sibling") as owned_sibling:
                    with self.assertRaisesRegex(
                        W3DJobPreparationError,
                        "resolution",
                    ):
                        preflight_w3d_job_preparations(
                            _plan((candidate,)),
                            self.root,
                            execute_accounted_jobs=False,
                        )
                owned_sibling.assert_not_called()

    def test_exact_embedded_animation_without_preparation_is_byte_identical(
        self,
    ) -> None:
        payload = _hierarchy() + _static_model()
        path = "inputs/embedded-animation.w3d"
        model_file = self._write(path, payload)
        base = _planned_job(
            0,
            path,
            payload,
            preparation_kind=None,
            hierarchy_path=path,
        )
        job = replace(
            base,
            asset_kind="animated",
            animations=(path,),
            animation_source_ids=(base.model_source_id,),
            animation_source_sha256s=(base.model_source_sha256,),
            animation_hierarchy_resolution_modes=("same-source",),
        )
        plan = _plan((job,))
        before = self._tree_snapshot()

        with patch.object(preparation, "_owned_sibling") as owned_sibling:
            report = preflight_w3d_job_preparations(
                plan,
                self.root,
                execute_accounted_jobs=False,
            )
            attested = attest_w3d_job_preparations(plan, self.root)

        owned_sibling.assert_not_called()
        self.assertIs(attested, plan)
        self.assertEqual(report.declared_preparation_count, 0)
        self.assertEqual(report.forced_terminal_rows, ())
        self.assertEqual(model_file.read_bytes(), payload)
        self.assertEqual(self._tree_snapshot(), before)

    def test_preparation_required_embedded_animation_fails_before_writes(
        self,
    ) -> None:
        payload = _hierarchy() + _model()
        path = "inputs/embedded-preparation.w3d"
        model_file = self._write(path, payload)
        base = _planned_job(
            0,
            path,
            payload,
            hierarchy_path=path,
        )
        job = replace(
            base,
            asset_kind="animated",
            animations=(path,),
            animation_source_ids=(base.model_source_id,),
            animation_source_sha256s=(base.model_source_sha256,),
            animation_hierarchy_resolution_modes=("same-source",),
        )
        plan = _plan((job,))
        before = self._tree_snapshot()

        for operation in (
            lambda: preflight_w3d_job_preparations(
                plan,
                self.root,
                execute_accounted_jobs=False,
            ),
            lambda: attest_w3d_job_preparations(plan, self.root),
        ):
            with self.subTest(operation=operation):
                with (
                    patch.object(preparation, "_owned_sibling") as owned_sibling,
                    patch.object(preparation, "_write_staged_file") as write_staged,
                    patch.object(preparation, "_commit_replacements") as commit,
                    self.assertRaisesRegex(
                        W3DJobPreparationError,
                        "resolution contract",
                    ),
                ):
                    operation()
                owned_sibling.assert_not_called()
                write_staged.assert_not_called()
                commit.assert_not_called()

        self.assertEqual(model_file.read_bytes(), payload)
        self.assertEqual(self._tree_snapshot(), before)

    def test_preflight_merge_unions_overlap_and_binds_exact_upstream(self) -> None:
        rejected = _preparation_rejected_model()
        hierarchy = _hierarchy()
        model_path = "inputs/rejected.w3d"
        hierarchy_path = "inputs/skeleton.w3d"
        self._write(model_path, rejected)
        self._write(hierarchy_path, hierarchy)
        job = _planned_job(
            0,
            model_path,
            rejected,
            hierarchy_path=hierarchy_path,
            hierarchy_bytes=hierarchy,
        )
        upstream_reason = "texture-closure-unresolved-reference"
        upstream_rows = ((job.model_source_id, (upstream_reason,)),)
        upstream_seal = _sha(b"exact upstream texture bridge")
        plan = _reseal_plan(
            _accounted_plan((job,), ()),
            forced_terminal_rows=upstream_rows,
            forced_terminal_evidence_sha256=upstream_seal,
        )
        report = preflight_w3d_job_preparations(
            plan,
            self.root,
            execute_accounted_jobs=True,
        )

        expected_reasons = tuple(
            sorted((MODEL_PREPARATION_PROOF_REJECTED, upstream_reason))
        )
        expected_mapping = {job.model_source_id: expected_reasons}
        first_mapping, first_seal = report.merged_forced_terminals()
        explicit_mapping, explicit_seal = report.merged_forced_terminals(
            {job.model_source_id: (upstream_reason,)},
            upstream_seal,
        )
        second_mapping, second_seal = report.merged_forced_terminals()

        self.assertEqual(first_mapping, expected_mapping)
        self.assertEqual(explicit_mapping, expected_mapping)
        self.assertEqual(second_mapping, expected_mapping)
        self.assertEqual(first_seal, explicit_seal)
        self.assertEqual(first_seal, second_seal)
        self.assertRegex(first_seal, r"^[0-9a-f]{64}$")
        merge_basis = {
            "schema": W3D_JOB_PREPARATION_FORCED_TERMINAL_SCHEMA,
            "schemaVersion": W3D_JOB_PREPARATION_FORCED_TERMINAL_VERSION,
            "upstream": {
                "evidenceSha256": upstream_seal,
                "forcedTerminalReasons": [
                    {
                        "sourceId": job.model_source_id,
                        "reasonCodes": [upstream_reason],
                    }
                ],
            },
            "preparationPreflight": report.neutral(),
            "mergedForcedTerminalReasons": [
                {
                    "sourceId": job.model_source_id,
                    "reasonCodes": list(expected_reasons),
                }
            ],
        }
        self.assertEqual(first_seal, _sha(_canonical(merge_basis)))
        self.assertEqual(report.upstream_forced_terminal_rows, upstream_rows)
        self.assertEqual(
            report.upstream_forced_terminal_evidence_sha256,
            upstream_seal,
        )

    def test_preflight_merge_rejects_tampered_or_malformed_upstream(self) -> None:
        rejected = _preparation_rejected_model()
        hierarchy = _hierarchy()
        model_path = "inputs/rejected.w3d"
        hierarchy_path = "inputs/skeleton.w3d"
        self._write(model_path, rejected)
        self._write(hierarchy_path, hierarchy)
        job = _planned_job(
            0,
            model_path,
            rejected,
            hierarchy_path=hierarchy_path,
            hierarchy_bytes=hierarchy,
        )
        upstream_reason = "texture-closure-unresolved-reference"
        upstream_seal = _sha(b"exact upstream texture bridge")
        plan = _reseal_plan(
            _accounted_plan((job,), ()),
            forced_terminal_rows=((job.model_source_id, (upstream_reason,)),),
            forced_terminal_evidence_sha256=upstream_seal,
        )
        report = preflight_w3d_job_preparations(
            plan,
            self.root,
            execute_accounted_jobs=True,
        )
        cases = (
            ({job.model_source_id: ("different-reason",)}, upstream_seal),
            ({job.model_source_id: (upstream_reason,)}, "f" * 64),
            ({"authored-source-name": (upstream_reason,)}, upstream_seal),
            ({job.model_source_id: ("Bad Reason",)}, upstream_seal),
            ({job.model_source_id: (upstream_reason,)}, upstream_seal.upper()),
        )

        for mapping, seal in cases:
            with self.subTest(mapping=mapping, seal=seal):
                with self.assertRaises(W3DJobPreparationError):
                    report.merged_forced_terminals(mapping, seal)

    def test_animation_mutation_is_rejected_before_preparation_writes(self) -> None:
        model = _model()
        hierarchy = _hierarchy()
        animation = b"source-bound animation"
        model_path = "inputs/model.w3d"
        hierarchy_path = "inputs/shared-skeleton.w3d"
        animation_path = "inputs/idle.w3d"
        model_file = self._write(model_path, model)
        self._write(hierarchy_path, hierarchy)
        animation_file = self._write(animation_path, animation)
        job = replace(
            _planned_job(
                0,
                model_path,
                model,
                hierarchy_path=hierarchy_path,
                hierarchy_bytes=hierarchy,
            ),
            asset_kind="animated",
            animations=(animation_path,),
            animation_source_ids=(_source_id("animation", 0),),
            animation_source_sha256s=(_sha(animation),),
            animation_hierarchy_resolution_modes=("sibling-path",),
        )
        plan = _plan((job,))
        before = model_file.read_bytes()
        animation_file.write_bytes(b"mutated animation")

        with self.assertRaisesRegex(
            W3DJobPreparationError,
            "animation source does not match its SHA-256",
        ):
            attest_w3d_job_preparations(plan, self.root)

        self.assertEqual(model_file.read_bytes(), before)
        self.assertFalse(
            any(
                path.name.startswith(".openbfme-w3d-preparation")
                for path in self.root.parent.iterdir()
            )
        )

    def test_default_policy_matches_explicit_false_bytes_and_hashes(self) -> None:
        model = _model()
        hierarchy = _hierarchy()
        model_path = "inputs/model.w3d"
        hierarchy_path = "inputs/shared-skeleton.w3d"
        self._write(model_path, model)
        self._write(hierarchy_path, hierarchy)
        second_root = Path(self.temporary.name) / "explicit-false-root"
        second_root.mkdir()
        for relative_path, payload in (
            (model_path, model),
            (hierarchy_path, hierarchy),
        ):
            path = second_root.joinpath(*relative_path.split("/"))
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(payload)
        plan = _plan(
            (
                _planned_job(
                    0,
                    model_path,
                    model,
                    hierarchy_path=hierarchy_path,
                    hierarchy_bytes=hierarchy,
                ),
            )
        )

        implicit = attest_w3d_job_preparations(plan, self.root)
        explicit = attest_w3d_job_preparations(
            plan,
            second_root,
            execute_accounted_jobs=False,
        )

        self.assertEqual(implicit, explicit)
        self.assertEqual(implicit.private_plan_sha256, explicit.private_plan_sha256)
        self.assertEqual(implicit.evidence_sha256, explicit.evidence_sha256)
        self.assertEqual(
            self.root.joinpath(*model_path.split("/")).read_bytes(),
            second_root.joinpath(*model_path.split("/")).read_bytes(),
        )

    def test_terminal_plan_is_rejected_by_default_before_writes(self) -> None:
        payload = _static_model()
        path = "inputs/static.w3d"
        model_file = self._write(path, payload)
        plan = _accounted_plan(
            (
                _planned_job(
                    0,
                    path,
                    payload,
                    preparation_kind=None,
                    hierarchy_path=None,
                ),
            ),
            (_terminal(0),),
        )
        evidence_sha256 = plan.evidence_sha256

        with patch.object(preparation, "_owned_sibling") as owned_sibling:
            with self.assertRaisesRegex(
                W3DJobPreparationError,
                "terminals or incomplete source accounting",
            ):
                attest_w3d_job_preparations(plan, self.root)

        owned_sibling.assert_not_called()
        self.assertEqual(model_file.read_bytes(), payload)
        self.assertEqual(plan.evidence_sha256, evidence_sha256)

    def test_accounted_jobs_prepare_only_planned_models_and_are_idempotent(
        self,
    ) -> None:
        model = _model()
        hierarchy = _hierarchy()
        model_path = "inputs/planned-model.w3d"
        hierarchy_path = "inputs/shared-skeleton.w3d"
        model_file = self._write(model_path, model)
        hierarchy_file = self._write(hierarchy_path, hierarchy)
        unplanned_path = "inputs/terminal-model.w3d"
        unplanned_payload = _model()
        unplanned_file = self._write(unplanned_path, unplanned_payload)
        plan = _accounted_plan(
            (
                _planned_job(
                    0,
                    model_path,
                    model,
                    hierarchy_path=hierarchy_path,
                    hierarchy_bytes=hierarchy,
                ),
            ),
            (_terminal(0),),
        )
        manifests_before = tuple(batch.manifest_bytes() for batch in plan.batches)

        first = attest_w3d_job_preparations(
            plan,
            self.root,
            execute_accounted_jobs=True,
        )

        self.assertNotEqual(model_file.read_bytes(), model)
        self.assertEqual(hierarchy_file.read_bytes(), hierarchy)
        self.assertEqual(unplanned_file.read_bytes(), unplanned_payload)
        self.assertEqual(first.terminals, plan.terminals)
        self.assertIs(first.terminals, plan.terminals)
        self.assertEqual(first.source_count, plan.source_count)
        self.assertEqual(first.consumed_source_count, plan.consumed_source_count)
        self.assertEqual(
            [terminal.neutral() for terminal in first.terminals],
            [terminal.neutral() for terminal in plan.terminals],
        )
        self.assertEqual(
            tuple(batch.manifest_bytes() for batch in first.batches),
            manifests_before,
        )

        second = attest_w3d_job_preparations(
            first,
            self.root,
            execute_accounted_jobs=True,
        )

        self.assertIs(second, first)
        self.assertEqual(second.private_plan_sha256, first.private_plan_sha256)
        self.assertEqual(second.evidence_sha256, first.evidence_sha256)
        self.assertEqual(unplanned_file.read_bytes(), unplanned_payload)

    def test_accounted_job_gaps_overlaps_and_duplicates_precede_writes(
        self,
    ) -> None:
        model = _model()
        hierarchy = _hierarchy()
        model_path = "inputs/model.w3d"
        hierarchy_path = "inputs/shared-skeleton.w3d"
        model_file = self._write(model_path, model)
        self._write(hierarchy_path, hierarchy)
        jobs = (
            _planned_job(
                0,
                model_path,
                model,
                hierarchy_path=hierarchy_path,
                hierarchy_bytes=hierarchy,
            ),
        )
        plan = _accounted_plan(jobs, (_terminal(0),))
        gap = _reseal_plan(plan, source_count=plan.source_count + 1)
        overlap = _reseal_plan(
            plan,
            terminals=(
                replace(
                    plan.terminals[0],
                    source_id=plan.jobs[0].model_source_id,
                ),
            ),
        )
        bad_consumed_count = _reseal_plan(
            plan,
            consumed_source_count=plan.consumed_source_count + 1,
            source_count=plan.source_count + 1,
        )
        duplicate_base = _accounted_plan(jobs, (_terminal(0), _terminal(1)))
        duplicate = _reseal_plan(
            duplicate_base,
            terminals=(
                duplicate_base.terminals[0],
                replace(
                    duplicate_base.terminals[1],
                    source_id=duplicate_base.terminals[0].source_id,
                ),
            ),
        )
        cases = (
            (gap, "source accounting is not exact"),
            (overlap, "overlap the terminal inventory"),
            (bad_consumed_count, "consumed source IDs are not exact"),
            (duplicate, "duplicate terminal source IDs"),
        )

        for candidate, error in cases:
            with self.subTest(error=error):
                with patch.object(preparation, "_owned_sibling") as owned_sibling:
                    with self.assertRaisesRegex(W3DJobPreparationError, error):
                        attest_w3d_job_preparations(
                            candidate,
                            self.root,
                            execute_accounted_jobs=True,
                        )
                owned_sibling.assert_not_called()
                self.assertEqual(model_file.read_bytes(), model)
        self.assertFalse(list(self.root.parent.glob(".openbfme-w3d-preparation-*")))

    def test_accounted_job_policy_requires_at_least_one_planned_job(self) -> None:
        plan = _accounted_plan((), (_terminal(0),))

        with patch.object(preparation, "_owned_sibling") as owned_sibling:
            with self.assertRaisesRegex(
                W3DJobPreparationError,
                "at least one planned job",
            ):
                attest_w3d_job_preparations(
                    plan,
                    self.root,
                    execute_accounted_jobs=True,
                )

        owned_sibling.assert_not_called()
        self.assertFalse(list(self.root.parent.glob(".openbfme-w3d-preparation-*")))

    def test_multi_job_proof_failure_changes_nothing(self) -> None:
        hierarchy = _hierarchy()
        hierarchy_path = "inputs/shared-skeleton.w3d"
        self._write(hierarchy_path, hierarchy)
        valid = _model()
        invalid = _model(secondary_vertices=((1.0, 3.0, 4.0), (30.0, 3.0, 7.0)))
        first_path = "inputs/first.w3d"
        second_path = "inputs/second.w3d"
        first = self._write(first_path, valid)
        second = self._write(second_path, invalid)
        plan = _plan(
            (
                _planned_job(
                    0,
                    first_path,
                    valid,
                    hierarchy_path=hierarchy_path,
                    hierarchy_bytes=hierarchy,
                ),
                _planned_job(
                    1,
                    second_path,
                    invalid,
                    hierarchy_path=hierarchy_path,
                    hierarchy_bytes=hierarchy,
                ),
            )
        )

        with self.assertRaisesRegex(
            W3DJobPreparationError,
            "preparation proof failed",
        ):
            attest_w3d_job_preparations(plan, self.root)

        self.assertEqual(first.read_bytes(), valid)
        self.assertEqual(second.read_bytes(), invalid)
        self.assertFalse(list(self.root.parent.glob(".openbfme-w3d-preparation-*")))

    def test_commit_failure_rolls_back_every_replacement(self) -> None:
        hierarchy = _hierarchy()
        hierarchy_path = "inputs/shared-skeleton.w3d"
        self._write(hierarchy_path, hierarchy)
        model = _model()
        paths = ("inputs/first.w3d", "inputs/second.w3d")
        files = tuple(self._write(path, model) for path in paths)
        plan = _plan(
            tuple(
                _planned_job(
                    index,
                    path,
                    model,
                    hierarchy_path=hierarchy_path,
                    hierarchy_bytes=hierarchy,
                )
                for index, path in enumerate(paths)
            )
        )
        real_replace = os.replace
        calls = 0

        def fail_fourth(source: Path | str, target: Path | str) -> None:
            nonlocal calls
            calls += 1
            if calls == 4:
                raise OSError("injected")
            real_replace(source, target)

        with patch.object(preparation.os, "replace", side_effect=fail_fourth):
            with self.assertRaisesRegex(W3DJobPreparationError, "rolled back"):
                attest_w3d_job_preparations(plan, self.root)

        self.assertEqual(tuple(path.read_bytes() for path in files), (model, model))

    def test_tamper_is_rejected_without_leaking_paths_or_job_ids(self) -> None:
        model = _model()
        hierarchy = _hierarchy()
        model_path = "inputs/SecretRetailModel.w3d"
        hierarchy_path = "inputs/SecretRetailSkeleton.w3d"
        model_file = self._write(model_path, model)
        self._write(hierarchy_path, hierarchy)
        plan = _plan(
            (
                _planned_job(
                    0,
                    model_path,
                    model,
                    hierarchy_path=hierarchy_path,
                    hierarchy_bytes=hierarchy,
                ),
            )
        )
        attested = attest_w3d_job_preparations(plan, self.root)
        model_file.write_bytes(model_file.read_bytes() + b"tamper")

        with self.assertRaises(W3DJobPreparationError) as raised:
            attest_w3d_job_preparations(attested, self.root)

        message = str(raised.exception)
        self.assertNotIn(str(self.root), message)
        self.assertNotIn("SecretRetail", message)
        self.assertNotIn(plan.jobs[0].job_id, message)

    def test_repeated_attestation_reuses_prepared_bytes_and_hashes(self) -> None:
        embedded = _hierarchy() + _model()
        model_path = "inputs/embedded.w3d"
        model_file = self._write(model_path, embedded)
        plan = _plan(
            (
                _planned_job(
                    0,
                    model_path,
                    embedded,
                    hierarchy_path=model_path,
                    hierarchy_bytes=None,
                ),
            )
        )
        first = attest_w3d_job_preparations(plan, self.root)
        first_bytes = model_file.read_bytes()

        second = attest_w3d_job_preparations(first, self.root)

        self.assertIs(second, first)
        self.assertEqual(model_file.read_bytes(), first_bytes)
        self.assertEqual(second.private_plan_sha256, first.private_plan_sha256)
        self.assertEqual(second.evidence_sha256, first.evidence_sha256)

    def test_partial_unsupported_and_shared_preparations_fail_closed(self) -> None:
        model = _model()
        hierarchy = _hierarchy()
        hierarchy_path = "inputs/skeleton.w3d"
        model_path = "inputs/model.w3d"
        self._write(model_path, model)
        self._write(hierarchy_path, hierarchy)
        base = _planned_job(
            0,
            model_path,
            model,
            hierarchy_path=hierarchy_path,
            hierarchy_bytes=hierarchy,
        )
        cases = (
            replace(base, prepared_model_sha256="0" * 64),
            replace(base, model_preparation="unproved-rewrite"),
        )
        for job in cases:
            with self.subTest(kind=job.model_preparation):
                with self.assertRaises(W3DJobPreparationError):
                    attest_w3d_job_preparations(_plan((job,)), self.root)

        duplicate = replace(
            base,
            job_id=f"w3d-{'1' * 40}",
            definition_sha256="1" * 64,
            output=f"glb/w3d-{'1' * 40}.glb",
        )
        with self.assertRaisesRegex(
            W3DJobPreparationError,
            "duplicate or conflicting",
        ):
            attest_w3d_job_preparations(_plan((base, duplicate)), self.root)

    def test_hard_linked_job_file_is_rejected_before_changes(self) -> None:
        payload = _static_model()
        path = "inputs/static.w3d"
        model_file = self._write(path, payload)
        external = self.root.parent / "external.w3d"
        os.link(model_file, external)
        plan = _plan(
            (
                _planned_job(
                    0,
                    path,
                    payload,
                    preparation_kind=None,
                    hierarchy_path=None,
                ),
            )
        )

        with self.assertRaisesRegex(W3DJobPreparationError, "non-ordinary"):
            attest_w3d_job_preparations(plan, self.root)


if __name__ == "__main__":
    unittest.main()
