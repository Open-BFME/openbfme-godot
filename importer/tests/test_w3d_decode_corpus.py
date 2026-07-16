from __future__ import annotations

from contextlib import redirect_stdout
from dataclasses import FrozenInstanceError
import hashlib
import io
import json
import os
from pathlib import Path
import struct
import tempfile
import unittest
from unittest import mock

import openbfme_importer.w3d_corpus as w3d_corpus_module
import openbfme_importer.w3d_decode_corpus as decode_corpus_module

from openbfme_importer.w3d_decode_corpus import (
    W3DDecodeCorpusError,
    W3DDecodeCorpusLimitError,
    W3DDecodeCorpusStrictError,
    scan_w3d_decode_corpus,
)


def _fixed(value: str) -> bytes:
    encoded = value.encode("ascii")
    return encoded + b"\0" * (16 - len(encoded))


def _chunk(
    chunk_id: int,
    payload: bytes,
    *,
    container: bool = False,
    declared_size: int | None = None,
) -> bytes:
    size = len(payload) if declared_size is None else declared_size
    if container:
        size |= 0x80000000
    return struct.pack("<II", chunk_id, size) + payload


def _mesh_header(name: str, *, vertices: int = 1) -> bytes:
    return struct.pack(
        "<II16s16s9I10f",
        0x00040000,
        0,
        _fixed(name),
        _fixed("Owner"),
        0,
        vertices,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        *([0.0] * 10),
    )


def _complete_mesh(name: str = "Mesh", value: float = 1.0) -> bytes:
    children = _chunk(0x1F, _mesh_header(name))
    children += _chunk(0x02, struct.pack("<3f", value, value + 1, value + 2))
    return _chunk(0x00, children, container=True)


def _unresolved_mesh() -> bytes:
    return _chunk(0x02, struct.pack("<3f", 1.0, 2.0, 3.0))


def _unsupported_mesh() -> bytes:
    source = _complete_mesh("Unsupported")
    return source + _chunk(0xDEADBEEF, b"opaque")


def _damaged_mesh() -> bytes:
    return _chunk(0x02, b"\0" * 4, declared_size=12)


def _pivot(name: str = "Root", *, marker: float = 0.0) -> bytes:
    values = [marker] + [0.0] * 9
    return struct.pack("<16si10f", _fixed(name), -1, *values)


def _hierarchy(
    name: str = "Rig",
    *,
    pivots: int = 1,
    marker: float = 0.0,
) -> bytes:
    header = struct.pack("<I16sI12x", 0x00040000, _fixed(name), pivots)
    records = b"".join(
        _pivot(f"P{index}", marker=marker + index) for index in range(pivots)
    )
    return _chunk(
        0x100,
        _chunk(0x101, header) + _chunk(0x102, records),
        container=True,
    )


