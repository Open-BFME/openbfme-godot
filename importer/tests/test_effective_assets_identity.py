"""A sealed effective-assets tree must name its own install, and refuse others.

Every edition extracts to a directory literally called ``effective-assets``,
holding the same virtual paths with different bytes.  Without an identity check
a lane can read the BFME2 tree, label the number RotWK, and nothing anywhere
disagrees.  These tests pin the refusal.
"""

from __future__ import annotations

from contextlib import redirect_stderr, redirect_stdout
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from openbfme_importer.catalog import InstallCatalog
from openbfme_importer.cli import main
from openbfme_importer.effective_assets_identity import (
    EffectiveAssetsIdentityError,
    describe_effective_assets_trees,
    effective_assets_identity,
    verify_effective_assets,
)
from openbfme_importer.pipeline import ImportPipeline

from importer.tests.test_big import make_big


def _bfme2_tree(root: Path) -> Path:
    """A BFME2-shaped install extracted to its own effective-assets tree."""

    install = root / "bfme2-install"
    install.mkdir()
    make_big(install / "ini.big", {"data/base.txt": b"base-only"})
    make_big(
        install / "_patch106.big",
        {"libraries/shared.map": b"bfme2-bytes"},
    )
    catalog = InstallCatalog.build(install)
    report = ImportPipeline(catalog, root / "bfme2-state").extract_all_assets()
    return Path(report["asset_root"])


def _rotwk_tree(root: Path) -> Path:
    """A RotWK-shaped layered install: 2.01 over the BFME2 layer beneath it."""

    install = root / "rotwk-install"
    (install / "layer-0-rotwk").mkdir(parents=True)
    (install / "layer-1-bfme2").mkdir(parents=True)
    make_big(
        install / "layer-0-rotwk" / "_patch201.big",
        {"libraries/shared.map": b"rotwk-bytes-which-are-longer"},
    )
    make_big(
        install / "layer-1-bfme2" / "_patch106.big",
        {"libraries/shared.map": b"bfme2-bytes", "data/base.txt": b"base-only"},
    )
    catalog = InstallCatalog.build(install)
    report = ImportPipeline(catalog, root / "rotwk-state").extract_all_assets()
    return Path(report["asset_root"])


