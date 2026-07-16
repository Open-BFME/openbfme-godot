from __future__ import annotations

import hashlib
import json
import struct
import unittest
from unittest import mock

from openbfme_importer.w3d_catalog import (
    W3DCatalogSource,
    W3DCatalogLimitError,
    W3DCatalogStrictError,
    catalog_source_id,
    scan_w3d_catalog,
)
from openbfme_importer.w3d_index import (
    W3DFileHeaders,
    W3DIndex,
    resolve_w3d_reference,
)


def _fixed(value: str, size: int) -> bytes:
    encoded = value.encode("cp1252")
    if len(encoded) > size:
        raise ValueError("synthetic fixture string is too long")
    return encoded + b"\0" * (size - len(encoded))


def _chunk(
    chunk_id: int,
    payload: bytes,
    *,
    children: bool = False,
    declared_size: int | None = None,
) -> bytes:
    size = len(payload) if declared_size is None else declared_size
    raw_size = size | (0x80000000 if children else 0)
    return struct.pack("<II", chunk_id, raw_size) + payload


def _mesh_header(mesh: str, container: str) -> bytes:
    return struct.pack(
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


def _mesh_source(container: str, mesh: str = "BODY") -> bytes:
    return _chunk(0x1F, _mesh_header(mesh, container))


def _rig_source(hierarchy: str, animation: str) -> bytes:
    hierarchy_chunk = _chunk(
        0x100,
        _chunk(
            0x101,
            struct.pack(
                "<I16sI3f",
                0x00040001,
                _fixed(hierarchy, 16),
                0,
                0.0,
                0.0,
                0.0,
            ),
        ),
    )
    animation_chunk = _chunk(
        0x200,
        _chunk(
            0x201,
            struct.pack(
                "<I16s16sII",
                0x00040001,
                _fixed(animation, 16),
                _fixed(hierarchy, 16),
                30,
                15,
            ),
        ),
        children=True,
    )
    return hierarchy_chunk + animation_chunk


def _canonical_sha256(value: object) -> str:
    payload = json.dumps(
        value,
        ensure_ascii=True,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


class W3DCatalogTests(unittest.TestCase):
    def test_catalog_source_ids_use_canonical_utf8_and_preserve_ascii_ids(
        self,
    ) -> None:
        ascii_source = W3DCatalogSource(
            "Art/W3D/Unit.w3d",
            "Art/W3D/Unit.w3d",
            123,
            "a" * 64,
        )
        unicode_source = W3DCatalogSource(
            "Art/\u00dcnit/\u6a21\u578b.w3d",
            "Art/\u00dcnit/\u6a21\u578b.w3d",
            123,
            "a" * 64,
        )

        self.assertEqual(
            catalog_source_id(ascii_source),
            "src-cc1bf9402d09c124f0ef255b98a2a379",
        )
        self.assertEqual(
            catalog_source_id(unicode_source),
            "src-2d269b1d337cb54ed3c31f15cbbf64e6",
        )

        for source in (
            W3DCatalogSource("../Unit.w3d", None, 123, "a" * 64),
            W3DCatalogSource(
                r"Art\W3D\Unit.w3d",
                r"Art\W3D\Unit.w3d",
                123,
                "a" * 64,
            ),
            W3DCatalogSource(
                "Art/W3D/Unit.w3d",
                "Art/W3D/Unit.w3d",
                123,
                "A" * 64,
            ),
        ):
            with self.subTest(source=source):
                with self.assertRaises(ValueError):
                    catalog_source_id(source)

    def test_order_independent_catalog_hashes_counts_and_file_header_bridge(
        self,
    ) -> None:
        alpha = _mesh_source("Alpha")
        zeta = _rig_source("Zeta_SKL", "Zeta_IDLE")
        first = scan_w3d_catalog(
            [(r"Art\W3D\Zeta.w3d", zeta), ("art/w3d/Alpha.w3d", alpha)]
        )
        second = scan_w3d_catalog(
            {
                "art/w3d/Alpha.w3d": alpha,
                r"Art\W3D\Zeta.w3d": zeta,
            }
        )

        self.assertTrue(first.complete)
        self.assertIsInstance(first.index, W3DIndex)
        self.assertEqual(first.neutral(), second.neutral())
        self.assertEqual(first.input_sha256, second.input_sha256)
        self.assertEqual(first.metadata_sha256, second.metadata_sha256)
        self.assertEqual(first.file_headers, first.index.file_headers)
        self.assertTrue(
            all(isinstance(item, W3DFileHeaders) for item in first.file_headers)
        )
        self.assertEqual(
            [item.virtual_path for item in first.file_headers],
            ["art/w3d/Alpha.w3d", "Art/W3D/Zeta.w3d"],
        )

        chunk_counts = {item.chunk_id: item.count for item in first.chunk_counts}
        self.assertEqual(chunk_counts[0x1F], 1)
        self.assertEqual(chunk_counts[0x100], 1)
        self.assertEqual(chunk_counts[0x101], 1)
        self.assertEqual(chunk_counts[0x200], 1)
        self.assertEqual(chunk_counts[0x201], 1)
        families = dict(first.asset_family_counts)
        self.assertEqual(families["meshes"], 1)
        self.assertEqual(families["hierarchies"], 1)
        self.assertEqual(families["animations"], 1)

        alpha_metadata = next(
            item for item in first.files if item.virtual_path.endswith("Alpha.w3d")
        )
        self.assertEqual(
            alpha_metadata.mesh_headers[0].provenance.virtual_path,
            "art/w3d/Alpha.w3d",
        )
        self.assertEqual(
            alpha_metadata.source_sha256, hashlib.sha256(alpha).hexdigest()
        )
        self.assertEqual(
            first.metadata_sha256,
            _canonical_sha256(first.metadata_hash_basis()),
        )
        dotted_animation = resolve_w3d_reference(
            first.index, "animation", "Zeta_SKL.Zeta_IDLE"
        )
        raw_animation = resolve_w3d_reference(first.index, "animation", "Zeta_IDLE")
        self.assertEqual(
            dotted_animation.physical_virtual_path,
            "Art/W3D/Zeta.w3d",
        )
        self.assertEqual(raw_animation.physical_virtual_path, "Art/W3D/Zeta.w3d")

        expected_input = {
            "schema": "openbfme.w3d-catalog-input",
            "schemaVersion": 0,
            "sources": [
                {
                    "suppliedVirtualPath": "art/w3d/Alpha.w3d",
                    "byteLength": len(alpha),
                    "sourceSha256": hashlib.sha256(alpha).hexdigest(),
                },
                {
                    "suppliedVirtualPath": r"Art\W3D\Zeta.w3d",
                    "byteLength": len(zeta),
                    "sourceSha256": hashlib.sha256(zeta).hexdigest(),
                },
            ],
        }
        self.assertEqual(first.input_sha256, _canonical_sha256(expected_input))

        changed = scan_w3d_catalog(
            [(r"Art\W3D\Zeta.w3d", zeta), ("art/w3d/Alpha.w3d", _mesh_source("Alphb"))]
        )
        self.assertNotEqual(first.input_sha256, changed.input_sha256)
        self.assertNotEqual(first.metadata_sha256, changed.metadata_sha256)

    def test_duplicate_logical_ids_preserve_case_and_offset_provenance(self) -> None:
        values = [
            ("art/w3d/a.w3d", _mesh_source("Shared")),
            ("art/w3d/b.w3d", _mesh_source("shared")),
        ]
        report = scan_w3d_catalog(values)

        self.assertFalse(report.complete)
        self.assertIsNotNone(report.index)
        self.assertEqual(len(report.index.duplicate_header_ids), 1)
        self.assertEqual(len(report.duplicate_logical_ids), 1)
        duplicate = report.duplicate_logical_ids[0]
        self.assertEqual((duplicate.kind, duplicate.scope), ("model", "across-files"))
        self.assertEqual(
            [item.identifier for item in duplicate.occurrences],
            ["Shared.BODY", "shared.BODY"],
        )
        self.assertEqual(
            [item.provenance.value_offset for item in duplicate.occurrences],
            [8, 8],
        )
        self.assertTrue(
            all(len(item.source_sha256) == 64 for item in duplicate.occurrences)
        )

        with self.assertRaises(W3DCatalogStrictError) as caught:
            scan_w3d_catalog(reversed(values), strict=True)
        self.assertEqual(caught.exception.report.neutral(), report.neutral())

        repeated = scan_w3d_catalog(
            [("art/w3d/repeated.w3d", _mesh_source("Same") * 2)]
        )
        self.assertIsNone(repeated.index)
        self.assertEqual(repeated.duplicate_logical_ids[0].scope, "within-file")
        self.assertEqual(
            [item.code for item in repeated.failures],
            ["duplicate-logical-id-within-file"],
        )

    def test_case_colliding_physical_paths_are_not_guessed_in_partial_index(
        self,
    ) -> None:
        report = scan_w3d_catalog(
            [
                ("Art/W3D/Unit.w3d", _mesh_source("First")),
                ("art/w3d/unit.W3D", _mesh_source("Second")),
                ("art/w3d/safe.w3d", _mesh_source("Safe")),
            ]
        )

        self.assertFalse(report.complete)
        self.assertIsNotNone(report.index)
        self.assertEqual(report.index.virtual_paths, ("art/w3d/safe.w3d",))
        self.assertEqual(
            [item.code for item in report.failures],
            ["case-colliding-virtual-path", "case-colliding-virtual-path"],
        )
        self.assertEqual(len(report.files), 3)
        with self.assertRaises(W3DCatalogStrictError):
            scan_w3d_catalog(
                [
                    ("Art/W3D/Unit.w3d", _mesh_source("First")),
                    ("art/w3d/unit.W3D", _mesh_source("Second")),
                    ("art/w3d/safe.w3d", _mesh_source("Safe")),
                ],
                strict=True,
            )

    def test_truncated_and_unsupported_files_retain_per_file_warnings(self) -> None:
        damaged = _chunk(0x500, b"opaque") + _chunk(0x101, b"\0" * 4, declared_size=36)
        report = scan_w3d_catalog([("art/w3d/damaged.w3d", damaged)])
        codes = [item.diagnostic.code for item in report.warnings]

        self.assertFalse(report.complete)
        self.assertIn("unsupported-chunk", codes)
        self.assertIn("truncated-chunk-payload", codes)
        self.assertIn("truncated-metadata-record", codes)
        self.assertTrue(
            all(
                item.source.source_sha256 == hashlib.sha256(damaged).hexdigest()
                for item in report.warnings
            )
        )
        counts = {item.chunk_id: item for item in report.chunk_counts}
        self.assertEqual(counts[0x500].classification, "unsupported")
        self.assertEqual(counts[0x101].count, 1)

        with self.assertRaises(W3DCatalogStrictError) as caught:
            scan_w3d_catalog([("art/w3d/damaged.w3d", damaged)], strict=True)
        self.assertEqual(
            caught.exception.report.metadata_sha256, report.metadata_sha256
        )

    def test_aggregate_bounds_and_per_file_scanner_limits_fail_closed(self) -> None:
        source = _mesh_source("Bounded")
        with self.assertRaisesRegex(W3DCatalogLimitError, "file count"):
            scan_w3d_catalog([("a.w3d", source), ("b.w3d", source)], max_files=1)
        with self.assertRaisesRegex(W3DCatalogLimitError, "total bytes"):
            scan_w3d_catalog([("a.w3d", source)], max_total_bytes=len(source) - 1)
        with self.assertRaisesRegex(W3DCatalogLimitError, "file count must"):
            scan_w3d_catalog([])
        with self.assertRaisesRegex(TypeError, "source must be bytes"):
            scan_w3d_catalog([("a.w3d", bytearray(source))])  # type: ignore[list-item]

        with mock.patch("openbfme_importer.w3d_metadata.MAX_W3D_METADATA_BYTES", 1):
            partial = scan_w3d_catalog([("large.w3d", b"12"), ("empty.w3d", b"")])
        self.assertEqual([item.code for item in partial.failures], ["metadata-limit"])
        self.assertEqual([item.virtual_path for item in partial.files], ["empty.w3d"])
        self.assertEqual(partial.index.virtual_paths, ("empty.w3d",))
        self.assertEqual(partial.total_input_bytes, 2)
        self.assertEqual(len(partial.sources), 2)


if __name__ == "__main__":
    unittest.main()
