from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest import mock

from importer.tests.test_sage_scb import _fixture

import openbfme_importer.scb_native_corpus as corpus_module
from openbfme_importer.scb_native_corpus import (
    MAX_SCB_NATIVE_FILES,
    MAX_SCB_NATIVE_TOTAL_BYTES,
    SCB_NATIVE_CORPUS_MANIFEST,
    ScbNativeCorpusError,
    ScbNativeCorpusLimitError,
    ScbNativeCorpusReuseError,
    build_scb_native_corpus,
)
from openbfme_importer.sage_scb import backtest_sage_scb_native


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
        sorted(files.items(), key=lambda item: (item[0].casefold(), item[0]))
    ):
        inventory.append(
            {
                "archive": "fixture/asset.big",
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
            "root": "synthetic-retail-install",
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
    (metadata / "manifest.json").write_bytes(
        (
            json.dumps(manifest, indent=2, sort_keys=True, ensure_ascii=False) + "\n"
        ).encode("utf-8")
    )


def _write_source(root: Path, files: dict[str, bytes]) -> dict[str, object]:
    root.mkdir(parents=True, exist_ok=True)
    for relative, payload in files.items():
        target = root.joinpath(*relative.split("/"))
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(payload)
    manifest = _manifest(files)
    _write_manifest(root, manifest)
    return manifest


def _corpus_manifest(root: Path) -> Path:
    return root.joinpath(*SCB_NATIVE_CORPUS_MANIFEST.split("/"))


def _ordinary_files(root: Path) -> list[str]:
    return sorted(
        (
            path.relative_to(root).as_posix()
            for path in root.rglob("*")
            if path.is_file()
        ),
        key=lambda value: (value.casefold(), value),
    )


class ScbNativeCorpusTests(unittest.TestCase):
    def test_converts_every_scb_deduplicates_and_exactly_backtests(self) -> None:
        first = _fixture()
        second = first.replace(b"fixture-camera", b"another-camera")
        files = {
            "maps/A.scb": first,
            "maps/Duplicate.SCB": first,
            "maps/B.sCb": second,
            "data/verified.bin": b"verified but not emitted",
        }
        with tempfile.TemporaryDirectory() as raw:
            base = Path(raw)
            source = base / "effective-assets"
            output = base / "scb-native"
            _write_source(source, files)

            report = build_scb_native_corpus(source, output)

            self.assertTrue(report.complete)
            self.assertFalse(report.reused)
            self.assertEqual(report.source_count, 3)
            self.assertEqual(report.output_count, 2)
            self.assertEqual(
                report.entries[0].output_path,
                report.entries[2].output_path,
            )
            self.assertEqual(
                _ordinary_files(output),
                [
                    SCB_NATIVE_CORPUS_MANIFEST,
                    *(item.path for item in report.outputs),
                ],
            )
            for native_output in report.outputs:
                target = output.joinpath(*native_output.path.split("/"))
                self.assertEqual(target.stat().st_nlink, 1)
                native = json.loads(target.read_text(encoding="utf-8"))
                source_entry = next(
                    item
                    for item in report.entries
                    if item.source_sha256 == native_output.source_sha256
                )
                evidence = backtest_sage_scb_native(
                    files[source_entry.source_path], native
                )
                self.assertEqual(evidence.to_dict(), native_output.backtest_evidence)
            neutral = json.dumps(report.neutral(), sort_keys=True)
            self.assertNotIn(str(source), neutral)
            self.assertNotIn("duplicate-script", neutral)
            self.assertNotIn("fixture-camera", neutral)
            self.assertIn("maps/A.scb", neutral)

    def test_matching_output_is_deterministic_exact_noop_reuse(self) -> None:
        files = {"maps/a.scb": _fixture(), "other.bin": b"other"}
        with tempfile.TemporaryDirectory() as raw:
            base = Path(raw)
            source = base / "source"
            output = base / "output"
            _write_source(source, files)
            first = build_scb_native_corpus(
                source, output, max_files=9, max_total_bytes=999_999
            )
            before = {
                path.relative_to(output).as_posix(): (
                    path.read_bytes(),
                    path.stat().st_mtime_ns,
                )
                for path in output.rglob("*")
                if path.is_file()
            }

            second = build_scb_native_corpus(
                source, output, max_files=9, max_total_bytes=999_999
            )

            self.assertTrue(second.reused)
            self.assertEqual(first.identity_sha256, second.identity_sha256)
            self.assertEqual(first.request_sha256, second.request_sha256)
            self.assertEqual(first.manifest_sha256, second.manifest_sha256)
            self.assertEqual(
                before,
                {
                    path.relative_to(output).as_posix(): (
                        path.read_bytes(),
                        path.stat().st_mtime_ns,
                    )
                    for path in output.rglob("*")
                    if path.is_file()
                },
            )

    def test_rejects_source_tamper_exact_tree_extras_and_manifest_tamper(self) -> None:
        files = {"maps/a.scb": _fixture(), "data.bin": b"abcde"}
        with tempfile.TemporaryDirectory() as raw:
            base = Path(raw)
            source = base / "source"
            output = base / "output"
            _write_source(source, files)
            (source / "data.bin").write_bytes(b"vwxyz")
            with self.assertRaisesRegex(ScbNativeCorpusError, "SHA-256"):
                build_scb_native_corpus(source, output)
            self.assertFalse(output.exists())

        with tempfile.TemporaryDirectory() as raw:
            base = Path(raw)
            source = base / "source"
            output = base / "output"
            _write_source(source, files)
            (source / "undeclared.bin").write_bytes(b"extra")
            with self.assertRaisesRegex(ScbNativeCorpusError, "undeclared files"):
                build_scb_native_corpus(source, output)
            self.assertFalse(output.exists())

        with tempfile.TemporaryDirectory() as raw:
            base = Path(raw)
            source = base / "source"
            output = base / "output"
            manifest = _write_source(source, files)
            manifest["aggregate_sha256"] = "0" * 64
            _write_manifest(source, manifest)
            with self.assertRaisesRegex(ScbNativeCorpusError, "aggregate SHA-256"):
                build_scb_native_corpus(source, output)
            self.assertFalse(output.exists())

    def test_malformed_scb_aborts_without_publishing(self) -> None:
        files = {"maps/good.scb": _fixture(), "maps/bad.SCB": b"not-an-scb"}
        with tempfile.TemporaryDirectory() as raw:
            base = Path(raw)
            source = base / "source"
            output = base / "output"
            _write_source(source, files)

            with self.assertRaisesRegex(ScbNativeCorpusError, "rejected conversion"):
                build_scb_native_corpus(source, output)

            self.assertFalse(output.exists())
            self.assertFalse(
                any(base.glob(f".{output.name}.staging-*")),
                "failed transactions must not leave staged retail payloads",
            )

    def test_reuse_rejects_output_tamper_and_force_repairs_it(self) -> None:
        files = {"maps/a.scb": _fixture()}
        with tempfile.TemporaryDirectory() as raw:
            base = Path(raw)
            source = base / "source"
            output = base / "output"
            _write_source(source, files)
            first = build_scb_native_corpus(source, output)
            native_path = output.joinpath(*first.outputs[0].path.split("/"))
            native_path.write_bytes(b"{" + b" " * (native_path.stat().st_size - 1))

            with self.assertRaises(ScbNativeCorpusReuseError):
                build_scb_native_corpus(source, output)
            rebuilt = build_scb_native_corpus(source, output, force=True)
            self.assertFalse(rebuilt.reused)
            self.assertEqual(
                hashlib.sha256(native_path.read_bytes()).hexdigest(),
                rebuilt.outputs[0].native_sha256,
            )

            (output / "extra.bin").write_bytes(b"extra")
            with self.assertRaisesRegex(
                ScbNativeCorpusReuseError, "exact declared paths"
            ):
                build_scb_native_corpus(source, output)
            build_scb_native_corpus(source, output, force=True)
            self.assertFalse((output / "extra.bin").exists())

    def test_limits_are_fail_closed_and_never_truncate(self) -> None:
        files = {"maps/a.scb": _fixture(), "maps/b.scb": _fixture()}
        with tempfile.TemporaryDirectory() as raw:
            base = Path(raw)
            source = base / "source"
            _write_source(source, files)
            with self.assertRaisesRegex(ScbNativeCorpusLimitError, "selects 2 files"):
                build_scb_native_corpus(source, base / "output", max_files=1)
            with self.assertRaisesRegex(ScbNativeCorpusLimitError, "bytes; limit"):
                build_scb_native_corpus(
                    source,
                    base / "output",
                    max_total_bytes=len(_fixture()),
                )
            self.assertFalse((base / "output").exists())
        for kwargs in (
            {"max_files": True},
            {"max_files": 0},
            {"max_files": MAX_SCB_NATIVE_FILES + 1},
            {"max_total_bytes": 0},
            {"max_total_bytes": MAX_SCB_NATIVE_TOTAL_BYTES + 1},
            {"force": 1},
        ):
            with self.subTest(kwargs=kwargs), tempfile.TemporaryDirectory() as raw:
                base = Path(raw)
                source = base / "source"
                _write_source(source, {"maps/a.scb": _fixture()})
                with self.assertRaises((TypeError, ValueError)):
                    build_scb_native_corpus(source, base / "output", **kwargs)

    def test_manifest_path_case_collisions_and_unsafe_paths_are_rejected(self) -> None:
        source_bytes = _fixture()
        with tempfile.TemporaryDirectory() as raw:
            base = Path(raw)
            source = base / "source"
            source.mkdir()
            target = source / "maps" / "A.scb"
            target.parent.mkdir()
            target.write_bytes(source_bytes)
            manifest = _manifest({"maps/A.scb": source_bytes})
            duplicate = dict(manifest["files"][0])
            duplicate["path"] = "maps/a.SCB"
            manifest["files"].append(duplicate)
            manifest["totals"]["files"] = 2
            manifest["totals"]["bytes"] = len(source_bytes) * 2
            manifest["catalog"]["entry_count"] = 2
            manifest["aggregate_sha256"] = _aggregate(manifest["files"])
            _write_manifest(source, manifest)

            with self.assertRaisesRegex(ScbNativeCorpusError, "case-colliding"):
                build_scb_native_corpus(source, base / "output")

        with tempfile.TemporaryDirectory() as raw:
            base = Path(raw)
            source = base / "source"
            source.mkdir()
            manifest = _manifest({"maps/a.scb": source_bytes})
            manifest["files"][0]["path"] = "../escape.scb"
            manifest["aggregate_sha256"] = _aggregate(manifest["files"])
            _write_manifest(source, manifest)
            with self.assertRaisesRegex(ScbNativeCorpusError, "unsafe"):
                build_scb_native_corpus(source, base / "output")

    def test_source_and_output_hard_links_are_rejected(self) -> None:
        files = {"maps/a.scb": _fixture()}
        with tempfile.TemporaryDirectory() as raw:
            base = Path(raw)
            source = base / "source"
            output = base / "output"
            _write_source(source, files)
            source_file = source / "maps" / "a.scb"
            sibling = base / "source-link.scb"
            try:
                os.link(source_file, sibling)
            except OSError as exc:
                self.skipTest(f"hard links unavailable: {exc}")
            with self.assertRaisesRegex(ScbNativeCorpusError, "hard-linked"):
                build_scb_native_corpus(source, output)
            sibling.unlink()

            report = build_scb_native_corpus(source, output)
            native = output.joinpath(*report.outputs[0].path.split("/"))
            output_link = base / "output-link.json"
            os.link(native, output_link)
            with self.assertRaises(ScbNativeCorpusReuseError):
                build_scb_native_corpus(source, output)

    def test_source_mutation_during_build_aborts_before_publication(self) -> None:
        files = {"maps/a.scb": _fixture(), "other.bin": b"other"}
        with tempfile.TemporaryDirectory() as raw:
            base = Path(raw)
            source = base / "source"
            output = base / "output"
            _write_source(source, files)
            original = corpus_module._effective._copy_or_hash_verified
            mutated = False

            def mutate_after_copy(*args: object, **kwargs: object) -> None:
                nonlocal mutated
                original(*args, **kwargs)
                if not mutated:
                    mutated = True
                    target = source / "other.bin"
                    target.write_bytes(b"OTHrR")

            with mock.patch.object(
                corpus_module._effective,
                "_copy_or_hash_verified",
                side_effect=mutate_after_copy,
            ):
                with self.assertRaises(ScbNativeCorpusError):
                    build_scb_native_corpus(source, output)
            self.assertFalse(output.exists())

    def test_post_publish_source_failure_rolls_back_prior_output(self) -> None:
        files = {"maps/a.scb": _fixture()}
        with tempfile.TemporaryDirectory() as raw:
            base = Path(raw)
            source = base / "source"
            output = base / "output"
            _write_source(source, files)
            build_scb_native_corpus(source, output)
            marker = output / "prior-output.marker"
            marker.write_bytes(b"prior")
            original_revalidate = corpus_module._revalidate_source
            calls = 0

            def fail_post_publish(*args: object, **kwargs: object) -> None:
                nonlocal calls
                calls += 1
                if calls == 3:
                    raise ScbNativeCorpusError("simulated post-publish mutation")
                original_revalidate(*args, **kwargs)

            with mock.patch.object(
                corpus_module,
                "_revalidate_source",
                side_effect=fail_post_publish,
            ):
                with self.assertRaisesRegex(
                    ScbNativeCorpusError, "prior output was preserved"
                ):
                    build_scb_native_corpus(source, output, force=True)

            self.assertEqual(marker.read_bytes(), b"prior")
            self.assertFalse(any(base.glob(f".{output.name}.staging-*")))

    def test_request_mismatch_requires_force(self) -> None:
        files = {"maps/a.scb": _fixture()}
        with tempfile.TemporaryDirectory() as raw:
            base = Path(raw)
            source = base / "source"
            output = base / "output"
            _write_source(source, files)
            first = build_scb_native_corpus(
                source, output, max_files=10, max_total_bytes=999_999
            )

            with self.assertRaisesRegex(ScbNativeCorpusReuseError, "verified request"):
                build_scb_native_corpus(
                    source, output, max_files=9, max_total_bytes=999_999
                )
            rebuilt = build_scb_native_corpus(
                source,
                output,
                max_files=9,
                max_total_bytes=999_999,
                force=True,
            )
            self.assertNotEqual(first.request_sha256, rebuilt.request_sha256)
            self.assertNotEqual(first.identity_sha256, rebuilt.identity_sha256)

    def test_output_and_manifest_tamper_are_rejected_on_reuse(self) -> None:
        files = {"maps/a.scb": _fixture()}
        with tempfile.TemporaryDirectory() as raw:
            base = Path(raw)
            source = base / "source"
            output = base / "output"
            _write_source(source, files)
            build_scb_native_corpus(source, output)
            manifest_path = _corpus_manifest(output)
            document = json.loads(manifest_path.read_text(encoding="utf-8"))
            document["summary"]["exactWireBacktestCount"] = 0
            manifest_path.write_bytes(
                (
                    json.dumps(document, indent=2, sort_keys=True, ensure_ascii=False)
                    + "\n"
                ).encode("utf-8")
            )
            with self.assertRaisesRegex(ScbNativeCorpusReuseError, "verified request"):
                build_scb_native_corpus(source, output)


if __name__ == "__main__":
    unittest.main()
