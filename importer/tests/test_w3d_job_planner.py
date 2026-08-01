from __future__ import annotations

from dataclasses import replace
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys
import types
import unittest

from openbfme_importer.w3d_catalog import scan_w3d_catalog
from openbfme_importer.w3d_job_planner import (
    PRESENTATION_POLICY_PRESERVE_ALL,
    SECONDARY_SKIN_PREPARATION,
    plan_w3d_batches,
    w3d_job_is_exact_embedded_model_animation,
    w3d_job_resolution_contract_is_valid,
)


def _load_batch_adapter():
    path = Path(__file__).parents[1] / "blender" / "w3d_batch_to_glb.py"
    name = "openbfme_test_w3d_job_planner_batch"
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError("could not load W3D batch adapter")
    module = importlib.util.module_from_spec(spec)
    previous_module = sys.modules.get(name)
    previous_bpy = sys.modules.get("bpy")
    sys.modules[name] = module
    sys.modules["bpy"] = types.ModuleType("bpy")
    try:
        spec.loader.exec_module(module)
    finally:
        if previous_module is None:
            sys.modules.pop(name, None)
        else:
            sys.modules[name] = previous_module
        if previous_bpy is None:
            sys.modules.pop("bpy", None)
        else:
            sys.modules["bpy"] = previous_bpy
    return module


BATCH = _load_batch_adapter()


def _fixed(value: str, size: int) -> bytes:
    encoded = value.encode("ascii")
    if len(encoded) >= size:
        raise ValueError("fixture identifier is too long")
    return encoded + b"\0" * (size - len(encoded))


def _chunk(
    chunk_id: int,
    payload: bytes,
    *,
    children: bool = False,
    declared_size: int | None = None,
) -> bytes:
    size = len(payload) if declared_size is None else declared_size
    return (
        struct.pack("<II", chunk_id, size | (0x80000000 if children else 0)) + payload
    )


def _mesh(
    container: str,
    mesh: str = "BODY",
    *,
    face_count: int = 1,
    vertex_count: int = 3,
    attributes: int = 0x0002A000,
) -> bytes:
    header = struct.pack(
        "<II16s16s9I10f",
        0x00040002,
        attributes,
        _fixed(mesh, 16),
        _fixed(container, 16),
        face_count,
        vertex_count,
        1,
        1,
        0,
        0,
        0,
        0,
        0,
        *[float(value) for value in range(10)],
    )
    return _chunk(0x1F, header) + _chunk(0x02, b"\0" * 36) + _chunk(0x20, b"\0" * 32)


def _secondary_skin_model(name: str, hierarchy: str = "") -> bytes:
    """A model whose single mesh carries valid dual-bone skin streams.

    The metadata scanner decodes ``vertices-2``/``normals-2`` fail-closed, so
    planner fixtures must supply the retail shape: streams inside a mesh
    container whose header vertex count matches the stream record count
    (``_mesh`` declares 3 vertices; each stream carries exactly 3 records).
    """

    header = struct.pack(
        "<II16s16s",
        0x00040001,
        1,
        _fixed(name, 16),
        _fixed(hierarchy, 16),
    )
    stream = struct.pack("<9f", *[0.25] * 9)
    return _chunk(0x700, _chunk(0x701, header), children=True) + _chunk(
        0x00000000,
        _mesh(name)
        + _chunk(0x00000C00, stream)
        + _chunk(0x00000C01, stream),
        children=True,
    )


def _model_reference(identifier: str, *, role: str, bone_index: int = 0) -> bytes:
    parent = {
        "lod": 0x00000702,
        "aggregate": 0x00000705,
        "proxy": 0x00000706,
    }[role]
    reference = struct.pack("<I32s", bone_index, _fixed(identifier, 32))
    return _chunk(parent, _chunk(0x00000704, reference), children=True)


def _model(
    name: str,
    hierarchy: str = "",
    *,
    meshes: int = 1,
    mesh_attributes: int = 0x0002A000,
) -> bytes:
    header = struct.pack(
        "<II16s16s",
        0x00040001,
        1,
        _fixed(name, 16),
        _fixed(hierarchy, 16),
    )
    mesh_payload = b"".join(
        _mesh(
            name,
            "BODY" if index == 0 else f"PART{index}",
            attributes=mesh_attributes,
        )
        for index in range(meshes)
    )
    return _chunk(0x700, _chunk(0x701, header), children=True) + mesh_payload


def _hierarchy(name: str, *, pivot_count: int = 1, include_pivot: bool = True) -> bytes:
    header = struct.pack(
        "<I16sI3f",
        0x00040001,
        _fixed(name, 16),
        pivot_count,
        0.0,
        0.0,
        0.0,
    )
    pivot = struct.pack("<16si", _fixed("ROOT", 16), -1) + b"\0" * 40
    return _chunk(
        0x100,
        _chunk(0x101, header) + (_chunk(0x102, pivot) if include_pivot else b""),
        children=True,
    )


def _animation(
    name: str,
    hierarchy: str,
    *,
    frame_count: int = 10,
    frame_rate: int = 30,
    include_channel: bool = True,
) -> bytes:
    header = struct.pack(
        "<I16s16sII",
        0x00040001,
        _fixed(name, 16),
        _fixed(hierarchy, 16),
        frame_count,
        frame_rate,
    )
    return _chunk(
        0x200,
        _chunk(0x201, header) + (_chunk(0x202, b"channel") if include_channel else b""),
        children=True,
    )


def _embedded_model_animation(name: str, hierarchy: str) -> bytes:
    return (
        _model(name, hierarchy) + _hierarchy(hierarchy) + _animation("IDLE", hierarchy)
    )


def _mapping(values: dict[str, bytes]) -> dict[str, str]:
    return {
        path: f"staged/{index:04d}.w3d"
        for index, path in enumerate(sorted(values, key=str.casefold))
    }


def _reason_map(plan) -> dict[str, tuple[str, ...]]:
    return {item.source_id: item.reason_codes for item in plan.terminals}