class EffectiveAssetsIdentityTests(unittest.TestCase):
    def test_the_two_trees_are_confusable_by_path_and_distinct_by_bytes(self) -> None:
        """The premise: same directory name, same virtual path, different bytes."""

        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            bfme2 = _bfme2_tree(root)
            rotwk = _rotwk_tree(root)

            self.assertEqual(bfme2.name, "effective-assets")
            self.assertEqual(rotwk.name, "effective-assets")
            shared = Path("libraries") / "shared.map"
            self.assertTrue((bfme2 / shared).is_file())
            self.assertTrue((rotwk / shared).is_file())
            self.assertNotEqual(
                (bfme2 / shared).read_bytes(), (rotwk / shared).read_bytes()
            )
            # RotWK wins its own layer: lowest precedence, not insertion order.
            self.assertEqual(
                (rotwk / shared).read_bytes(), b"rotwk-bytes-which-are-longer"
            )

    def test_identity_names_the_edition_each_tree_holds(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            self.assertEqual(effective_assets_identity(_bfme2_tree(root))["edition"], "bfme2")
            self.assertEqual(effective_assets_identity(_rotwk_tree(root))["edition"], "rotwk")

    def test_expansion_evidence_outranks_the_base_layer_beneath_it(self) -> None:
        """A RotWK tree contains BFME2 archives; it is still not a BFME2 tree."""

        with tempfile.TemporaryDirectory() as raw:
            identity = effective_assets_identity(_rotwk_tree(Path(raw)))
            self.assertEqual(identity["evidenced_games"], ["bfme2", "rotwk"])
            self.assertEqual(identity["edition"], "rotwk")

    def test_an_unpatched_tree_stays_unidentified_instead_of_being_labelled(self) -> None:
        """No patch archive is no evidence. Absence must not become a default."""

        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            install = root / "install"
            install.mkdir()
            make_big(install / "ini.big", {"libraries/shared.map": b"unlabelled"})
            catalog = InstallCatalog.build(install)
            tree = Path(
                ImportPipeline(catalog, root / "state").extract_all_assets()["asset_root"]
            )
            identity = effective_assets_identity(tree)
            self.assertEqual(identity["patch_archives"], [])
            self.assertEqual(identity["evidenced_games"], [])
            self.assertIsNone(identity["edition"])
            # Unidentified is not the same as refused: an explicitly requested
            # edition is still admitted, because nothing contradicts it.
            for game in ("bfme2", "rotwk"):
                with self.subTest(game=game):
                    self.assertTrue(verify_effective_assets(tree, game=game)["trusted"])

    def test_reading_the_bfme2_tree_as_rotwk_is_refused(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            tree = _bfme2_tree(root)
            self.assertTrue(verify_effective_assets(tree, game="bfme2")["trusted"])
            with self.assertRaises(EffectiveAssetsIdentityError) as caught:
                verify_effective_assets(tree, game="rotwk")
            self.assertEqual(
                caught.exception.diagnostic["error"], "effective-assets-game-mismatch"
            )
            self.assertIn("wrong-tree measurement", str(caught.exception))

    def test_reading_the_rotwk_tree_as_bfme2_is_refused(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            tree = _rotwk_tree(root)
            self.assertTrue(verify_effective_assets(tree, game="rotwk")["trusted"])
            with self.assertRaises(EffectiveAssetsIdentityError) as caught:
                verify_effective_assets(tree, game="bfme2")
            self.assertEqual(
                caught.exception.diagnostic["error"], "effective-assets-game-mismatch"
            )
            self.assertEqual(caught.exception.diagnostic["tree_edition"], "rotwk")

    def test_a_cache_from_an_older_install_is_refused_as_stale(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            install = root / "install"
            install.mkdir()
            make_big(install / "_patch106.big", {"data/base.txt": b"before"})
            catalog = InstallCatalog.build(install)
            tree = Path(
                ImportPipeline(catalog, root / "state").extract_all_assets()["asset_root"]
            )
            self.assertTrue(verify_effective_assets(tree, catalog=catalog)["trusted"])

            # The install moves on; the cache does not.
            make_big(
                install / "_patch106.big",
                {"data/base.txt": b"after", "data/added.txt": b"new in this patch"},
            )
            moved_on = InstallCatalog.build(install)
            self.assertNotEqual(moved_on.identity_sha256(), catalog.identity_sha256())

            with self.assertRaises(EffectiveAssetsIdentityError) as caught:
                verify_effective_assets(tree, catalog=moved_on)
            diagnostic = caught.exception.diagnostic
            self.assertEqual(diagnostic["error"], "effective-assets-stale")
            self.assertEqual(
                diagnostic["cache_catalog_identity_sha256"], catalog.identity_sha256()
            )
            self.assertEqual(
                diagnostic["live_catalog_identity_sha256"], moved_on.identity_sha256()
            )
            self.assertIn("extract-all-assets --force", str(caught.exception))

    def test_edited_bytes_are_caught_by_size_and_by_hash(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            tree = _bfme2_tree(root)
            self.assertTrue(verify_effective_assets(tree, verify="hashes")["trusted"])

            target = tree / "libraries" / "shared.map"
            original = target.read_bytes()
            target.write_bytes(original + b"!")
            with self.assertRaises(EffectiveAssetsIdentityError) as caught:
                verify_effective_assets(tree, verify="sizes")
            self.assertEqual(
                caught.exception.diagnostic["error"], "effective-assets-bytes-mismatch"
            )

            # Same length, different bytes: only a hash pass can see this.
            target.write_bytes(b"X" * len(original))
            verify_effective_assets(tree, verify="sizes")
            with self.assertRaises(EffectiveAssetsIdentityError) as caught:
                verify_effective_assets(tree, verify="hashes")
            self.assertEqual(
                caught.exception.diagnostic["verification"]["hash_mismatch"], 1
            )

    def test_a_tree_without_a_manifest_is_refused_rather_than_guessed(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            bare = root / "effective-assets"
            (bare / "libraries").mkdir(parents=True)
            (bare / "libraries" / "shared.map").write_bytes(b"who knows")
            with self.assertRaises(EffectiveAssetsIdentityError) as caught:
                effective_assets_identity(bare)
            self.assertEqual(
                caught.exception.diagnostic["error"],
                "effective-assets-manifest-missing",
            )
            self.assertIn("Refusing to guess", str(caught.exception))

    def test_describe_records_failures_instead_of_raising(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            described = describe_effective_assets_trees(
                [_bfme2_tree(root), root / "does-not-exist"]
            )
            self.assertEqual(described[0]["edition"], "bfme2")
            self.assertEqual(
                described[1]["error"]["error"], "effective-assets-root-missing"
            )


class EffectiveAssetsIdentityCliTests(unittest.TestCase):
    def _run(self, argv: list[str]) -> tuple[int, str, str]:
        stdout, stderr = io.StringIO(), io.StringIO()
        with patch(
            "openbfme_importer.cli.ArchivePolicy.load", return_value=None
        ), redirect_stdout(stdout), redirect_stderr(stderr):
            code = main(argv)
        return code, stdout.getvalue(), stderr.getvalue()

    def test_cli_reports_the_edition_and_exits_zero(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            tree = _rotwk_tree(Path(raw))
            code, out, _ = self._run(
                ["--json", "verify-effective-assets", "--assets-root", str(tree)]
            )
            self.assertEqual(code, 0)
            report = json.loads(out)
            self.assertEqual(report["edition"], "rotwk")
            self.assertTrue(report["trusted"])

    def test_cli_refuses_a_wrong_tree_with_a_structured_diagnostic(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            tree = _bfme2_tree(Path(raw))
            code, _, err = self._run(
                [
                    "--json",
                    "verify-effective-assets",
                    "--assets-root",
                    str(tree),
                    "--expect-game",
                    "rotwk",
                ]
            )
            self.assertEqual(code, 1)
            diagnostic = json.loads(err)
            self.assertEqual(diagnostic["error"], "effective-assets-game-mismatch")
            self.assertEqual(diagnostic["requested_game"], "rotwk")


if __name__ == "__main__":
    unittest.main()
