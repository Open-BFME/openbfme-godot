from __future__ import annotations

import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest

from openbfme_importer.measurement_provenance import (
    measurement_fingerprint,
    measurement_provenance,
)
from openbfme_importer.w3d_chunk_backlog import (
    CENSUS_MEASUREMENT_MODULE,
    DECODE_CORPUS_MEASUREMENT_MODULE,
    RETAIL_W3D_CHUNK_BACKLOG_SCHEMA,
    W3DChunkBacklogError,
    build_w3d_chunk_backlog,
    collect_w3d_backlog_file_evidence,
    render_w3d_chunk_backlog,
    scan_w3d_chunk_backlog_tree,
    w3d_paths_from_slice_plan,
)


def _fixed(value: str, size: int) -> bytes:
    encoded = value.encode("cp1252")
    return encoded + b"\0" * (size - len(encoded))


def _chunk(chunk_id: int, payload: bytes, *, children: bool = False) -> bytes:
    raw_size = len(payload) | (0x80000000 if children else 0)
    return struct.pack("<II", chunk_id, raw_size) + payload


def _mesh(container: str, *, vertex_count: int = 3, extra: bytes = b"") -> bytes:
    header = struct.pack(
        "<II16s16s9I10f",
        0x00040002,
        0x00020000,
        _fixed("BODY", 16),
        _fixed(container, 16),
        1,
        vertex_count,
        1,
        1,
        0,
        0,
        0,
        0x13,
        1,
        *[0.0] * 10,
    )
    return _chunk(0x00000000, _chunk(0x1F, header) + extra, children=True)


def _secondary_streams(vertex_count: int = 3) -> bytes:
    stream = struct.pack(f"<{vertex_count * 3}f", *[0.5] * (vertex_count * 3))
    return _chunk(0x00000C00, stream) + _chunk(0x00000C01, stream)


_CLOSURES = {
    "men": ("art/w3d/gu/deform.w3d",),
    "wild": (),
}


def _identity() -> dict[str, object]:
    return {"game": "bfme2", "w3dFileCount": 3}


def _evidence_rows():
    # One faction-reachable file with two unsupported deform chunks, one
    # unreachable file with one deform chunk plus an unknown chunk, and one
    # clean file whose secondary streams decode without any flag.
    reachable = collect_w3d_backlog_file_evidence(
        _mesh("Deformed", extra=b"")
        + _chunk(0x00000058, b"d" * 8)
        + _chunk(0x00000058, b"d" * 8),
        "art/w3d/gu/deform.w3d",
    )
    unreachable = collect_w3d_backlog_file_evidence(
        _chunk(0x00000058, b"d" * 8) + _chunk(0xDEADBEEF, b"??"),
        "art/w3d/xx/prop.w3d",
    )
    clean = collect_w3d_backlog_file_evidence(
        _mesh("DualSkin", extra=_secondary_streams()),
        "art/w3d/gu/dualskin.w3d",
    )
    return [reachable, unreachable, clean]


