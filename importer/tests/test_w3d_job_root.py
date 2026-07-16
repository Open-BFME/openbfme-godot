from __future__ import annotations

from contextlib import contextmanager
from dataclasses import dataclass, replace
import hashlib
import json
import os
from pathlib import Path
import struct
import tempfile
from typing import Iterator
import unittest
from unittest.mock import patch
import zlib

from openbfme_importer.native_backtest import validate_native_output
from openbfme_importer.w3d_catalog import scan_w3d_catalog
from openbfme_importer.w3d_input_stage import build_w3d_input_stage
from openbfme_importer.w3d_job_planner import (
    W3DJobPlan,
    plan_w3d_batches,
)
from openbfme_importer.w3d_job_preparation import (
    MODEL_PREPARATION_PROOF_REJECTED,
    W3DJobPreparationForcedTerminal,
    W3DJobPreparationPreflightReport,
    merge_w3d_preparation_forced_terminals,
    preflight_w3d_job_preparations,
    seal_w3d_job_preparation_fixed_point,
)
from openbfme_importer.w3d_job_root import (
    MAX_W3D_JOB_ROOT_BYTES,
    MAX_W3D_JOB_ROOT_FILES,
    W3DJobRootError,
    W3DJobRootLimitError,
    W3DJobRootReuseError,
    materialize_w3d_job_root,
)
from openbfme_importer.w3d_texture_closure import (
    W3DTextureClosurePlan,
    plan_w3d_texture_closure,
    texture_closure_forced_terminals,
)


def _canonical(
    value: object, *, pretty: bool = False, ensure_ascii: bool = True
) -> bytes:
    options: dict[str, object] = {
        "allow_nan": False,
        "ensure_ascii": ensure_ascii,
        "sort_keys": True,
    }
    if pretty:
        options["indent"] = 2
    else:
        options["separators"] = (",", ":")
    return (json.dumps(value, **options) + "\n").encode("utf-8")


def _seal(value: object) -> str:
    return hashlib.sha256(_canonical(value)).hexdigest()


def _fixed(value: str, size: int) -> bytes:
    encoded = value.encode("ascii")
    if len(encoded) >= size:
        raise ValueError("fixture identifier is too long")
    return encoded + b"\0" * (size - len(encoded))


def _chunk(chunk_id: int, payload: bytes, *, children: bool = False) -> bytes:
    return (
        struct.pack("<II", chunk_id, len(payload) | (0x80000000 if children else 0))
        + payload
    )


def _model(
    name: str,
    *textures: str,
    hierarchy: str = "",
    mesh_attributes: int = 0x0002A000,
    root_reference: bool = False,
) -> bytes:
    model_header = struct.pack(
        "<II16s16s", 0x00040001, 1, _fixed(name, 16), _fixed(hierarchy, 16)
    )
    mesh_header = struct.pack(
        "<II16s16s9I10f",
        0x00040002,
        mesh_attributes,
        _fixed("BODY", 16),
        _fixed(name, 16),
        1,
        3,
        1,
        1,
        0,
        0,
        0,
        0,
        0,
        *[float(value) for value in range(10)],
    )
    texture_chunks = b"".join(
        _chunk(
            0x30,
            _chunk(
                0x31,
                _chunk(0x32, texture.encode("ascii") + b"\0"),
                children=True,
            ),
            children=True,
        )
        for texture in textures
    )
    model_reference = b""
    if root_reference:
        reference = struct.pack("<I32s", 0, _fixed(f"{name}.BODY", 32))
        model_reference = _chunk(
            0x00000702,
            _chunk(0x00000704, reference),
            children=True,
        )
    return (
        _chunk(0x700, _chunk(0x701, model_header), children=True)
        + _chunk(0x1F, mesh_header)
        + _chunk(0x02, b"\0" * 36)
        + _chunk(0x20, b"\0" * 32)
        + texture_chunks
        + model_reference
    )


def _unsafe_root_skin_model(name: str, hierarchy: str) -> bytes:
    model_header = struct.pack(
        "<II16s16s",
        0x00040001,
        1,
        _fixed(name, 16),
        _fixed(hierarchy, 16),
    )
    mesh_header = struct.pack(
        "<II16s16s9I10f",
        0x00040002,
        0x0002A000,
        _fixed("BODY", 16),
        _fixed(name, 16),
        1,
        3,
        1,
        1,
        0,
        0,
        0,
        0,
        0,
        *[float(value) for value in range(10)],
    )
    mesh = (
        _chunk(0x1F, mesh_header)
        + _chunk(0x02, b"\0" * 36)
        + _chunk(0x20, b"\0" * 32)
        + _chunk(0x0E, struct.pack("<4H", 0, 0, 100, 0) * 3)
    )
    return _chunk(
        0x700,
        _chunk(0x701, model_header),
        children=True,
    ) + _chunk(0x00, mesh, children=True)


def _hierarchy(name: str) -> bytes:
    header = struct.pack(
        "<I16sI3f",
        0x00040001,
        _fixed(name, 16),
        1,
        0.0,
        0.0,
        0.0,
    )
    pivot = struct.pack("<16si", _fixed("ROOT", 16), -1) + b"\0" * 40
    return _chunk(
        0x100,
        _chunk(0x101, header) + _chunk(0x102, pivot),
        children=True,
    )


def _animation(name: str, hierarchy: str) -> bytes:
    header = struct.pack(
        "<I16s16sII",
        0x00040001,
        _fixed(name, 16),
        _fixed(hierarchy, 16),
        10,
        30,
    )
    return _chunk(
        0x200,
        _chunk(0x201, header) + _chunk(0x202, b"channel"),
        children=True,
    )


def _embedded_model_animation(name: str, hierarchy: str) -> bytes:
    return (
        _model(name, hierarchy=hierarchy)
        + _hierarchy(hierarchy)
        + _animation("IDLE", hierarchy)
    )


def _png(red: int = 20, green: int = 40, blue: int = 60) -> bytes:
    def chunk(kind: bytes, payload: bytes) -> bytes:
        return (
            struct.pack(">I", len(payload))
            + kind
            + payload
            + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)
        )

    header = struct.pack(">IIBBBBB", 1, 1, 8, 6, 0, 0, 0)
    pixels = zlib.compress(bytes((0, red, green, blue, 255)))
    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", header)
        + chunk(b"IDAT", pixels)
        + chunk(b"IEND", b"")
    )


def _aggregate(files: list[dict[str, object]]) -> str:
    digest = hashlib.sha256()
    for item in files:
        digest.update(str(item["path"]).encode("utf-8"))
        digest.update(b"\0")
        digest.update(str(item["size"]).encode("ascii"))
        digest.update(b"\0")
        digest.update(str(item["sha256"]).encode("ascii"))
        digest.update(b"\n")
    return digest.hexdigest()


def _effective_document(files: dict[str, bytes]) -> dict[str, object]:
    rows: list[dict[str, object]] = []
    offset = 64
    for precedence, (path, payload) in enumerate(
        sorted(files.items(), key=lambda item: (item[0].casefold(), item[0]))
    ):
        rows.append(
            {
                "archive": "fixture.big",
                "offset": offset,
                "path": path,
                "precedence": precedence,
                "sha256": hashlib.sha256(payload).hexdigest(),
                "size": len(payload),
            }
        )
        offset += len(payload)
    return {
        "aggregate_sha256": _aggregate(rows),
        "catalog": {
            "archive_count": 1,
            "entry_count": len(rows),
            "format": 1,
            "identity_sha256": "1" * 64,
        },
        "files": rows,
        "install": {"identity_sha256": "2" * 64, "root": "fixture"},
        "schema": "openbfme.effective-assets-manifest",
        "schema_version": 0,
        "totals": {
            "bytes": sum(len(value) for value in files.values()),
            "files": len(rows),
        },
    }