class W3DJobPlannerTests(unittest.TestCase):
    def test_single_root_unskinned_lod_hierarchy_proves_root_rigid_bake(
        self,
    ) -> None:
        values = {
            "models/root-rigid.w3d": (
                _model("RootRigid", "RootRig", mesh_attributes=0x0000A000)
                + _model_reference("RootRigid.BODY", role="lod")
            ),
            "models/RootRig.w3d": _hierarchy("RootRig"),
        }
        report = scan_w3d_catalog(values)
        plan = plan_w3d_batches(report, _mapping(values), index=report.index)

        self.assertEqual(len(plan.jobs), 1)
        self.assertTrue(plan.jobs[0].proven_root_rigid_bake)
        self.assertTrue(plan.jobs[0].neutral()["provenRootRigidBake"])
        parsed = BATCH.parse_manifest_document(plan.batches[0].manifest_document())
        self.assertEqual(len(parsed), 1)
        self.assertTrue(parsed[0].proven_root_rigid_bake)

    def test_secondary_skin_chunks_require_attested_preparation_without_entering_manifest(
        self,
    ) -> None:
        values = {
            "models/skin.w3d": _secondary_skin_model("Skin", "Rig"),
            "models/Rig.w3d": _hierarchy("Rig"),
        }
        report = scan_w3d_catalog(values)
        plan = plan_w3d_batches(report, _mapping(values), index=report.index)

        self.assertTrue(plan.source_accounting_complete)
        self.assertEqual(len(plan.jobs), 1)
        job = plan.jobs[0]
        self.assertEqual(job.model_preparation, SECONDARY_SKIN_PREPARATION)
        self.assertIsNone(job.prepared_model_sha256)
        self.assertIsNone(job.model_preparation_evidence_sha256)
        self.assertEqual(job.hierarchy_source_id is not None, True)
        self.assertEqual(
            job.hierarchy_source_sha256,
            hashlib.sha256(values["models/Rig.w3d"]).hexdigest(),
        )
        self.assertNotIn("model_preparation", job.manifest_row())
        parsed = BATCH.parse_manifest_document(plan.batches[0].manifest_document())
        self.assertEqual(len(parsed), 1)

    def test_secondary_skin_preparation_without_a_hierarchy_is_terminal(self) -> None:
        values = {"models/skin.w3d": _secondary_skin_model("Skin")}
        report = scan_w3d_catalog(values)
        plan = plan_w3d_batches(report, _mapping(values), index=report.index)

        self.assertFalse(plan.jobs)
        self.assertEqual(len(plan.terminals), 1)
        self.assertIn(
            "model-preparation-hierarchy-association-missing",
            plan.terminals[0].reason_codes,
        )

    def test_static_hierarchical_and_animated_unique_closures_match_adapter(
        self,
    ) -> None:
        values = {
            "Retail/StaticSecret.w3d": _model("StaticSecret"),
            "Retail/HierModel.w3d": _model("HierModel", "HierRig"),
            "Retail/HierRig.w3d": _hierarchy("HierRig"),
            "Retail/AnimModel.w3d": _model("AnimModel", "AnimRig"),
            "Retail/AnimRig.w3d": _hierarchy("AnimRig"),
            "Retail/AnimIdle.w3d": _animation("AnimIdle", "AnimRig"),
        }
        report = scan_w3d_catalog(values)
        plan = plan_w3d_batches(report, _mapping(values), index=report.index)

        self.assertTrue(plan.source_accounting_complete)
        self.assertEqual(plan.source_count, 6)
        self.assertEqual(plan.consumed_source_count, 6)
        self.assertEqual(
            sorted(job.asset_kind for job in plan.jobs),
            ["animated", "hierarchical", "static"],
        )
        self.assertEqual(len(plan.batches), 1)
        parsed = BATCH.parse_manifest_document(plan.batches[0].manifest_document())
        self.assertEqual(len(parsed), 3)
        for job in parsed:
            self.assertEqual(job.required_equipment, ())
            self.assertEqual(job.excluded_optional_meshes, ())
            self.assertFalse(job.proven_root_rigid_bake)
        animated = next(job for job in parsed if job.asset_kind == "animated")
        self.assertEqual(len(animated.animations), 1)
        planned_animated = next(
            job for job in plan.jobs if job.asset_kind == "animated"
        )
        expected_animation_sha256 = hashlib.sha256(
            values["Retail/AnimIdle.w3d"]
        ).hexdigest()
        self.assertEqual(
            planned_animated.animation_source_sha256s,
            (expected_animation_sha256,),
        )
        self.assertEqual(
            planned_animated.neutral()["animationSourceSha256s"],
            [expected_animation_sha256],
        )
        expected_definition = (
            json.dumps(
                {
                    "assetKind": "animated",
                    "model": {
                        "sourceId": planned_animated.model_source_id,
                        "sourceSha256": planned_animated.model_source_sha256,
                    },
                    "hierarchySourceId": planned_animated.hierarchy_source_id,
                    "modelPreparation": None,
                    "animations": [
                        {
                            "sourceId": planned_animated.animation_source_ids[0],
                            "sourceSha256": expected_animation_sha256,
                        }
                    ],
                },
                ensure_ascii=False,
                sort_keys=True,
                separators=(",", ":"),
            )
            + "\n"
        ).encode("utf-8")
        self.assertEqual(
            planned_animated.definition_sha256,
            hashlib.sha256(expected_definition).hexdigest(),
        )
        self.assertEqual(
            planned_animated.neutral()["hierarchyResolutionMode"],
            "sibling-path",
        )
        self.assertEqual(
            planned_animated.neutral()["animationHierarchyResolutionModes"],
            ["sibling-path"],
        )

    def test_shared_hierarchy_fans_out_with_unique_consumption_and_stable_order(
        self,
    ) -> None:
        entries = [
            ("units/one.w3d", _model("One", "Rig")),
            ("units/two.w3d", _model("Two", "Rig")),
            ("units/Rig.w3d", _hierarchy("Rig")),
            ("units/idle.w3d", _animation("Idle", "Rig")),
            ("units/walk.w3d", _animation("Walk", "Rig")),
        ]
        values = dict(entries)
        staged = _mapping(values)
        first_report = scan_w3d_catalog(entries)
        second_report = scan_w3d_catalog(reversed(entries))

        first = plan_w3d_batches(first_report, staged, index=first_report.index)
        second = plan_w3d_batches(
            second_report,
            dict(reversed(tuple(staged.items()))),
            index=second_report.index,
        )

        self.assertEqual(first.neutral(), second.neutral())
        self.assertEqual(first.source_count, 5)
        self.assertEqual(first.consumed_source_count, 5)
        self.assertTrue(first.source_accounting_complete)
        self.assertEqual(len(first.jobs), 2)
        self.assertEqual(
            len({job.hierarchy_source_id for job in first.jobs}),
            1,
        )
        self.assertEqual(
            len({job.animation_source_ids for job in first.jobs}),
            1,
        )
        self.assertEqual(
            {job.hierarchy_resolution_mode for job in first.jobs},
            {"sibling-path"},
        )
        self.assertEqual(
            {job.animation_hierarchy_resolution_modes for job in first.jobs},
            {("sibling-path", "sibling-path")},
        )
        self.assertEqual(
            [batch.manifest_document() for batch in first.batches],
            [batch.manifest_document() for batch in second.batches],
        )

    def test_forced_model_does_not_poison_other_shared_hierarchy_owner(self) -> None:
        values = {
            "units/one.w3d": _model("One", "Rig"),
            "units/two.w3d": _model("Two", "Rig"),
            "units/Rig.w3d": _hierarchy("Rig"),
            "units/idle.w3d": _animation("Idle", "Rig"),
        }
        report = scan_w3d_catalog(values)
        staged = _mapping(values)
        baseline = plan_w3d_batches(report, staged, index=report.index)
        forced_job = next(
            job
            for job in baseline.jobs
            if job.model_source_sha256
            == hashlib.sha256(values["units/one.w3d"]).hexdigest()
        )

        plan = plan_w3d_batches(
            report,
            staged,
            index=report.index,
            forced_terminal_reasons={forced_job.model_source_id: ("decode-terminal",)},
            forced_terminal_evidence_sha256="a" * 64,
        )

        self.assertEqual(len(plan.jobs), 1)
        self.assertEqual(
            plan.jobs[0].model_source_sha256,
            hashlib.sha256(values["units/two.w3d"]).hexdigest(),
        )
        self.assertEqual(plan.consumed_source_count, 3)
        self.assertEqual(len(plan.terminals), 1)
        self.assertEqual(
            _reason_map(plan)[forced_job.model_source_id],
            ("decode-terminal",),
        )
        self.assertEqual(
            plan.consumed_source_count + len(plan.terminals),
            plan.source_count,
        )

    def test_forced_shared_hierarchy_or_clip_fails_all_dependents(self) -> None:
        values = {
            "units/one.w3d": _model("One", "Rig"),
            "units/two.w3d": _model("Two", "Rig"),
            "units/Rig.w3d": _hierarchy("Rig"),
            "units/idle.w3d": _animation("Idle", "Rig"),
        }
        report = scan_w3d_catalog(values)
        staged = _mapping(values)
        baseline = plan_w3d_batches(report, staged, index=report.index)
        model_source_ids = {job.model_source_id for job in baseline.jobs}
        hierarchy_source_id = baseline.jobs[0].hierarchy_source_id
        clip_source_id = baseline.jobs[0].animation_source_ids[0]
        assert hierarchy_source_id is not None

        for dependency_source_id, dependent_reason in (
            (hierarchy_source_id, "hierarchy-source-not-convertible"),
            (clip_source_id, "animation-closure-ambiguous"),
        ):
            with self.subTest(dependency_source_id=dependency_source_id):
                plan = plan_w3d_batches(
                    report,
                    staged,
                    index=report.index,
                    forced_terminal_reasons={
                        dependency_source_id: ("upstream-decode-terminal",)
                    },
                    forced_terminal_evidence_sha256="b" * 64,
                )
                reasons = _reason_map(plan)

                self.assertFalse(plan.jobs)
                for model_source_id in model_source_ids:
                    self.assertIn(dependent_reason, reasons[model_source_id])
                self.assertEqual(
                    reasons[dependency_source_id],
                    ("upstream-decode-terminal",),
                )
                self.assertEqual(
                    plan.consumed_source_count + len(plan.terminals),
                    plan.source_count,
                )

    def test_same_named_sibling_groups_never_cross_attach_between_directories(
        self,
    ) -> None:
        values = {
            "first/one.w3d": _model("One", "Rig"),
            "first/Rig.w3d": _hierarchy("FirstRig"),
            "first/idle.w3d": _animation("FirstIdle", "Rig"),
            "second/two.w3d": _model("Two", "Rig"),
            "second/rIG.W3D": _hierarchy("SecondRig"),
            "second/walk.w3d": _animation("SecondWalk", "Rig"),
        }
        report = scan_w3d_catalog(values)
        plan = plan_w3d_batches(report, _mapping(values), index=report.index)
        jobs_by_model_sha = {job.model_source_sha256: job for job in plan.jobs}
        first = jobs_by_model_sha[hashlib.sha256(values["first/one.w3d"]).hexdigest()]
        second = jobs_by_model_sha[hashlib.sha256(values["second/two.w3d"]).hexdigest()]

        self.assertTrue(plan.source_accounting_complete)
        self.assertEqual(len(plan.jobs), 2)
        self.assertNotEqual(first.hierarchy_source_id, second.hierarchy_source_id)
        self.assertEqual(
            first.hierarchy_source_sha256,
            hashlib.sha256(values["first/Rig.w3d"]).hexdigest(),
        )
        self.assertEqual(
            second.hierarchy_source_sha256,
            hashlib.sha256(values["second/rIG.W3D"]).hexdigest(),
        )
        self.assertEqual(
            first.animation_source_sha256s,
            (hashlib.sha256(values["first/idle.w3d"]).hexdigest(),),
        )
        self.assertEqual(
            second.animation_source_sha256s,
            (hashlib.sha256(values["second/walk.w3d"]).hexdigest(),),
        )

    def test_complete_same_source_hierarchy_precedes_sibling_lookup(self) -> None:
        values = {
            "units/model.w3d": _model("Model", "Rig") + _hierarchy("Rig"),
            "units/Rig.w3d": _hierarchy("OtherRig"),
        }
        report = scan_w3d_catalog(values)
        plan = plan_w3d_batches(report, _mapping(values), index=report.index)

        self.assertEqual(len(plan.jobs), 1)
        job = plan.jobs[0]
        self.assertEqual(job.model_source_id, job.hierarchy_source_id)
        self.assertEqual(job.hierarchy_resolution_mode, "same-source")
        self.assertEqual(
            job.hierarchy_source_sha256,
            hashlib.sha256(values["units/model.w3d"]).hexdigest(),
        )
        self.assertEqual(plan.consumed_source_count, 1)
        self.assertEqual(len(plan.terminals), 1)

    def test_exact_embedded_model_animation_is_one_deterministic_consumed_source(
        self,
    ) -> None:
        entries = [
            (
                "units/embedded.w3d",
                _embedded_model_animation("Embedded", "EmbeddedRig"),
            )
        ]
        values = dict(entries)
        staged = _mapping(values)
        first_report = scan_w3d_catalog(entries)
        second_report = scan_w3d_catalog(reversed(entries))

        first = plan_w3d_batches(first_report, staged, index=first_report.index)
        second = plan_w3d_batches(
            second_report,
            dict(reversed(tuple(staged.items()))),
            index=second_report.index,
        )

        self.assertEqual(first.neutral(), second.neutral())
        self.assertTrue(first.source_accounting_complete)
        self.assertEqual(first.source_count, 1)
        self.assertEqual(first.consumed_source_count, 1)
        self.assertEqual(len(first.jobs), 1)
        self.assertFalse(first.terminals)
        job = first.jobs[0]
        self.assertTrue(w3d_job_is_exact_embedded_model_animation(job))
        self.assertTrue(w3d_job_resolution_contract_is_valid(job))
        self.assertEqual(job.asset_kind, "animated")
        self.assertEqual(len(job.animations), 1)
        self.assertEqual(
            (job.model, job.model_source_id, job.model_source_sha256),
            (job.hierarchy, job.hierarchy_source_id, job.hierarchy_source_sha256),
        )
        self.assertEqual(
            (job.model, job.model_source_id, job.model_source_sha256),
            (
                job.animations[0],
                job.animation_source_ids[0],
                job.animation_source_sha256s[0],
            ),
        )
        self.assertEqual(job.hierarchy_resolution_mode, "same-source")
        self.assertEqual(
            job.animation_hierarchy_resolution_modes,
            ("same-source",),
        )
        self.assertIsNone(job.model_preparation)
        self.assertIsNone(job.prepared_model_sha256)
        self.assertIsNone(job.model_preparation_evidence_sha256)
        self.assertEqual(
            job.manifest_row()["model"], job.manifest_row()["animations"][0]
        )

        malformed = {
            "not-animated": replace(job, asset_kind="hierarchical"),
            "two-animations": replace(
                job,
                animations=(job.model, job.model),
                animation_source_ids=(job.model_source_id, job.model_source_id),
                animation_source_sha256s=(
                    job.model_source_sha256,
                    job.model_source_sha256,
                ),
                animation_hierarchy_resolution_modes=("same-source", "same-source"),
            ),
            "different-animation-path": replace(
                job, animations=("staged/different.w3d",)
            ),
            "different-animation-source": replace(
                job, animation_source_ids=("src-00000000000000000000000000000000",)
            ),
            "different-animation-sha": replace(
                job, animation_source_sha256s=("0" * 64,)
            ),
            "sibling-hierarchy": replace(job, hierarchy_resolution_mode="sibling-path"),
            "sibling-animation": replace(
                job, animation_hierarchy_resolution_modes=("sibling-path",)
            ),
            "preparation": replace(job, model_preparation=SECONDARY_SKIN_PREPARATION),
            "prepared-model": replace(job, prepared_model_sha256="0" * 64),
            "preparation-evidence": replace(
                job, model_preparation_evidence_sha256="0" * 64
            ),
        }
        for label, malformed_job in malformed.items():
            with self.subTest(label=label):
                self.assertFalse(
                    w3d_job_is_exact_embedded_model_animation(malformed_job)
                )

    def test_near_embedded_shapes_remain_terminal(self) -> None:
        cases = {
            "incomplete-model": (
                _model("Embedded", "Rig", meshes=0)
                + _hierarchy("Rig")
                + _animation("IDLE", "Rig")
            ),
            "incomplete-hierarchy": (
                _model("Embedded", "Rig")
                + _hierarchy("Rig", include_pivot=False)
                + _animation("IDLE", "Rig")
            ),
            "incomplete-animation": (
                _model("Embedded", "Rig")
                + _hierarchy("Rig")
                + _animation("IDLE", "Rig", frame_count=0)
            ),
            "multiple-models": (
                _model("Embedded", "Rig")
                + _model("Other", "Rig")
                + _hierarchy("Rig")
                + _animation("IDLE", "Rig")
            ),
            "multiple-hierarchies": (
                _model("Embedded", "Rig")
                + _hierarchy("Rig")
                + _hierarchy("OtherRig")
                + _animation("IDLE", "Rig")
            ),
            "multiple-animations": (
                _embedded_model_animation("Embedded", "Rig") + _animation("RUN", "Rig")
            ),
            "mismatched-hierarchy": (
                _model("Embedded", "Rig")
                + _hierarchy("Rig")
                + _animation("IDLE", "OtherRig")
            ),
            "empty-hierarchy": (
                _model("Embedded", "") + _hierarchy("") + _animation("IDLE", "")
            ),
            "preparation-required": (
                _embedded_model_animation("Embedded", "Rig")
                + _chunk(0x00000C00, b"v" * 36)
                + _chunk(0x00000C01, b"n" * 36)
            ),
        }

        for label, payload in cases.items():
            with self.subTest(label=label):
                values = {f"units/{label}.w3d": payload}
                report = scan_w3d_catalog(values)
                plan = plan_w3d_batches(
                    report,
                    _mapping(values),
                    index=report.index,
                )

                self.assertFalse(plan.jobs)
                self.assertEqual(plan.source_count, 1)
                self.assertEqual(plan.consumed_source_count, 0)
                self.assertEqual(len(plan.terminals), 1)
                self.assertIn(
                    "embedded-action-ambiguity",
                    plan.terminals[0].reason_codes,
                )

    def test_embedded_animation_rejects_external_clips_and_extra_owners(self) -> None:
        external_values = {
            "units/Rig.w3d": _embedded_model_animation("Embedded", "Rig"),
            "units/walk.w3d": _animation("Walk", "Rig"),
        }
        external_report = scan_w3d_catalog(external_values)
        external = plan_w3d_batches(
            external_report,
            _mapping(external_values),
            index=external_report.index,
        )
        external_reasons = {
            item.source_sha256: item.reason_codes for item in external.terminals
        }

        self.assertFalse(external.jobs)
        self.assertEqual(len(external.terminals), 2)
        self.assertIn(
            "animation-closure-ambiguous",
            external_reasons[
                hashlib.sha256(external_values["units/Rig.w3d"]).hexdigest()
            ],
        )
        self.assertIn(
            "animation-no-unique-model-owner",
            external_reasons[
                hashlib.sha256(external_values["units/walk.w3d"]).hexdigest()
            ],
        )

        owner_values = {
            "units/Rig.w3d": _embedded_model_animation("Embedded", "Rig"),
            "units/other.w3d": _model("Other", "Rig"),
        }
        owner_report = scan_w3d_catalog(owner_values)
        owner = plan_w3d_batches(
            owner_report,
            _mapping(owner_values),
            index=owner_report.index,
        )

        self.assertFalse(owner.jobs)
        self.assertEqual(len(owner.terminals), 2)
        for terminal in owner.terminals:
            self.assertIn("animation-closure-ambiguous", terminal.reason_codes)

    def test_resolution_contract_predicate_is_exact_and_modes_are_evidence(
        self,
    ) -> None:
        values = {
            "units/static.w3d": _model("Static"),
            "units/model.w3d": _model("Model", "Rig"),
            "units/Rig.w3d": _hierarchy("Rig"),
            "units/idle.w3d": _animation("Idle", "Rig"),
        }
        report = scan_w3d_catalog(values)
        plan = plan_w3d_batches(report, _mapping(values), index=report.index)
        static = next(job for job in plan.jobs if job.asset_kind == "static")
        animated = next(job for job in plan.jobs if job.asset_kind == "animated")
        embedded_values = {
            "units/embedded.w3d": _model("Embedded", "EmbeddedRig")
            + _hierarchy("EmbeddedRig")
        }
        embedded_report = scan_w3d_catalog(embedded_values)
        embedded = plan_w3d_batches(
            embedded_report,
            _mapping(embedded_values),
            index=embedded_report.index,
        ).jobs[0]

        self.assertTrue(w3d_job_resolution_contract_is_valid(static))
        self.assertTrue(w3d_job_resolution_contract_is_valid(animated))
        self.assertTrue(w3d_job_resolution_contract_is_valid(embedded))
        self.assertFalse(w3d_job_is_exact_embedded_model_animation(static))
        self.assertFalse(w3d_job_is_exact_embedded_model_animation(animated))
        self.assertFalse(w3d_job_is_exact_embedded_model_animation(embedded))
        future_same_source_animation = replace(
            animated,
            animations=(animated.hierarchy,),
            animation_source_ids=(animated.hierarchy_source_id,),
            animation_source_sha256s=(animated.hierarchy_source_sha256,),
            animation_hierarchy_resolution_modes=("same-source",),
        )
        self.assertFalse(
            w3d_job_resolution_contract_is_valid(future_same_source_animation)
        )
        self.assertFalse(
            w3d_job_is_exact_embedded_model_animation(future_same_source_animation)
        )

        malformed = {
            "missing-hierarchy-mode": replace(animated, hierarchy_resolution_mode=None),
            "false-same-source-hierarchy": replace(
                animated, hierarchy_resolution_mode="same-source"
            ),
            "case-alias-hierarchy": replace(
                animated, hierarchy=animated.model.swapcase()
            ),
            "shared-sibling-hierarchy-id": replace(
                animated, hierarchy_source_id=animated.model_source_id
            ),
            "missing-animation-mode": replace(
                animated, animation_hierarchy_resolution_modes=()
            ),
            "extra-animation-mode": replace(
                animated,
                animation_hierarchy_resolution_modes=(
                    "sibling-path",
                    "sibling-path",
                ),
            ),
            "wrong-animation-mode-type": replace(
                animated, animation_hierarchy_resolution_modes=["sibling-path"]
            ),
            "false-same-source-animation": replace(
                animated, animation_hierarchy_resolution_modes=("same-source",)
            ),
            "false-sibling-animation": replace(
                animated,
                animations=(animated.hierarchy,),
                animation_source_ids=(animated.hierarchy_source_id,),
                animation_source_sha256s=(animated.hierarchy_source_sha256,),
            ),
            "case-alias-animation": replace(
                animated, animations=(animated.hierarchy.swapcase(),)
            ),
            "static-extra-mode": replace(
                static, animation_hierarchy_resolution_modes=("sibling-path",)
            ),
            "wrong-hash-type": replace(animated, model_source_sha256=1),
        }
        for label, job in malformed.items():
            with self.subTest(label=label):
                self.assertFalse(w3d_job_resolution_contract_is_valid(job))

        neutral = animated.neutral()
        self.assertEqual(neutral["hierarchyResolutionMode"], "sibling-path")
        self.assertEqual(neutral["animationHierarchyResolutionModes"], ["sibling-path"])
        self.assertIn(neutral, plan.evidence_hash_basis()["jobs"])
        self.assertNotEqual(
            neutral,
            replace(animated, hierarchy_resolution_mode="same-source").neutral(),
        )

    def test_case_colliding_and_damaged_sibling_hierarchies_fail_closed(self) -> None:
        case_values = {
            "units/model.w3d": _model("Model", "Rig"),
            "units/Rig.w3d": _hierarchy("FirstRig"),
            "units/rIG.W3D": _hierarchy("SecondRig"),
        }
        case_report = scan_w3d_catalog(case_values)
        case_plan = plan_w3d_batches(
            case_report,
            _mapping(case_values),
            index=case_report.index,
        )
        model_sha = hashlib.sha256(case_values["units/model.w3d"]).hexdigest()
        model_terminal = next(
            item for item in case_plan.terminals if item.source_sha256 == model_sha
        )
        self.assertFalse(case_plan.jobs)
        self.assertIn("hierarchy-not-unique", model_terminal.reason_codes)

        damaged_values = {
            "units/model.w3d": _model("Model", "Rig"),
            "units/Rig.w3d": _hierarchy("Rig")
            + _chunk(0x101, b"\0" * 4, declared_size=36),
        }
        damaged_report = scan_w3d_catalog(damaged_values)
        damaged_plan = plan_w3d_batches(
            damaged_report,
            _mapping(damaged_values),
            index=damaged_report.index,
        )
        damaged_model_sha = hashlib.sha256(
            damaged_values["units/model.w3d"]
        ).hexdigest()
        damaged_model_terminal = next(
            item
            for item in damaged_plan.terminals
            if item.source_sha256 == damaged_model_sha
        )
        self.assertFalse(damaged_plan.jobs)
        self.assertIn(
            "hierarchy-source-not-convertible",
            damaged_model_terminal.reason_codes,
        )

    def test_identifier_prefix_path_stages_cross_shard_skeleton(self) -> None:
        # The skin lives in art/w3d/cu/ but its skeleton is authored under
        # art/w3d/nu/ -- the engine finds it by identifier prefix
        # (OpenSAGE Bfme2W3dPathResolver), not by sibling directory.
        values = {
            "art/w3d/cu/cuelk_skn.w3d": _model("CUElk", "NUHORSE_SKL"),
            "art/w3d/nu/nuhorse_skl.w3d": _hierarchy("NUHORSE_SKL"),
            "art/w3d/nu/nuhorse_runa.w3d": _animation("Run", "NUHORSE_SKL"),
        }
        report = scan_w3d_catalog(values)
        plan = plan_w3d_batches(report, _mapping(values), index=report.index)

        self.assertTrue(plan.source_accounting_complete)
        self.assertEqual(len(plan.jobs), 1)
        job = plan.jobs[0]
        self.assertTrue(w3d_job_resolution_contract_is_valid(job))
        self.assertEqual(job.asset_kind, "animated")
        self.assertEqual(job.hierarchy_resolution_mode, "identifier-prefix-path")
        self.assertEqual(
            job.hierarchy_source_sha256,
            hashlib.sha256(values["art/w3d/nu/nuhorse_skl.w3d"]).hexdigest(),
        )
        # The clip sits beside its skeleton, so it stays sibling-resolved.
        self.assertEqual(job.animation_hierarchy_resolution_modes, ("sibling-path",))
        neutral = job.neutral()
        self.assertEqual(neutral["hierarchyResolutionMode"], "identifier-prefix-path")

        # A prefix-mode job may never collapse onto its own model source.
        self.assertFalse(
            w3d_job_resolution_contract_is_valid(
                replace(
                    job,
                    hierarchy=job.model,
                    hierarchy_source_id=job.model_source_id,
                    hierarchy_source_sha256=job.model_source_sha256,
                )
            )
        )

    def test_sibling_candidate_precedes_identifier_prefix_candidate(self) -> None:
        values = {
            "art/w3d/mu/model.w3d": _model("Model", "Rig"),
            "art/w3d/mu/Rig.w3d": _hierarchy("SiblingRig"),
            "art/w3d/ri/rig.w3d": _hierarchy("PrefixRig"),
        }
        report = scan_w3d_catalog(values)
        plan = plan_w3d_batches(report, _mapping(values), index=report.index)
        job = next(
            item
            for item in plan.jobs
            if item.model_source_sha256
            == hashlib.sha256(values["art/w3d/mu/model.w3d"]).hexdigest()
        )
        self.assertEqual(job.hierarchy_resolution_mode, "sibling-path")
        self.assertEqual(
            job.hierarchy_source_sha256,
            hashlib.sha256(values["art/w3d/mu/Rig.w3d"]).hexdigest(),
        )

    def test_identifier_prefix_never_guesses_a_near_name(self) -> None:
        # CUGSHEEP_SKL is authored nowhere; a one-character-different
        # skeleton exists and must NOT be substituted.
        values = {
            "art/w3d/cu/cusheep_skn.w3d": _model("Sheep", "CUGSHEEP_SKL"),
            "art/w3d/cu/cusheep_skl.w3d": _hierarchy("CUSHEEP_SKL"),
        }
        report = scan_w3d_catalog(values)
        plan = plan_w3d_batches(report, _mapping(values), index=report.index)
        model_sha = hashlib.sha256(values["art/w3d/cu/cusheep_skn.w3d"]).hexdigest()
        model_terminal = next(
            item for item in plan.terminals if item.source_sha256 == model_sha
        )
        self.assertFalse(plan.jobs)
        self.assertIn("hierarchy-not-found", model_terminal.reason_codes)

    def test_sanitized_animation_name_collision_fails_closed(self) -> None:
        values = {
            "units/model.w3d": _model("Model", "Rig"),
            "units/Rig.w3d": _hierarchy("Rig"),
            "units/run-dash.w3d": _animation("Run-A", "Rig"),
            "units/run-space.w3d": _animation("Run A", "Rig"),
        }
        report = scan_w3d_catalog(values)
        staged = _mapping(values)
        staged["units/run-dash.w3d"] = "staged/Run-A.w3d"
        staged["units/run-space.w3d"] = "staged/Run A.w3d"
        plan = plan_w3d_batches(report, staged, index=report.index)
        model_sha = hashlib.sha256(values["units/model.w3d"]).hexdigest()
        model_terminal = next(
            item for item in plan.terminals if item.source_sha256 == model_sha
        )

        self.assertFalse(plan.jobs)
        self.assertIn("animation-closure-ambiguous", model_terminal.reason_codes)

    def test_unsafe_catalog_paths_are_path_free_deterministic_terminals(self) -> None:
        payload = _model("Unsafe")
        terminal_ids: list[str] = []
        for unsafe_path in ("../first.w3d", "/second.w3d"):
            report = scan_w3d_catalog({unsafe_path: payload})
            plan = plan_w3d_batches(report, {}, index=report.index)

            self.assertFalse(plan.jobs)
            self.assertEqual(len(plan.terminals), 1)
            self.assertIn("unsafe-physical-path", plan.terminals[0].reason_codes)
            self.assertIn("catalog-source-failure", plan.terminals[0].reason_codes)
            terminal_ids.append(plan.terminals[0].source_id)
            self.assertNotIn(unsafe_path, json.dumps(plan.neutral(), sort_keys=True))

        self.assertEqual(terminal_ids[0], terminal_ids[1])

    def test_duplicate_clip_ids_block_owner_without_global_guessing(self) -> None:
        values = {
            "models/model.w3d": _model("Model", "Rig"),
            "models/Rig.w3d": _hierarchy("Rig"),
            "models/a.w3d": _animation("Idle", "Rig"),
            "models/b.w3d": _animation("IDLE", "Rig"),
        }
        report = scan_w3d_catalog(values)
        plan = plan_w3d_batches(report, _mapping(values), index=report.index)

        self.assertFalse(plan.jobs)
        reasons = [code for item in plan.terminals for code in item.reason_codes]
        self.assertIn("animation-closure-ambiguous", reasons)
        self.assertEqual(reasons.count("duplicate-logical-id"), 2)
        self.assertEqual(len(plan.terminals), 4)

    def test_orphan_animation_and_missing_mapping_are_explicit_terminals(self) -> None:
        orphan_values = {"clips/orphan.w3d": _animation("Idle", "LonelyRig")}
        orphan_report = scan_w3d_catalog(orphan_values)
        orphan = plan_w3d_batches(
            orphan_report,
            _mapping(orphan_values),
            index=orphan_report.index,
        )
        self.assertEqual(
            orphan.terminals[0].reason_codes,
            ("animation-no-unique-model-owner",),
        )

        model_values = {"models/only.w3d": _model("Only")}
        model_report = scan_w3d_catalog(model_values)
        missing = plan_w3d_batches(model_report, {}, index=model_report.index)
        self.assertFalse(missing.jobs)
        self.assertIn("missing-staged-input", missing.terminals[0].reason_codes)

    def test_unsafe_and_ambiguous_sources_remain_explicit(self) -> None:
        values = {
            "models/safe.w3d": _model("Safe"),
            "models/unsafe.w3d": _model("Bad/Name"),
            "models/multi.w3d": _model("Multi")
            + _chunk(
                0x700,
                _chunk(
                    0x701,
                    struct.pack(
                        "<II16s16s",
                        0x00040001,
                        1,
                        _fixed("Second", 16),
                        _fixed("", 16),
                    ),
                ),
                children=True,
            ),
            "models/embedded.w3d": (
                _model("Embed", "EmbedRig")
                + _hierarchy("EmbedRig")
                + _animation("IDLE", "OtherRig")
            ),
            "models/profile.w3d": _model("Profile", meshes=2),
            "models/multi-hierarchy.w3d": _hierarchy("RigOne") + _hierarchy("RigTwo"),
            "broken/empty.w3d": b"",
            "broken/unsupported.w3d": _chunk(0x500, b"opaque"),
            "broken/unknown.w3d": _chunk(0xDEADBEEF, b"opaque"),
            "broken/damaged.w3d": _chunk(0x101, b"\0" * 4, declared_size=36),
        }
        report = scan_w3d_catalog(values)
        plan = plan_w3d_batches(report, _mapping(values), index=report.index)
        all_reasons = {code for item in plan.terminals for code in item.reason_codes}

        self.assertEqual(len(plan.jobs), 1)
        self.assertIn("unsafe-header-id", all_reasons)
        self.assertIn("multiple-model-headers", all_reasons)
        self.assertIn("multiple-hierarchy-headers", all_reasons)
        self.assertIn("embedded-action-ambiguity", all_reasons)
        self.assertIn("needs-explicit-presentation-profile", all_reasons)
        self.assertIn("empty-source", all_reasons)
        self.assertIn("unsupported-source", all_reasons)
        self.assertIn("unknown-source", all_reasons)
        self.assertIn("damaged-source", all_reasons)

    def test_preserve_all_retains_every_mesh_without_resolving_presentation(
        self,
    ) -> None:
        values = {
            "models/profile.w3d": (
                _model("Profile", meshes=2)
                + _model_reference("EQUIPMENT", role="aggregate")
            )
        }
        report = scan_w3d_catalog(values)
        staged = _mapping(values)

        strict = plan_w3d_batches(report, staged, index=report.index)
        preserve = plan_w3d_batches(
            report,
            staged,
            index=report.index,
            presentation_policy=PRESENTATION_POLICY_PRESERVE_ALL,
        )

        self.assertFalse(strict.jobs)
        self.assertIn(
            "needs-explicit-presentation-profile",
            strict.terminals[0].reason_codes,
        )
        self.assertTrue(preserve.source_accounting_complete)
        self.assertEqual(len(preserve.jobs), 1)
        job = preserve.jobs[0]
        self.assertEqual(job.authored_presentation_mesh_count, 2)
        self.assertEqual(job.manifest_row()["required_equipment"], [])
        self.assertEqual(job.manifest_row()["excluded_optional_meshes"], [])
        parsed = BATCH.parse_manifest_document(preserve.batches[0].manifest_document())
        self.assertEqual(len(parsed), 1)
        self.assertEqual(parsed[0].required_equipment, ())
        self.assertEqual(parsed[0].excluded_optional_meshes, ())

        plan_presentation = preserve.neutral()["presentation"]
        self.assertTrue(plan_presentation["allAuthoredPresentationMeshesRetained"])
        self.assertFalse(plan_presentation["runtimePresentationResolved"])
        self.assertFalse(plan_presentation["runtimeFidelityClaimed"])
        job_presentation = preserve.neutral()["jobs"][0]["presentation"]
        self.assertEqual(job_presentation["authoredPresentationMeshCount"], 2)
        self.assertTrue(job_presentation["allAuthoredPresentationMeshesRetained"])
        self.assertFalse(job_presentation["equipmentExclusionsApplied"])
        self.assertFalse(job_presentation["conditionStateSelectionApplied"])
        self.assertFalse(job_presentation["runtimePresentationResolved"])

    def test_preserve_all_is_policy_bound_while_default_strict_is_unchanged(
        self,
    ) -> None:
        values = {"models/static.w3d": _model("Static")}
        report = scan_w3d_catalog(values)
        staged = _mapping(values)

        default = plan_w3d_batches(report, staged, index=report.index)
        strict = plan_w3d_batches(
            report,
            staged,
            index=report.index,
            presentation_policy="strict",
        )
        preserve = plan_w3d_batches(
            report,
            staged,
            index=report.index,
            presentation_policy=PRESENTATION_POLICY_PRESERVE_ALL,
        )

        self.assertEqual(default, strict)
        self.assertEqual(default.neutral(), strict.neutral())
        self.assertEqual(default.jobs[0].manifest_row(), strict.jobs[0].manifest_row())
        self.assertNotIn("presentation", strict.neutral())
        self.assertNotIn("presentation", strict.jobs[0].neutral())
        self.assertNotEqual(strict.jobs[0].job_id, preserve.jobs[0].job_id)
        self.assertNotEqual(
            strict.jobs[0].definition_sha256,
            preserve.jobs[0].definition_sha256,
        )
        self.assertNotEqual(strict.private_plan_sha256, preserve.private_plan_sha256)
        self.assertNotEqual(strict.evidence_sha256, preserve.evidence_sha256)

    def test_preserve_all_does_not_relax_other_terminal_evidence(self) -> None:
        values = {
            "models/multiple.w3d": _model("First", meshes=2) + _model("Second"),
            "models/incomplete.w3d": (
                _model("Incomplete") + _mesh("Incomplete", "BROKEN", face_count=0)
            ),
            "models/embedded.w3d": (
                _model("Embed", "EmbedRig")
                + _hierarchy("EmbedRig")
                + _animation("IDLE", "OtherRig")
            ),
            "models/missing-rig.w3d": _model("MissingRig", "Absent", meshes=2),
            "models/unknown.w3d": (
                _model("Unknown", meshes=2) + _chunk(0xDEADBEEF, b"opaque")
            ),
            "models/damaged.w3d": (
                _model("Damaged", meshes=2) + _chunk(0x101, b"\0" * 4, declared_size=36)
            ),
        }
        report = scan_w3d_catalog(values)
        plan = plan_w3d_batches(
            report,
            _mapping(values),
            index=report.index,
            presentation_policy=PRESENTATION_POLICY_PRESERVE_ALL,
        )
        reasons_by_sha = {
            item.source_sha256: item.reason_codes for item in plan.terminals
        }

        self.assertFalse(plan.jobs)
        self.assertEqual(len(plan.terminals), len(values))
        self.assertIn(
            "multiple-model-headers",
            reasons_by_sha[hashlib.sha256(values["models/multiple.w3d"]).hexdigest()],
        )
        self.assertIn(
            "no-complete-mesh-evidence",
            reasons_by_sha[hashlib.sha256(values["models/incomplete.w3d"]).hexdigest()],
        )
        self.assertIn(
            "embedded-action-ambiguity",
            reasons_by_sha[hashlib.sha256(values["models/embedded.w3d"]).hexdigest()],
        )
        self.assertIn(
            "hierarchy-not-found",
            reasons_by_sha[
                hashlib.sha256(values["models/missing-rig.w3d"]).hexdigest()
            ],
        )
        self.assertIn(
            "unknown-source",
            reasons_by_sha[hashlib.sha256(values["models/unknown.w3d"]).hexdigest()],
        )
        self.assertIn(
            "damaged-source",
            reasons_by_sha[hashlib.sha256(values["models/damaged.w3d"]).hexdigest()],
        )

    def test_preserve_all_order_and_batching_are_deterministic(self) -> None:
        entries = [
            (
                f"models/model-{index}.w3d",
                _model(f"Model{index}", meshes=2)
                + _model_reference(f"Part{index}", role="proxy"),
            )
            for index in range(3)
        ]
        first_report = scan_w3d_catalog(entries)
        second_report = scan_w3d_catalog(reversed(entries))
        staged = _mapping(dict(entries))

        first = plan_w3d_batches(
            first_report,
            staged,
            index=first_report.index,
            max_jobs_per_batch=2,
            presentation_policy=PRESENTATION_POLICY_PRESERVE_ALL,
        )
        second = plan_w3d_batches(
            second_report,
            dict(reversed(tuple(staged.items()))),
            index=second_report.index,
            max_jobs_per_batch=2,
            presentation_policy=PRESENTATION_POLICY_PRESERVE_ALL,
        )

        self.assertEqual(first.neutral(), second.neutral())
        self.assertEqual([len(batch.jobs) for batch in first.batches], [2, 1])
        self.assertEqual(
            [batch.manifest_document() for batch in first.batches],
            [batch.manifest_document() for batch in second.batches],
        )

    def test_invalid_presentation_policy_is_rejected(self) -> None:
        values = {"models/static.w3d": _model("Static")}
        report = scan_w3d_catalog(values)
        for policy in ("archive", "PRESERVE-ALL", None, []):
            with self.subTest(policy=policy):
                with self.assertRaisesRegex(ValueError, "presentation_policy"):
                    plan_w3d_batches(
                        report,
                        _mapping(values),
                        index=report.index,
                        presentation_policy=policy,
                    )

    def test_forced_terminal_inputs_are_canonical_and_evidence_sealed(self) -> None:
        values = {"models/static.w3d": _model("Static")}
        report = scan_w3d_catalog(values)
        staged = _mapping(values)
        baseline = plan_w3d_batches(report, staged, index=report.index)
        source_id = baseline.jobs[0].model_source_id
        evidence_sha256 = "a" * 64

        plan = plan_w3d_batches(
            report,
            staged,
            index=report.index,
            forced_terminal_reasons={
                source_id: (
                    "asset-dependency-missing",
                    "decode-evidence-rejected",
                )
            },
            forced_terminal_evidence_sha256=evidence_sha256,
        )

        self.assertFalse(plan.jobs)
        self.assertEqual(plan.consumed_source_count, 0)
        self.assertEqual(len(plan.terminals), 1)
        self.assertEqual(
            plan.terminals[0].reason_codes,
            ("asset-dependency-missing", "decode-evidence-rejected"),
        )
        self.assertEqual(
            plan.neutral()["hashes"]["forcedTerminalEvidenceSha256"],
            evidence_sha256,
        )
        self.assertEqual(
            plan.forced_terminal_rows,
            (
                (
                    source_id,
                    ("asset-dependency-missing", "decode-evidence-rejected"),
                ),
            ),
        )
        self.assertEqual(
            plan.neutral()["forcedTerminalReasons"],
            [
                {
                    "sourceId": source_id,
                    "reasonCodes": [
                        "asset-dependency-missing",
                        "decode-evidence-rejected",
                    ],
                }
            ],
        )
        self.assertNotEqual(plan.private_plan_sha256, baseline.private_plan_sha256)
        self.assertNotEqual(plan.evidence_sha256, baseline.evidence_sha256)
        self.assertEqual(
            plan.consumed_source_count + len(plan.terminals),
            plan.source_count,
        )

    def test_forced_hierarchy_terminal_cascades_without_losing_sources(self) -> None:
        values = {
            "models/model.w3d": _model("Model", "Rig"),
            "models/Rig.w3d": _hierarchy("Rig"),
            "models/idle.w3d": _animation("Idle", "Rig"),
        }
        report = scan_w3d_catalog(values)
        staged = _mapping(values)
        baseline = plan_w3d_batches(report, staged, index=report.index)
        self.assertEqual(len(baseline.jobs), 1)
        job = baseline.jobs[0]
        assert job.hierarchy_source_id is not None

        plan = plan_w3d_batches(
            report,
            staged,
            index=report.index,
            forced_terminal_reasons={
                job.hierarchy_source_id: ("upstream-decode-terminal",)
            },
            forced_terminal_evidence_sha256="b" * 64,
        )
        reasons = _reason_map(plan)

        self.assertFalse(plan.jobs)
        self.assertEqual(plan.consumed_source_count, 0)
        self.assertEqual(len(plan.terminals), 3)
        self.assertEqual(
            reasons[job.hierarchy_source_id],
            ("upstream-decode-terminal",),
        )
        self.assertIn(
            "hierarchy-source-not-convertible",
            reasons[job.model_source_id],
        )
        self.assertEqual(
            reasons[job.animation_source_ids[0]],
            ("animation-no-unique-model-owner",),
        )
        self.assertEqual(
            plan.consumed_source_count + len(plan.terminals),
            plan.source_count,
        )

    def test_forced_terminal_mapping_order_is_identity_stable(self) -> None:
        entries = [
            ("models/one.w3d", _model("One")),
            ("models/two.w3d", _model("Two")),
        ]
        first_report = scan_w3d_catalog(entries)
        second_report = scan_w3d_catalog(reversed(entries))
        staged = _mapping(dict(entries))
        baseline = plan_w3d_batches(
            first_report,
            staged,
            index=first_report.index,
        )
        source_ids = sorted(job.model_source_id for job in baseline.jobs)
        first_forced = {
            source_ids[0]: ("decode-terminal",),
            source_ids[1]: ("texture-terminal",),
        }
        second_forced = dict(reversed(tuple(first_forced.items())))

        first = plan_w3d_batches(
            first_report,
            staged,
            index=first_report.index,
            forced_terminal_reasons=first_forced,
            forced_terminal_evidence_sha256="c" * 64,
        )
        second = plan_w3d_batches(
            second_report,
            dict(reversed(tuple(staged.items()))),
            index=second_report.index,
            forced_terminal_reasons=second_forced,
            forced_terminal_evidence_sha256="c" * 64,
        )

        self.assertEqual(first, second)
        self.assertEqual(first.neutral(), second.neutral())
        self.assertEqual(first.private_plan_sha256, second.private_plan_sha256)
        self.assertEqual(first.evidence_sha256, second.evidence_sha256)

    def test_invalid_forced_terminal_sources_and_reasons_fail_closed(self) -> None:
        values = {"models/static.w3d": _model("Static")}
        report = scan_w3d_catalog(values)
        staged = _mapping(values)
        baseline = plan_w3d_batches(report, staged, index=report.index)
        source_id = baseline.jobs[0].model_source_id
        evidence_sha256 = "d" * 64

        with self.assertRaisesRegex(TypeError, "must be a mapping"):
            plan_w3d_batches(
                report,
                staged,
                index=report.index,
                forced_terminal_reasons=[],
                forced_terminal_evidence_sha256=evidence_sha256,
            )
        with self.assertRaisesRegex(ValueError, "source IDs must be canonical"):
            plan_w3d_batches(
                report,
                staged,
                index=report.index,
                forced_terminal_reasons={source_id.upper(): ("forced-terminal",)},
                forced_terminal_evidence_sha256=evidence_sha256,
            )
        with self.assertRaisesRegex(ValueError, "absent from the catalog"):
            plan_w3d_batches(
                report,
                staged,
                index=report.index,
                forced_terminal_reasons={f"src-{'f' * 32}": ("forced-terminal",)},
                forced_terminal_evidence_sha256=evidence_sha256,
            )

        invalid_reason_sets = (
            [],
            (),
            ("",),
            ("Uppercase",),
            ("leading-space", "trailing-space "),
            ("not--canonical",),
            ("second", "first"),
            ("duplicate", "duplicate"),
        )
        for reasons in invalid_reason_sets:
            with self.subTest(reasons=reasons):
                with self.assertRaises((TypeError, ValueError)):
                    plan_w3d_batches(
                        report,
                        staged,
                        index=report.index,
                        forced_terminal_reasons={source_id: reasons},
                        forced_terminal_evidence_sha256=evidence_sha256,
                    )

    def test_invalid_forced_terminal_evidence_seals_fail_closed(self) -> None:
        values = {"models/static.w3d": _model("Static")}
        report = scan_w3d_catalog(values)
        staged = _mapping(values)
        baseline = plan_w3d_batches(report, staged, index=report.index)
        source_id = baseline.jobs[0].model_source_id
        forced = {source_id: ("forced-terminal",)}

        for evidence_sha256 in (None, "A" * 64, "a" * 63, b"a" * 64):
            with self.subTest(evidence_sha256=evidence_sha256):
                with self.assertRaisesRegex(ValueError, "canonical lowercase SHA-256"):
                    plan_w3d_batches(
                        report,
                        staged,
                        index=report.index,
                        forced_terminal_reasons=forced,
                        forced_terminal_evidence_sha256=evidence_sha256,
                    )
        with self.assertRaisesRegex(ValueError, "requires forced_terminal_reasons"):
            plan_w3d_batches(
                report,
                staged,
                index=report.index,
                forced_terminal_evidence_sha256="e" * 64,
            )

    def test_forced_terminal_defaults_preserve_legacy_neutral_hashes(self) -> None:
        values = {"models/static.w3d": _model("Static")}
        report = scan_w3d_catalog(values)
        staged = _mapping(values)

        default = plan_w3d_batches(report, staged, index=report.index)
        explicit = plan_w3d_batches(
            report,
            staged,
            index=report.index,
            forced_terminal_reasons=None,
            forced_terminal_evidence_sha256=None,
        )

        self.assertEqual(default, explicit)
        # Golden values re-pinned when the metadata neutral form gained the
        # (empty here) secondaryGeometryStreams family; the invariant under
        # test is unchanged: omitting the forced-terminal arguments must be
        # byte-identical to passing their explicit defaults.
        self.assertEqual(
            default.private_plan_sha256,
            "cf8f1aa8d0195eac055a0ff512a0196ccf51ae814037282f2bb5984aa77467c2",
        )
        self.assertEqual(
            default.evidence_sha256,
            "c0c84560f25354ff261b6414378e0e619d05bcac21d9430d9719733b76c0de20",
        )
        neutral_bytes = (
            json.dumps(
                default.neutral(),
                ensure_ascii=False,
                sort_keys=True,
                separators=(",", ":"),
            )
            + "\n"
        ).encode("utf-8")
        self.assertEqual(
            hashlib.sha256(neutral_bytes).hexdigest(),
            "7a26cd6be217a8e45ff53bfde69d25d32ee7c9e84ebff316ff25f44aa43a8318",
        )
        self.assertNotIn(
            "forcedTerminalEvidenceSha256",
            default.neutral()["hashes"],
        )
        self.assertIsNone(default.forced_terminal_rows)
        self.assertNotIn("forcedTerminalReasons", default.neutral())

    def test_257_jobs_are_deterministically_bounded_into_two_batches(self) -> None:
        values = {
            f"models/model-{index:03d}.w3d": _model(f"M{index:03d}")
            for index in range(257)
        }
        report = scan_w3d_catalog(values)
        plan = plan_w3d_batches(report, _mapping(values), index=report.index)

        self.assertEqual(len(plan.jobs), 257)
        self.assertEqual([len(batch.jobs) for batch in plan.batches], [256, 1])
        for batch in plan.batches:
            parsed = BATCH.parse_manifest_document(batch.manifest_document())
            self.assertEqual(len(parsed), len(batch.jobs))
            self.assertLessEqual(len(batch.manifest_bytes()), BATCH.MAX_MANIFEST_BYTES)
            self.assertEqual(
                batch.manifest_sha256,
                hashlib.sha256(batch.manifest_bytes()).hexdigest(),
            )

    def test_order_hashes_and_neutral_evidence_are_stable_and_name_free(self) -> None:
        entries = [
            ("Retail/SecretModel.w3d", _model("SecretModel", "SecretRig")),
            ("Retail/SecretRig.w3d", _hierarchy("SecretRig")),
            ("Retail/SecretDance.w3d", _animation("SecretDance", "SecretRig")),
        ]
        first_report = scan_w3d_catalog(entries)
        second_report = scan_w3d_catalog(reversed(entries))
        staged = {
            "Retail/SecretDance.w3d": "private/retail-secret-dance.w3d",
            "Retail/SecretModel.w3d": "private/retail-secret-model.w3d",
            "Retail/SecretRig.w3d": "private/retail-secret-rig.w3d",
        }
        first = plan_w3d_batches(first_report, staged, index=first_report.index)
        second = plan_w3d_batches(
            second_report,
            dict(reversed(tuple(staged.items()))),
            index=second_report.index,
        )

        self.assertEqual(first.neutral(), second.neutral())
        self.assertEqual(
            [batch.manifest_document() for batch in first.batches],
            [batch.manifest_document() for batch in second.batches],
        )
        self.assertEqual(
            first.evidence_sha256,
            hashlib.sha256(
                (
                    json.dumps(
                        first.evidence_hash_basis(),
                        ensure_ascii=False,
                        sort_keys=True,
                        separators=(",", ":"),
                    )
                    + "\n"
                ).encode("utf-8")
            ).hexdigest(),
        )
        neutral = json.dumps(first.neutral(), sort_keys=True)
        for authored_name in (
            "Retail",
            "SecretModel",
            "SecretRig",
            "SecretDance",
            "retail-secret",
            "private/",
        ):
            self.assertNotIn(authored_name, neutral)
        self.assertFalse(first.neutral()["summary"]["conversionComplete"])
        self.assertFalse(first.neutral()["summary"]["corpusComplete"])


if __name__ == "__main__":
    unittest.main()
