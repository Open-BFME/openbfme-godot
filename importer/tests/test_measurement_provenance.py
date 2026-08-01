from __future__ import annotations

from pathlib import Path
import tempfile
import unittest

from openbfme_importer.measurement_provenance import (
    MeasurementProvenanceError,
    measurement_fingerprint,
    measurement_fingerprint_verdict,
    measurement_module_closure,
    measurement_provenance,
)


def _fake_package(root: Path) -> None:
    (root / "alpha.py").write_text(
        "from .beta import helper\n\n\ndef run():\n"
        "    from .gamma import lazy\n    return helper() + lazy()\n",
        encoding="utf-8",
    )
    (root / "beta.py").write_text(
        "import openbfme_importer.delta\n\n\ndef helper():\n    return 1\n",
        encoding="utf-8",
    )
    (root / "gamma.py").write_text(
        "def lazy():\n    return 2\n", encoding="utf-8"
    )
    (root / "delta.py").write_text("VALUE = 3\n", encoding="utf-8")
    # Present but never imported: must stay outside the closure.
    (root / "unrelated.py").write_text("VALUE = 4\n", encoding="utf-8")


class MeasurementClosureTests(unittest.TestCase):
    def test_closure_follows_relative_absolute_and_lazy_imports(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            _fake_package(root)
            closure = measurement_module_closure("alpha", package_root=root)

        self.assertEqual(
            [module for module, _ in closure],
            ["alpha", "beta", "delta", "gamma"],
        )
        for _, digest in closure:
            self.assertRegex(digest, r"^[0-9a-f]{64}$")

    def test_unrelated_code_movement_does_not_change_the_fingerprint(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            _fake_package(root)
            before = measurement_fingerprint("alpha", package_root=root)
            (root / "unrelated.py").write_text(
                "VALUE = 999  # moved\n", encoding="utf-8"
            )
            after = measurement_fingerprint("alpha", package_root=root)

        self.assertEqual(
            before["fingerprintSha256"], after["fingerprintSha256"]
        )

    def test_any_byte_change_in_the_closure_changes_the_fingerprint(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            _fake_package(root)
            before = measurement_fingerprint("alpha", package_root=root)
            # Even a comment-only edit to a lazily imported module counts.
            (root / "gamma.py").write_text(
                "# touched\ndef lazy():\n    return 2\n", encoding="utf-8"
            )
            after = measurement_fingerprint("alpha", package_root=root)

        self.assertNotEqual(
            before["fingerprintSha256"], after["fingerprintSha256"]
        )

    def test_unresolvable_imports_fail_the_walk(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            (root / "alpha.py").write_text(
                "from .missing import x\n", encoding="utf-8"
            )
            with self.assertRaisesRegex(
                MeasurementProvenanceError, "no source file"
            ):
                measurement_module_closure("alpha", package_root=root)
            with self.assertRaisesRegex(
                MeasurementProvenanceError, "no source file"
            ):
                measurement_module_closure("absent", package_root=root)

    def test_the_guard_never_fingerprints_itself(self) -> None:
        with self.assertRaisesRegex(
            MeasurementProvenanceError, "not a measurement"
        ):
            measurement_module_closure("measurement_provenance")

    def test_real_decode_closure_is_measurement_code_only(self) -> None:
        fingerprint = measurement_fingerprint(
            "openbfme_importer.w3d_decode_corpus"
        )
        modules = {row["module"] for row in fingerprint["modules"]}
        self.assertIn("w3d_decode_corpus", modules)
        self.assertIn("w3d_decode_plan", modules)
        self.assertIn("w3d_metadata", modules)
        # Provenance stamping and its git helpers are infrastructure, not
        # measurement; their edits must not mark stored evidence stale.
        self.assertNotIn("measurement_provenance", modules)
        self.assertNotIn("tools", modules)
        self.assertNotIn("w3d_decode_corpus_report", modules)
        repeated = measurement_fingerprint(
            "openbfme_importer.w3d_decode_corpus"
        )
        self.assertEqual(fingerprint, repeated)


class MeasurementProvenanceBlockTests(unittest.TestCase):
    def test_provenance_block_is_json_ready_and_deterministic(self) -> None:
        first = measurement_provenance("openbfme_importer.w3d_decode_corpus")
        second = measurement_provenance("openbfme_importer.w3d_decode_corpus")

        self.assertEqual(first, second)
        self.assertEqual(first["schema"], "openbfme.measurement-provenance")
        self.assertEqual(
            first["rootModule"], "openbfme_importer.w3d_decode_corpus"
        )
        self.assertRegex(first["fingerprintSha256"], r"^[0-9a-f]{64}$")
        self.assertEqual(first["moduleCount"], len(first["modules"]))
        self.assertIn("gitCommit", first)
        self.assertIn("gitWorktreeClean", first)


class MeasurementVerdictTests(unittest.TestCase):
    def test_matching_fingerprint_reads_fresh(self) -> None:
        recorded = measurement_provenance(
            "openbfme_importer.w3d_decode_corpus"
        )
        verdict = measurement_fingerprint_verdict(
            recorded, "openbfme_importer.w3d_decode_corpus"
        )

        self.assertEqual(verdict["status"], "fresh")
        self.assertEqual(verdict["staleModules"], [])
        self.assertEqual(
            verdict["recordedFingerprintSha256"],
            verdict["currentFingerprintSha256"],
        )

    def test_moved_measurement_module_reads_stale_and_is_named(self) -> None:
        recorded = measurement_provenance(
            "openbfme_importer.w3d_decode_corpus"
        )
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
        verdict = measurement_fingerprint_verdict(
            tampered, "openbfme_importer.w3d_decode_corpus"
        )

        self.assertEqual(verdict["status"], "stale")
        self.assertEqual(verdict["staleModules"], ["w3d_decode_plan"])

    def test_undatable_provenance_is_refused_never_fresh(self) -> None:
        for undatable in (
            None,
            "not-a-mapping",
            {},
            {"fingerprintSha256": "zz"},
            {
                "fingerprintSha256": "a" * 64,
                "rootModule": "openbfme_importer.some_other_measurement",
            },
        ):
            with self.assertRaises(MeasurementProvenanceError):
                measurement_fingerprint_verdict(
                    undatable, "openbfme_importer.w3d_decode_corpus"
                )


if __name__ == "__main__":
    unittest.main()