def _write_effective(
    root: Path,
    files: dict[str, bytes],
    physical_paths: dict[str, str] | None = None,
) -> dict[str, object]:
    for relative, payload in files.items():
        physical = (physical_paths or {}).get(relative, relative)
        path = root.joinpath(*physical.split("/"))
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(payload)
    document = _effective_document(files)
    metadata = root / ".openbfme"
    metadata.mkdir()
    (metadata / "manifest.json").write_bytes(
        _canonical(document, pretty=True, ensure_ascii=False)
    )
    return document


def _write_native(
    root: Path,
    effective: dict[str, object],
    source_files: dict[str, bytes],
    texture_paths: tuple[str, ...],
) -> dict[str, object]:
    effective_rows = {
        str(item["path"]): item
        for item in effective["files"]  # type: ignore[index]
    }
    outputs_by_path: dict[str, dict[str, object]] = {}
    entries: list[dict[str, object]] = []
    request_sources: list[dict[str, object]] = []
    for source_path in sorted(
        texture_paths, key=lambda value: (value.casefold(), value)
    ):
        payload = source_files[source_path]
        digest = hashlib.sha256(payload).hexdigest()
        output_path = f"objects/sha256/{digest[:2]}/{digest}.png"
        path = root.joinpath(*output_path.split("/"))
        path.parent.mkdir(parents=True, exist_ok=True)
        if not path.exists():
            path.write_bytes(payload)
        evidence = validate_native_output("png", path)
        assert evidence["valid"] is True
        output = {
            "path": output_path,
            "bytes": len(payload),
            "sha256": digest,
            "nativeFamily": "png",
            "evidence": evidence,
        }
        outputs_by_path[output_path] = output
        source_row = effective_rows[source_path]
        entries.append(
            {
                "sourcePath": source_path,
                "sourceArchive": source_row["archive"],
                "sourceExtension": ".png",
                "sourceBytes": len(payload),
                "sourceSha256": hashlib.sha256(payload).hexdigest(),
                "family": "texture",
                "outputPath": output_path,
                "outputBytes": len(payload),
                "outputSha256": digest,
                "nativeFamily": "png",
                "evidence": evidence,
            }
        )
        request_sources.append(
            {
                "path": source_path,
                "bytes": len(payload),
                "sha256": hashlib.sha256(payload).hexdigest(),
                "family": "texture",
                "extension": ".png",
                "disposition": "media-conversion",
            }
        )
    outputs = sorted(outputs_by_path.values(), key=lambda item: str(item["path"]))
    effective_raw = _canonical(effective, pretty=True, ensure_ascii=False)
    selection = {
        "families": ["texture"],
        "sourceManifestSha256": hashlib.sha256(effective_raw).hexdigest(),
        "sourceManifestAggregateSha256": effective["aggregate_sha256"],
        "requestSha256": _seal(
            {
                "schema": "openbfme.native-corpus-request",
                "schemaVersion": 0,
                "families": ["texture"],
                "sourceManifestSha256": hashlib.sha256(effective_raw).hexdigest(),
                "sourceManifestAggregateSha256": effective["aggregate_sha256"],
                "conversion": {},
                "sources": request_sources,
            }
        ),
        "conversion": {},
    }
    basis: dict[str, object] = {
        "schema": "openbfme.native-corpus",
        "schemaVersion": 0,
        "selection": selection,
        "totals": {
            "candidateFileCount": len(entries),
            "candidateBytes": sum(int(item["sourceBytes"]) for item in entries),
            "convertedFileCount": len(entries),
            "convertedBytes": sum(int(item["sourceBytes"]) for item in entries),
            "reclassifiedFileCount": 0,
            "reclassifiedBytes": 0,
            "outputFileCount": len(outputs),
            "outputBytes": sum(int(item["bytes"]) for item in outputs),
        },
        "entries": entries,
        "reclassified": [],
        "outputs": outputs,
    }
    document = {**basis, "identitySha256": _seal(basis)}
    root.mkdir(parents=True, exist_ok=True)
    (root / "manifest.json").write_bytes(_canonical(document, pretty=True))
    return document


@dataclass
class _Fixture:
    temporary: tempfile.TemporaryDirectory[str]
    root: Path
    effective_root: Path
    stage_root: Path
    native_root: Path
    output_root: Path
    source_files: dict[str, bytes]
    effective: dict[str, object]
    native: dict[str, object]
    stage_report: object
    plan: W3DTextureClosurePlan

    def close(self) -> None:
        self.temporary.cleanup()


@contextmanager
def _fixture(
    models: dict[str, tuple[str, tuple[str, ...]] | bytes] | None = None,
    textures: dict[str, bytes] | None = None,
    *,
    physical_paths: dict[str, str] | None = None,
) -> Iterator[_Fixture]:
    selected_models = (
        models
        if models is not None
        else {"Models/Hero.w3d": ("Hero", ("Textures/Hero_D.png",))}
    )
    selected_textures = (
        textures if textures is not None else {"Textures/Hero_D.png": _png()}
    )
    temporary = tempfile.TemporaryDirectory()
    root = Path(temporary.name)
    effective_root = root / "effective"
    stage_root = root / "stage"
    native_root = root / "native"
    output_root = root / "job-root"
    source_files: dict[str, bytes] = {}
    for path, model in selected_models.items():
        if isinstance(model, bytes):
            source_files[path] = model
        else:
            name, references = model
            source_files[path] = _model(name, *references)
    source_files.update(selected_textures)
    effective = _write_effective(effective_root, source_files, physical_paths)
    stage_report = build_w3d_input_stage(effective_root, stage_root)
    native = _write_native(
        native_root,
        effective,
        source_files,
        tuple(selected_textures),
    )
    catalog = scan_w3d_catalog({path: source_files[path] for path in selected_models})
    plan = plan_w3d_texture_closure(
        catalog,
        stage_report.staged_inputs,
        effective,
        native,
        source_seal=stage_report.identity_sha256,
    )
    fixture = _Fixture(
        temporary,
        root,
        effective_root,
        stage_root,
        native_root,
        output_root,
        source_files,
        effective,
        native,
        stage_report,
        plan,
    )
    try:
        yield fixture
    finally:
        fixture.close()


def _build(fixture: _Fixture, **kwargs: object):
    return materialize_w3d_job_root(
        fixture.stage_report,  # type: ignore[arg-type]
        fixture.plan,
        fixture.stage_root,
        fixture.native_root,
        fixture.output_root,
        **kwargs,
    )


def _rename_case(path: Path, name: str) -> Path:
    renamed = path.with_name(name)
    temporary = path.with_name(f".{path.name}.case-rename")
    path.replace(temporary)
    temporary.replace(renamed)
    return renamed


def _job_plan(fixture: _Fixture, *, bind_texture_terminals: bool = True) -> W3DJobPlan:
    catalog = scan_w3d_catalog(
        {
            path: payload
            for path, payload in fixture.source_files.items()
            if Path(path).suffix.casefold() == ".w3d"
        }
    )
    kwargs: dict[str, object] = {}
    if bind_texture_terminals:
        forced, seal = texture_closure_forced_terminals(fixture.plan)
        kwargs = {
            "forced_terminal_reasons": forced,
            "forced_terminal_evidence_sha256": seal,
        }
    return plan_w3d_batches(
        catalog,
        fixture.stage_report.staged_inputs,
        **kwargs,
    )


