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

import openbfme_importer.w3d_prepared_root as prepared_root
from openbfme_importer.w3d_batch_runner import (
    W3DBatchRunnerError,
    W3DProcessResult,
    hash_w3d_adapter_bundle,
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
from openbfme_importer.w3d_job_root import (
    W3D_JOB_ROOT_MANIFEST,
    W3DJobRootFile,
    W3DJobRootReport,
)
from openbfme_importer.w3d_prepared_root import (
    W3D_PREPARED_ROOT_MANIFEST,
    W3DPreparedRootError,
    W3DPreparedRootReuseError,
    prepare_w3d_execution_root,
    validate_w3d_prepared_root,
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
HLOD = 0x00000700
HLOD_HEADER = 0x00000701
HLOD_LOD_ARRAY = 0x00000702
HLOD_LOD_ARRAY_HEADER = 0x00000703
HLOD_SUB_OBJECT = 0x00000704
VERTICES_2 = 0x00000C00
NORMALS_2 = 0x00000C01


def _canonical(value: object, *, pretty: bool = False, newline: bool = True) -> bytes:
    options: dict[str, object] = {
        "allow_nan": False,
        "ensure_ascii": False,
        "sort_keys": True,
    }
    if pretty:
        options["indent"] = 2
    else:
        options["separators"] = (",", ":")
    return (json.dumps(value, **options) + ("\n" if newline else "")).encode()


def _source_canonical(value: object, *, newline: bool = True) -> bytes:
    return (
        json.dumps(
            value,
            allow_nan=False,
            ensure_ascii=True,
            sort_keys=True,
            separators=(",", ":"),
        )
        + ("\n" if newline else "")
    ).encode()


def _sha(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def _glb() -> bytes:
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
    indices = struct.pack("<3H", 0, 1, 2) + b"\0\0"
    inverse_bind = struct.pack("<48f", *([1.0] * 48))
    animation_input = struct.pack("<2f", 0.0, 1.0)
    animation_output = struct.pack(
        "<6f",
        0.0,
        0.0,
        0.0,
        1.0,
        0.0,
        0.0,
    )
    payload = (
        positions
        + indices
        + inverse_bind
        + animation_input
        + animation_output
        + b"PNG!"
    )
    document = {
        "asset": {"version": "2.0"},
        "buffers": [{"byteLength": len(payload)}],
        "bufferViews": [
            {"buffer": 0, "byteOffset": 0, "byteLength": 36, "target": 34962},
            {"buffer": 0, "byteOffset": 36, "byteLength": 6, "target": 34963},
            {"buffer": 0, "byteOffset": 44, "byteLength": 192},
            {"buffer": 0, "byteOffset": 236, "byteLength": 8},
            {"buffer": 0, "byteOffset": 244, "byteLength": 24},
            {"buffer": 0, "byteOffset": 268, "byteLength": 4},
        ],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"},
            {"bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR"},
            {"bufferView": 2, "componentType": 5126, "count": 3, "type": "MAT4"},
            {"bufferView": 3, "componentType": 5126, "count": 2, "type": "SCALAR"},
            {"bufferView": 4, "componentType": 5126, "count": 2, "type": "VEC3"},
        ],
        "images": [{"bufferView": 5, "mimeType": "image/png"}],
        "textures": [{"source": 0}],
        "materials": [{"pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}}],
        "meshes": [
            {
                "primitives": [
                    {"attributes": {"POSITION": 0}, "indices": 1, "material": 0}
                ]
            }
        ],
        "nodes": [{"mesh": 0, "skin": 0}, {}, {}, {}],
        "skins": [{"joints": [1, 2, 3], "inverseBindMatrices": 2, "skeleton": 1}],
        "animations": [
            {
                "samplers": [{"input": 3, "output": 4, "interpolation": "LINEAR"}],
                "channels": [
                    {"sampler": 0, "target": {"node": 1, "path": "translation"}}
                ],
            }
        ],
        "scenes": [{"nodes": [0, 1, 2, 3]}],
        "scene": 0,
    }
    encoded = _canonical(document).rstrip(b"\n")
    encoded += b" " * (-len(encoded) % 4)
    chunks = struct.pack("<II", len(encoded), 0x4E4F534A) + encoded
    chunks += struct.pack("<II", len(payload), 0x004E4942) + payload
    return struct.pack("<4sII", b"glTF", 2, 12 + len(chunks)) + chunks


class _SuccessfulRunner:
    def __init__(self) -> None:
        self.commands: list[tuple[str, ...]] = []

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
        manifest = Path(command[command.index("--manifest") + 1])
        output_root = Path(command[command.index("--output-root") + 1])
        raw_manifest = manifest.read_bytes()
        jobs = json.loads(raw_manifest)["jobs"]
        lines: list[str] = []
        for job in jobs:
            output = output_root.joinpath(*job["output"].split("/"))
            output.parent.mkdir(parents=True, exist_ok=True)
            payload = _glb()
            output.write_bytes(payload)
            marker = {
                "job_id": job["job_id"],
                "status": "succeeded",
                "output_sha256": _sha(payload),
                "report_schema": "openbfme.w3d-adapter-report",
                "report_version": 2,
                "asset_kind": job["asset_kind"],
                "adapter_report_sha256": _sha(b"adapter-report"),
                "meshes": 1,
                "animations": len(job["animations"]),
                "bones": 3,
                "skeletons": 1,
                "vertices": 2,
                "triangles": 1,
                "skinned_meshes": 1,
                "materials": 1,
                "images": 1,
            }
            lines.append(
                "OPENBFME_W3D_BATCH_JOB " + _canonical(marker).decode().rstrip("\n")
            )
        done = {
            "report_schema": "openbfme.w3d-batch-report",
            "report_version": 1,
            "manifest_sha256": _sha(raw_manifest),
            "jobs": len(jobs),
            "succeeded": len(jobs),
            "failed": 0,
            "complete": True,
        }
        lines.append(
            "OPENBFME_W3D_BATCH_DONE " + _canonical(done).decode().rstrip("\n")
        )
        return W3DProcessResult(0, ("\n".join(lines) + "\n").encode())


def _fixed(value: str, size: int) -> bytes:
    encoded = value.encode("ascii")
    return encoded + b"\0" * (size - len(encoded))


def _chunk(kind: int, payload: bytes, *, container: bool = False) -> bytes:
    return (
        struct.pack("<II", kind, len(payload) | (CONTAINER if container else 0))
        + payload
    )


def _pivot(name: str, parent: int, xyz: tuple[float, float, float]) -> bytes:
    return struct.pack(
        "<16si10f",
        _fixed(name, 16),
        parent,
        *xyz,
        0.0,
        0.0,
        0.0,
        0.0,
        0.0,
        0.0,
        1.0,
    )


def _hierarchy() -> bytes:
    pivots = b"".join(
        (
            _pivot("ROOTTRANSFORM", -1, (0.0, 0.0, 0.0)),
            _pivot("BONE1", 0, (1.0, 0.0, 0.0)),
            _pivot("BONE2", 0, (0.0, 2.0, 0.0)),
        )
    )
    header = struct.pack(
        "<I16sI3f", 0x00040001, _fixed("TEST_SKL", 16), 3, 0.0, 0.0, 0.0
    )
    return _chunk(
        HIERARCHY,
        _chunk(HIERARCHY_HEADER, header) + _chunk(PIVOTS, pivots),
        container=True,
    )


def _model() -> bytes:
    header = struct.pack(
        "<II16s16s9I10f",
        0x00040002,
        0x00020000,
        _fixed("DUAL", 16),
        _fixed("TEST_SKIN", 16),
        0,
        2,
        0,
        0,
        0,
        0,
        0,
        0x13,
        1,
        *([0.0] * 10),
    )

    def vec3(values: tuple[tuple[float, float, float], ...]) -> bytes:
        return b"".join(struct.pack("<3f", *item) for item in values)

    normals = ((0.0, 0.0, 1.0), (0.0, 1.0, 0.0))
    mesh = _chunk(
        MESH,
        b"".join(
            (
                _chunk(MESH_HEADER, header),
                _chunk(VERTICES, vec3(((1.0, 3.0, 4.0), (2.0, 5.0, 7.0)))),
                _chunk(VERTICES_2, vec3(((1.0, 3.0, 4.0), (3.0, 3.0, 7.0)))),
                _chunk(NORMALS, vec3(normals)),
                _chunk(NORMALS_2, vec3(normals)),
                _chunk(
                    INFLUENCES,
                    b"".join(
                        struct.pack("<4H", *item)
                        for item in ((1, 0, 100, 0), (1, 2, 60, 40))
                    ),
                ),
            )
        ),
        container=True,
    )
    hlod_header = struct.pack(
        "<II16s16s", 0x00010000, 1, _fixed("TEST_SKIN", 16), _fixed("TEST_SKL", 16)
    )
    lod = _chunk(
        HLOD_LOD_ARRAY,
        _chunk(HLOD_LOD_ARRAY_HEADER, struct.pack("<If", 1, 1.0))
        + _chunk(
            HLOD_SUB_OBJECT, struct.pack("<I32s", 0, _fixed("TEST_SKIN.DUAL", 32))
        ),
        container=True,
    )
    return mesh + _chunk(HLOD, _chunk(HLOD_HEADER, hlod_header) + lod, container=True)


def _tree_sha(domain: str, rows: list[dict[str, object]]) -> str:
    digest = hashlib.sha256()
    digest.update(domain.encode("ascii") + b"\n")
    for row in rows:
        digest.update(str(row["path"]).encode() + b"\0")
        digest.update(str(row["size"]).encode() + b"\0")
        digest.update(str(row["sha256"]).encode() + b"\n")
    return digest.hexdigest()


def _plan(
    model: bytes, hierarchy: bytes, animation: bytes, *, accounted: bool
) -> W3DJobPlan:
    definition = _sha(b"prepared-root-definition")
    job_id = f"w3d-{definition[:40]}"
    job = W3DPlannedJob(
        job_id=job_id,
        asset_kind="animated",
        model="inputs/model.w3d",
        animations=("inputs/idle.w3d",),
        output=f"glb/{job_id}.glb",
        model_source_id="src-model",
        model_source_sha256=_sha(model),
        hierarchy_source_id="src-hierarchy",
        animation_source_ids=("src-animation",),
        definition_sha256=definition,
        animation_source_sha256s=(_sha(animation),),
        hierarchy="inputs/hierarchy.w3d",
        hierarchy_source_sha256=_sha(hierarchy),
        model_preparation=SECONDARY_SKIN_PREPARATION,
        hierarchy_resolution_mode="sibling-path",
        animation_hierarchy_resolution_modes=("sibling-path",),
    )
    batch_document = {
        "manifest_schema": "openbfme.w3d-batch-jobs",
        "manifest_version": 1,
        "jobs": [job.manifest_row()],
    }
    batch_sha = _sha(_canonical(batch_document))
    terminal = (
        (
            W3DTerminal(
                "terminal-00000000000000000000000000000000",
                _sha(b"terminal"),
                ("unsupported-source",),
            ),
        )
        if accounted
        else ()
    )
    provisional = W3DJobPlan(
        jobs=(job,),
        batches=(W3DJobBatch(f"batch-{batch_sha[:32]}", (job,), batch_sha),),
        terminals=terminal,
        catalog_input_sha256=_sha(b"catalog-input"),
        catalog_metadata_sha256=_sha(b"catalog-metadata"),
        source_count=3 + len(terminal),
        consumed_source_count=3,
        private_plan_sha256=_sha(b"accounted-plan" if accounted else b"strict-plan"),
        evidence_sha256="",
    )
    return replace(
        provisional,
        evidence_sha256=_sha(_canonical(provisional.evidence_hash_basis())),
    )


def _replace_plan_job(plan: W3DJobPlan, job: W3DPlannedJob) -> W3DJobPlan:
    batch = replace(plan.batches[0], jobs=(job,))
    provisional = replace(
        plan,
        jobs=(job,),
        batches=(batch,),
        evidence_sha256="",
    )
    return replace(
        provisional,
        evidence_sha256=_sha(_canonical(provisional.evidence_hash_basis())),
    )


def _source_report(
    root: Path, plan: W3DJobPlan, *, accounted: bool
) -> W3DJobRootReport:
    payloads = {
        "inputs/hierarchy.w3d": _hierarchy(),
        "inputs/idle.w3d": b"exact animation bytes",
        "inputs/model.w3d": _model(),
        "textures/model.png": b"exact png closure bytes",
    }
    kinds = {
        "inputs/hierarchy.w3d": "w3d",
        "inputs/idle.w3d": "w3d",
        "inputs/model.w3d": "w3d",
        "textures/model.png": "texture-png",
    }
    root.mkdir()
    files: list[W3DJobRootFile] = []
    for path, payload in payloads.items():
        target = root.joinpath(*path.split("/"))
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(payload)
        files.append(
            W3DJobRootFile(
                kinds[path],
                path,
                path,
                len(payload),
                _sha(payload),
                "texcopy-fixture" if kinds[path] == "texture-png" else None,
            )
        )
    file_tuple = tuple(files)
    rows = [item.private() for item in file_tuple]
    inventory = _sha(
        _source_canonical(
            {
                "schema": "openbfme.w3d-job-root-inventory",
                "schemaVersion": 0,
                "files": rows,
            },
            newline=False,
        )
    )
    tree_rows = [
        {"path": item.destination_path, "size": item.byte_length, "sha256": item.sha256}
        for item in file_tuple
    ]
    output_tree = _tree_sha("openbfme.w3d-job-root-output-tree-v0", tree_rows)
    hashes = [_sha(f"binding-{index}".encode()) for index in range(8)]
    bindings = {
        "catalogInputSha256": plan.catalog_input_sha256,
        "catalogMetadataSha256": plan.catalog_metadata_sha256,
        "effectiveManifestSha256": hashes[0],
        "effectiveManifestAggregateSha256": hashes[1],
        "stageIdentitySha256": hashes[2],
        "stageManifestSha256": hashes[3],
        "stageOutputTreeSha256": hashes[4],
        "textureClosurePrivatePlanSha256": hashes[5],
        "textureClosureEvidenceSha256": hashes[6],
        "nativeManifestSha256": hashes[7],
        "nativeTextureIdentitySha256": _sha(b"native-identity"),
    }
    limits = {
        "hardMaxFiles": 100_000,
        "hardMaxTotalBytes": 16 * 1024**3,
        "maxFiles": 100,
        "maxTotalBytes": 1024 * 1024,
    }
    summary = {
        "fileCount": len(files),
        "totalBytes": sum(item.byte_length for item in files),
        "w3dFileCount": 3,
        "textureFileCount": 1,
        "glbConversionComplete": False,
        "renderParityProven": False,
    }
    request_basis: dict[str, object] = {
        "schema": "openbfme.w3d-job-root-request",
        "schemaVersion": 0,
        "bindings": bindings,
        "inventorySha256": inventory,
        "outputTreeSha256": output_tree,
        "summary": summary,
        "limits": limits,
    }
    accounting = None
    version = 0
    if accounted:
        version = 1
        accounting = {
            "materializationPolicy": "accounted-planned-jobs-v1",
            "jobPlan": plan.neutral(),
            "textureClosurePlan": {
                "schema": "fixture.texture-closure",
                "complete": True,
            },
            "selectedInstructionIds": ["texcopy-fixture"],
            "selectedW3DSourceIds": ["src-animation", "src-hierarchy", "src-model"],
        }
        request_basis = {
            **request_basis,
            "schema": "openbfme.w3d-job-root-accounted-request",
            "schemaVersion": 1,
            "accounting": accounting,
        }
    request = _sha(_source_canonical(request_basis, newline=False))
    basis: dict[str, object] = {
        "schema": "openbfme.w3d-job-root",
        "schemaVersion": version,
        "bindings": bindings,
        "limits": limits,
        "summary": summary,
        "files": rows,
        "inventorySha256": inventory,
        "outputTreeSha256": output_tree,
        "requestSha256": request,
    }
    if accounting is not None:
        basis["accounting"] = accounting
    identity = _sha(_source_canonical(basis, newline=False))
    document = {**basis, "identitySha256": identity}
    raw = _canonical(document, pretty=True)
    manifest = root.joinpath(*W3D_JOB_ROOT_MANIFEST.split("/"))
    manifest.parent.mkdir(exist_ok=True)
    manifest.write_bytes(raw)
    return W3DJobRootReport(
        input_stage_root=root.parent / "stage",
        native_texture_corpus_root=root.parent / "textures",
        output_root=root,
        manifest_path=manifest,
        files=file_tuple,
        stage_identity_sha256=bindings["stageIdentitySha256"],
        stage_manifest_sha256=bindings["stageManifestSha256"],
        texture_closure_private_plan_sha256=bindings["textureClosurePrivatePlanSha256"],
        texture_closure_evidence_sha256=bindings["textureClosureEvidenceSha256"],
        native_manifest_sha256=bindings["nativeManifestSha256"],
        native_texture_identity_sha256=bindings["nativeTextureIdentitySha256"],
        inventory_sha256=inventory,
        output_tree_sha256=output_tree,
        request_sha256=request,
        identity_sha256=identity,
        manifest_sha256=_sha(raw),
        max_files=limits["maxFiles"],
        max_total_bytes=limits["maxTotalBytes"],
        reused=False,
    )


class W3DPreparedRootTests(unittest.TestCase):
    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)

    def _fixture(self, *, accounted: bool = False):
        model = _model()
        hierarchy = _hierarchy()
        animation = b"exact animation bytes"
        plan = _plan(model, hierarchy, animation, accounted=accounted)
        source = _source_report(self.root / "source", plan, accounted=accounted)
        return plan, source, self.root / "prepared"

    def test_distinct_root_preserves_raw_tree_and_changes_only_declared_model(
        self,
    ) -> None:
        plan, source, destination = self._fixture()
        before = {
            path.relative_to(source.output_root).as_posix(): path.read_bytes()
            for path in source.output_root.rglob("*")
            if path.is_file()
        }

        report = prepare_w3d_execution_root(plan, source, destination)

        after = {
            path.relative_to(source.output_root).as_posix(): path.read_bytes()
            for path in source.output_root.rglob("*")
            if path.is_file()
        }
        self.assertEqual(after, before)
        self.assertNotEqual(
            (destination / "inputs/model.w3d").read_bytes(),
            before["inputs/model.w3d"],
        )
        for path in (
            "inputs/hierarchy.w3d",
            "inputs/idle.w3d",
            "textures/model.png",
            W3D_JOB_ROOT_MANIFEST,
        ):
            self.assertEqual((destination / path).read_bytes(), before[path])
        self.assertEqual(report.output_root, destination.resolve())
        self.assertEqual(len(report.preparations), 1)
        self.assertNotEqual(report.prepared_plan, plan)
        self.assertEqual(
            report.runner_binding().runner_tree_sha256, report.runner_tree_sha256
        )
        neutral = json.dumps(report.neutral(), sort_keys=True)
        self.assertNotIn(str(self.root), neutral)
        self.assertNotIn("inputs/model.w3d", neutral)
        self.assertNotIn("textures/model.png", neutral)
        self.assertTrue(report.neutral()["summary"]["rawSourceUnchanged"])

    def test_resolution_contract_forgery_matrix_fails_before_acceptance(
        self,
    ) -> None:
        model = _model()
        hierarchy = _hierarchy()
        animation = b"exact animation bytes"
        plan = _plan(model, hierarchy, animation, accounted=False)
        job = plan.jobs[0]
        malformed = {
            "missing-hierarchy-mode": replace(job, hierarchy_resolution_mode=None),
            "false-same-source-hierarchy": replace(
                job, hierarchy_resolution_mode="same-source"
            ),
            "case-alias-hierarchy": replace(job, hierarchy=job.model.swapcase()),
            "shared-sibling-id": replace(job, hierarchy_source_id=job.model_source_id),
            "missing-animation-mode": replace(
                job, animation_hierarchy_resolution_modes=()
            ),
            "extra-animation-mode": replace(
                job,
                animation_hierarchy_resolution_modes=(
                    "sibling-path",
                    "sibling-path",
                ),
            ),
            "false-same-source-animation": replace(
                job, animation_hierarchy_resolution_modes=("same-source",)
            ),
            "case-alias-animation": replace(
                job, animations=(job.hierarchy.swapcase(),)
            ),
            "wrong-mode-type": replace(job, hierarchy_resolution_mode=1),
            "wrong-sha-type": replace(job, hierarchy_source_sha256=1),
        }
        for index, (label, forged_job) in enumerate(malformed.items()):
            with self.subTest(label=label):
                forged = _replace_plan_job(plan, forged_job)
                source = _source_report(
                    self.root / f"source-resolution-{index}",
                    forged,
                    accounted=False,
                )
                with self.assertRaisesRegex(
                    W3DPreparedRootError, "resolution contract"
                ) as caught:
                    prepare_w3d_execution_root(
                        forged,
                        source,
                        self.root / f"prepared-resolution-{index}",
                    )
                self.assertNotIn("inputs/", str(caught.exception))
                self.assertFalse((self.root / f"prepared-resolution-{index}").exists())

    def test_exact_noop_reuse_does_not_rewrite_or_reprove_model(self) -> None:
        plan, source, destination = self._fixture()
        first = prepare_w3d_execution_root(plan, source, destination)
        manifest_before = first.manifest_path.read_bytes()
        model_before = (destination / "inputs/model.w3d").read_bytes()

        with patch(
            "openbfme_importer.w3d_job_preparation.strip_proven_redundant_secondary_skin_streams",
            side_effect=AssertionError("reuse must not rerun the rewrite"),
        ):
            reused = prepare_w3d_execution_root(plan, source, destination)

        self.assertTrue(reused.reused)
        self.assertEqual(reused.identity_sha256, first.identity_sha256)
        self.assertEqual(reused.manifest_sha256, first.manifest_sha256)
        self.assertEqual(reused.manifest_path.read_bytes(), manifest_before)
        self.assertEqual((destination / "inputs/model.w3d").read_bytes(), model_before)

    def test_plan_without_rewrites_still_gets_a_distinct_exact_execution_root(
        self,
    ) -> None:
        model = _model()
        hierarchy = _hierarchy()
        animation = b"exact animation bytes"
        prepared_plan = _plan(model, hierarchy, animation, accounted=False)
        job = replace(prepared_plan.jobs[0], model_preparation=None)
        batch = replace(prepared_plan.batches[0], jobs=(job,))
        provisional = replace(
            prepared_plan,
            jobs=(job,),
            batches=(batch,),
            evidence_sha256="",
        )
        plan = replace(
            provisional,
            evidence_sha256=_sha(_canonical(provisional.evidence_hash_basis())),
        )
        source = _source_report(self.root / "source", plan, accounted=False)
        destination = self.root / "prepared"

        first = prepare_w3d_execution_root(plan, source, destination)
        reused = prepare_w3d_execution_root(plan, source, destination)

        self.assertEqual(first.prepared_plan, plan)
        self.assertFalse(first.preparations)
        self.assertTrue(reused.reused)
        self.assertEqual(
            (destination / "inputs/model.w3d").read_bytes(),
            (source.output_root / "inputs/model.w3d").read_bytes(),
        )

    def test_source_payload_and_source_manifest_tamper_are_rejected(self) -> None:
        for relative in ("inputs/model.w3d", W3D_JOB_ROOT_MANIFEST):
            with self.subTest(relative=relative):
                plan, source, destination = self._fixture()
                path = source.output_root / relative
                path.write_bytes(path.read_bytes() + b"tamper")
                with self.assertRaises(W3DPreparedRootError):
                    prepare_w3d_execution_root(plan, source, destination)
                self.assertFalse(destination.exists())
                for child in self.root.iterdir():
                    if child.name != "source":
                        self.assertFalse(child.name.startswith(".prepared.staging-"))
                # A fresh temporary fixture is needed for the next subtest.
                self.tearDown()
                self.setUp()

    def test_prepared_tree_tamper_is_rejected_for_every_protected_role(self) -> None:
        protected = (
            "inputs/model.w3d",
            "inputs/hierarchy.w3d",
            "inputs/idle.w3d",
            "textures/model.png",
            W3D_JOB_ROOT_MANIFEST,
            W3D_PREPARED_ROOT_MANIFEST,
        )
        for relative in protected:
            with self.subTest(relative=relative):
                plan, source, destination = self._fixture()
                prepare_w3d_execution_root(plan, source, destination)
                path = destination / relative
                path.write_bytes(path.read_bytes() + b"tamper")
                with self.assertRaises(W3DPreparedRootReuseError):
                    prepare_w3d_execution_root(plan, source, destination)
                self.tearDown()
                self.setUp()

    def test_force_transactionally_replaces_a_tampered_prepared_root(self) -> None:
        plan, source, destination = self._fixture()
        first = prepare_w3d_execution_root(plan, source, destination)
        (destination / "textures/model.png").write_bytes(b"tampered")

        replaced = prepare_w3d_execution_root(plan, source, destination, force=True)

        self.assertFalse(replaced.reused)
        self.assertEqual(replaced.identity_sha256, first.identity_sha256)
        self.assertEqual(
            (destination / "textures/model.png").read_bytes(),
            b"exact png closure bytes",
        )
        self.assertEqual(
            (source.output_root / "inputs/model.w3d").read_bytes(),
            _model(),
        )

    def test_committed_force_publish_survives_best_effort_backup_cleanup(self) -> None:
        plan, source, destination = self._fixture()
        prepare_w3d_execution_root(plan, source, destination)
        (destination / "textures/model.png").write_bytes(b"tampered")
        remove_owned_tree = prepared_root._remove_owned_tree

        def fail_backup_cleanup(path: Path, parent: Path, prefix: str) -> None:
            if ".backup-" in path.name:
                raise OSError("injected backup cleanup failure")
            remove_owned_tree(path, parent, prefix)

        with patch.object(
            prepared_root,
            "_remove_owned_tree",
            side_effect=fail_backup_cleanup,
        ):
            replaced = prepare_w3d_execution_root(
                plan,
                source,
                destination,
                force=True,
            )

        self.assertFalse(replaced.reused)
        self.assertEqual(
            (destination / "textures/model.png").read_bytes(),
            b"exact png closure bytes",
        )
        self.assertEqual(
            validate_w3d_prepared_root(
                replaced,
                replaced.prepared_plan,
                destination,
                execute_accounted_jobs=False,
            ).identity_sha256,
            replaced.identity_sha256,
        )

    def test_accounted_root_binds_source_job_and_texture_plan(self) -> None:
        plan, source, destination = self._fixture(accounted=True)

        report = prepare_w3d_execution_root(
            plan,
            source,
            destination,
            execute_accounted_jobs=True,
        )
        validated = validate_w3d_prepared_root(
            report,
            report.prepared_plan,
            destination,
            execute_accounted_jobs=True,
        )

        self.assertEqual(validated.identity_sha256, report.identity_sha256)
        self.assertTrue(report.execute_accounted_jobs)
        self.assertTrue(report.prepared_plan.terminals)
        self.assertEqual(
            report.runner_binding().source_identity_sha256,
            source.identity_sha256,
        )

    def test_stale_report_plan_policy_and_root_are_rejected(self) -> None:
        plan, source, destination = self._fixture(accounted=True)
        report = prepare_w3d_execution_root(
            plan, source, destination, execute_accounted_jobs=True
        )
        stale = replace(report, identity_sha256="0" * 64)
        with self.assertRaises(W3DPreparedRootError):
            validate_w3d_prepared_root(
                stale,
                report.prepared_plan,
                destination,
                execute_accounted_jobs=True,
            )
        with self.assertRaises(W3DPreparedRootError):
            validate_w3d_prepared_root(
                report,
                report.prepared_plan,
                destination,
                execute_accounted_jobs=False,
            )
        other = self.root / "other"
        other.mkdir()
        with self.assertRaises(W3DPreparedRootError):
            validate_w3d_prepared_root(
                report,
                report.prepared_plan,
                other,
                execute_accounted_jobs=True,
            )

    def test_accounted_fake_runner_requires_and_binds_the_real_prepared_root(
        self,
    ) -> None:
        plan, source, destination = self._fixture(accounted=True)
        prepared = prepare_w3d_execution_root(
            plan, source, destination, execute_accounted_jobs=True
        )
        tools = self.root / "tools"
        plugin = tools / "plugin"
        plugin.mkdir(parents=True)
        blender = tools / "blender.exe"
        adapter = tools / "adapter.py"
        blender.write_bytes(b"pinned blender")
        adapter.write_bytes(b"pinned adapter")
        adapter.with_name("w3d_to_glb.py").write_bytes(b"pinned converter")
        (plugin / "plugin.py").write_bytes(b"pinned plugin")
        runner = _SuccessfulRunner()

        result = run_w3d_job_plan(
            prepared.prepared_plan,
            blender,
            plugin,
            destination,
            adapter,
            self.root / "converted",
            blender_executable_sha256=_sha(blender.read_bytes()),
            adapter_sha256=_sha(adapter.read_bytes()),
            plugin_tree_sha256=hash_w3d_tool_tree(plugin).sha256,
            job_tree_sha256=prepared.runner_tree_sha256,
            blender_runtime_tree_sha256=hash_w3d_tool_tree(blender.parent).sha256,
            adapter_bundle_tree_sha256=hash_w3d_adapter_bundle(adapter).sha256,
            execute_accounted_jobs=True,
            prepared_root_report=prepared,
            subprocess_runner=runner,
        )

        self.assertEqual(len(runner.commands), 1)
        self.assertTrue(result.job_conversion_complete)
        self.assertFalse(result.conversion_complete)
        self.assertEqual(
            result.neutral()["preparedRootAttestation"],
            prepared.runner_binding().neutral(),
        )
        self.assertEqual(
            (source.output_root / "inputs/model.w3d").read_bytes(), _model()
        )

    def test_runner_rejects_texture_mutated_prepared_report_before_process(
        self,
    ) -> None:
        plan, source, destination = self._fixture(accounted=True)
        prepared = prepare_w3d_execution_root(
            plan, source, destination, execute_accounted_jobs=True
        )
        tools = self.root / "tools"
        plugin = tools / "plugin"
        plugin.mkdir(parents=True)
        blender = tools / "blender.exe"
        adapter = tools / "adapter.py"
        blender.write_bytes(b"pinned blender")
        adapter.write_bytes(b"pinned adapter")
        adapter.with_name("w3d_to_glb.py").write_bytes(b"pinned converter")
        (plugin / "plugin.py").write_bytes(b"pinned plugin")
        (destination / "textures/model.png").write_bytes(b"mutated texture")
        runner = _SuccessfulRunner()

        with self.assertRaisesRegex(W3DBatchRunnerError, "failed attestation"):
            run_w3d_job_plan(
                prepared.prepared_plan,
                blender,
                plugin,
                destination,
                adapter,
                self.root / "converted",
                blender_executable_sha256=_sha(blender.read_bytes()),
                adapter_sha256=_sha(adapter.read_bytes()),
                plugin_tree_sha256=hash_w3d_tool_tree(plugin).sha256,
                blender_runtime_tree_sha256=hash_w3d_tool_tree(blender.parent).sha256,
                adapter_bundle_tree_sha256=hash_w3d_adapter_bundle(adapter).sha256,
                execute_accounted_jobs=True,
                prepared_root_report=prepared,
                subprocess_runner=runner,
            )
        self.assertFalse(runner.commands)
        self.assertFalse((self.root / "converted").exists())

    @unittest.skipUnless(hasattr(os, "link"), "hard links unavailable")
    def test_linked_source_and_prepared_files_are_rejected(self) -> None:
        plan, source, destination = self._fixture()
        source_model = source.output_root / "inputs/model.w3d"
        linked = self.root / "linked-model.w3d"
        source_model.replace(linked)
        os.link(linked, source_model)
        with self.assertRaises(W3DPreparedRootError):
            prepare_w3d_execution_root(plan, source, destination)


if __name__ == "__main__":
    unittest.main()
