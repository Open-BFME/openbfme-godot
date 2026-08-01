from __future__ import annotations

from contextlib import redirect_stdout
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

from openbfme_importer.w3d_corpus import (
    W3DCorpusError,
    W3DCorpusLimitError,
    W3DCorpusStrictError,
    scan_w3d_corpus,
)


def _fixed(value: str, size: int) -> bytes:
    encoded = value.encode("cp1252")
    return encoded + b"\0" * (size - len(encoded))


def _chunk(
    chunk_id: int,
    payload: bytes,
    *,
    declared_size: int | None = None,
) -> bytes:
    size = len(payload) if declared_size is None else declared_size
    return struct.pack("<II", chunk_id, size) + payload


def _mesh_source(container: str, mesh: str = "BODY") -> bytes:
    header = struct.pack(
        "<II16s16s9I10f",
        0x00040002,
        0x0002A000,
        _fixed(mesh, 16),
        _fixed(container, 16),
        12,
        8,
        2,
        3,
        4,
        5,
        6,
        0x13,
        1,
        *[float(value) for value in range(10)],
    )
    return _chunk(0x1F, header)


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
    offset = 32
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
        json.dumps(manifest, indent=2, sort_keys=True, ensure_ascii=False) + "\n",
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
        return any(_contains_bytes(key) or _contains_bytes(item) for key, item in value.items())
    if isinstance(value, (list, tuple)):
        return any(_contains_bytes(item) for item in value)
    return False


