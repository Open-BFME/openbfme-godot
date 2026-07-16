from __future__ import annotations

import hashlib
import json
import struct
import unittest

from openbfme_importer.w3d_catalog import scan_w3d_catalog
from openbfme_importer.w3d_support_matrix import (
    W3DBacktestAttestation,
    W3DSupportMatrixError,
    build_w3d_support_matrix,
)


def _chunk(
    chunk_id: int,
    payload: bytes,
    *,
    declared_size: int | None = None,
) -> bytes:
    size = len(payload) if declared_size is None else declared_size
    return struct.pack("<II", chunk_id, size) + payload


def _catalog(*chunks: bytes):
    return scan_w3d_catalog([("synthetic.w3d", b"".join(chunks))])


def _sha(label: str) -> str:
    return hashlib.sha256(label.encode("ascii")).hexdigest()


def _attestation(report, chunk_id: int, *, label: str | None = None):
    entry = report.entry_for(chunk_id)
    return W3DBacktestAttestation(
        chunk_id=chunk_id,
        chunk_count=entry.chunk_count,
        source_evidence_sha256=report.source_evidence_sha256,
        backtest_evidence_sha256=_sha(label or f"backtest-{chunk_id}"),
    )


class W3DSupportMatrixTests(unittest.TestCase):
    def test_known_data_is_metadata_only_even_when_catalog_is_complete(self) -> None:
        catalog = _catalog(
            _chunk(0x00000002, b"vertices"),
            _chunk(0x00000020, b"triangles"),
        )
        self.assertTrue(catalog.complete)

        report = build_w3d_support_matrix(catalog)

        self.assertFalse(report.conversion_complete)
        self.assertEqual(
            report.entry_for(0x00000002).capability_state,
            "metadata-only",
        )
        self.assertEqual(
            report.entry_for(0x00000020).capability_state,
            "metadata-only",
        )
        self.assertEqual(report.entry_for(0x00000002).domain, "geometry")
        self.assertEqual(report.entry_for(0x00000020).domain, "geometry")

    def test_secondary_geometry_is_explicitly_unsupported(self) -> None:
        report = build_w3d_support_matrix(
            _catalog(
                _chunk(0x00000C00, b"secondary vertices"),
                _chunk(0x00000C01, b"secondary normals"),
            )
        )

        for chunk_id in (0x00000C00, 0x00000C01):
            entry = report.entry_for(chunk_id)
            self.assertEqual(entry.domain, "geometry")
            self.assertEqual(entry.capability_state, "explicit-unsupported")
            self.assertTrue(entry.conversion_relevant)
        self.assertFalse(report.conversion_complete)

    def test_animation_payload_chunks_remain_metadata_only(self) -> None:
        report = build_w3d_support_matrix(
            _catalog(
                _chunk(0x00000202, b"raw channel"),
                _chunk(0x00000203, b"bit channel"),
                _chunk(0x00000282, b"compressed channel"),
                _chunk(0x00000284, b"motion channel"),
            )
        )

        for chunk_id in (0x00000202, 0x00000203, 0x00000282, 0x00000284):
            entry = report.entry_for(chunk_id)
            self.assertEqual(entry.domain, "animation")
            self.assertEqual(entry.capability_state, "metadata-only")
        self.assertFalse(report.conversion_complete)

    def test_unknown_and_structurally_damaged_chunks_are_not_promotable(self) -> None:
        report = build_w3d_support_matrix(
            _catalog(
                _chunk(0xDEADBEEF, b"opaque"),
                _chunk(0x00000002, b"short", declared_size=64),
            )
        )

        self.assertEqual(
            report.entry_for(0xDEADBEEF).capability_state,
            "unknown",
        )
        self.assertEqual(
            report.entry_for(0x00000002).capability_state,
            "source-invalid",
        )
        self.assertEqual(report.diagnostics.source_invalid_warning_count, 1)
        self.assertFalse(report.conversion_complete)

    def test_exact_attestations_are_the_only_decoded_override(self) -> None:
        catalog = _catalog(
            _chunk(0x00000002, b"vertices"),
            _chunk(0x00000020, b"triangles"),
            _chunk(0x0000000C, b"metadata text"),
        )
        baseline = build_w3d_support_matrix(catalog)
        self.assertFalse(baseline.conversion_complete)

        report = build_w3d_support_matrix(
            catalog,
            backtest_attestations=(
                _attestation(baseline, 0x00000002),
                _attestation(baseline, 0x00000020),
            ),
        )

        self.assertTrue(report.conversion_complete)
        self.assertEqual(
            report.entry_for(0x00000002).capability_state,
            "decoded-and-backtested",
        )
        self.assertEqual(
            report.entry_for(0x00000020).capability_state,
            "decoded-and-backtested",
        )
        metadata = report.entry_for(0x0000000C)
        self.assertEqual(metadata.domain, "metadata")
        self.assertEqual(metadata.capability_state, "metadata-only")
        self.assertFalse(metadata.conversion_relevant)

    def test_hashes_are_deterministic_for_objects_and_exported_catalogs(self) -> None:
        alpha = _chunk(0x00000002, b"alpha")
        beta = _chunk(0x00000020, b"beta")
        first_catalog = scan_w3d_catalog([("zeta.w3d", beta), ("alpha.w3d", alpha)])
        second_catalog = scan_w3d_catalog([("alpha.w3d", alpha), ("zeta.w3d", beta)])

        first = build_w3d_support_matrix(first_catalog)
        second = build_w3d_support_matrix(second_catalog.neutral())

        self.assertEqual(first.neutral(), second.neutral())
        self.assertEqual(first.matrix_sha256, second.matrix_sha256)
        self.assertEqual(
            first.matrix_sha256,
            hashlib.sha256(
                json.dumps(
                    first.matrix_hash_basis(),
                    ensure_ascii=True,
                    sort_keys=True,
                    separators=(",", ":"),
                ).encode("utf-8")
            ).hexdigest(),
        )

    def test_attestations_reject_partial_stale_duplicate_and_unsupported_claims(
        self,
    ) -> None:
        catalog = _catalog(_chunk(0x00000002, b"vertices"))
        baseline = build_w3d_support_matrix(catalog)
        valid = _attestation(baseline, 0x00000002)

        bad_values = (
            W3DBacktestAttestation(
                valid.chunk_id,
                valid.chunk_count + 1,
                valid.source_evidence_sha256,
                valid.backtest_evidence_sha256,
            ),
            W3DBacktestAttestation(
                valid.chunk_id,
                valid.chunk_count,
                "0" * 64,
                valid.backtest_evidence_sha256,
            ),
            W3DBacktestAttestation(
                valid.chunk_id,
                valid.chunk_count,
                valid.source_evidence_sha256,
                valid.backtest_evidence_sha256,
                decoded=False,
            ),
        )
        for value in bad_values:
            with self.subTest(value=value):
                with self.assertRaises(W3DSupportMatrixError):
                    build_w3d_support_matrix(catalog, backtest_attestations=(value,))

        with self.assertRaisesRegex(W3DSupportMatrixError, "duplicate attestation"):
            build_w3d_support_matrix(catalog, backtest_attestations=(valid, valid))

        unsupported_catalog = _catalog(_chunk(0x00000C00, b"secondary"))
        unsupported = build_w3d_support_matrix(unsupported_catalog)
        with self.assertRaisesRegex(W3DSupportMatrixError, "cannot override"):
            build_w3d_support_matrix(
                unsupported_catalog,
                backtest_attestations=(_attestation(unsupported, 0x00000C00),),
            )

    def test_header_index_failures_and_duplicates_are_separate_and_payload_free(
        self,
    ) -> None:
        source_hash = _sha("aggregate metadata")
        catalog_export = {
            "schema": "openbfme.w3d-catalog",
            "schemaVersion": 0,
            "hashes": {"metadataSha256": source_hash},
            "chunkCounts": [
                {
                    "chunkId": 0x00000002,
                    "chunkName": "vertices",
                    "classification": "known-data",
                    "count": 3,
                }
            ],
            "warnings": [],
            "failures": [
                {
                    "phase": "index",
                    "code": "index-rejected-header-metadata",
                    "message": "authored name omitted from support output",
                }
            ],
            "duplicateLogicalIds": [
                {
                    "kind": "model",
                    "identifier": "DO_NOT_COPY_THIS_ASSET_NAME",
                }
            ],
        }

        report = build_w3d_support_matrix(catalog_export)
        rendered = json.dumps(report.neutral(), sort_keys=True)

        self.assertEqual(report.diagnostics.source_failure_count, 1)
        self.assertEqual(report.diagnostics.header_index_failure_count, 1)
        self.assertEqual(report.diagnostics.duplicate_logical_id_count, 1)
        self.assertFalse(report.conversion_complete)
        self.assertNotIn("DO_NOT_COPY_THIS_ASSET_NAME", rendered)
        self.assertNotIn("authored name omitted", rendered)


if __name__ == "__main__":
    unittest.main()