class W3DChunkBacklogTests(unittest.TestCase):
    def test_ranks_by_files_blocked_with_faction_reachability(self) -> None:
        census = build_w3d_chunk_backlog(
            _evidence_rows(),
            faction_closures=_CLOSURES,
            corpus_identity=_identity(),
        )

        backlog = census["backlog"]
        self.assertEqual(
            [entry["chunkIdHex"] for entry in backlog],
            ["0x00000058", "0xDEADBEEF"],
        )
        deform = backlog[0]
        self.assertEqual(deform["chunkName"], "deform")
        self.assertEqual(deform["occurrences"], 3)
        self.assertEqual(deform["filesBlocked"], 2)
        self.assertEqual(deform["factionReachableFiles"], 1)
        self.assertEqual(deform["factionFiles"], {"men": 1, "wild": 0})
        self.assertEqual(deform["scannerClassifications"], ["unsupported"])
        unknown = backlog[1]
        self.assertIsNone(unknown["chunkName"])
        self.assertEqual(unknown["scannerClassifications"], ["unknown"])
        self.assertEqual(unknown["filesBlocked"], 1)
        self.assertEqual(unknown["factionReachableFiles"], 0)

        summary = census["summary"]
        self.assertEqual(summary["scannedFileCount"], 3)
        self.assertEqual(summary["blockedFileCount"], 2)
        self.assertEqual(summary["factionReachableBlockedFileCount"], 1)

    def test_decoded_secondary_streams_do_not_appear_in_the_backlog(self) -> None:
        # The Part-2 counter: a file whose vertices-2/normals-2 streams decode
        # cleanly must contribute no blocked files and no warnings.
        census = build_w3d_chunk_backlog(
            _evidence_rows(),
            faction_closures=_CLOSURES,
            corpus_identity=_identity(),
        )

        flagged_ids = {entry["chunkId"] for entry in census["backlog"]}
        self.assertNotIn(0x00000C00, flagged_ids)
        self.assertNotIn(0x00000C01, flagged_ids)
        # The other fixture files still warn (deform, unknown); the decoded
        # secondary streams contribute no warnings of any kind.
        self.assertEqual(
            census["summary"]["warningCodeCounts"],
            {"unknown-chunk": 1, "unsupported-chunk": 3},
        )

        # A malformed stream fails closed and surfaces as metadata damage.
        damaged = collect_w3d_backlog_file_evidence(
            _mesh("Short", extra=_secondary_streams(vertex_count=2)),
            "art/w3d/gu/short.w3d",
        )
        census = build_w3d_chunk_backlog(
            [damaged],
            faction_closures=_CLOSURES,
            corpus_identity=_identity(),
        )
        self.assertEqual(
            census["summary"]["warningCodeCounts"],
            {"invalid-secondary-geometry": 2},
        )
        self.assertEqual(census["anomalies"]["metadataDamaged"]["fileCount"], 1)

    def test_decode_corpus_join_and_index_rejections_are_aggregated(self) -> None:
        rows = _evidence_rows()
        rejected = collect_w3d_backlog_file_evidence(
            _chunk(
                0x00000700,
                _chunk(
                    0x00000701,
                    struct.pack(
                        "<II16s16s",
                        0x00010000,
                        1,
                        _fixed("Padded ", 16),
                        _fixed("Rig", 16),
                    ),
                ),
                children=True,
            ),
            "art/w3d/gu/padded.w3d",
        )
        self.assertEqual(
            rejected.index_rejection_causes,
            ("surrounding-whitespace-identifier",),
        )
        # The padding trim is the exact single-file repair, so the row is
        # trim-pending; corpus-wide admission is decided during aggregation.
        self.assertEqual(
            rejected.trimmed_identifiers,
            (("model", "Padded ", "Padded"),),
        )
        rows.append(rejected)
        states = {
            row.source_sha256: {
                "damaged": row.path_key.endswith("prop.w3d"),
                "incomplete": row.path_key.endswith("prop.w3d"),
                "unresolved": False,
                "unsupported": False,
                "streamComplete": not row.path_key.endswith("prop.w3d"),
            }
            for row in rows
        }
        census = build_w3d_chunk_backlog(
            rows,
            faction_closures=_CLOSURES,
            corpus_identity=_identity(),
            decode_source_states=states,
            decode_chunk_rows={0x00000058: {
                "decodedStreamCount": 0,
                "terminalCount": 3,
            }},
        )

        anomalies = census["anomalies"]
        # The whitespace-padded id has a provably unique trimmed form, so the
        # file is admitted with the trim recorded instead of staying rejected.
        self.assertEqual(
            anomalies["indexRejected"],
            {
                "fileCount": 0,
                "causeCounts": {},
                "factionReachableFileCount": 0,
            },
        )
        self.assertEqual(
            anomalies["trimAdmitted"],
            {
                "fileCount": 1,
                "identifierCount": 1,
                "kindCounts": {"model": 1},
                "factionReachableFileCount": 0,
            },
        )
        decode = anomalies["decodeCorpus"]
        self.assertTrue(decode["joinedBySourceSha256"])
        self.assertEqual(decode["damagedFileCount"], 1)
        self.assertEqual(decode["notStreamCompleteFileCount"], 1)
        self.assertEqual(decode["unjoinedFileCount"], 0)
        deform = census["backlog"][0]
        self.assertEqual(
            deform["decodeLayer"],
            {"decodedStreamCount": 0, "terminalCount": 3},
        )

    def test_vacuous_stream_completeness_is_separately_counted(self) -> None:
        rows = _evidence_rows()
        # The reachable deform file plays the vacuously complete role; the
        # unreachable prop is complete-by-decoding; the clean file too.
        states = {
            row.source_sha256: {
                "damaged": False,
                "incomplete": False,
                "unresolved": False,
                "unsupported": False,
                "streamComplete": True,
                "streamCompleteVacuous": row.path_key == "art/w3d/gu/deform.w3d",
            }
            for row in rows
        }
        census = build_w3d_chunk_backlog(
            rows,
            faction_closures=_CLOSURES,
            corpus_identity=_identity(),
            decode_source_states=states,
        )
        decode = census["anomalies"]["decodeCorpus"]
        self.assertEqual(decode["notStreamCompleteFileCount"], 0)
        self.assertEqual(decode["vacuouslyStreamCompleteFileCount"], 1)
        self.assertEqual(decode["vacuouslyStreamCompleteFactionReachableFileCount"], 1)

        # A report predating the marker still aggregates, with zero vacuous.
        legacy_states = {
            sha: {key: value for key, value in row.items() if key != "streamCompleteVacuous"}
            for sha, row in states.items()
        }
        legacy = build_w3d_chunk_backlog(
            rows,
            faction_closures=_CLOSURES,
            corpus_identity=_identity(),
            decode_source_states=legacy_states,
        )
        decode = legacy["anomalies"]["decodeCorpus"]
        self.assertEqual(decode["vacuouslyStreamCompleteFileCount"], 0)

        # The marker is validated fail-closed, never coerced.
        tampered = {
            sha: {**row, "streamCompleteVacuous": "yes"}
            for sha, row in states.items()
        }
        with self.assertRaisesRegex(W3DChunkBacklogError, "streamCompleteVacuous"):
            build_w3d_chunk_backlog(
                rows,
                faction_closures=_CLOSURES,
                corpus_identity=_identity(),
                decode_source_states=tampered,
            )

    def test_trim_collisions_keep_the_fail_closed_rejection(self) -> None:
        def _hlod(identifier: str, path: str):
            return collect_w3d_backlog_file_evidence(
                _chunk(
                    0x00000700,
                    _chunk(
                        0x00000701,
                        struct.pack(
                            "<II16s16s",
                            0x00010000,
                            1,
                            _fixed(identifier, 16),
                            _fixed("Rig", 16),
                        ),
                    ),
                    children=True,
                ),
                path,
            )

        # A trimmed id colliding with a plainly authored id in another file
        # keeps the fail-closed rejection for the padded file only.
        padded = _hlod("Padded ", "art/w3d/gu/padded.w3d")
        plain = _hlod("Padded", "art/w3d/gu/plain.w3d")
        census = build_w3d_chunk_backlog(
            [padded, plain],
            faction_closures=_CLOSURES,
            corpus_identity=_identity(),
        )
        anomalies = census["anomalies"]
        self.assertEqual(anomalies["trimAdmitted"]["fileCount"], 0)
        self.assertEqual(
            anomalies["indexRejected"],
            {
                "fileCount": 1,
                "causeCounts": {
                    "surrounding-whitespace-identifier": 1,
                    "trimmed-identifier-collision": 1,
                },
                "factionReachableFileCount": 0,
            },
        )

        # Two different files trimming to one logical id are both rejected.
        tabbed = _hlod("Padded\t", "art/w3d/gu/tabbed.w3d")
        census = build_w3d_chunk_backlog(
            [padded, tabbed],
            faction_closures=_CLOSURES,
            corpus_identity=_identity(),
        )
        anomalies = census["anomalies"]
        self.assertEqual(anomalies["trimAdmitted"]["fileCount"], 0)
        self.assertEqual(anomalies["indexRejected"]["fileCount"], 2)
        self.assertEqual(
            anomalies["indexRejected"]["causeCounts"][
                "trimmed-identifier-collision"
            ],
            2,
        )

        # The counter-case: with no collision the same padded file is
        # admitted, proving the rejection above is the collision's doing.
        census = build_w3d_chunk_backlog(
            [padded],
            faction_closures=_CLOSURES,
            corpus_identity=_identity(),
        )
        self.assertEqual(
            census["anomalies"]["trimAdmitted"],
            {
                "fileCount": 1,
                "identifierCount": 1,
                "kindCounts": {"model": 1},
                "factionReachableFileCount": 0,
            },
        )
        self.assertEqual(census["anomalies"]["indexRejected"]["fileCount"], 0)

    def test_duplicate_logical_ids_split_identical_and_distinct_bytes(self) -> None:
        first = collect_w3d_backlog_file_evidence(
            _mesh("Same"), "art/w3d/aa/one.w3d"
        )
        second = collect_w3d_backlog_file_evidence(
            _mesh("Same"), "art/w3d/aa/two.w3d"
        )
        third = collect_w3d_backlog_file_evidence(
            _mesh("Same", vertex_count=4), "art/w3d/aa/three.w3d"
        )
        census = build_w3d_chunk_backlog(
            [first, second, third],
            faction_closures=_CLOSURES,
            corpus_identity=_identity(),
        )

        duplicates = census["anomalies"]["duplicateLogicalIds"]
        self.assertEqual(duplicates["groupCount"], 1)
        self.assertEqual(duplicates["kindCounts"], {"model": 1})
        # Three files claim the same logical model id; the bytes disagree, so
        # the group counts as distinct-bytes ambiguity, not packaging reuse.
        self.assertEqual(duplicates["identicalByteGroupCount"], 0)
        self.assertEqual(duplicates["distinctByteGroupCount"], 1)

    def test_rendering_is_deterministic_lf_terminated_and_order_stable(self) -> None:
        rows = _evidence_rows()
        first = render_w3d_chunk_backlog(
            build_w3d_chunk_backlog(
                rows,
                faction_closures=_CLOSURES,
                corpus_identity=_identity(),
            )
        )
        second = render_w3d_chunk_backlog(
            build_w3d_chunk_backlog(
                list(reversed(rows)),
                faction_closures={
                    faction: list(paths) for faction, paths in _CLOSURES.items()
                },
                corpus_identity=_identity(),
            )
        )

        self.assertEqual(first, second)
        self.assertTrue(first.endswith("\n"))
        self.assertNotIn("\r", first)
        self.assertNotIn("art/w3d", first)

    def test_fail_closed_on_untrustworthy_inputs(self) -> None:
        rows = _evidence_rows()
        with self.assertRaisesRegex(W3DChunkBacklogError, "duplicate evidence"):
            build_w3d_chunk_backlog(
                [rows[0], rows[0]],
                faction_closures=_CLOSURES,
                corpus_identity=_identity(),
            )
        with self.assertRaisesRegex(W3DChunkBacklogError, "at least one"):
            build_w3d_chunk_backlog(
                [],
                faction_closures=_CLOSURES,
                corpus_identity=_identity(),
            )
        with self.assertRaisesRegex(W3DChunkBacklogError, "non-empty mapping"):
            build_w3d_chunk_backlog(
                rows, faction_closures={}, corpus_identity=_identity()
            )
        with self.assertRaisesRegex(W3DChunkBacklogError, "SHA-256"):
            build_w3d_chunk_backlog(
                rows,
                faction_closures=_CLOSURES,
                corpus_identity=_identity(),
                decode_source_states={"nope": {}},
            )
        with self.assertRaisesRegex(W3DChunkBacklogError, "must be a boolean"):
            build_w3d_chunk_backlog(
                rows,
                faction_closures=_CLOSURES,
                corpus_identity=_identity(),
                decode_source_states={"a" * 64: {"damaged": "yes"}},
            )
        with self.assertRaisesRegex(W3DChunkBacklogError, "integer counts"):
            build_w3d_chunk_backlog(
                rows,
                faction_closures=_CLOSURES,
                corpus_identity=_identity(),
                decode_chunk_rows={1: {"decodedStreamCount": -1, "terminalCount": 0}},
            )
        with self.assertRaisesRegex(W3DChunkBacklogError, "backlog schema"):
            render_w3d_chunk_backlog({"schema": "something-else"})

    def test_slice_plan_paths_are_extracted_casefolded(self) -> None:
        plan = {
            "pack": "bfme2-men-vslice",
            "resources": [
                {"source": {"virtualPath": "Art/W3D/GU/GUHero.W3D"}},
                {"nested": [{"path": "art/w3d/gu/guarcher.w3d"}]},
                {"texture": "art/textures/skin.tga"},
            ],
        }
        self.assertEqual(
            w3d_paths_from_slice_plan(plan),
            frozenset({"art/w3d/gu/guhero.w3d", "art/w3d/gu/guarcher.w3d"}),
        )
        with self.assertRaises(W3DChunkBacklogError):
            w3d_paths_from_slice_plan("not-a-plan")  # type: ignore[arg-type]

    def test_schema_identity_is_present(self) -> None:
        census = build_w3d_chunk_backlog(
            _evidence_rows(),
            faction_closures=_CLOSURES,
            corpus_identity=_identity(),
        )
        self.assertEqual(census["schema"], RETAIL_W3D_CHUNK_BACKLOG_SCHEMA)
        self.assertEqual(census["corpus"], _identity())


