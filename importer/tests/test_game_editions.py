from __future__ import annotations

from contextlib import redirect_stdout
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest


sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from openbfme_importer.catalog import CORE_ARCHIVES, archive_precedence, doctor_install
from openbfme_importer.cli import main
from openbfme_importer.game import retail_game, workspace_root
from openbfme_importer.pipeline import ImportPipeline

from importer.tests.test_big import make_big


class RetailGameEditionTests(unittest.TestCase):
    def test_game_definitions_and_workspace_routing_are_bounded(self) -> None:
        state = Path("private-state")
        self.assertEqual(retail_game("BFME2").id, "bfme2")
        self.assertEqual(retail_game("rotwk").executable_names, ("lotrbfme2ep1.exe",))
        self.assertEqual(workspace_root(state, "bfme2"), state)
        self.assertEqual(
            workspace_root(state, "rotwk"), state / "editions" / "rotwk"
        )
        with self.assertRaisesRegex(ValueError, "unsupported retail game"):
            retail_game("../escape")

    def test_expansion_patch_precedence_is_explicit(self) -> None:
        self.assertLess(
            archive_precedence("__patch202.big"),
            archive_precedence("_patch201.big"),
        )
        self.assertLess(
            archive_precedence("_patch201.big"),
            archive_precedence("_patch106.big"),
        )
        self.assertLess(
            archive_precedence("_patch106.big"),
            archive_precedence("ini.big"),
        )

    def test_doctor_recognizes_each_installed_game_identity(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            for archive in CORE_ARCHIVES[:3]:
                (root / archive).write_bytes(b"BIGF")
            (root / "lotrbfme2ep1.exe").write_bytes(b"exe")
            (root / "__patch202.big").write_bytes(b"BIGF")
            (root / "patch.doc").write_text("Patch 2.02", encoding="ascii")

            report = doctor_install(root, game="rotwk")
            self.assertTrue(report["ready"])
            self.assertEqual(report["game"], "rotwk")
            self.assertEqual(report["executable"], "lotrbfme2ep1.exe")
            self.assertEqual(report["patch_archives"], ["__patch202.big"])
            self.assertEqual(report["declared_patch"], "2.02")

            (root / "patch.doc").unlink()
            inferred = doctor_install(root, game="rotwk")
            self.assertEqual(inferred["declared_patch"], "2.02")

            wrong_game = doctor_install(root, game="bfme2")
            self.assertFalse(wrong_game["ready"])
            self.assertFalse(wrong_game["executable_present"])

    def test_rotwk_catalog_and_effective_tree_do_not_touch_bfme2_paths(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            install = root / "rotwk"
            state = root / "state"
            install.mkdir()
            make_big(install / "ini.big", {"data/expansion.ini": b"rotwk"})

            stdout = io.StringIO()
            with redirect_stdout(stdout):
                result = main(
                    [
                        "--json",
                        "--state-root",
                        str(state),
                        "extract-all-assets",
                        "--install",
                        str(install),
                        "--game",
                        "rotwk",
                    ]
                )
            self.assertEqual(result, 0)
            report = json.loads(stdout.getvalue())
            asset_root = Path(report["asset_root"])
            self.assertEqual(
                asset_root,
                state / "editions" / "rotwk" / "cache" / "effective-assets",
            )
            self.assertEqual((asset_root / "data" / "expansion.ini").read_bytes(), b"rotwk")
            self.assertTrue((state / "catalog" / "rotwk.json").is_file())
            self.assertFalse((state / "catalog" / "bfme2.json").exists())
            self.assertFalse((state / "cache" / "effective-assets").exists())

            pipeline = ImportPipeline(None, state, game="rotwk")
            self.assertEqual(pipeline.workspace_root, state / "editions" / "rotwk")

            census_stdout = io.StringIO()
            with redirect_stdout(census_stdout):
                census_result = main(
                    [
                        "--json",
                        "--state-root",
                        str(state),
                        "census-assets",
                        "--install",
                        str(install),
                        "--game",
                        "rotwk",
                    ]
                )
            self.assertEqual(census_result, 0)
            census = json.loads(census_stdout.getvalue())
            self.assertEqual(census["winner_file_count"], 1)
            self.assertEqual(census["family_counts"]["data"], 1)
            self.assertEqual(census["unclassified_file_count"], 0)
            self.assertTrue(
                (
                    state
                    / "editions"
                    / "rotwk"
                    / "reports"
                    / "rotwk-asset-census.json"
                ).is_file()
            )


if __name__ == "__main__":
    unittest.main()