def _raw_animation(
    *,
    hierarchy: str = "Rig",
    frames: int = 1,
    pivot: int = 0,
) -> bytes:
    header = struct.pack(
        "<I16s16sII",
        0x00040000,
        _fixed("PrivateWalk"),
        _fixed(hierarchy),
        frames,
        30,
    )
    channel = struct.pack("<6Hf", 0, 0, frames, 0, pivot, 0, 1.0)
    return _chunk(
        0x200,
        _chunk(0x201, header) + _chunk(0x202, channel),
        container=True,
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


def _manifest(files: dict[str, bytes]) -> dict[str, object]:
    inventory: list[dict[str, object]] = []
    offset = 64
    for precedence, (path, payload) in enumerate(
        sorted(files.items(), key=lambda item: item[0].casefold())
    ):
        inventory.append(
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
        "schema": "openbfme.effective-assets-manifest",
        "schema_version": 0,
        "catalog": {
            "archive_count": 1,
            "entry_count": len(inventory),
            "format": 4,
            "identity_sha256": "a" * 64,
        },
        "install": {
            "identity_sha256": "b" * 64,
            "root": "synthetic-install",
        },
        "totals": {
            "bytes": sum(len(payload) for payload in files.values()),
            "files": len(files),
        },
        "aggregate_sha256": _aggregate(inventory),
        "files": inventory,
    }


def _write_manifest(root: Path, manifest: dict[str, object]) -> None:
    metadata = root / ".openbfme"
    metadata.mkdir(parents=True, exist_ok=True)
    (metadata / "manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def _write_corpus(root: Path, files: dict[str, bytes]) -> dict[str, object]:
    for relative, payload in files.items():
        target = root.joinpath(*relative.split("/"))
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(payload)
    manifest = _manifest(files)
    _write_manifest(root, manifest)
    return manifest


def _contains_bytes(value: object) -> bool:
    if isinstance(value, (bytes, bytearray, memoryview)):
        return True
    if isinstance(value, dict):
        return any(
            _contains_bytes(key) or _contains_bytes(item) for key, item in value.items()
        )
    if isinstance(value, (list, tuple)):
        return any(_contains_bytes(item) for item in value)
    return False


class W3DDecodeCorpusCoverageTests(unittest.TestCase):
    def test_hundreds_are_streamed_without_omission(self) -> None:
        files = {
            f"art/models/model-{index:03d}.w3d": _complete_mesh(
                f"M{index:03d}", float(index)
            )
            for index in range(257)
        }
        files["data/ignored.bin"] = b"not a W3D"
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            _write_corpus(root, files)
            stdout = io.StringIO()
            with redirect_stdout(stdout):
                report = scan_w3d_decode_corpus(root)

        self.assertEqual(stdout.getvalue(), "")
        self.assertTrue(report.complete)
        self.assertEqual(report.selected_file_count, 257)
        self.assertEqual(report.scanned_file_count, 257)
        self.assertEqual(report.unique_source_count, 257)
        self.assertEqual(report.stream_complete_file_count, 257)
        self.assertEqual(report.incomplete_file_count, 0)
        self.assertEqual(sum(item.manifest_entry_count for item in report.sources), 257)
        self.assertEqual(
            {item.domain: item.decoded_stream_count for item in report.domains},
            {"geometry": 257},
        )
        evidence = report.json_ready()
        encoded = json.dumps(evidence, sort_keys=True)
        self.assertFalse(_contains_bytes(evidence))
        self.assertNotIn(str(root).casefold(), encoded.casefold())
        self.assertNotIn("model-000", encoded.casefold())
        self.assertNotIn("owner", encoded.casefold())
        self.assertEqual(len(report.inventory_sha256), 64)
        self.assertEqual(len(report.plan_set_sha256), 64)
        self.assertEqual(len(report.corpus_sha256), 64)
        with self.assertRaises(FrozenInstanceError):
            report.selected_file_count = 0  # type: ignore[misc]

    def test_duplicate_payloads_are_planned_once_but_fully_accounted(self) -> None:
        shared = _complete_mesh("Shared")
        distinct = _complete_mesh("Distinct", 7.0)
        files = {
            "art/a.w3d": shared,
            "art/b.w3d": shared,
            "art/c.w3d": distinct,
        }
        original = decode_corpus_module.plan_w3d_stream_decode
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            _write_corpus(root, files)
            with mock.patch.object(
                decode_corpus_module,
                "plan_w3d_stream_decode",
                wraps=original,
            ) as planner:
                report = scan_w3d_decode_corpus(root)

        self.assertEqual(planner.call_count, 2)
        self.assertEqual(report.selected_file_count, 3)
        self.assertEqual(report.scanned_file_count, 3)
        self.assertEqual(report.unique_source_count, 2)
        self.assertEqual(report.stream_complete_file_count, 3)
        self.assertEqual(report.decoded_stream_count, 3)
        self.assertEqual(
            sorted(item.manifest_entry_count for item in report.sources),
            [1, 2],
        )

    def test_incomplete_damaged_unresolved_and_unsupported_are_explicit(self) -> None:
        files = {
            "art/complete.w3d": _complete_mesh(),
            "art/damaged.w3d": _damaged_mesh(),
            "art/unresolved.w3d": _unresolved_mesh(),
            "art/unsupported.w3d": _unsupported_mesh(),
        }
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            _write_corpus(root, files)
            report = scan_w3d_decode_corpus(root)

        self.assertFalse(report.complete)
        self.assertEqual(report.selected_file_count, 4)
        self.assertEqual(report.scanned_file_count, 4)
        self.assertEqual(report.stream_complete_file_count, 1)
        self.assertEqual(report.incomplete_file_count, 3)
        self.assertGreaterEqual(report.damaged_file_count, 1)
        self.assertGreaterEqual(report.unresolved_file_count, 1)
        self.assertGreaterEqual(report.unsupported_file_count, 1)
        terminal_pairs = {(item.state, item.code) for item in report.terminals}
        self.assertIn(("damaged", "metadata-truncated-chunk-payload"), terminal_pairs)
        self.assertIn(("unresolved", "missing-mesh-owner"), terminal_pairs)
        self.assertIn(("unsupported", "unknown-chunk-layout"), terminal_pairs)
        self.assertEqual(
            sum(item.count for item in report.terminals), report.terminal_count
        )

    def test_planner_rejection_becomes_damaged_evidence_and_scan_continues(
        self,
    ) -> None:
        files = {
            "art/a.w3d": _complete_mesh("A"),
            "art/b.w3d": _complete_mesh("B", 2.0),
        }
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            _write_corpus(root, files)
            with mock.patch.object(
                decode_corpus_module,
                "plan_w3d_stream_decode",
                side_effect=ValueError("malformed content"),
            ) as planner:
                report = scan_w3d_decode_corpus(root)

        self.assertEqual(planner.call_count, 2)
        self.assertEqual(report.scanned_file_count, 2)
        self.assertEqual(report.damaged_file_count, 2)
        self.assertEqual(report.incomplete_file_count, 2)
        self.assertTrue(all(not item.planner_accepted for item in report.sources))
        self.assertTrue(all(item.plan_sha256 is None for item in report.sources))
        self.assertIn(
            ("damaged", "planner-content-rejected"),
            {(item.state, item.code) for item in report.terminals},
        )


class W3DDecodeCorpusExternalHierarchyTests(unittest.TestCase):
    def test_primary_skipped_channels_are_deterministically_accounted(self) -> None:
        common = {
            "art/rig.w3d": _hierarchy("Rig", pivots=1) + _complete_mesh("RigMesh"),
            "art/bound.w3d": _raw_animation(pivot=0),
        }
        with (
            tempfile.TemporaryDirectory() as first_raw,
            tempfile.TemporaryDirectory() as changed_raw,
        ):
            first_root = Path(first_raw)
            changed_root = Path(changed_raw)
            _write_corpus(
                first_root,
                {**common, "art/skipped.w3d": _raw_animation(pivot=1)},
            )
            _write_corpus(
                changed_root,
                {**common, "art/skipped.w3d": _raw_animation(pivot=2)},
            )
            first = scan_w3d_decode_corpus(first_root, strict=True)
            repeated = scan_w3d_decode_corpus(first_root, strict=True)
            changed = scan_w3d_decode_corpus(changed_root, strict=True)

        self.assertEqual(first, repeated)
        self.assertTrue(first.complete)
        self.assertEqual(first.decoded_stream_count, 3)
        self.assertEqual(first.primary_bound_animation_stream_count, 1)
        self.assertEqual(first.primary_skipped_animation_stream_count, 1)
        animation_domain = next(
            item for item in first.domains if item.domain == "animation"
        )
        self.assertEqual(animation_domain.decoded_stream_count, 2)
        self.assertEqual(animation_domain.primary_bound_stream_count, 1)
        self.assertEqual(animation_domain.primary_skipped_stream_count, 1)
        animation_chunk = next(item for item in first.chunks if item.chunk_id == 0x202)
        self.assertEqual(animation_chunk.decoded_stream_count, 2)
        self.assertEqual(animation_chunk.terminal_count, 0)
        self.assertEqual(animation_chunk.primary_bound_animation_stream_count, 1)
        self.assertEqual(animation_chunk.primary_skipped_animation_stream_count, 1)
        self.assertEqual(
            sum(item.primary_bound_animation_stream_count for item in first.sources),
            1,
        )
        self.assertEqual(
            sum(item.primary_skipped_animation_stream_count for item in first.sources),
            1,
        )
        self.assertFalse(
            any(
                item.state == "damaged"
                and item.code == "animation-stream-payload-invalid"
                for item in first.terminals
            )
        )
        self.assertNotEqual(first.plan_set_sha256, changed.plan_set_sha256)
        self.assertNotEqual(first.corpus_sha256, changed.corpus_sha256)

    def test_unique_external_hierarchy_resolves_and_strict_scan_succeeds(self) -> None:
        files = {
            "art/PrivateRig.w3d": _hierarchy("PrivateRig") + _complete_mesh("RigMesh"),
            "art/private-walk.w3d": _raw_animation(hierarchy="PrivateRig"),
        }
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            _write_corpus(root, files)
            report = scan_w3d_decode_corpus(root, strict=True)

        self.assertTrue(report.complete)
        self.assertEqual(report.selected_file_count, 2)
        self.assertEqual(report.scanned_file_count, 2)
        self.assertEqual(report.stream_complete_file_count, 2)
        self.assertEqual(report.decoded_stream_count, 2)
        self.assertEqual(
            {item.domain: item.decoded_stream_count for item in report.domains},
            {"animation": 1, "geometry": 1},
        )
        self.assertNotIn(
            "missing-hierarchy-pivot-cardinality",
            {item.code for item in report.terminals},
        )
        self.assertEqual(len(report.hierarchy_resolver_sha256), 64)
        encoded = json.dumps(report.neutral(), sort_keys=True).casefold()
        self.assertNotIn("privaterig", encoded)
        self.assertNotIn("privatewalk", encoded)
        self.assertNotIn("private-rig", encoded)
        self.assertNotIn("identifier", encoded)

    def test_byte_identical_manifest_duplicates_are_safe_and_fully_accounted(
        self,
    ) -> None:
        hierarchy_source = _hierarchy("Rig") + _complete_mesh("RigMesh")
        files = {
            "art/Rig.w3d": hierarchy_source,
            "other/Rig.w3d": hierarchy_source,
            "art/walk.w3d": _raw_animation(),
        }
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            _write_corpus(root, files)
            report = scan_w3d_decode_corpus(root, strict=True)

        self.assertTrue(report.complete)
        self.assertEqual(report.selected_file_count, 3)
        self.assertEqual(report.unique_source_count, 2)
        self.assertEqual(report.stream_complete_file_count, 3)
        self.assertEqual(report.decoded_stream_count, 3)
        self.assertNotIn(
            "ambiguous-external-hierarchy-pivot-cardinality",
            {item.code for item in report.terminals},
        )

    def test_different_exact_hierarchy_evidence_is_explicitly_ambiguous(self) -> None:
        files = {
            "art/Rig.w3d": (
                _hierarchy("RigA", marker=1.0)
                + _hierarchy("RigB", marker=2.0)
                + _complete_mesh("A")
            ),
            "art/walk.w3d": _raw_animation(),
        }
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            _write_corpus(root, files)
            with self.assertRaises(W3DDecodeCorpusStrictError) as caught:
                scan_w3d_decode_corpus(root, strict=True)

        report = caught.exception.report
        self.assertEqual(report.scanned_file_count, 2)
        self.assertEqual(report.stream_complete_file_count, 1)
        self.assertEqual(report.incomplete_file_count, 1)
        self.assertIn(
            ("unresolved", "ambiguous-external-hierarchy-pivot-cardinality"),
            {(item.state, item.code) for item in report.terminals},
        )
        self.assertNotIn(
            "missing-hierarchy-pivot-cardinality",
            {item.code for item in report.terminals},
        )

    def test_exact_hierarchy_change_rebinds_resolver_plan_and_corpus_hashes(
        self,
    ) -> None:
        animation = _raw_animation()
        first_files = {
            "art/rig.w3d": _hierarchy("Rig", marker=1.0) + _complete_mesh("RigMesh"),
            "art/walk.w3d": animation,
        }
        second_files = {
            "art/rig.w3d": _hierarchy("Rig", marker=2.0) + _complete_mesh("RigMesh"),
            "art/walk.w3d": animation,
        }
        with (
            tempfile.TemporaryDirectory() as first_raw,
            tempfile.TemporaryDirectory() as second_raw,
        ):
            first_root = Path(first_raw)
            second_root = Path(second_raw)
            _write_corpus(first_root, first_files)
            _write_corpus(second_root, second_files)
            first = scan_w3d_decode_corpus(first_root)
            second = scan_w3d_decode_corpus(second_root)

        self.assertNotEqual(
            first.hierarchy_resolver_sha256,
            second.hierarchy_resolver_sha256,
        )
        self.assertNotEqual(first.inventory_sha256, second.inventory_sha256)
        self.assertNotEqual(first.plan_set_sha256, second.plan_set_sha256)
        self.assertNotEqual(first.corpus_sha256, second.corpus_sha256)

    def test_identical_animation_bytes_are_sealed_per_directory_context(self) -> None:
        animation = _raw_animation()
        files = {
            "private-alpha/Rig.w3d": (
                _hierarchy("UnrelatedA", pivots=1) + _complete_mesh("Alpha")
            ),
            "private-alpha/walk.w3d": animation,
            "private-beta/Rig.w3d": (
                _hierarchy("UnrelatedB", pivots=2) + _complete_mesh("Beta")
            ),
            "private-beta/walk.w3d": animation,
        }
        reversed_files = dict(reversed(tuple(files.items())))
        with (
            tempfile.TemporaryDirectory() as first_raw,
            tempfile.TemporaryDirectory() as second_raw,
        ):
            first_root = Path(first_raw)
            second_root = Path(second_raw)
            _write_corpus(first_root, files)
            _write_corpus(second_root, reversed_files)
            first = scan_w3d_decode_corpus(first_root, strict=True)
            reordered = scan_w3d_decode_corpus(second_root, strict=True)

        animation_sha256 = hashlib.sha256(animation).hexdigest()
        animation_plans = tuple(
            item for item in first.sources if item.source_sha256 == animation_sha256
        )
        self.assertEqual(first, reordered)
        self.assertTrue(first.complete)
        self.assertEqual(first.selected_file_count, 4)
        self.assertEqual(first.unique_source_count, 3)
        self.assertEqual(first.plan_context_count, 4)
        self.assertEqual(len(animation_plans), 2)
        self.assertEqual(len({item.path_context_sha256 for item in animation_plans}), 2)
        self.assertEqual(len({item.plan_sha256 for item in animation_plans}), 2)
        self.assertEqual(
            sum(item.manifest_entry_count for item in first.sources),
            first.selected_file_count,
        )
        encoded = json.dumps(first.neutral(), sort_keys=True).casefold()
        self.assertNotIn("private-alpha", encoded)
        self.assertNotIn("private-beta", encoded)
        self.assertNotIn("unrelateda", encoded)
        self.assertNotIn("unrelatedb", encoded)


class W3DDecodeCorpusSafetyTests(unittest.TestCase):
    def test_rejects_manifest_tree_tamper_and_collisions(self) -> None:
        source = _complete_mesh("Safe")

        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            manifest = _write_corpus(root, {"art/safe.w3d": source})
            manifest["aggregate_sha256"] = "0" * 64
            _write_manifest(root, manifest)
            with self.assertRaisesRegex(W3DDecodeCorpusError, "aggregate SHA-256"):
                scan_w3d_decode_corpus(root)

        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            _write_corpus(root, {"art/safe.w3d": source})
            (root / "undeclared.bin").write_bytes(b"extra")
            with self.assertRaisesRegex(W3DDecodeCorpusError, "undeclared files"):
                scan_w3d_decode_corpus(root)

        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            _write_corpus(root, {"art/safe.w3d": source})
            changed = bytearray(source)
            changed[-1] ^= 1
            (root / "art" / "safe.w3d").write_bytes(changed)
            with self.assertRaisesRegex(W3DDecodeCorpusError, "SHA-256"):
                scan_w3d_decode_corpus(root)

        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            collision = _manifest(
                {
                    "art/conflict": b"file",
                    "art/conflict/model.w3d": source,
                }
            )
            _write_manifest(root, collision)
            with self.assertRaisesRegex(
                W3DDecodeCorpusError,
                "file/directory path collision",
            ):
                scan_w3d_decode_corpus(root)

        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            collision = _manifest({"Art/A.w3d": source, "art/a.W3D": source})
            _write_manifest(root, collision)
            with self.assertRaisesRegex(
                W3DDecodeCorpusError,
                "case-colliding paths",
            ):
                scan_w3d_decode_corpus(root)

    def test_rejects_linked_entries(self) -> None:
        source = _complete_mesh("Link")
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            _write_corpus(root, {"art/link.w3d": source})
            declared = root / "art" / "link.w3d"
            target = root.parent / f"{root.name}-target.w3d"
            target.write_bytes(source)
            declared.unlink()
            try:
                os.symlink(target, declared)
            except OSError:
                declared.write_bytes(source)
                original = w3d_corpus_module._is_link_like
                with mock.patch.object(
                    w3d_corpus_module,
                    "_is_link_like",
                    side_effect=lambda path: path == declared or original(path),
                ):
                    with self.assertRaisesRegex(
                        W3DDecodeCorpusError,
                        "contains a link",
                    ):
                        scan_w3d_decode_corpus(root)
            else:
                with self.assertRaisesRegex(
                    W3DDecodeCorpusError,
                    "contains a link",
                ):
                    scan_w3d_decode_corpus(root)
            finally:
                target.unlink(missing_ok=True)

    def test_manifest_must_remain_stable_through_planning(self) -> None:
        source = _complete_mesh("Stable")
        original = decode_corpus_module.plan_w3d_stream_decode
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            manifest = _write_corpus(root, {"art/stable.w3d": source})

            def change_manifest(payload: bytes, **kwargs):
                _write_manifest(root, manifest)
                manifest_path = root / ".openbfme" / "manifest.json"
                text = manifest_path.read_text(encoding="utf-8")
                manifest_path.write_text(text + " ", encoding="utf-8")
                return original(payload, **kwargs)

            with mock.patch.object(
                decode_corpus_module,
                "plan_w3d_stream_decode",
                side_effect=change_manifest,
            ):
                with self.assertRaisesRegex(
                    W3DDecodeCorpusError,
                    "manifest changed during",
                ):
                    scan_w3d_decode_corpus(root)

    def test_source_mutation_between_hierarchy_and_planning_passes_is_rejected(
        self,
    ) -> None:
        source = _complete_mesh("Mutable")
        original = decode_corpus_module._collect_external_hierarchy_observations
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            _write_corpus(root, {"art/mutable.w3d": source})
            target = root / "art" / "mutable.w3d"
            mutated = False

            def change_source(payload: bytes):
                nonlocal mutated
                observations = original(payload)
                if not mutated:
                    changed = bytearray(source)
                    changed[-1] ^= 1
                    target.write_bytes(changed)
                    mutated = True
                return observations

            with mock.patch.object(
                decode_corpus_module,
                "_collect_external_hierarchy_observations",
                side_effect=change_source,
            ):
                with self.assertRaisesRegex(W3DDecodeCorpusError, "SHA-256"):
                    scan_w3d_decode_corpus(root)

    def test_source_mutation_during_planning_is_rejected_by_final_verification(
        self,
    ) -> None:
        source = _complete_mesh("FinalCheck")
        original = decode_corpus_module.plan_w3d_stream_decode
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            _write_corpus(root, {"art/final-check.w3d": source})
            target = root / "art" / "final-check.w3d"
            mutated = False

            def change_source(payload: bytes, **kwargs):
                nonlocal mutated
                plan = original(payload, **kwargs)
                if not mutated:
                    changed = bytearray(source)
                    changed[-1] ^= 1
                    target.write_bytes(changed)
                    mutated = True
                return plan

            with mock.patch.object(
                decode_corpus_module,
                "plan_w3d_stream_decode",
                side_effect=change_source,
            ):
                with self.assertRaisesRegex(W3DDecodeCorpusError, "SHA-256"):
                    scan_w3d_decode_corpus(root)

    def test_limits_fail_before_planning_and_never_truncate(self) -> None:
        first = _complete_mesh("First")
        second = _complete_mesh("Second", 3.0)
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            _write_corpus(
                root,
                {"art/a.w3d": first, "art/b.w3d": second},
            )
            with mock.patch.object(
                decode_corpus_module,
                "plan_w3d_stream_decode",
            ) as planner:
                with self.assertRaisesRegex(
                    W3DDecodeCorpusLimitError,
                    "file count",
                ):
                    scan_w3d_decode_corpus(root, max_files=1)
                with self.assertRaisesRegex(
                    W3DDecodeCorpusLimitError,
                    "total bytes",
                ):
                    scan_w3d_decode_corpus(
                        root,
                        max_total_bytes=max(len(first), len(second)),
                    )
            self.assertEqual(planner.call_count, 0)

    def test_empty_selection_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            _write_corpus(root, {"data/readme.txt": b"no W3D"})
            with self.assertRaisesRegex(W3DDecodeCorpusError, "declares no W3D"):
                scan_w3d_decode_corpus(root)


class W3DDecodeCorpusIdentityAndStrictTests(unittest.TestCase):
    def test_hashes_and_reports_are_deterministic_across_copied_roots(self) -> None:
        files = {
            "art/a.w3d": _complete_mesh("A"),
            "art/b.w3d": _unresolved_mesh(),
            "data/ignored.txt": b"ignored",
        }
        with (
            tempfile.TemporaryDirectory() as first_raw,
            tempfile.TemporaryDirectory() as second_raw,
        ):
            first_root = Path(first_raw)
            second_root = Path(second_raw)
            _write_corpus(first_root, files)
            _write_corpus(second_root, files)
            first = scan_w3d_decode_corpus(first_root)
            repeated = scan_w3d_decode_corpus(first_root)
            copied = scan_w3d_decode_corpus(second_root)

        self.assertEqual(first, repeated)
        self.assertEqual(first, copied)
        self.assertEqual(first.neutral(), copied.neutral())
        self.assertEqual(first.inventory_sha256, copied.inventory_sha256)
        self.assertEqual(first.plan_set_sha256, copied.plan_set_sha256)
        self.assertEqual(first.corpus_sha256, copied.corpus_sha256)

    def test_strict_mode_finishes_all_sources_then_raises_with_report(self) -> None:
        files = {
            "art/a.w3d": _complete_mesh("A"),
            "art/b.w3d": _unresolved_mesh(),
            "art/c.w3d": _complete_mesh("C", 4.0),
        }
        original = decode_corpus_module.plan_w3d_stream_decode
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            _write_corpus(root, files)
            with mock.patch.object(
                decode_corpus_module,
                "plan_w3d_stream_decode",
                wraps=original,
            ) as planner:
                with self.assertRaises(W3DDecodeCorpusStrictError) as caught:
                    scan_w3d_decode_corpus(root, strict=True)

        self.assertEqual(planner.call_count, 3)
        report = caught.exception.report
        self.assertEqual(report.selected_file_count, 3)
        self.assertEqual(report.scanned_file_count, 3)
        self.assertEqual(report.stream_complete_file_count, 2)
        self.assertEqual(report.incomplete_file_count, 1)
        self.assertFalse(report.complete)


if __name__ == "__main__":
    unittest.main()