class W3DChunkBacklogStalenessGuardTests(unittest.TestCase):
    """The census may republish decode-corpus figures only when it can date
    them; an undatable report is refused, a stale one is loudly marked."""

    def _tree(self, base: Path) -> tuple[Path, bytes]:
        from tests.test_w3d_decode_corpus import _write_corpus

        root = base / "effective-assets"
        root.mkdir()
        payload = _mesh("Deformed") + _chunk(0x00000058, b"d" * 8)
        _write_corpus(root, {"art/w3d/gu/deform.w3d": payload})
        return root, payload

    def _decode_corpus(
        self, payload: bytes, provenance: object
    ) -> dict[str, object]:
        document: dict[str, object] = {
            "schema": "openbfme.w3d-decode-corpus",
            "schemaVersion": 2,
            "hashes": {"corpusSha256": "c" * 64},
            "sources": [
                {
                    "sourceSha256": hashlib.sha256(payload).hexdigest(),
                    "status": {
                        "streamComplete": False,
                        "incomplete": True,
                        "damaged": True,
                        "unresolved": False,
                        "unsupported": False,
                    },
                }
            ],
            "chunks": [],
        }
        if provenance is not None:
            document["provenance"] = provenance
        return document

    def test_undatable_decode_corpus_is_refused(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root, payload = self._tree(Path(raw))
            with self.assertRaisesRegex(W3DChunkBacklogError, "undatable"):
                scan_w3d_chunk_backlog_tree(
                    root,
                    faction_closures=_CLOSURES,
                    decode_corpus=self._decode_corpus(payload, None),
                )
            with self.assertRaisesRegex(W3DChunkBacklogError, "undatable"):
                scan_w3d_chunk_backlog_tree(
                    root,
                    faction_closures=_CLOSURES,
                    decode_corpus=self._decode_corpus(
                        payload, {"fingerprintSha256": "not-a-hash"}
                    ),
                )

    def test_fresh_decode_corpus_is_dated_in_the_census(self) -> None:
        recorded = measurement_provenance(DECODE_CORPUS_MEASUREMENT_MODULE)
        with tempfile.TemporaryDirectory() as raw:
            root, payload = self._tree(Path(raw))
            census = scan_w3d_chunk_backlog_tree(
                root,
                faction_closures=_CLOSURES,
                decode_corpus=self._decode_corpus(payload, recorded),
            )

        verdict = census["corpus"]["decodeCorpus"]["provenance"]
        self.assertEqual(verdict["status"], "fresh")
        self.assertEqual(verdict["staleModules"], [])
        self.assertEqual(
            verdict["recordedFingerprintSha256"],
            verdict["currentFingerprintSha256"],
        )
        # The census also dates itself.
        own = census["provenance"]
        self.assertEqual(own["rootModule"], CENSUS_MEASUREMENT_MODULE)
        self.assertEqual(
            own["fingerprintSha256"],
            measurement_fingerprint(CENSUS_MEASUREMENT_MODULE)[
                "fingerprintSha256"
            ],
        )
        # The stale figures joined from the report remain visible beside the
        # verdict rather than being silently dropped.
        decode = census["anomalies"]["decodeCorpus"]
        self.assertTrue(decode["joinedBySourceSha256"])
        self.assertEqual(decode["damagedFileCount"], 1)

    def test_stale_decode_corpus_is_marked_loudly_with_the_moved_module(
        self,
    ) -> None:
        recorded = measurement_provenance(DECODE_CORPUS_MEASUREMENT_MODULE)
        tampered = dict(recorded)
        tampered["modules"] = [
            (
                {**row, "sha256": "0" * 64}
                if row["module"] == "w3d_decode_plan"
                else dict(row)
            )
            for row in recorded["modules"]
        ]
        tampered["fingerprintSha256"] = "f" * 64
        with tempfile.TemporaryDirectory() as raw:
            root, payload = self._tree(Path(raw))
            census = scan_w3d_chunk_backlog_tree(
                root,
                faction_closures=_CLOSURES,
                decode_corpus=self._decode_corpus(payload, tampered),
            )

        verdict = census["corpus"]["decodeCorpus"]["provenance"]
        self.assertEqual(verdict["status"], "stale")
        self.assertEqual(verdict["staleModules"], ["w3d_decode_plan"])
        # The verdict survives rendering into the committed document form.
        rendered = render_w3d_chunk_backlog(census)
        self.assertIn('"status": "stale"', rendered)


class CommittedCensusProvenanceTests(unittest.TestCase):
    """The committed census must be dated, and dated FRESH against the code
    in this working tree.  A red run here means a stored report's figures
    were measured by code that has since changed: regenerate the decode
    corpus report (python -m openbfme_importer.w3d_decode_corpus_report)
    and/or the census (python -m openbfme_importer.w3d_chunk_backlog)."""

    _CENSUS_PATH = (
        Path(__file__).resolve().parents[2]
        / "game"
        / "data"
        / "retail_w3d_chunk_backlog.json"
    )

    def _census(self) -> dict[str, object]:
        return json.loads(self._CENSUS_PATH.read_text(encoding="utf-8"))

    def test_committed_census_dates_itself_and_is_current(self) -> None:
        census = self._census()
        own = census.get("provenance")
        self.assertIsInstance(
            own,
            dict,
            "committed census carries no provenance block: regenerate it "
            "with python -m openbfme_importer.w3d_chunk_backlog",
        )
        current = measurement_fingerprint(CENSUS_MEASUREMENT_MODULE)
        recorded_modules = {
            row["module"]: row["sha256"] for row in own.get("modules", [])
        }
        current_modules = {
            row["module"]: row["sha256"] for row in current["modules"]
        }
        moved = sorted(
            module
            for module in set(recorded_modules) | set(current_modules)
            if recorded_modules.get(module) != current_modules.get(module)
        )
        self.assertEqual(
            own.get("fingerprintSha256"),
            current["fingerprintSha256"],
            "the committed census predates the census-measurement code now "
            f"on disk (moved modules: {moved}); its figures can no longer "
            "be trusted -- regenerate game/data/retail_w3d_chunk_backlog"
            ".json with python -m openbfme_importer.w3d_chunk_backlog",
        )

    def test_committed_census_decode_corpus_figures_are_dated_fresh(
        self,
    ) -> None:
        census = self._census()
        corpus = census.get("corpus")
        self.assertIsInstance(corpus, dict)
        decode = corpus.get("decodeCorpus")
        self.assertIsInstance(
            decode,
            dict,
            "committed census republishes no decode-corpus section; if that "
            "is intentional, update this test with the reasoning",
        )
        verdict = decode.get("provenance")
        self.assertIsInstance(
            verdict,
            dict,
            "committed census republishes decode-corpus figures it cannot "
            "date: regenerate the stored decode-corpus report with python "
            "-m openbfme_importer.w3d_decode_corpus_report and rebuild the "
            "census",
        )
        self.assertEqual(
            verdict.get("status"),
            "fresh",
            "the committed census knowingly republishes STALE decode-corpus "
            f"figures (stale modules: {verdict.get('staleModules')}); "
            "regenerate the stored report and rebuild the census",
        )
        current = measurement_fingerprint(DECODE_CORPUS_MEASUREMENT_MODULE)
        self.assertEqual(
            verdict.get("currentFingerprintSha256"),
            current["fingerprintSha256"],
            "the decode-measurement code moved after the committed census "
            "was generated; the stored decode-corpus report (and then the "
            "census) must be regenerated before its figures are trusted",
        )


if __name__ == "__main__":
    unittest.main()