def _reseal_job_plan(plan: W3DJobPlan, **changes: object) -> W3DJobPlan:
    provisional = replace(plan, **changes, evidence_sha256="")
    return replace(
        provisional,
        evidence_sha256=_seal(provisional.evidence_hash_basis()),
    )


def _reseal_preflight_report(
    report: W3DJobPreparationPreflightReport,
    **changes: object,
) -> W3DJobPreparationPreflightReport:
    provisional = replace(report, **changes, evidence_sha256="")
    return replace(
        provisional,
        evidence_sha256=_seal(provisional.evidence_hash_basis()),
    )


def _preflight_report(
    fixture: _Fixture,
    texture_job_plan: W3DJobPlan,
) -> W3DJobPreparationPreflightReport:
    texture_forced, texture_seal = texture_closure_forced_terminals(fixture.plan)
    rejected = next(
        terminal
        for terminal in texture_job_plan.terminals
        if terminal.source_id not in texture_forced
    )
    row = W3DJobPreparationForcedTerminal(
        source_id=rejected.source_id,
        source_sha256=rejected.source_sha256,
    )
    provisional = W3DJobPreparationPreflightReport(
        input_private_plan_sha256=texture_job_plan.private_plan_sha256,
        input_plan_evidence_sha256=texture_job_plan.evidence_sha256,
        catalog_input_sha256=texture_job_plan.catalog_input_sha256,
        catalog_metadata_sha256=texture_job_plan.catalog_metadata_sha256,
        source_count=texture_job_plan.source_count,
        upstream_forced_terminal_rows=tuple(sorted(texture_forced.items())),
        upstream_forced_terminal_evidence_sha256=texture_seal,
        declared_preparation_count=1,
        provable_preparation_count=0,
        rejected_preparation_count=1,
        forced_terminal_rows=(row,),
        skin_safety_report=None,
        evidence_sha256="",
    )
    return _reseal_preflight_report(provisional)


def _job_plan_with_preflight(
    fixture: _Fixture,
    report: W3DJobPreparationPreflightReport,
) -> W3DJobPlan:
    texture_forced, texture_seal = texture_closure_forced_terminals(fixture.plan)
    merged, merged_seal = merge_w3d_preparation_forced_terminals(
        report,
        upstream_forced_terminal_reasons=texture_forced,
        upstream_forced_terminal_evidence_sha256=texture_seal,
    )
    catalog = scan_w3d_catalog(
        {
            path: payload
            for path, payload in fixture.source_files.items()
            if Path(path).suffix.casefold() == ".w3d"
        }
    )
    return plan_w3d_batches(
        catalog,
        fixture.stage_report.staged_inputs,
        forced_terminal_reasons=merged,
        forced_terminal_evidence_sha256=merged_seal,
    )


def _accounted_fixture_models() -> dict[str, tuple[str, tuple[str, ...]] | bytes]:
    blocked = _model("Blocked", "Textures/Blocked.png") + _model("Second")[56:]
    missing = _model("Missing", "Textures/Missing.png") + _model("Third")[56:]
    return {
        "Models/Hero.w3d": ("Hero", ("Textures/Hero_D.png",)),
        "Models/Blocked.w3d": blocked,
        "Models/Missing.w3d": missing,
    }


def _reseal_plan(
    plan: W3DTextureClosurePlan, **changes: object
) -> W3DTextureClosurePlan:
    provisional = replace(
        plan,
        **changes,
        private_plan_sha256="",
        evidence_sha256="",
    )
    with_private = replace(
        provisional,
        private_plan_sha256=_seal(provisional.private_hash_basis()),
    )
    return replace(
        with_private,
        evidence_sha256=_seal(with_private.evidence_hash_basis()),
    )


def _rewrite_destination(
    plan: W3DTextureClosurePlan, index: int, destination: str
) -> W3DTextureClosurePlan:
    old = plan.copy_instructions[index]
    basis = {
        "sourceOutputPath": old.source_output_path,
        "sourceOutputSha256": old.source_output_sha256,
        "sourceOutputBytes": old.source_output_bytes,
        "destinationPath": destination,
        "modelSourceIds": list(old.model_source_ids),
        "referenceEvidenceSha256s": list(old.reference_evidence_sha256s),
    }
    definition = _seal(basis)
    new = replace(
        old,
        destination_path=destination,
        definition_sha256=definition,
        instruction_id=f"texcopy-{definition[:40]}",
    )
    instructions = list(plan.copy_instructions)
    instructions[index] = new
    instructions.sort(key=lambda item: item.instruction_id)
    models = tuple(
        replace(
            model,
            instruction_ids=tuple(
                sorted(
                    new.instruction_id if value == old.instruction_id else value
                    for value in model.instruction_ids
                )
            ),
        )
        for model in plan.models
    )
    return _reseal_plan(plan, copy_instructions=tuple(instructions), models=models)


