from __future__ import annotations

import hashlib
import io
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest import mock

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen

import openbfme_importer.support_corpus as support_module
from openbfme_importer.support_corpus import (
    OPAQUE_EXTENSIONS,
    SUPPORT_CORPUS_EXTENSIONS,
    SUPPORT_CORPUS_MANIFEST,
    SupportCorpusBuildError,
    SupportCorpusDependencyError,
    SupportCorpusError,
    SupportCorpusLimitError,
    SupportCorpusReuseError,
    build_support_corpus,
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


def _manifest(
    files: dict[str, tuple[bytes, str]],
    *,
    declared_hashes: dict[str, str] | None = None,
) -> dict[str, object]:
    inventory: list[dict[str, object]] = []
    offset = 32
    for precedence, (path, source) in enumerate(
        sorted(files.items(), key=lambda item: item[0].casefold())
    ):
        payload, archive = source
        inventory.append(
            {
                "archive": archive,
                "offset": offset,
                "path": path,
                "precedence": precedence,
                "sha256": (
                    declared_hashes[path]
                    if declared_hashes and path in declared_hashes
                    else hashlib.sha256(payload).hexdigest()
                ),
                "size": len(payload),
            }
        )
        offset += len(payload)
    return {
        "schema": "openbfme.effective-assets-manifest",
        "schema_version": 0,
        "catalog": {
            "archive_count": len({archive for _, archive in files.values()}),
            "entry_count": len(inventory),
            "format": 4,
            "identity_sha256": "a" * 64,
        },
        "install": {"identity_sha256": "b" * 64, "root": "fixture-install"},
        "totals": {
            "bytes": sum(len(payload) for payload, _ in files.values()),
            "files": len(files),
        },
        "aggregate_sha256": _aggregate(inventory),
        "files": inventory,
    }


def _write_manifest(root: Path, document: dict[str, object]) -> None:
    metadata = root / ".openbfme"
    metadata.mkdir(parents=True, exist_ok=True)
    (metadata / "manifest.json").write_text(
        json.dumps(document, ensure_ascii=True, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def _write_effective_assets(
    root: Path,
    files: dict[str, tuple[bytes, str]],
    *,
    declared_hashes: dict[str, str] | None = None,
) -> dict[str, object]:
    for relative, (payload, _) in files.items():
        target = root.joinpath(*relative.split("/"))
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(payload)
    document = _manifest(files, declared_hashes=declared_hashes)
    _write_manifest(root, document)
    return document


def _font_payload() -> bytes:
    builder = FontBuilder(1024, isTTF=True)
    glyph_order = [".notdef", "A"]
    builder.setupGlyphOrder(glyph_order)
    builder.setupCharacterMap({65: "A"})

    empty_pen = TTGlyphPen(None)
    notdef = empty_pen.glyph()
    pen = TTGlyphPen(None)
    pen.moveTo((100, 0))
    pen.lineTo((300, 700))
    pen.lineTo((500, 0))
    pen.closePath()
    glyph_a = pen.glyph()
    builder.setupGlyf({".notdef": notdef, "A": glyph_a})
    builder.setupHorizontalMetrics({".notdef": (600, 0), "A": (600, 50)})
    builder.setupHorizontalHeader(ascent=800, descent=-200)
    builder.setupOS2(
        sTypoAscender=800,
        sTypoDescender=-200,
        usWinAscent=800,
        usWinDescent=200,
    )
    builder.setupNameTable(
        {
            "familyName": "OpenBFME Test Fixture",
            "styleName": "Regular",
            "uniqueFontIdentifier": "OpenBFME-Test-Fixture-Regular",
            "fullName": "OpenBFME Test Fixture Regular",
            "psName": "OpenBFMETestFixture-Regular",
        }
    )
    builder.setupPost()
    builder.setupMaxp()
    output = io.BytesIO()
    builder.save(output)
    return output.getvalue()


def _tree_snapshot(root: Path) -> dict[str, str]:
    return {
        item.relative_to(root).as_posix(): hashlib.sha256(item.read_bytes()).hexdigest()
        for item in sorted(path for path in root.rglob("*") if path.is_file())
    }


def _base_files() -> dict[str, tuple[bytes, str]]:
    font = _font_payload()
    files: dict[str, tuple[bytes, str]] = {
        "Support/a.basis": (b"basis = utf8\n", "assets0.big"),
        "Support/b.bhav": (b"behavior = utf8\n", "assets0.big"),
        "Support/c.csv": (b"key,value\n1,2\n", "assets0.big"),
        "Support/d.dat": (b"key=value\n", "assets0.big"),
        "Support/e.nvp": (b"profile utf8\n", "assets0.big"),
        "Support/f.ru": ("text=\u041f\u0440\u0438\u0432\u0435\u0442\n".encode(), "assets0.big"),
        "Schema/a.xsd": (b"<schema><node value='1'/></schema>", "assets1.big"),
        "Schema/b.xsx": (b"<schema><node value='2'/></schema>", "assets1.big"),
        "Fonts/a.ttf": (font, "assets2.big"),
        "Fonts/b.otf": (font, "assets2.big"),
    }
    opaque_payloads = {
        ".cah": b"CAH\0\x80\xff",
        ".doc": bytes.fromhex("d0cf11e0a1b11ae1") + b"fixture",
        ".fxo": b"DXBC" + b"\0" * 28,
        ".pso": b"PSO\0\x80\xff",
        ".sec": b"SEC\0\x80\xff",
        ".vso": b"VSO\0\x80\xff",
        ".wak": b"WAK\0\x80\xff",
    }
    for index, extension in enumerate(sorted(opaque_payloads)):
        files[f"Opaque/item{index}{extension}"] = (
            opaque_payloads[extension],
            "assets3.big",
        )
    return files


class SupportCorpusTests(unittest.TestCase):
    def test_all_whitelisted_extensions_have_exactly_one_safe_disposition(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / "effective"
            output = root / "support"
            files = _base_files()
            _write_effective_assets(source, files)

            report = build_support_corpus(source, output)

            self.assertEqual(report.candidate_file_count, len(SUPPORT_CORPUS_EXTENSIONS))
            self.assertEqual(report.converted_file_count, 10)
            self.assertEqual(report.handoff_file_count, 7)
            self.assertEqual(
                {item.source_extension for item in report.entries},
                set(SUPPORT_CORPUS_EXTENSIONS) - set(OPAQUE_EXTENSIONS),
            )
            self.assertEqual(
                {item.source_extension for item in report.handoffs},
                set(OPAQUE_EXTENSIONS),
            )
            self.assertTrue(
                all(item.reason == "explicit-opaque-format" for item in report.handoffs)
            )
            handoffs_by_extension = {
                item.source_extension: item for item in report.handoffs
            }
            self.assertEqual(
                handoffs_by_extension[".doc"].evidence["signatureKind"],
                "ole-compound-file",
            )
            self.assertEqual(
                handoffs_by_extension[".fxo"].evidence["signatureKind"],
                "directx-bytecode-container",
            )
            document = json.loads((output / SUPPORT_CORPUS_MANIFEST).read_text())
            self.assertEqual(report.neutral(), document)
            self.assertTrue(
                all(
                    item["status"] == "runtime-converter-required"
                    and "outputPath" not in item
                    for item in document["handoffs"]
                )
            )
            totals = document["totals"]
            self.assertEqual(
                totals["candidateBytes"],
                totals["convertedBytes"] + totals["handoffBytes"],
            )
            self.assertEqual(
                totals["candidateFileCount"],
                totals["convertedFileCount"] + totals["handoffFileCount"],
            )

    def test_outputs_are_exact_copy_content_addressed_and_deduplicated(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / "effective"
            output = root / "support"
            shared = b"same strict utf8\n"
            font = _font_payload()
            files = {
                "Text/a.csv": (shared, "one.big"),
                "Text/b.dat": (shared, "two.big"),
                "Fonts/a.ttf": (font, "three.big"),
                "Fonts/b.otf": (font, "four.big"),
            }
            _write_effective_assets(source, files)

            report = build_support_corpus(source, output)

            self.assertEqual(len(report.entries), 4)
            self.assertEqual(len(report.outputs), 2)
            self.assertEqual(report.entries[0].output_path, report.entries[1].output_path)
            self.assertEqual(report.entries[2].output_path, report.entries[3].output_path)
            for entry in report.entries:
                target = output.joinpath(*entry.output_path.split("/"))
                self.assertEqual(target.read_bytes(), files[entry.source_path][0])
                self.assertIn(entry.source_sha256, entry.output_path)

    def test_strict_text_evidence_is_content_neutral_and_canonical(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / "effective"
            output = root / "support"
            payload = "\ufeffalpha\r\nbeta\n".encode("utf-8")
            _write_effective_assets(source, {"Text/a.csv": (payload, "one.big")})

            report = build_support_corpus(source, output)
            evidence = report.entries[0].evidence

            self.assertEqual(evidence["family"], "utf8-text")
            self.assertTrue(evidence["byteOrderMark"])
            self.assertEqual(evidence["lineFeeds"], 2)
            self.assertEqual(evidence["carriageReturns"], 1)
            self.assertNotIn("alpha", json.dumps(evidence))
            manifest = (output / SUPPORT_CORPUS_MANIFEST).read_bytes()
            parsed = json.loads(manifest)
            expected = (
                json.dumps(parsed, ensure_ascii=True, indent=2, sort_keys=True) + "\n"
            ).encode()
            self.assertEqual(manifest, expected)

    def test_binary_text_extension_is_handed_off_without_overclaim(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / "effective"
            output = root / "support"
            payload = b"BHAV\0\x80\xffsecret-name"
            _write_effective_assets(source, {"Data/a.bhav": (payload, "one.big")})

            report = build_support_corpus(source, output)

            self.assertEqual(len(report.entries), 0)
            self.assertEqual(len(report.outputs), 0)
            self.assertEqual(len(report.handoffs), 1)
            handoff = report.handoffs[0]
            self.assertEqual(handoff.reason, "invalid-utf8")
            self.assertEqual(handoff.evidence["sha256"], hashlib.sha256(payload).hexdigest())
            self.assertNotIn("secret-name", json.dumps(handoff.evidence))

    def test_nul_and_control_payloads_are_handoffs(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / "effective"
            output = root / "support"
            _write_effective_assets(
                source,
                {
                    "Data/a.dat": (b"alpha\0beta", "one.big"),
                    "Data/b.nvp": (b"alpha\x01beta", "one.big"),
                },
            )
            report = build_support_corpus(source, output)
            self.assertEqual(
                {item.reason for item in report.handoffs},
                {"forbidden-text-controls"},
            )

    def test_xml_is_parsed_without_dtd_entities_or_external_resolution(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / "effective"
            output = root / "support"
            safe = b"<schema><element/><element a='1'/></schema>"
            dtd = b"<!DOCTYPE x [<!ENTITY y SYSTEM 'file:///secret'>]><x>&y;</x>"
            _write_effective_assets(
                source,
                {
                    "Schema/good.xsd": (safe, "one.big"),
                    "Schema/bad.xsx": (dtd, "one.big"),
                },
            )

            report = build_support_corpus(source, output)

            self.assertEqual(len(report.entries), 1)
            self.assertEqual(report.entries[0].native_family, "xml-schema")
            self.assertFalse(report.entries[0].evidence["externalResolution"])
            self.assertEqual(report.entries[0].evidence["elementCount"], 3)
            self.assertEqual(report.handoffs[0].reason, "unsafe-or-invalid-xml")

    def test_font_tables_and_checksums_are_authoritatively_backtested(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / "effective"
            output = root / "support"
            font = _font_payload()
            _write_effective_assets(source, {"Fonts/a.ttf": (font, "one.big")})

            report = build_support_corpus(source, output)
            evidence = report.entries[0].evidence

            self.assertEqual(evidence["family"], "sfnt-font")
            self.assertEqual(evidence["glyphCount"], 2)
            self.assertTrue(evidence["checksumsValidated"])
            self.assertGreaterEqual(evidence["tableCount"], 7)
            self.assertNotIn("OpenBFME Test Fixture", json.dumps(evidence))

    def test_invalid_font_fails_instead_of_becoming_a_handoff(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / "effective"
            output = root / "support"
            _write_effective_assets(
                source, {"Fonts/private-name.ttf": (b"not a font", "one.big")}
            )

            with self.assertRaises(SupportCorpusBuildError) as raised:
                build_support_corpus(source, output)

            self.assertFalse(output.exists())
            self.assertIn("font-structure-invalid", str(raised.exception))
            self.assertNotIn("private-name", str(raised.exception))

    def test_missing_fonttools_fails_dependency_instead_of_faking_validation(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / "effective"
            output = root / "support"
            _write_effective_assets(
                source, {"Fonts/a.ttf": (_font_payload(), "one.big")}
            )
            with mock.patch.object(
                support_module,
                "_font_dependency",
                side_effect=SupportCorpusDependencyError("fontTools unavailable"),
            ):
                with self.assertRaises(SupportCorpusDependencyError):
                    build_support_corpus(source, output)
            self.assertFalse(output.exists())

    def test_fonttools_version_is_pinned(self) -> None:
        import fontTools

        with mock.patch.object(fontTools, "__version__", "99.0"):
            with self.assertRaisesRegex(
                SupportCorpusDependencyError,
                "fontTools 4.61.1 is required",
            ):
                support_module._font_dependency()

    def test_defusedxml_version_is_pinned(self) -> None:
        import defusedxml

        with mock.patch.object(defusedxml, "__version__", "99.0"):
            with self.assertRaisesRegex(
                SupportCorpusDependencyError,
                "defusedxml 0.7.1 is required",
            ):
                support_module._xml_dependency()

    def test_declared_hash_mutation_is_aggregated_and_redacted(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / "effective"
            output = root / "support"
            private_path = "Private/secret-file.csv"
            _write_effective_assets(
                source,
                {private_path: (b"valid utf8\n", "one.big")},
                declared_hashes={private_path: "0" * 64},
            )
            with self.assertRaises(SupportCorpusBuildError) as raised:
                build_support_corpus(source, output)
            self.assertIn("source-sha256-mismatch", str(raised.exception))
            self.assertNotIn("secret-file", str(raised.exception))
            self.assertFalse(output.exists())

    def test_revalidates_manifest_and_exact_tree_after_inspection(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / "effective"
            output = root / "support"
            _write_effective_assets(source, {"Text/a.csv": (b"a,b\n", "one.big")})
            original = support_module._inspect_sources

            def mutate(*args: object, **kwargs: object) -> object:
                result = original(*args, **kwargs)
                (source / "undeclared.bin").write_bytes(b"changed")
                return result

            with mock.patch.object(support_module, "_inspect_sources", side_effect=mutate):
                with self.assertRaisesRegex(SupportCorpusError, "undeclared"):
                    build_support_corpus(source, output)
            self.assertFalse(output.exists())

    def test_revalidates_manifest_and_selected_payload_identity_after_copy(self) -> None:
        for mode in ("manifest", "payload"):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                source = root / "effective"
                output = root / "support"
                relative = "Private/secret-file.csv"
                _write_effective_assets(
                    source, {relative: (b"first\n", "one.big")}
                )
                original = support_module._inspect_sources

                def mutate(*args: object, **kwargs: object) -> object:
                    result = original(*args, **kwargs)
                    if mode == "payload":
                        source.joinpath(*relative.split("/")).write_bytes(b"other\n")
                    else:
                        manifest_path = source / ".openbfme" / "manifest.json"
                        document = json.loads(manifest_path.read_text())
                        document["install"]["root"] = "another-valid-fixture-root"
                        _write_manifest(source, document)
                    return result

                with mock.patch.object(
                    support_module, "_inspect_sources", side_effect=mutate
                ):
                    with self.assertRaises(SupportCorpusError) as raised:
                        build_support_corpus(source, output)
                self.assertFalse(output.exists())
                if mode == "payload":
                    self.assertIn("source-sha256-mismatch", str(raised.exception))
                    self.assertNotIn("secret-file", str(raised.exception))
                else:
                    self.assertIn("manifest identity changed", str(raised.exception))

    def test_reuse_is_deterministic_and_tamper_detection_is_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / "effective"
            output = root / "support"
            _write_effective_assets(source, {"Text/a.csv": (b"a,b\n", "one.big")})

            first = build_support_corpus(source, output)
            snapshot = _tree_snapshot(output)
            second = build_support_corpus(source, output)

            self.assertFalse(first.reused)
            self.assertTrue(second.reused)
            self.assertEqual(first.identity_sha256, second.identity_sha256)
            self.assertEqual(snapshot, _tree_snapshot(output))
            object_path = output.joinpath(*first.outputs[0].path.split("/"))
            object_path.write_bytes(b"tampered")
            with self.assertRaises(SupportCorpusReuseError):
                build_support_corpus(source, output)

    def test_force_rebuild_replaces_tampered_output_only_after_verification(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / "effective"
            output = root / "support"
            payload = b"a,b\n"
            _write_effective_assets(source, {"Text/a.csv": (payload, "one.big")})
            first = build_support_corpus(source, output)
            target = output.joinpath(*first.outputs[0].path.split("/"))
            target.write_bytes(b"tampered")

            rebuilt = build_support_corpus(source, output, force=True)

            self.assertFalse(rebuilt.reused)
            self.assertEqual(target.read_bytes(), payload)
            self.assertEqual(rebuilt.identity_sha256, first.identity_sha256)

    def test_publish_failure_rolls_back_prior_verified_tree(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / "effective"
            output = root / "support"
            files = {"Text/a.csv": (b"first\n", "one.big")}
            _write_effective_assets(source, files)
            build_support_corpus(source, output)
            before = _tree_snapshot(output)

            real_replace = support_module.os.replace
            calls = 0

            def fail_stage(source_path: object, target_path: object) -> None:
                nonlocal calls
                calls += 1
                if calls == 2:
                    raise OSError("synthetic publish failure")
                real_replace(source_path, target_path)

            with mock.patch.object(support_module.os, "replace", side_effect=fail_stage):
                with self.assertRaisesRegex(SupportCorpusError, "prior output was preserved"):
                    build_support_corpus(source, output, force=True)

            self.assertEqual(_tree_snapshot(output), before)
            self.assertFalse(any("staging-" in item.name for item in root.iterdir()))
            self.assertFalse(any("backup-" in item.name for item in root.iterdir()))

    def test_manifest_and_tree_tamper_are_rejected_on_reuse(self) -> None:
        for mode in ("identity", "extra"):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                source = root / "effective"
                output = root / "support"
                _write_effective_assets(
                    source, {"Text/a.csv": (b"a,b\n", "one.big")}
                )
                build_support_corpus(source, output)
                if mode == "identity":
                    manifest_path = output / SUPPORT_CORPUS_MANIFEST
                    document = json.loads(manifest_path.read_text())
                    document["identitySha256"] = "0" * 64
                    manifest_path.write_text(
                        json.dumps(document, indent=2, sort_keys=True) + "\n",
                        encoding="utf-8",
                    )
                else:
                    (output / "extra.bin").write_bytes(b"extra")
                with self.assertRaises(SupportCorpusReuseError):
                    build_support_corpus(source, output)

    def test_limits_reject_before_output_publication(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / "effective"
            output = root / "support"
            _write_effective_assets(
                source,
                {
                    "Text/a.csv": (b"one", "one.big"),
                    "Text/b.dat": (b"two", "one.big"),
                },
            )
            with self.assertRaises(SupportCorpusLimitError):
                build_support_corpus(source, output, max_files=1)
            with self.assertRaises(SupportCorpusLimitError):
                build_support_corpus(source, output, max_total_bytes=5)
            self.assertFalse(output.exists())

    def test_unsupported_extensions_are_ignored_and_empty_selection_fails(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / "effective"
            output = root / "support"
            _write_effective_assets(
                source,
                {
                    "Text/a.csv": (b"a,b\n", "one.big"),
                    "Other/readme.txt": (b"ignored", "one.big"),
                },
            )
            report = build_support_corpus(source, output)
            self.assertEqual(report.candidate_file_count, 1)

        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / "effective"
            output = root / "support"
            _write_effective_assets(
                source, {"Other/readme.txt": (b"ignored", "one.big")}
            )
            with self.assertRaisesRegex(SupportCorpusError, "no whitelisted"):
                build_support_corpus(source, output)

    def test_effective_tree_rejects_extras_and_links(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / "effective"
            output = root / "support"
            _write_effective_assets(source, {"Text/a.csv": (b"a,b\n", "one.big")})
            (source / "extra.bin").write_bytes(b"extra")
            with self.assertRaisesRegex(SupportCorpusError, "undeclared"):
                build_support_corpus(source, output)

        if not hasattr(os, "symlink"):
            return
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / "effective"
            output = root / "support"
            target = root / "target.csv"
            target.write_bytes(b"a,b\n")
            files = {"Text/a.csv": (target.read_bytes(), "one.big")}
            _write_effective_assets(source, files)
            (source / "Text" / "a.csv").unlink()
            try:
                os.symlink(target, source / "Text" / "a.csv")
            except OSError:
                self.skipTest("symlink creation is unavailable")
            with self.assertRaisesRegex(SupportCorpusError, "link"):
                build_support_corpus(source, output)

    def test_source_and_output_roots_must_not_overlap(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / "effective"
            _write_effective_assets(source, {"Text/a.csv": (b"a,b\n", "one.big")})
            with self.assertRaisesRegex(SupportCorpusError, "must not overlap"):
                build_support_corpus(source, source / "output")

    def test_invalid_argument_types_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / "effective"
            output = root / "support"
            _write_effective_assets(source, {"Text/a.csv": (b"a,b\n", "one.big")})
            with self.assertRaises(TypeError):
                build_support_corpus(source, output, force=1)  # type: ignore[arg-type]
            with self.assertRaises(TypeError):
                build_support_corpus(source, output, max_files=True)
            with self.assertRaises(ValueError):
                build_support_corpus(source, output, max_total_bytes=0)


if __name__ == "__main__":
    unittest.main()
