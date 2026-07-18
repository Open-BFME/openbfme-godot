from __future__ import annotations

from contextlib import redirect_stdout
import hashlib
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from openbfme_importer.big import BigArchive
from openbfme_importer.catalog import CatalogEntry, InstallCatalog
from openbfme_importer.cli import main
from openbfme_importer.pipeline import ImportPipeline

from importer.tests.test_big import make_big


def _make_install(root: Path) -> InstallCatalog:
    install = root / "install"
    install.mkdir()
    make_big(
        install / "ini.big",
        {
            "Data/shared.bin": b"base",
            "data/base.txt": b"base-only",
        },
    )
    make_big(
        install / "_patch106.big",
        {
            "data/SHARED.bin": b"patch",
            "data/patch.txt": b"patch-only",
        },
    )
    return InstallCatalog.build(install)


class FullAssetExtractTests(unittest.TestCase):
    def test_plan_report_retains_catalog_identity(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            catalog = _make_install(root)
            profile = Mock(
                id="fixture-profile",
                source_sha256="a" * 64,
                pack_id="fixture-pack",
                pack_metadata={},
                runtime_data={},
                resources=(),
            )
            resolved = Mock(
                profile=profile,
                missing_required=(),
                resources=(),
                selected_entries=(),
            )
            report = ImportPipeline(catalog, root / "state").plan_report(resolved)
            self.assertEqual(
                report["catalog_identity_sha256"], catalog.identity_sha256()
            )

    def test_core_plan_extract_and_build_reject_m3_catalog_drift(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            catalog = _make_install(root)
            profile = Mock(
                id="men-fords-v1",
                source_sha256="a" * 64,
                pack_id="fixture-pack",
                pack_metadata={"sourceCatalogIdentitySha256": "0" * 64},
                runtime_data={"data/m3/ranger-runtime.json": {}},
                resources=(),
            )
            resolved = Mock(
                profile=profile,
                missing_required=(),
                resources=(),
                selected_entries=(),
            )
            pipeline = ImportPipeline(catalog, root / "state")
            for operation in (
                lambda: pipeline.plan_report(resolved),
                lambda: pipeline.extract_sources(resolved),
                lambda: pipeline.build(resolved),
            ):
                with self.subTest(operation=operation), self.assertRaisesRegex(
                    ValueError, "does not match the current catalog"
                ):
                    operation()

            profile.pack_metadata = {}
            with self.assertRaisesRegex(ValueError, "missing its source catalog identity"):
                pipeline.plan_report(resolved)

    def test_extracts_canonical_effective_tree_and_manifest(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            catalog = _make_install(root)
            report = ImportPipeline(catalog, root / "state").extract_all_assets()

            asset_root = Path(report["asset_root"])
            manifest_path = Path(report["manifest"])
            self.assertFalse(report["reused"])
            self.assertEqual((asset_root / "data" / "SHARED.bin").read_bytes(), b"patch")
            self.assertEqual((asset_root / "data" / "base.txt").read_bytes(), b"base-only")
            self.assertEqual((asset_root / "data" / "patch.txt").read_bytes(), b"patch-only")

            manifest_bytes = manifest_path.read_bytes()
            manifest = json.loads(manifest_bytes)
            self.assertEqual(manifest["schema"], "openbfme.effective-assets-manifest")
            self.assertEqual(manifest["schema_version"], 0)
            self.assertEqual(manifest["totals"], {"bytes": 24, "files": 3})
            self.assertEqual(
                [item["path"] for item in manifest["files"]],
                ["data/base.txt", "data/patch.txt", "data/SHARED.bin"],
            )
            shared = manifest["files"][2]
            self.assertEqual(shared["archive"], "_patch106.big")
            self.assertEqual(shared["size"], 5)
            self.assertGreater(shared["offset"], 15)
            self.assertEqual(shared["sha256"], hashlib.sha256(b"patch").hexdigest())
            self.assertEqual(len(manifest["catalog"]["identity_sha256"]), 64)
            self.assertEqual(len(manifest["install"]["identity_sha256"]), 64)
            self.assertEqual(
                manifest_bytes,
                (
                    json.dumps(
                        manifest, indent=2, sort_keys=True, ensure_ascii=False
                    )
                    + "\n"
                ).encode("utf-8"),
            )
            self.assertEqual(report["manifest_sha256"], hashlib.sha256(manifest_bytes).hexdigest())

            aggregate = hashlib.sha256()
            for item in manifest["files"]:
                aggregate.update(item["path"].encode("utf-8"))
                aggregate.update(b"\0")
                aggregate.update(str(item["size"]).encode("ascii"))
                aggregate.update(b"\0")
                aggregate.update(item["sha256"].encode("ascii"))
                aggregate.update(b"\n")
            self.assertEqual(manifest["aggregate_sha256"], aggregate.hexdigest())

    def test_repeat_is_verified_noop_and_force_is_transactional(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            catalog = _make_install(root)
            pipeline = ImportPipeline(catalog, root / "state")
            first = pipeline.extract_all_assets()
            shared = Path(first["asset_root"]) / "data" / "SHARED.bin"
            fixed_ns = 1_700_000_000_000_000_000
            os.utime(shared, ns=(fixed_ns, fixed_ns))

            second = pipeline.extract_all_assets()
            self.assertTrue(second["reused"])
            self.assertEqual(shared.stat().st_mtime_ns, fixed_ns)

            with patch.object(BigArchive, "extract", side_effect=RuntimeError("synthetic failure")):
                with self.assertRaisesRegex(RuntimeError, "synthetic failure"):
                    pipeline.extract_all_assets(force=True)
            self.assertEqual(shared.read_bytes(), b"patch")
            self.assertFalse(Path(first["asset_root"] + ".building").exists())
            self.assertFalse(Path(first["asset_root"] + ".previous").exists())

            shared.write_bytes(b"wrong")
            with self.assertRaisesRegex(RuntimeError, "use --force"):
                pipeline.extract_all_assets()
            rebuilt = pipeline.extract_all_assets(force=True)
            self.assertFalse(rebuilt["reused"])
            self.assertEqual(shared.read_bytes(), b"patch")

    def test_noop_verifies_cached_bytes_against_archive_payload(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            catalog = _make_install(root)
            pipeline = ImportPipeline(catalog, root / "state")
            pipeline.extract_all_assets()

            archive_path = catalog.install_root / "_patch106.big"
            original_stat = archive_path.stat()
            payload = bytearray(archive_path.read_bytes())
            payload[-1] = ord("X")
            archive_path.write_bytes(payload)
            os.utime(
                archive_path,
                ns=(original_stat.st_atime_ns, original_stat.st_mtime_ns),
            )
            self.assertEqual(catalog.stale_reasons(), [])
            with self.assertRaisesRegex(RuntimeError, "use --force"):
                pipeline.extract_all_assets()

    def test_rejects_stale_unsafe_and_over_limit_catalogs_before_writing(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            catalog = _make_install(root)
            state = root / "state"
            make_big(catalog.install_root / "new.big", {"data/new.txt": b"new"})
            with self.assertRaisesRegex(RuntimeError, "catalog is stale"):
                ImportPipeline(catalog, state).extract_all_assets()
            self.assertFalse((state / "cache" / "effective-assets").exists())

        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            catalog = _make_install(root)
            entry = catalog.entries[0]
            unsafe = InstallCatalog(
                catalog.install_root,
                catalog.archives,
                (
                    CatalogEntry(
                        entry.archive,
                        "../escape.bin",
                        entry.offset,
                        entry.size,
                        entry.precedence,
                    ),
                ),
            )
            state = root / "state"
            with self.assertRaisesRegex(ValueError, "relative path traversal"):
                ImportPipeline(unsafe, state).extract_all_assets()
            self.assertFalse((state / "cache" / "effective-assets").exists())

        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            catalog = _make_install(root)
            state = root / "state"
            with self.assertRaisesRegex(RuntimeError, "limit is 2"):
                ImportPipeline(catalog, state).extract_all_assets(max_files=2)
            self.assertFalse((state / "cache" / "effective-assets").exists())

    def test_rejects_file_directory_collision_before_writing(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            install = root / "install"
            install.mkdir()
            make_big(
                install / "ini.big",
                {"data/conflict": b"file", "data/conflict/child.bin": b"child"},
            )
            state = root / "state"
            with self.assertRaisesRegex(RuntimeError, "file/directory output collision"):
                ImportPipeline(InstallCatalog.build(install), state).extract_all_assets()
            self.assertFalse((state / "cache" / "effective-assets").exists())

    def test_cli_exposes_extract_all_assets(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            catalog = _make_install(root)
            state = root / "state"
            stdout = io.StringIO()
            with patch(
                "openbfme_importer.cli.ArchivePolicy.load", return_value=None
            ), redirect_stdout(stdout):
                result = main(
                    [
                        "--json",
                        "--state-root",
                        str(state),
                        "extract-all-assets",
                        "--install",
                        str(catalog.install_root),
                    ]
                )
            self.assertEqual(result, 0)
            report = json.loads(stdout.getvalue())
            self.assertTrue(report["ready"])
            self.assertEqual(report["file_count"], 3)
            self.assertTrue((Path(report["asset_root"]) / "data" / "SHARED.bin").is_file())


if __name__ == "__main__":
    unittest.main()