class W3DJobRootTests(unittest.TestCase):
    def test_strict_fixture_hashes_remain_byte_identical(self) -> None:
        with _fixture() as fixture:
            report = _build(fixture)
            self.assertEqual(
                report.request_sha256,
                "de323f6f29d36e74d4a21a1c3918f4c62ba815ffd3fcf75929cff78571012ac2",
            )
            self.assertEqual(
                report.identity_sha256,
                "b355a63c2bd1a959a44436c7a599396f00dea3be7c734263ba3862762e7e5baa",
            )
            self.assertEqual(
                report.manifest_sha256,
                "726129d26173b040c093a17f7d71da618f216b6e90883d8918b23cc32d922954",
            )
            self.assertEqual(
                report.output_tree_sha256,
                "e60e5849aeff4ea4248144d7a6cc044b0dd8dc09868d8882efe454bcc2053891",
            )
            manifest = json.loads(report.manifest_path.read_text(encoding="utf-8"))
            self.assertEqual(manifest["schemaVersion"], 0)
            self.assertNotIn("accounting", manifest)

    def test_effective_parent_case_aliases_join_one_physical_tree(self) -> None:
        models = {
            "Art/W3D/Hero.w3d": (
                "Hero",
                ("Textures/Hero_D.png", "textures/Guard_D.png"),
            ),
            "art/w3d/Guard.w3d": ("Guard", ()),
        }
        textures = {
            "Textures/Hero_D.png": _png(),
            "textures/Guard_D.png": _png(80, 100, 120),
        }
        with _fixture(
            models,
            textures,
            physical_paths={
                "art/w3d/Guard.w3d": "Art/W3D/Guard.w3d",
                "textures/Guard_D.png": "Textures/Guard_D.png",
            },
        ) as fixture:
            report = _build(fixture)

            self.assertEqual(report.w3d_file_count, 2)
            self.assertEqual(report.texture_file_count, 2)
            self.assertTrue(
                (
                    fixture.output_root / "Art" / "W3D" / "Textures" / "Hero_D.png"
                ).is_file()
            )
            self.assertTrue(
                (
                    fixture.output_root / "Art" / "W3D" / "Textures" / "Guard_D.png"
                ).is_file()
            )
            self.assertEqual(
                [item.source_path for item in report.files if item.kind == "w3d"],
                ["Art/W3D/Guard.w3d", "Art/W3D/Hero.w3d"],
            )
            self.assertEqual(
                [
                    str(item["path"])
                    for item in fixture.effective["files"]  # type: ignore[index]
                ],
                [
                    "art/w3d/Guard.w3d",
                    "Art/W3D/Hero.w3d",
                    "textures/Guard_D.png",
                    "Textures/Hero_D.png",
                ],
            )

    def test_effective_file_case_and_file_directory_collisions_still_reject(
        self,
    ) -> None:
        with _fixture() as fixture:
            document = json.loads(json.dumps(fixture.effective))
            rows = document["files"]
            duplicate = dict(rows[0])
            duplicate["path"] = str(duplicate["path"]).swapcase()
            rows.append(duplicate)
            rows.sort(key=lambda item: (str(item["path"]).casefold(), item["path"]))
            raw = _canonical(document, pretty=True, ensure_ascii=False)
            (fixture.effective_root / ".openbfme" / "manifest.json").write_bytes(raw)
            fixture.stage_report = replace(  # type: ignore[assignment]
                fixture.stage_report,
                source_manifest_sha256=hashlib.sha256(raw).hexdigest(),
            )
            with self.assertRaisesRegex(W3DJobRootError, "paths case-collide"):
                _build(fixture)

        with _fixture() as fixture:
            document = json.loads(json.dumps(fixture.effective))
            rows = document["files"]
            colliding = dict(rows[0])
            colliding["path"] = "Models"
            rows.append(colliding)
            rows.sort(key=lambda item: (str(item["path"]).casefold(), item["path"]))
            raw = _canonical(document, pretty=True, ensure_ascii=False)
            (fixture.effective_root / ".openbfme" / "manifest.json").write_bytes(raw)
            fixture.stage_report = replace(  # type: ignore[assignment]
                fixture.stage_report,
                source_manifest_sha256=hashlib.sha256(raw).hexdigest(),
            )
            with self.assertRaisesRegex(W3DJobRootError, "file/directory collision"):
                _build(fixture)

    def test_effective_physical_join_rejects_extra_missing_and_tampered_files(
        self,
    ) -> None:
        with _fixture() as fixture:
            (fixture.effective_root / "undeclared.bin").write_bytes(b"extra")
            with self.assertRaisesRegex(W3DJobRootError, "one-to-one"):
                _build(fixture)

        with _fixture() as fixture:
            (fixture.effective_root / "Textures" / "Hero_D.png").unlink()
            with self.assertRaisesRegex(W3DJobRootError, "one-to-one"):
                _build(fixture)

        with _fixture() as fixture:
            path = fixture.effective_root / "Textures" / "Hero_D.png"
            payload = path.read_bytes()
            path.write_bytes(bytes((payload[0] ^ 1,)) + payload[1:])
            with self.assertRaisesRegex(W3DJobRootError, "SHA-256"):
                _build(fixture)

        with _fixture() as fixture:
            path = fixture.effective_root / "Models" / "Hero.w3d"
            _rename_case(path, "hero.w3d")
            with self.assertRaisesRegex(W3DJobRootError, "physical file path"):
                _build(fixture)

    def test_stage_and_destination_path_casing_remain_strict(self) -> None:
        with _fixture() as fixture:
            path = fixture.stage_root / "Models" / "Hero.w3d"
            _rename_case(path, "hero.w3d")
            with self.assertRaisesRegex(W3DJobRootError, "path casing changed"):
                _build(fixture)

        with _fixture() as fixture:
            _build(fixture)
            path = fixture.output_root / "Models" / "Hero.w3d"
            _rename_case(path, "hero.w3d")
            with self.assertRaisesRegex(W3DJobRootReuseError, "path casing changed"):
                _build(fixture)

    def test_materializes_png_at_referenced_relative_path_and_reports_no_parity_claim(
        self,
    ) -> None:
        with _fixture() as fixture:
            report = _build(fixture)
            texture = fixture.output_root / "Models" / "Textures" / "Hero_D.png"
            self.assertEqual(report.w3d_file_count, 1)
            self.assertEqual(report.texture_file_count, 1)
            self.assertEqual(report.file_count, 2)
            self.assertTrue((fixture.output_root / "Models" / "Hero.w3d").is_file())
            self.assertTrue(texture.is_file())
            self.assertFalse(report.neutral()["summary"]["glbConversionComplete"])
            self.assertFalse(report.neutral()["summary"]["renderParityProven"])
            self.assertEqual(
                (fixture.output_root / "Models" / "Hero.w3d").stat().st_nlink,
                1,
            )
            self.assertEqual(
                texture.stat().st_nlink,
                1,
            )

    def test_accounted_mode_requires_both_explicit_flag_and_job_plan(self) -> None:
        with _fixture() as fixture:
            job_plan = _job_plan(fixture, bind_texture_terminals=False)
            with self.assertRaises(W3DJobRootError):
                _build(fixture, job_plan=job_plan)
            with self.assertRaises(TypeError):
                _build(fixture, materialize_accounted_jobs=True)
            with self.assertRaises(TypeError):
                _build(fixture, materialize_accounted_jobs=1)
            legacy_empty_bridge = _build(
                fixture,
                job_plan=job_plan,
                materialize_accounted_jobs=True,
            )
            self.assertEqual(legacy_empty_bridge.file_count, 2)
        with _fixture(
            _accounted_fixture_models(),
            {
                "Textures/Hero_D.png": _png(),
                "Textures/Blocked.png": _png(70, 80, 90),
            },
        ) as fixture:
            self.assertFalse(fixture.plan.complete)
            with self.assertRaises(W3DJobRootError):
                _build(fixture)

    def test_accounted_mode_materializes_proven_root_rigid_job(self) -> None:
        models = {
            "Models/RootRigid.w3d": _model(
                "RootRigid",
                hierarchy="RootRig",
                mesh_attributes=0x0000A000,
                root_reference=True,
            ),
            "Models/RootRig.w3d": _hierarchy("RootRig"),
        }
        with _fixture(models, {}) as fixture:
            job_plan = _job_plan(fixture, bind_texture_terminals=False)
            self.assertEqual((len(job_plan.jobs), len(job_plan.terminals)), (1, 0))
            self.assertTrue(job_plan.jobs[0].proven_root_rigid_bake)

            first = _build(
                fixture,
                job_plan=job_plan,
                materialize_accounted_jobs=True,
            )
            second = _build(
                fixture,
                job_plan=job_plan,
                materialize_accounted_jobs=True,
            )

            self.assertFalse(first.reused)
            self.assertTrue(second.reused)
            self.assertEqual(first.w3d_file_count, 2)
            manifest = json.loads(first.manifest_path.read_text(encoding="utf-8"))
            self.assertTrue(
                manifest["accounting"]["jobPlan"]["jobs"][0]["provenRootRigidBake"]
            )

    def test_accounted_mode_materializes_exact_planned_dependency_union(self) -> None:
        textures = {
            "Textures/Hero_D.png": _png(),
            "Textures/Blocked.png": _png(70, 80, 90),
        }
        with _fixture(_accounted_fixture_models(), textures) as fixture:
            job_plan = _job_plan(fixture)
            self.assertEqual((len(job_plan.jobs), len(job_plan.terminals)), (1, 2))
            self.assertEqual(len(fixture.plan.copy_instructions), 2)
            first = _build(
                fixture,
                job_plan=job_plan,
                materialize_accounted_jobs=True,
            )
            second = _build(
                fixture,
                job_plan=job_plan,
                materialize_accounted_jobs=True,
            )
            self.assertFalse(first.reused)
            self.assertTrue(second.reused)
            self.assertEqual((first.w3d_file_count, first.texture_file_count), (1, 1))
            self.assertTrue((fixture.output_root / "Models" / "Hero.w3d").is_file())
            self.assertTrue(
                (fixture.output_root / "Models" / "Textures" / "Hero_D.png").is_file()
            )
            self.assertFalse((fixture.output_root / "Models" / "Blocked.w3d").exists())
            self.assertFalse(
                (fixture.output_root / "Models" / "Textures" / "Blocked.png").exists()
            )
            manifest = json.loads(first.manifest_path.read_text(encoding="utf-8"))
            self.assertEqual(manifest["schemaVersion"], 1)
            accounting = manifest["accounting"]
            self.assertEqual(
                accounting["materializationPolicy"],
                "accounted-planned-jobs-v1",
            )
            self.assertEqual(accounting["jobPlan"], job_plan.neutral())
            self.assertEqual(accounting["textureClosurePlan"], fixture.plan.neutral())
            planned_model = next(
                model
                for model in fixture.plan.models
                if model.staged_model_path == "Models/Hero.w3d"
            )
            self.assertEqual(
                accounting["selectedInstructionIds"],
                list(planned_model.instruction_ids),
            )

    def test_accounted_resolution_contract_forgery_matrix_fails_before_reuse(
        self,
    ) -> None:
        model = _model("Hero", hierarchy="Rig") + _hierarchy("Rig")
        with _fixture({"Models/Hero.w3d": model}, {}) as fixture:
            job_plan = _job_plan(fixture)
            self.assertEqual(len(job_plan.jobs), 1)
            job = job_plan.jobs[0]
            self.assertEqual(job.hierarchy_resolution_mode, "same-source")
            published = _build(
                fixture,
                job_plan=job_plan,
                materialize_accounted_jobs=True,
            )
            manifest_before = published.manifest_path.read_bytes()
            malformed = {
                "missing-mode": replace(job, hierarchy_resolution_mode=None),
                "false-sibling": replace(job, hierarchy_resolution_mode="sibling-path"),
                "case-alias": replace(job, hierarchy=job.model.swapcase()),
                "mismatched-id": replace(
                    job, hierarchy_source_id="src-forged-hierarchy"
                ),
                "mismatched-sha": replace(job, hierarchy_source_sha256="0" * 64),
                "extra-animation-mode": replace(
                    job,
                    animation_hierarchy_resolution_modes=("same-source",),
                ),
                "wrong-mode-type": replace(job, hierarchy_resolution_mode=1),
            }
            for label, forged_job in malformed.items():
                with self.subTest(label=label):
                    forged = _reseal_job_plan(job_plan, jobs=(forged_job,))
                    with self.assertRaisesRegex(
                        W3DJobRootError, "resolution contract"
                    ) as caught:
                        _build(
                            fixture,
                            job_plan=forged,
                            materialize_accounted_jobs=True,
                        )
                    self.assertNotIn("Models/Hero", str(caught.exception))
                    self.assertEqual(
                        published.manifest_path.read_bytes(), manifest_before
                    )

    def test_accounted_exact_embedded_animation_materializes_one_copy(self) -> None:
        payload = _embedded_model_animation("Hero", "Rig")
        with _fixture({"Models/Hero.w3d": payload}, {}) as fixture:
            job_plan = _job_plan(fixture)
            self.assertEqual(
                (
                    len(job_plan.jobs),
                    len(job_plan.terminals),
                    job_plan.consumed_source_count,
                ),
                (1, 0, 1),
            )
            job = job_plan.jobs[0]
            self.assertEqual(job.asset_kind, "animated")
            self.assertEqual(job.animations, (job.model,))
            self.assertEqual(job.hierarchy, job.model)
            self.assertEqual(job.animation_source_ids, (job.model_source_id,))
            self.assertEqual(job.hierarchy_source_id, job.model_source_id)

            first = _build(
                fixture,
                job_plan=job_plan,
                materialize_accounted_jobs=True,
            )
            second = _build(
                fixture,
                job_plan=job_plan,
                materialize_accounted_jobs=True,
            )

            self.assertFalse(first.reused)
            self.assertTrue(second.reused)
            self.assertEqual((first.w3d_file_count, first.texture_file_count), (1, 0))
            manifest = json.loads(first.manifest_path.read_bytes())
            self.assertEqual(
                manifest["accounting"]["selectedW3DSourceIds"],
                [job.model_source_id],
            )
            self.assertEqual(
                (fixture.output_root / "Models" / "Hero.w3d").read_bytes(),
                payload,
            )

    def test_accounted_animated_partial_role_overlap_fails_closed(self) -> None:
        model = _model("Hero", hierarchy="Rig")
        hierarchy = _hierarchy("Rig")
        animation = _animation("Idle", "Rig")
        with _fixture(
            {
                "Models/Hero.w3d": model,
                "Models/Rig.w3d": hierarchy,
                "Models/Idle.w3d": animation,
            },
            {},
        ) as fixture:
            job_plan = _job_plan(fixture)
            job = next(item for item in job_plan.jobs if item.asset_kind == "animated")
            partial = replace(
                job,
                hierarchy=job.model,
                hierarchy_source_id=job.model_source_id,
                hierarchy_source_sha256=job.model_source_sha256,
                hierarchy_resolution_mode="same-source",
            )
            forged = _reseal_job_plan(job_plan, jobs=(partial,))

            with self.assertRaisesRegex(
                W3DJobRootError,
                "dependency accounting contains duplicates",
            ) as caught:
                _build(
                    fixture,
                    job_plan=forged,
                    materialize_accounted_jobs=True,
                )
            self.assertNotIn("Models/", str(caught.exception))
            self.assertFalse(fixture.output_root.exists())

    def test_accounted_mode_rejects_texture_terminal_consumed_by_a_job(self) -> None:
        models = {
            "Models/Hero.w3d": ("Hero", ("Textures/Missing.png",)),
        }
        with _fixture(models, {}) as fixture:
            job_plan = _job_plan(fixture, bind_texture_terminals=False)
            self.assertEqual((len(job_plan.jobs), len(job_plan.terminals)), (1, 0))
            self.assertFalse(fixture.plan.complete)
            _, forced_seal = texture_closure_forced_terminals(fixture.plan)
            candidates = (
                job_plan,
                _reseal_job_plan(
                    job_plan,
                    forced_terminal_evidence_sha256=forced_seal,
                ),
            )
            for candidate in candidates:
                with self.assertRaises(W3DJobRootError):
                    _build(
                        fixture,
                        job_plan=candidate,
                        materialize_accounted_jobs=True,
                    )

    def test_deduplicates_shared_destination_and_supports_zero_texture_models(
        self,
    ) -> None:
        models = {
            "Models/Hero.w3d": ("Hero", ("Textures/Shared.png",)),
            "Models/Guard.w3d": ("Guard", ("Textures/Shared.png",)),
        }
        with _fixture(models, {"Textures/Shared.png": _png()}) as fixture:
            self.assertEqual(len(fixture.plan.copy_instructions), 1)
            report = _build(fixture)
            self.assertEqual((report.w3d_file_count, report.texture_file_count), (2, 1))
        with _fixture(
            {"Models/Plain.w3d": ("Plain", ())},
            {},
        ) as fixture:
            report = _build(fixture)
            self.assertEqual((report.w3d_file_count, report.texture_file_count), (1, 0))
            self.assertEqual(report.file_count, 1)

    def test_reuse_tamper_extra_and_force_rebuild_are_fail_closed(self) -> None:
        with _fixture() as fixture:
            first = _build(fixture)
            second = _build(fixture)
            self.assertFalse(first.reused)
            self.assertTrue(second.reused)
            self.assertEqual(first.identity_sha256, second.identity_sha256)
            payload = fixture.output_root / "Models" / "Hero.w3d"
            payload.write_bytes(b"tampered")
            with self.assertRaises(W3DJobRootReuseError):
                _build(fixture)
            rebuilt = _build(fixture, force=True)
            self.assertFalse(rebuilt.reused)
            self.assertEqual(
                payload.read_bytes(), fixture.source_files["Models/Hero.w3d"]
            )
            extra = fixture.output_root / "extra.bin"
            extra.write_bytes(b"extra")
            with self.assertRaises(W3DJobRootReuseError):
                _build(fixture)
            _build(fixture, force=True)
            self.assertFalse(extra.exists())

    def test_stage_and_native_source_tamper_are_rejected(self) -> None:
        with _fixture() as fixture:
            staged = fixture.stage_root / "Models" / "Hero.w3d"
            staged.write_bytes(b"tampered")
            with self.assertRaises(W3DJobRootError):
                _build(fixture)
        with _fixture() as fixture:
            instruction = fixture.plan.copy_instructions[0]
            native = fixture.native_root.joinpath(
                *instruction.source_output_path.split("/")
            )
            native.write_bytes(_png(90, 80, 70))
            with self.assertRaises(W3DJobRootError):
                _build(fixture)

    def test_incomplete_plan_and_forged_seals_or_bindings_are_rejected(self) -> None:
        with _fixture() as fixture:
            model = replace(fixture.plan.models[0], terminal_reasons=("missing",))
            fixture.plan = _reseal_plan(fixture.plan, models=(model,))
            with self.assertRaises(W3DJobRootError):
                _build(fixture)
        with _fixture() as fixture:
            fixture.plan = replace(fixture.plan, evidence_sha256="0" * 64)
            with self.assertRaises(W3DJobRootError):
                _build(fixture)
        with _fixture() as fixture:
            fixture.plan = _reseal_plan(fixture.plan, source_seal="0" * 64)
            with self.assertRaises(W3DJobRootError):
                _build(fixture)
        with _fixture() as fixture:
            fixture.plan = _reseal_plan(fixture.plan, catalog_input_sha256="0" * 64)
            with self.assertRaises(W3DJobRootError):
                _build(fixture)
        with _fixture() as fixture:
            fixture.stage_report = replace(
                fixture.stage_report,
                identity_sha256="0" * 64,  # type: ignore[arg-type]
            )
            with self.assertRaises(W3DJobRootError):
                _build(fixture)

    def test_accounted_mode_rejects_gap_overlap_and_duplicate_accounting(
        self,
    ) -> None:
        textures = {
            "Textures/Hero_D.png": _png(),
            "Textures/Blocked.png": _png(70, 80, 90),
        }
        with _fixture(_accounted_fixture_models(), textures) as fixture:
            job_plan = _job_plan(fixture)
            terminal = job_plan.terminals[0]
            job = job_plan.jobs[0]
            malformed = {
                "gap": _reseal_job_plan(
                    job_plan,
                    source_count=job_plan.source_count + 1,
                ),
                "consumed-cardinality": _reseal_job_plan(
                    job_plan,
                    consumed_source_count=job_plan.consumed_source_count + 1,
                ),
                "overlap": _reseal_job_plan(
                    job_plan,
                    terminals=(
                        replace(
                            terminal,
                            source_id=job.model_source_id,
                            source_sha256=job.model_source_sha256,
                        ),
                    ),
                ),
                "duplicate-terminal": _reseal_job_plan(
                    job_plan,
                    terminals=(terminal, terminal),
                ),
            }
            for label, candidate in malformed.items():
                with self.subTest(label=label):
                    with self.assertRaises(W3DJobRootError):
                        _build(
                            fixture,
                            job_plan=candidate,
                            materialize_accounted_jobs=True,
                        )

    def test_accounted_mode_rejects_catalog_and_source_seal_mismatches(self) -> None:
        textures = {
            "Textures/Hero_D.png": _png(),
            "Textures/Blocked.png": _png(70, 80, 90),
        }
        with _fixture(_accounted_fixture_models(), textures) as fixture:
            job_plan = _job_plan(fixture)
            for field in ("catalog_input_sha256", "catalog_metadata_sha256"):
                with self.subTest(field=field):
                    forged = _reseal_job_plan(job_plan, **{field: "0" * 64})
                    with self.assertRaises(W3DJobRootError):
                        _build(
                            fixture,
                            job_plan=forged,
                            materialize_accounted_jobs=True,
                        )
        with _fixture(_accounted_fixture_models(), textures) as fixture:
            job_plan = _job_plan(fixture)
            fixture.plan = _reseal_plan(fixture.plan, source_seal="0" * 64)
            with self.assertRaises(W3DJobRootError):
                _build(
                    fixture,
                    job_plan=job_plan,
                    materialize_accounted_jobs=True,
                )

    def test_accounted_mode_requires_exact_texture_terminal_bridge(self) -> None:
        textures = {
            "Textures/Hero_D.png": _png(),
            "Textures/Blocked.png": _png(70, 80, 90),
        }
        with _fixture(_accounted_fixture_models(), textures) as fixture:
            unbound = _job_plan(fixture, bind_texture_terminals=False)
            with self.assertRaises(W3DJobRootError):
                _build(
                    fixture,
                    job_plan=unbound,
                    materialize_accounted_jobs=True,
                )

            job_plan = _job_plan(fixture)
            forged_seal = _reseal_job_plan(
                job_plan,
                forced_terminal_evidence_sha256="f" * 64,
            )
            with self.assertRaises(W3DJobRootError):
                _build(
                    fixture,
                    job_plan=forged_seal,
                    materialize_accounted_jobs=True,
                )

            assert job_plan.forced_terminal_rows
            first_source_id, first_reasons = job_plan.forced_terminal_rows[0]
            extra_rows = (
                (
                    first_source_id,
                    tuple(sorted((*first_reasons, "unrelated-forced-omission"))),
                ),
                *job_plan.forced_terminal_rows[1:],
            )
            extra_terminals = tuple(
                replace(
                    terminal,
                    reason_codes=tuple(
                        sorted((*terminal.reason_codes, "unrelated-forced-omission"))
                    ),
                )
                if terminal.source_id == first_source_id
                else terminal
                for terminal in job_plan.terminals
            )
            forged_rows = _reseal_job_plan(
                job_plan,
                forced_terminal_rows=extra_rows,
                terminals=extra_terminals,
            )
            with self.assertRaisesRegex(
                W3DJobRootError,
                "forced-terminal rows mismatch the texture bridge",
            ):
                _build(
                    fixture,
                    job_plan=forged_rows,
                    materialize_accounted_jobs=True,
                )

            forced, _ = texture_closure_forced_terminals(fixture.plan)
            forced_source_id = next(iter(forced))
            terminals = tuple(
                replace(
                    terminal,
                    reason_codes=tuple(
                        reason
                        for reason in terminal.reason_codes
                        if reason not in forced[forced_source_id]
                    ),
                )
                if terminal.source_id == forced_source_id
                else terminal
                for terminal in job_plan.terminals
            )
            missing_reasons = _reseal_job_plan(job_plan, terminals=terminals)
            with self.assertRaises(W3DJobRootError):
                _build(
                    fixture,
                    job_plan=missing_reasons,
                    materialize_accounted_jobs=True,
                )

    def test_accounted_combined_texture_and_preflight_bridge_reuses_exactly(
        self,
    ) -> None:
        textures = {
            "Textures/Hero_D.png": _png(),
            "Textures/Blocked.png": _png(70, 80, 90),
        }
        with _fixture(_accounted_fixture_models(), textures) as fixture:
            texture_job_plan = _job_plan(fixture)
            report = _preflight_report(fixture, texture_job_plan)
            job_plan = _job_plan_with_preflight(fixture, report)
            texture_forced, texture_seal = texture_closure_forced_terminals(
                fixture.plan
            )
            merged, merged_seal = merge_w3d_preparation_forced_terminals(
                report,
                upstream_forced_terminal_reasons=texture_forced,
                upstream_forced_terminal_evidence_sha256=texture_seal,
            )

            first = _build(
                fixture,
                job_plan=job_plan,
                materialize_accounted_jobs=True,
                preparation_preflight_report=report,
            )
            second = _build(
                fixture,
                job_plan=job_plan,
                materialize_accounted_jobs=True,
                preparation_preflight_report=report,
            )

            self.assertFalse(first.reused)
            self.assertTrue(second.reused)
            self.assertEqual((first.w3d_file_count, first.texture_file_count), (1, 1))
            self.assertEqual(
                job_plan.forced_terminal_rows, tuple(sorted(merged.items()))
            )
            self.assertEqual(job_plan.forced_terminal_evidence_sha256, merged_seal)
            rejected_id = report.forced_terminal_rows[0].source_id
            rejected_terminal = next(
                terminal
                for terminal in job_plan.terminals
                if terminal.source_id == rejected_id
            )
            self.assertIn(
                MODEL_PREPARATION_PROOF_REJECTED,
                rejected_terminal.reason_codes,
            )
            private_manifest = json.dumps(
                json.loads(first.manifest_path.read_bytes()),
                sort_keys=True,
            )
            self.assertNotIn("preparationPreflight", private_manifest)
            self.assertNotIn(report.evidence_sha256, private_manifest)

    def test_combined_plan_requires_report_and_strict_mode_rejects_report(
        self,
    ) -> None:
        textures = {
            "Textures/Hero_D.png": _png(),
            "Textures/Blocked.png": _png(70, 80, 90),
        }
        with _fixture(_accounted_fixture_models(), textures) as fixture:
            texture_job_plan = _job_plan(fixture)
            report = _preflight_report(fixture, texture_job_plan)
            combined = _job_plan_with_preflight(fixture, report)

            with self.assertRaises(W3DJobRootError) as missing:
                _build(
                    fixture,
                    job_plan=combined,
                    materialize_accounted_jobs=True,
                )
            with self.assertRaisesRegex(
                W3DJobRootError,
                "preparation preflight requires materialize_accounted_jobs=True",
            ) as strict:
                _build(fixture, preparation_preflight_report=report)

            for error in (missing.exception, strict.exception):
                self.assertNotIn("Models/", str(error))
                self.assertNotIn("Hero", str(error))
            self.assertFalse(fixture.output_root.exists())

    def test_accounted_preparation_fixed_point_is_bound_and_reuses_exactly(
        self,
    ) -> None:
        models = {
            "Models/Safe.w3d": _model("Safe"),
            "Models/Unsafe.w3d": _unsafe_root_skin_model(
                "Unsafe",
                "UnsafeRig",
            ),
            "Models/UnsafeRig.w3d": _hierarchy("UnsafeRig"),
        }
        with _fixture(models, {}) as fixture:
            texture_job_plan = _job_plan(fixture)
            rejecting = preflight_w3d_job_preparations(
                texture_job_plan,
                fixture.stage_root,
                execute_accounted_jobs=True,
            )
            self.assertIsNotNone(rejecting.skin_safety_report)
            self.assertEqual(len(rejecting.forced_terminal_rows), 1)
            job_plan = _job_plan_with_preflight(fixture, rejecting)
            final = preflight_w3d_job_preparations(
                job_plan,
                fixture.stage_root,
                execute_accounted_jobs=True,
            )
            self.assertIsNotNone(final.skin_safety_report)
            self.assertEqual(final.forced_terminal_rows, ())
            fixed_point = seal_w3d_job_preparation_fixed_point(
                (rejecting, final),
                job_plan,
            )

            first = _build(
                fixture,
                job_plan=job_plan,
                materialize_accounted_jobs=True,
                preparation_fixed_point_report=fixed_point,
            )
            second = _build(
                fixture,
                job_plan=job_plan,
                materialize_accounted_jobs=True,
                preparation_fixed_point_report=fixed_point,
            )

            self.assertFalse(first.reused)
            self.assertTrue(second.reused)
            manifest = json.loads(first.manifest_path.read_bytes())
            self.assertEqual(manifest["schemaVersion"], 2)
            self.assertEqual(
                manifest["accounting"]["preparationFixedPoint"],
                {
                    "evidenceSha256": fixed_point.evidence_sha256,
                    "iterationCount": 2,
                },
            )
            private_manifest = json.dumps(manifest, sort_keys=True)
            self.assertNotIn("iterations", private_manifest)
            self.assertNotIn("preparationPreflight", private_manifest)

            rejected = rejecting.forced_terminal_rows[0]
            missing_reason_plan = _reseal_job_plan(
                job_plan,
                terminals=tuple(
                    replace(
                        terminal,
                        reason_codes=("unrelated-forced-omission",),
                    )
                    if terminal.source_id == rejected.source_id
                    else terminal
                    for terminal in job_plan.terminals
                ),
            )
            missing_reason_final = preflight_w3d_job_preparations(
                missing_reason_plan,
                fixture.stage_root,
                execute_accounted_jobs=True,
            )
            missing_reason_fixed_point = seal_w3d_job_preparation_fixed_point(
                (rejecting, missing_reason_final),
                missing_reason_plan,
            )
            with self.assertRaisesRegex(
                W3DJobRootError,
                "preparation-preflight rejection",
            ) as missing_reason:
                _build(
                    fixture,
                    job_plan=missing_reason_plan,
                    materialize_accounted_jobs=True,
                    preparation_fixed_point_report=missing_reason_fixed_point,
                    force=True,
                )
            self.assertNotIn("Models/", str(missing_reason.exception))
            self.assertNotIn("Unsafe", str(missing_reason.exception))

            tampered = replace(fixed_point, evidence_sha256="f" * 64)
            with self.assertRaisesRegex(W3DJobRootError, "fixed-point evidence"):
                _build(
                    fixture,
                    job_plan=job_plan,
                    materialize_accounted_jobs=True,
                    preparation_fixed_point_report=tampered,
                    force=True,
                )
            with self.assertRaisesRegex(W3DJobRootError, "mutually exclusive"):
                _build(
                    fixture,
                    job_plan=job_plan,
                    materialize_accounted_jobs=True,
                    preparation_preflight_report=rejecting,
                    preparation_fixed_point_report=fixed_point,
                    force=True,
                )

    def test_preflight_bridge_forgery_matrix_fails_closed_and_name_free(
        self,
    ) -> None:
        textures = {
            "Textures/Hero_D.png": _png(),
            "Textures/Blocked.png": _png(70, 80, 90),
        }
        with _fixture(_accounted_fixture_models(), textures) as fixture:
            texture_job_plan = _job_plan(fixture)
            report = _preflight_report(fixture, texture_job_plan)
            valid_plan = _job_plan_with_preflight(fixture, report)

            bad_evidence = replace(report, evidence_sha256="0" * 64)
            bad_upstream = _reseal_preflight_report(
                report,
                upstream_forced_terminal_evidence_sha256="0" * 64,
            )
            bad_catalog_input = _reseal_preflight_report(
                report,
                catalog_input_sha256="0" * 64,
            )
            bad_catalog_metadata = _reseal_preflight_report(
                report,
                catalog_metadata_sha256="0" * 64,
            )
            bad_source_count = _reseal_preflight_report(
                report,
                source_count=report.source_count + 1,
            )
            row = report.forced_terminal_rows[0]
            bad_sha_report = _reseal_preflight_report(
                report,
                forced_terminal_rows=(replace(row, source_sha256="0" * 64),),
            )
            bad_reason_row = W3DJobPreparationForcedTerminal(
                source_id=row.source_id,
                source_sha256=row.source_sha256,
            )
            object.__setattr__(
                bad_reason_row,
                "reason_codes",
                ("forged-preparation-reason",),
            )
            bad_reason_report = _reseal_preflight_report(
                report,
                forced_terminal_rows=(bad_reason_row,),
            )

            catalog_input_plan = _job_plan_with_preflight(fixture, bad_catalog_input)
            catalog_metadata_plan = _job_plan_with_preflight(
                fixture, bad_catalog_metadata
            )
            source_count_plan = _job_plan_with_preflight(fixture, bad_source_count)
            bad_sha_plan = _job_plan_with_preflight(fixture, bad_sha_report)
            bad_merged_seal_plan = _reseal_job_plan(
                valid_plan,
                forced_terminal_evidence_sha256="f" * 64,
            )
            rejected_id = row.source_id
            missing_reason_terminals = tuple(
                replace(
                    terminal,
                    reason_codes=tuple(
                        reason
                        for reason in terminal.reason_codes
                        if reason != MODEL_PREPARATION_PROOF_REJECTED
                    ),
                )
                if terminal.source_id == rejected_id
                else terminal
                for terminal in valid_plan.terminals
            )
            missing_reason_plan = _reseal_job_plan(
                valid_plan,
                terminals=missing_reason_terminals,
            )

            malformed = {
                "report-seal": (valid_plan, bad_evidence),
                "upstream-seal": (valid_plan, bad_upstream),
                "catalog-input": (catalog_input_plan, bad_catalog_input),
                "catalog-metadata": (
                    catalog_metadata_plan,
                    bad_catalog_metadata,
                ),
                "source-count": (source_count_plan, bad_source_count),
                "row-sha": (bad_sha_plan, bad_sha_report),
                "row-reason": (valid_plan, bad_reason_report),
                "merged-seal": (bad_merged_seal_plan, report),
                "terminal-reason": (missing_reason_plan, report),
            }
            for label, (candidate_plan, candidate_report) in malformed.items():
                with self.subTest(label=label):
                    with self.assertRaises(W3DJobRootError) as caught:
                        _build(
                            fixture,
                            job_plan=candidate_plan,
                            materialize_accounted_jobs=True,
                            preparation_preflight_report=candidate_report,
                        )
                    self.assertNotIn("Models/", str(caught.exception))
                    self.assertNotIn("Hero", str(caught.exception))
                    self.assertFalse(fixture.output_root.exists())

    def test_accounted_policy_and_terminal_reason_changes_invalidate_reuse(
        self,
    ) -> None:
        with _fixture() as fixture:
            strict = _build(fixture)
            job_plan = _job_plan(fixture)
            with self.assertRaises(W3DJobRootReuseError):
                _build(
                    fixture,
                    job_plan=job_plan,
                    materialize_accounted_jobs=True,
                )
            accounted = _build(
                fixture,
                job_plan=job_plan,
                materialize_accounted_jobs=True,
                force=True,
            )
            self.assertNotEqual(strict.identity_sha256, accounted.identity_sha256)

        textures = {
            "Textures/Hero_D.png": _png(),
            "Textures/Blocked.png": _png(70, 80, 90),
        }
        with _fixture(_accounted_fixture_models(), textures) as fixture:
            job_plan = _job_plan(fixture)
            first = _build(
                fixture,
                job_plan=job_plan,
                materialize_accounted_jobs=True,
            )
            models = tuple(
                replace(model, terminal_reasons=("changed-texture-terminal",))
                if model.terminal_reasons
                else model
                for model in fixture.plan.models
            )
            fixture.plan = _reseal_plan(fixture.plan, models=models)
            changed_job_plan = _job_plan(fixture)
            with self.assertRaises(W3DJobRootReuseError):
                _build(
                    fixture,
                    job_plan=changed_job_plan,
                    materialize_accounted_jobs=True,
                )
            rebuilt = _build(
                fixture,
                job_plan=changed_job_plan,
                materialize_accounted_jobs=True,
                force=True,
            )
            self.assertNotEqual(first.identity_sha256, rebuilt.identity_sha256)

    def test_unsafe_and_case_colliding_destinations_are_rejected(self) -> None:
        textures = {
            "Textures/Hero_D.png": _png(),
            "Textures/Hero_N.png": _png(80, 90, 100),
        }
        models = {
            "Models/Hero.w3d": (
                "Hero",
                ("Textures/Hero_D.png", "Textures/Hero_N.png"),
            )
        }
        with _fixture(models, textures) as fixture:
            fixture.plan = _rewrite_destination(fixture.plan, 0, "../escape.png")
            with self.assertRaises(W3DJobRootError):
                _build(fixture)
        with _fixture(models, textures) as fixture:
            first_destination = fixture.plan.copy_instructions[0].destination_path
            fixture.plan = _rewrite_destination(
                fixture.plan,
                1,
                first_destination.swapcase(),
            )
            with self.assertRaises(W3DJobRootError):
                _build(fixture)

    def test_limits_only_lower_hard_caps_and_never_truncate(self) -> None:
        with _fixture() as fixture:
            with self.assertRaises(W3DJobRootLimitError):
                _build(fixture, max_files=1)
            with self.assertRaises(W3DJobRootLimitError):
                _build(fixture, max_total_bytes=1)
            with self.assertRaises(ValueError):
                _build(fixture, max_files=MAX_W3D_JOB_ROOT_FILES + 1)
            with self.assertRaises(ValueError):
                _build(fixture, max_total_bytes=MAX_W3D_JOB_ROOT_BYTES + 1)
            with self.assertRaises(TypeError):
                _build(fixture, max_files=True)
            with self.assertRaises(TypeError):
                _build(fixture, force=1)

    def test_source_symlink_is_rejected_when_supported(self) -> None:
        with _fixture() as fixture:
            staged = fixture.stage_root / "Models" / "Hero.w3d"
            original = staged.with_suffix(".original")
            staged.replace(original)
            try:
                os.symlink(original, staged)
            except OSError as exc:
                self.skipTest(f"symlink creation is unavailable: {exc}")
            with self.assertRaises(W3DJobRootError):
                _build(fixture)

    def test_post_publish_source_mutation_rolls_back_prior_output(self) -> None:
        with _fixture() as fixture:
            first = _build(fixture)
            prior_manifest = first.manifest_path.read_bytes()
            staged = fixture.stage_root / "Models" / "Hero.w3d"
            from openbfme_importer import w3d_job_root as module

            original = module._revalidate_sources
            calls = 0

            def mutate_on_post_publish(*args: object, **kwargs: object) -> None:
                nonlocal calls
                calls += 1
                if calls == 2:
                    staged.write_bytes(b"changed-after-publication")
                original(*args, **kwargs)

            with patch.object(module, "_revalidate_sources", mutate_on_post_publish):
                with self.assertRaises(W3DJobRootError):
                    _build(fixture, force=True)
            self.assertEqual(
                (fixture.output_root / ".openbfme" / "w3d-job-root.json").read_bytes(),
                prior_manifest,
            )

    def test_repeat_to_distinct_roots_is_deterministic(self) -> None:
        with _fixture() as fixture:
            first = _build(fixture)
            second_root = fixture.root / "job-root-second"
            second = materialize_w3d_job_root(
                fixture.stage_report,  # type: ignore[arg-type]
                fixture.plan,
                fixture.stage_root,
                fixture.native_root,
                second_root,
            )
            self.assertEqual(first.identity_sha256, second.identity_sha256)
            self.assertEqual(first.output_tree_sha256, second.output_tree_sha256)
            self.assertEqual(
                first.manifest_path.read_bytes(), second.manifest_path.read_bytes()
            )


if __name__ == "__main__":
    unittest.main()
