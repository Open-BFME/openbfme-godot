from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest import mock

from openbfme_importer import edition_overlay, native_corpus
from openbfme_importer.edition_overlay import (
    EditionOverlayError,
    EditionOverlayLimitError,
    EditionOverlayReuseError,
    build_edition_overlay,
)


def _sha256(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def _canonical(value: object) -> bytes:
    return (
        json.dumps(value, indent=2, sort_keys=True, ensure_ascii=False) + "\n"
    ).encode("utf-8")


def _aggregate(rows: list[dict[str, object]]) -> str:
    digest = hashlib.sha256()
    for row in rows:
        digest.update(str(row["path"]).encode("utf-8"))
        digest.update(b"\0")
        digest.update(str(row["size"]).encode("ascii"))
        digest.update(b"\0")
        digest.update(str(row["sha256"]).encode("ascii"))
        digest.update(b"\n")
    return digest.hexdigest()


def _document(
    rows: list[dict[str, object]],
    *,
    identity_tag: str,
    catalog_format: int = 1,
) -> dict[str, object]:
    return {
        "schema": "openbfme.effective-assets-manifest",
        "schema_version": 0,
        "catalog": {
            "archive_count": 1,
            "entry_count": len(rows),
            "format": catalog_format,
            "identity_sha256": _sha256(f"catalog:{identity_tag}".encode()),
        },
        "install": {
            "identity_sha256": _sha256(f"install:{identity_tag}".encode()),
            "root": "synthetic-fixture-install",
        },
        "totals": {
            "bytes": sum(int(row["size"]) for row in rows),
            "files": len(rows),
        },
        "aggregate_sha256": _aggregate(rows),
        "files": rows,
    }


def _write_document(root: Path, document: dict[str, object]) -> None:
    metadata = root / ".openbfme"
    metadata.mkdir(parents=True, exist_ok=True)
    (metadata / "manifest.json").write_bytes(_canonical(document))


def _write_tree(
    root: Path,
    payloads: dict[str, bytes],
    *,
    identity_tag: str,
    catalog_format: int = 1,
) -> dict[str, object]:
    root.mkdir(parents=True)
    rows: list[dict[str, object]] = []
    for index, relative in enumerate(
        sorted(payloads, key=lambda value: (value.casefold(), value))
    ):
        payload = payloads[relative]
        target = root.joinpath(*relative.split("/"))
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(payload)
        rows.append(
            {
                "archive": f"archives/{identity_tag}.big",
                "offset": index * 4096,
                "path": relative,
                "precedence": index,
                "sha256": _sha256(payload),
                "size": len(payload),
            }
        )
    document = _document(
        rows,
        identity_tag=identity_tag,
        catalog_format=catalog_format,
    )
    _write_document(root, document)
    return document


def _manifest(root: Path) -> dict[str, object]:
    return json.loads((root / ".openbfme" / "manifest.json").read_text("utf-8"))


class EditionOverlayTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.base = self.root / "base"
        self.expansion = self.root / "expansion"
        self.output = self.root / "combined"
        self.base_payloads = {
            "data/base.bin": b"base-only",
            "data/changed.bin": b"old-content",
            "data/same.bin": b"shared-content",
        }
        self.expansion_payloads = {
            "data/changed.bin": b"new-content-is-longer",
            "data/expansion.bin": b"expansion-only",
            "data/same.bin": b"shared-content",
        }
        _write_tree(self.base, self.base_payloads, identity_tag="base")
        _write_tree(self.expansion, self.expansion_payloads, identity_tag="expansion")

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_expansion_wins_with_bound_identity_and_established_manifest_schema(
        self,
    ) -> None:
        report = build_edition_overlay(self.base, self.expansion, self.output)

        self.assertFalse(report.reused)
        self.assertEqual(report.file_count, 4)
        self.assertEqual(
            report.total_bytes,
            len(self.base_payloads["data/base.bin"])
            + len(self.expansion_payloads["data/changed.bin"])
            + len(self.expansion_payloads["data/expansion.bin"])
            + len(self.expansion_payloads["data/same.bin"]),
        )
        self.assertEqual(report.stats.base_only_files, 1)
        self.assertEqual(report.stats.expansion_only_files, 1)
        self.assertEqual(report.stats.overlap_identical_files, 1)
        self.assertEqual(report.stats.overlap_overridden_files, 1)
        self.assertEqual(report.stats.overlap_files, 2)
        self.assertEqual(report.stats.output_files, 4)
        self.assertEqual(
            report.stats.shadowed_base_bytes,
            len(self.base_payloads["data/changed.bin"])
            + len(self.base_payloads["data/same.bin"]),
        )
        self.assertEqual(
            (self.output / "data" / "changed.bin").read_bytes(),
            self.expansion_payloads["data/changed.bin"],
        )
        self.assertEqual(
            (self.output / "data" / "same.bin").read_bytes(),
            self.expansion_payloads["data/same.bin"],
        )

        manifest = _manifest(self.output)
        self.assertEqual(
            set(manifest),
            {
                "aggregate_sha256",
                "catalog",
                "files",
                "install",
                "schema",
                "schema_version",
                "totals",
            },
        )
        self.assertEqual(manifest["schema"], "openbfme.effective-assets-manifest")
        self.assertEqual(manifest["schema_version"], 0)
        self.assertEqual(
            set(manifest["files"][0]),
            {"archive", "offset", "path", "precedence", "sha256", "size"},
        )
        by_path = {row["path"]: row for row in manifest["files"]}
        self.assertTrue(by_path["data/base.bin"]["archive"].startswith("layer-base/"))
        self.assertTrue(
            by_path["data/changed.bin"]["archive"].startswith("layer-expansion/")
        )
        self.assertEqual(manifest["catalog"]["identity_sha256"], report.identity_sha256)
        self.assertEqual(manifest["aggregate_sha256"], report.aggregate_sha256)
        self.assertEqual(report.manifest_path.read_bytes(), _canonical(manifest))
        neutral = report.neutral()
        self.assertEqual(neutral["policy"], "expansion-wins")
        self.assertEqual(neutral["stats"], report.stats.neutral())
        downstream_manifest = native_corpus._load_effective_manifest(self.output)
        downstream_tree = native_corpus._validate_effective_tree(
            self.output, downstream_manifest
        )
        self.assertEqual(downstream_manifest.aggregate_sha256, report.aggregate_sha256)
        self.assertEqual(len(downstream_tree), report.file_count + 1)

    def test_output_payloads_are_independent_regular_files_not_hardlinks(self) -> None:
        build_edition_overlay(self.base, self.expansion, self.output)
        source_by_output = {
            "data/base.bin": self.base / "data" / "base.bin",
            "data/changed.bin": self.expansion / "data" / "changed.bin",
            "data/expansion.bin": self.expansion / "data" / "expansion.bin",
            "data/same.bin": self.expansion / "data" / "same.bin",
        }
        for relative, source in source_by_output.items():
            target = self.output.joinpath(*relative.split("/"))
            self.assertTrue(target.is_file())
            self.assertFalse(target.is_symlink())
            self.assertEqual(target.stat().st_nlink, 1)
            self.assertFalse(os.path.samefile(source, target))

    def test_noop_reuse_fully_verifies_and_does_not_rewrite_output(self) -> None:
        first = build_edition_overlay(self.base, self.expansion, self.output)
        before = {
            path.relative_to(self.output).as_posix(): (
                path.read_bytes(),
                path.stat().st_mtime_ns,
            )
            for path in self.output.rglob("*")
            if path.is_file()
        }

        second = build_edition_overlay(self.base, self.expansion, self.output)

        after = {
            path.relative_to(self.output).as_posix(): (
                path.read_bytes(),
                path.stat().st_mtime_ns,
            )
            for path in self.output.rglob("*")
            if path.is_file()
        }
        self.assertTrue(second.reused)
        self.assertEqual(second.identity_sha256, first.identity_sha256)
        self.assertEqual(second.manifest_sha256, first.manifest_sha256)
        self.assertEqual(after, before)

    def test_manifest_is_deterministic_across_output_locations(self) -> None:
        second_output = self.root / "combined-two"
        first = build_edition_overlay(self.base, self.expansion, self.output)
        second = build_edition_overlay(self.base, self.expansion, second_output)
        self.assertEqual(first.identity_sha256, second.identity_sha256)
        self.assertEqual(first.manifest_sha256, second.manifest_sha256)
        self.assertEqual(
            first.manifest_path.read_bytes(), second.manifest_path.read_bytes()
        )

    def test_source_manifest_seals_change_identity_without_changing_payload_aggregate(
        self,
    ) -> None:
        first = build_edition_overlay(self.base, self.expansion, self.output)
        document = _manifest(self.base)
        document["catalog"]["identity_sha256"] = _sha256(b"replacement-catalog")
        _write_document(self.base, document)

        with self.assertRaisesRegex(EditionOverlayReuseError, "identity"):
            build_edition_overlay(self.base, self.expansion, self.output)
        second = build_edition_overlay(
            self.base,
            self.expansion,
            self.output,
            force=True,
        )
        self.assertEqual(second.aggregate_sha256, first.aggregate_sha256)
        self.assertNotEqual(second.identity_sha256, first.identity_sha256)
        self.assertNotEqual(second.manifest_sha256, first.manifest_sha256)

    def test_source_payload_tamper_and_extraneous_entries_fail_closed(self) -> None:
        (self.base / "data" / "base.bin").write_bytes(b"tampered!!")
        with self.assertRaisesRegex(EditionOverlayError, "size|SHA-256"):
            build_edition_overlay(self.base, self.expansion, self.output)

        (self.base / "data" / "base.bin").write_bytes(
            self.base_payloads["data/base.bin"]
        )
        (self.base / "undeclared.bin").write_bytes(b"extra")
        with self.assertRaisesRegex(EditionOverlayError, "undeclared"):
            build_edition_overlay(self.base, self.expansion, self.output)

    def test_source_missing_file_and_stale_aggregate_fail_closed(self) -> None:
        (self.expansion / "data" / "expansion.bin").unlink()
        with self.assertRaisesRegex(EditionOverlayError, "missing declared"):
            build_edition_overlay(self.base, self.expansion, self.output)

        (self.expansion / "data" / "expansion.bin").write_bytes(
            self.expansion_payloads["data/expansion.bin"]
        )
        document = _manifest(self.expansion)
        document["aggregate_sha256"] = "0" * 64
        _write_document(self.expansion, document)
        with self.assertRaisesRegex(EditionOverlayError, "aggregate"):
            build_edition_overlay(self.base, self.expansion, self.output)

    def test_source_hardlink_is_rejected(self) -> None:
        peer = self.root / "base-hardlink-peer.bin"
        try:
            os.link(self.base / "data" / "base.bin", peer)
        except OSError as exc:
            self.skipTest(f"hardlinks unavailable: {exc}")
        with self.assertRaisesRegex(EditionOverlayError, "hard-linked"):
            build_edition_overlay(self.base, self.expansion, self.output)

    def test_source_symlink_is_rejected(self) -> None:
        source = self.base / "data" / "base.bin"
        source.unlink()
        try:
            source.symlink_to(self.root / "symlink-target.bin")
        except OSError as exc:
            self.skipTest(f"symlinks unavailable: {exc}")
        (self.root / "symlink-target.bin").write_bytes(
            self.base_payloads["data/base.bin"]
        )
        with self.assertRaisesRegex(EditionOverlayError, "link"):
            build_edition_overlay(self.base, self.expansion, self.output)

    def test_manifest_path_escape_and_case_collision_are_rejected(self) -> None:
        escape_root = self.root / "escape"
        escape_root.mkdir()
        escape_rows = [
            {
                "archive": "archives/escape.big",
                "offset": 0,
                "path": "../escaped.bin",
                "precedence": 0,
                "sha256": _sha256(b"escape"),
                "size": 6,
            }
        ]
        _write_document(
            escape_root,
            _document(escape_rows, identity_tag="escape"),
        )
        with self.assertRaisesRegex(EditionOverlayError, "unsafe path"):
            build_edition_overlay(escape_root, self.expansion, self.output)

        collision_root = self.root / "collision"
        collision_root.mkdir()
        collision_rows = [
            {
                "archive": "archives/collision.big",
                "offset": index,
                "path": path,
                "precedence": index,
                "sha256": _sha256(path.encode()),
                "size": len(path.encode()),
            }
            for index, path in enumerate(("Data/A.bin", "data/a.bin"))
        ]
        _write_document(
            collision_root,
            _document(collision_rows, identity_tag="collision"),
        )
        with self.assertRaisesRegex(EditionOverlayError, "case-colliding"):
            build_edition_overlay(collision_root, self.expansion, self.output)

    def test_cross_layer_case_variant_is_shadowed_but_file_directory_collision_rejects(
        self,
    ) -> None:
        case_base = self.root / "case-base"
        case_expansion = self.root / "case-expansion"
        _write_tree(
            case_base,
            {"Data/Shared.bin": b"base"},
            identity_tag="case-base",
        )
        _write_tree(
            case_expansion,
            {"data/shared.bin": b"expansion"},
            identity_tag="case-expansion",
        )
        case_output = self.root / "case-output"
        report = build_edition_overlay(case_base, case_expansion, case_output)
        self.assertEqual(report.file_count, 1)
        self.assertEqual(report.stats.overlap_overridden_files, 1)
        self.assertEqual(
            (case_output / "data" / "shared.bin").read_bytes(), b"expansion"
        )
        self.assertEqual(_manifest(case_output)["files"][0]["path"], "data/shared.bin")
        self.assertEqual(
            [
                path.relative_to(case_output).as_posix()
                for path in case_output.rglob("*.bin")
            ],
            ["data/shared.bin"],
        )

        file_base = self.root / "file-base"
        directory_expansion = self.root / "directory-expansion"
        _write_tree(file_base, {"data": b"file"}, identity_tag="file-base")
        _write_tree(
            directory_expansion,
            {"data/child.bin": b"child"},
            identity_tag="directory-expansion",
        )
        with self.assertRaisesRegex(EditionOverlayError, "file/directory"):
            build_edition_overlay(
                file_base,
                directory_expansion,
                self.root / "directory-output",
            )

    def test_configurable_file_and_byte_limits_reject_before_copy(self) -> None:
        with self.assertRaisesRegex(EditionOverlayLimitError, "files; limit"):
            build_edition_overlay(
                self.base,
                self.expansion,
                self.output,
                max_files=3,
            )
        self.assertFalse(self.output.exists())

        with self.assertRaisesRegex(EditionOverlayLimitError, "bytes; limit"):
            build_edition_overlay(
                self.base,
                self.expansion,
                self.output,
                max_total_bytes=1,
            )
        self.assertFalse(self.output.exists())
        with self.assertRaises(ValueError):
            build_edition_overlay(
                self.base,
                self.expansion,
                self.output,
                max_files=edition_overlay.MAX_OVERLAY_FILES + 1,
            )

    def test_output_tamper_extra_file_and_hardlink_are_rejected_on_reuse(self) -> None:
        build_edition_overlay(self.base, self.expansion, self.output)
        changed = self.output / "data" / "changed.bin"
        changed.write_bytes(b"x" * changed.stat().st_size)
        with self.assertRaisesRegex(EditionOverlayReuseError, "SHA-256"):
            build_edition_overlay(self.base, self.expansion, self.output)

        repaired = build_edition_overlay(
            self.base,
            self.expansion,
            self.output,
            force=True,
        )
        self.assertFalse(repaired.reused)
        (self.output / "extra.bin").write_bytes(b"extra")
        with self.assertRaisesRegex(EditionOverlayReuseError, "undeclared"):
            build_edition_overlay(self.base, self.expansion, self.output)

        build_edition_overlay(
            self.base,
            self.expansion,
            self.output,
            force=True,
        )
        peer = self.root / "output-hardlink-peer.bin"
        try:
            os.link(self.output / "data" / "base.bin", peer)
        except OSError as exc:
            self.skipTest(f"hardlinks unavailable: {exc}")
        with self.assertRaisesRegex(EditionOverlayReuseError, "hard-linked"):
            build_edition_overlay(self.base, self.expansion, self.output)

    def test_force_publish_failure_rolls_back_and_removes_staging(self) -> None:
        first = build_edition_overlay(self.base, self.expansion, self.output)
        before = {
            path.relative_to(self.output).as_posix(): path.read_bytes()
            for path in self.output.rglob("*")
            if path.is_file()
        }
        real_replace = os.replace
        injected = False

        def fail_stage_publish(
            source: os.PathLike[str], target: os.PathLike[str]
        ) -> None:
            nonlocal injected
            source_path = Path(source)
            target_path = Path(target)
            if (
                not injected
                and source_path.name.startswith(f".{self.output.name}.staging-")
                and target_path == self.output
            ):
                injected = True
                raise OSError("injected publish failure")
            real_replace(source, target)

        with mock.patch.object(
            edition_overlay.os,
            "replace",
            side_effect=fail_stage_publish,
        ):
            with self.assertRaisesRegex(
                EditionOverlayError, "prior output was preserved"
            ):
                build_edition_overlay(
                    self.base,
                    self.expansion,
                    self.output,
                    force=True,
                )

        after = {
            path.relative_to(self.output).as_posix(): path.read_bytes()
            for path in self.output.rglob("*")
            if path.is_file()
        }
        self.assertTrue(injected)
        self.assertEqual(after, before)
        self.assertEqual(
            _sha256((self.output / ".openbfme" / "manifest.json").read_bytes()),
            first.manifest_sha256,
        )
        self.assertEqual(
            list(self.root.glob(f".{self.output.name}.staging-*")),
            [],
        )
        self.assertEqual(
            list(self.root.glob(f".{self.output.name}.backup-*")),
            [],
        )

    def test_post_publish_backtest_failure_restores_prior_output(self) -> None:
        first = build_edition_overlay(self.base, self.expansion, self.output)
        before = {
            path.relative_to(self.output).as_posix(): path.read_bytes()
            for path in self.output.rglob("*")
            if path.is_file()
        }
        source_manifest = _manifest(self.base)
        source_manifest["install"]["identity_sha256"] = _sha256(b"new-install-seal")
        _write_document(self.base, source_manifest)
        original_verify = edition_overlay._verify_output
        injected = False

        def fail_published_backtest(
            root: Path, *args: object, **kwargs: object
        ) -> object:
            nonlocal injected
            report = original_verify(root, *args, **kwargs)
            if not injected and Path(root) == self.output:
                injected = True
                raise EditionOverlayError("injected published backtest failure")
            return report

        with mock.patch.object(
            edition_overlay,
            "_verify_output",
            side_effect=fail_published_backtest,
        ):
            with self.assertRaisesRegex(
                EditionOverlayError, "prior output was preserved"
            ):
                build_edition_overlay(
                    self.base,
                    self.expansion,
                    self.output,
                    force=True,
                )

        after = {
            path.relative_to(self.output).as_posix(): path.read_bytes()
            for path in self.output.rglob("*")
            if path.is_file()
        }
        self.assertTrue(injected)
        self.assertEqual(after, before)
        self.assertEqual(
            _sha256((self.output / ".openbfme" / "manifest.json").read_bytes()),
            first.manifest_sha256,
        )
        self.assertEqual(
            list(self.root.glob(f".{self.output.name}.staging-*")),
            [],
        )
        self.assertEqual(
            list(self.root.glob(f".{self.output.name}.backup-*")),
            [],
        )

    def test_source_mutation_after_hash_is_detected_before_publish(self) -> None:
        original = edition_overlay._copy_or_hash_file
        mutated = False

        def mutate_after_hash(
            actual: object,
            expected: object,
            *,
            target: Path | None,
            label: str,
        ) -> None:
            nonlocal mutated
            original(actual, expected, target=target, label=label)
            if not mutated and target is None and label.startswith("base "):
                actual.path.write_bytes(actual.path.read_bytes())
                mutated = True

        with mock.patch.object(
            edition_overlay,
            "_copy_or_hash_file",
            side_effect=mutate_after_hash,
        ):
            with self.assertRaisesRegex(EditionOverlayError, "changed during"):
                build_edition_overlay(self.base, self.expansion, self.output)
        self.assertTrue(mutated)
        self.assertFalse(self.output.exists())

    def test_roots_and_output_must_not_overlap_without_creating_paths(self) -> None:
        nested = self.base / "not-created" / "combined"
        with self.assertRaisesRegex(EditionOverlayError, "must not overlap"):
            build_edition_overlay(self.base, self.expansion, nested)
        self.assertFalse((self.base / "not-created").exists())

        with self.assertRaisesRegex(EditionOverlayError, "must not overlap"):
            build_edition_overlay(self.base, self.base, self.output)

    def test_different_source_catalog_formats_are_rejected(self) -> None:
        other = self.root / "other-format"
        _write_tree(
            other,
            {"data/other.bin": b"other"},
            identity_tag="other-format",
            catalog_format=2,
        )
        with self.assertRaisesRegex(EditionOverlayError, "formats differ"):
            build_edition_overlay(self.base, other, self.output)


if __name__ == "__main__":
    unittest.main()
