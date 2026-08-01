from __future__ import annotations

import json
from pathlib import Path
import tempfile
import unittest

from openbfme_importer.measurement_provenance import measurement_provenance
from openbfme_importer.w3d_decode_corpus import scan_w3d_decode_corpus
from openbfme_importer.w3d_decode_corpus_report import (
    DECODE_CORPUS_MEASUREMENT_MODULE,
    render_w3d_decode_corpus_report,
    write_w3d_decode_corpus_report,
)

from tests.test_w3d_decode_corpus import _complete_mesh, _write_corpus


class W3DDecodeCorpusReportDocumentTests(unittest.TestCase):
    def test_document_is_the_neutral_report_plus_dated_provenance(self) -> None:
        files = {
            "art/models/one.w3d": _complete_mesh("One", 1.0),
            "art/models/two.w3d": _complete_mesh("Two", 2.0),
        }
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            _write_corpus(root, files)
            report = scan_w3d_decode_corpus(root)
            rendered = render_w3d_decode_corpus_report(report)

        document = json.loads(rendered)
        provenance = document.pop("provenance")
        self.assertEqual(document, json.loads(json.dumps(report.neutral())))
        self.assertEqual(
            provenance["rootModule"], DECODE_CORPUS_MEASUREMENT_MODULE
        )
        self.assertEqual(
            provenance,
            measurement_provenance(DECODE_CORPUS_MEASUREMENT_MODULE),
        )

    def test_serialization_matches_the_stored_document_exactly(self) -> None:
        # Compact separators, ASCII, sorted keys, one trailing LF: the same
        # canonical form as the established stored report, so regeneration
        # diffs show fact changes only.
        files = {"art/models/one.w3d": _complete_mesh("One", 1.0)}
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            _write_corpus(root, files)
            report = scan_w3d_decode_corpus(root)
            rendered = render_w3d_decode_corpus_report(report)

        self.assertTrue(rendered.endswith("\n"))
        self.assertNotIn("\r", rendered)
        canonical = (
            json.dumps(
                json.loads(rendered),
                ensure_ascii=True,
                sort_keys=True,
                separators=(",", ":"),
            )
            + "\n"
        )
        self.assertEqual(rendered, canonical)

    def test_cli_writer_is_byte_identical_across_runs(self) -> None:
        files = {"art/models/one.w3d": _complete_mesh("One", 1.0)}
        with tempfile.TemporaryDirectory() as raw:
            base = Path(raw)
            root = base / "effective-assets"
            root.mkdir()
            output = base / "reports"
            output.mkdir()
            _write_corpus(root, files)
            first_path = output / "first.json"
            second_path = output / "second.json"
            write_w3d_decode_corpus_report(root, first_path)
            write_w3d_decode_corpus_report(root, second_path)
            first = first_path.read_bytes()
            second = second_path.read_bytes()

        self.assertEqual(first, second)
        self.assertTrue(first.endswith(b"\n"))
        self.assertNotIn(b"\r", first)

    def test_rendering_rejects_non_reports(self) -> None:
        with self.assertRaises(TypeError):
            render_w3d_decode_corpus_report({"summary": {}})  # type: ignore[arg-type]


if __name__ == "__main__":
    unittest.main()