class W3DCorpusTests(unittest.TestCase):
    def test_verified_full_and_compact_reports_are_deterministic_and_payload_free(self) -> None:
        files = {
            "art/w3d/Alpha.w3d": _mesh_source("Alpha"),
            "Art/W3D/Zeta.W3D": _mesh_source("Zeta"),
            "data/textures/ignored.tga": b"not parsed as W3D",
        }
        with tempfile.TemporaryDirectory() as first_raw, tempfile.TemporaryDirectory() as second_raw:
            first_root = Path(first_raw)
            second_root = Path(second_raw)
            _write_corpus(first_root, files)
            _write_corpus(second_root, files)

            stdout = io.StringIO()
            with redirect_stdout(stdout):
                first = scan_w3d_corpus(first_root)
                repeated = scan_w3d_corpus(first_root)
                copied = scan_w3d_corpus(second_root)
                compact = scan_w3d_corpus(first_root, compact=True)

        self.assertEqual(stdout.getvalue(), "")
        self.assertTrue(first.complete)
        self.assertEqual(first.neutral(), repeated.neutral())
        self.assertEqual(first.neutral(), copied.neutral())
        self.assertEqual(first.corpus_sha256, compact.corpus_sha256)
        self.assertEqual(
            [item.virtual_path for item in first.w3d_files],
            ["art/w3d/Alpha.w3d", "Art/W3D/Zeta.W3D"],
        )
        self.assertEqual(
            [item.supplied_virtual_path for item in first.catalog.sources],
            ["art/w3d/Alpha.w3d", "Art/W3D/Zeta.W3D"],
        )
        full_json = first.neutral()
        compact_json = compact.json_ready()
        self.assertEqual(full_json["mode"], "full")
        self.assertEqual(compact_json["mode"], "compact")
        self.assertIn("catalog", full_json)
        self.assertNotIn("catalog", compact_json)
        self.assertNotIn("w3dFiles", compact_json)
        self.assertIn("chunkCounts", compact_json)
        self.assertLess(
            len(json.dumps(compact_json, sort_keys=True)),
            len(json.dumps(full_json, sort_keys=True)),
        )
        self.assertFalse(_contains_bytes(full_json))
        self.assertFalse(_contains_bytes(compact_json))
        json.dumps(full_json)
        json.dumps(compact_json)

    def test_rejects_w3d_size_and_sha256_mismatches(self) -> None:
        source = _mesh_source("Trusted")
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            _write_corpus(root, {"art/trusted.w3d": source})
            (root / "art" / "trusted.w3d").write_bytes(source + b"x")
            with self.assertRaisesRegex(W3DCorpusError, "file size"):
                scan_w3d_corpus(root)

        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            _write_corpus(root, {"art/trusted.w3d": source})
            changed = bytearray(source)
            changed[-1] ^= 1
            (root / "art" / "trusted.w3d").write_bytes(changed)
            with self.assertRaisesRegex(W3DCorpusError, "SHA-256"):
                scan_w3d_corpus(root)

    def test_rejects_bad_schema_totals_aggregate_paths_and_inventory_order(self) -> None:
        source = _mesh_source("Validation")

        def assert_manifest_rejected(
            mutate: object, expected: str, *, create_files: bool = True
        ) -> None:
            with tempfile.TemporaryDirectory() as raw:
                root = Path(raw)
                files = {
                    "art/a.w3d": source,
                    "data/other.bin": b"other",
                }
                manifest = _write_corpus(root, files) if create_files else _manifest(files)
                mutate(manifest)  # type: ignore[operator]
                _write_manifest(root, manifest)
                with self.assertRaisesRegex(W3DCorpusError, expected):
                    scan_w3d_corpus(root)

        assert_manifest_rejected(
            lambda value: value.__setitem__("schema", "wrong"), "schema is unsupported"
        )
        assert_manifest_rejected(
            lambda value: value["totals"].__setitem__("bytes", 1),  # type: ignore[index,union-attr]
            "totals do not match",
        )
        assert_manifest_rejected(
            lambda value: value.__setitem__("aggregate_sha256", "0" * 64),
            "aggregate SHA-256",
        )
        assert_manifest_rejected(
            lambda value: value["files"].reverse(),  # type: ignore[index,union-attr]
            "inventory is not canonical",
        )

        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            manifest = _manifest({"art/a.w3d": source})
            entry = manifest["files"][0]  # type: ignore[index]
            entry["path"] = "../escape.w3d"  # type: ignore[index]
            manifest["aggregate_sha256"] = _aggregate(manifest["files"])  # type: ignore[arg-type,index]
            _write_manifest(root, manifest)
            with self.assertRaisesRegex(W3DCorpusError, "unsafe path"):
                scan_w3d_corpus(root)

        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            manifest = _manifest({"Art/A.w3d": source, "art/a.W3D": source})
            _write_manifest(root, manifest)
            with self.assertRaisesRegex(W3DCorpusError, "case-colliding paths"):
                scan_w3d_corpus(root)

        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            manifest = _manifest(
                {
                    "art/conflict": b"file",
                    "art/conflict-child.bin": b"separator ordering",
                    "art/conflict/model.w3d": source,
                }
            )
            _write_manifest(root, manifest)
            with self.assertRaisesRegex(W3DCorpusError, "file/directory path collision"):
                scan_w3d_corpus(root)

    def test_rejects_missing_and_extra_tree_entries_before_scanning(self) -> None:
        source = _mesh_source("Tree")
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            _write_corpus(root, {"art/tree.w3d": source})
            (root / "extra.bin").write_bytes(b"extra")
            with self.assertRaisesRegex(W3DCorpusError, "undeclared files"):
                scan_w3d_corpus(root)

        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            _write_corpus(root, {"art/tree.w3d": source})
            (root / "empty-extra").mkdir()
            with self.assertRaisesRegex(W3DCorpusError, "undeclared directories"):
                scan_w3d_corpus(root)

        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            _write_corpus(root, {"art/tree.w3d": source})
            (root / "art" / "tree.w3d").unlink()
            with self.assertRaisesRegex(W3DCorpusError, "missing declared files"):
                scan_w3d_corpus(root)

    def test_rejects_links_when_the_platform_can_create_them(self) -> None:
        source = _mesh_source("Link")
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            _write_corpus(root, {"art/link.w3d": source})
            target = root / "actual.w3d"
            target.write_bytes(source)
            declared = root / "art" / "link.w3d"
            declared.unlink()
            try:
                os.symlink(target, declared)
            except OSError:
                target.unlink()
                declared.write_bytes(source)
                original = w3d_corpus_module._is_link_like
                with mock.patch.object(
                    w3d_corpus_module,
                    "_is_link_like",
                    side_effect=lambda path: path == declared or original(path),
                ):
                    with self.assertRaisesRegex(W3DCorpusError, "contains a link"):
                        scan_w3d_corpus(root)
            else:
                with self.assertRaisesRegex(W3DCorpusError, "contains a link"):
                    scan_w3d_corpus(root)

    def test_strict_mode_preserves_verified_corpus_report(self) -> None:
        damaged = _chunk(0x500, b"opaque") + _chunk(
            0x101, b"\0" * 4, declared_size=36
        )
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            _write_corpus(root, {"art/damaged.w3d": damaged})
            partial = scan_w3d_corpus(root, compact=True)
            with self.assertRaises(W3DCorpusStrictError) as caught:
                scan_w3d_corpus(root, strict=True, compact=True)

        self.assertFalse(partial.complete)
        self.assertEqual(caught.exception.report.neutral(), partial.neutral())
        self.assertGreater(partial.neutral()["summary"]["warningCount"], 0)  # type: ignore[index]

    def test_empty_selection_and_selected_bounds_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            _write_corpus(root, {"data/readme.txt": b"no models"})
            with self.assertRaisesRegex(W3DCorpusError, "declares no W3D"):
                scan_w3d_corpus(root)

        source = _mesh_source("Bounds")
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            _write_corpus(
                root,
                {
                    "art/a.w3d": source,
                    "art/b.w3d": _mesh_source("Other"),
                },
            )
            with self.assertRaisesRegex(W3DCorpusLimitError, "file count"):
                scan_w3d_corpus(root, max_files=1)
            with self.assertRaisesRegex(W3DCorpusLimitError, "total bytes"):
                scan_w3d_corpus(root, max_total_bytes=len(source))


if __name__ == "__main__":
    unittest.main()
