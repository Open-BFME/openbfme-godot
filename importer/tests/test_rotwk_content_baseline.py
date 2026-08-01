"""RotWK is the content baseline (owner decision, 2026-07-27).

These tests pin three things that the rest of the suite does not:

1. An *unqualified* invocation resolves RotWK, not BFME2.
2. BFME2 stays reachable through an explicit ``--game bfme2`` — it is the
   comparison baseline that proves what RotWK adds.
3. The catalog producer FAILS CLOSED rather than silently supplying content
   from the wrong edition. A silent fallback here is the exact defect class
   this project has been removing.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys
import tempfile
import unittest


sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from openbfme_importer import cli
from openbfme_importer.catalog import (
    CatalogProvenanceError,
    catalog_provenance_reason,
)


GAME_BEARING_COMMANDS = (
    "doctor",
    "index",
    "extract-all-assets",
    "census-assets",
    "census-faction",
    "census-factions",
    "census-strategic",
    "living-world",
    "convert-videos",
    "import-faction",
    "generate-faction-profile",
    "generate-map-profile",
    "import-unit",
    "publish-faction-to-slice",
    "plan",
    "extract",
    "build",
)


class DefaultEditionTests(unittest.TestCase):
    def test_every_game_bearing_command_defaults_to_rotwk(self) -> None:
        parser = cli.build_parser()
        for command in GAME_BEARING_COMMANDS:
            with self.subTest(command=command):
                extra = []
                if command == "import-faction":
                    extra = ["--faction", "men", "--plan-only"]
                elif command in {"census-faction", "publish-faction-to-slice"}:
                    extra = ["--faction", "men"]
                elif command == "import-unit":
                    extra = ["--object", "GondorFighter"]
                args = parser.parse_args(
                    [command, "--install", "C:/Install", *extra]
                )
                self.assertEqual(args.game, "rotwk")

    def test_bfme2_remains_reachable_on_every_game_bearing_command(self) -> None:
        parser = cli.build_parser()
        for command in GAME_BEARING_COMMANDS:
            with self.subTest(command=command):
                extra = []
                if command == "import-faction":
                    extra = ["--faction", "men", "--plan-only"]
                elif command in {"census-faction", "publish-faction-to-slice"}:
                    extra = ["--faction", "men"]
                elif command == "import-unit":
                    extra = ["--object", "GondorFighter"]
                args = parser.parse_args(
                    [command, "--game", "bfme2", "--install", "C:/Install", *extra]
                )
                self.assertEqual(args.game, "bfme2")

    def test_unqualified_invocation_selects_the_rotwk_catalog_file(self) -> None:
        parser = cli.build_parser()
        with tempfile.TemporaryDirectory() as raw:
            args = parser.parse_args(
                ["--state-root", raw, "index", "--install", "C:/Install"]
            )
            self.assertEqual(cli._catalog_path(args).name, "rotwk.json")
            explicit = parser.parse_args(
                [
                    "--state-root",
                    raw,
                    "index",
                    "--game",
                    "bfme2",
                    "--install",
                    "C:/Install",
                ]
            )
            self.assertEqual(cli._catalog_path(explicit).name, "bfme2.json")


class CatalogProvenanceReasonTests(unittest.TestCase):
    def test_rotwk_evidence_accepts_a_layered_install_tree(self) -> None:
        # Layered RotWK carries its patch archives under per-layer subdirs.
        archives = [
            "layer-0-rotwk/_patch201.big",
            "layer-0-rotwk/ini.big",
            "layer-1-bfme2/_patch101.big",
        ]
        self.assertIsNone(catalog_provenance_reason(archives, "rotwk"))

    def test_a_bfme2_only_tree_is_rejected_as_rotwk(self) -> None:
        archives = ["_patch101.big", "_patch103.big", "ini.big"]
        reason = catalog_provenance_reason(archives, "rotwk")
        self.assertIsNotNone(reason)
        self.assertIn("Rise of the Witch-king", reason)
        self.assertIn("_patch101.big", reason)

    def test_bfme2_evidence_is_accepted_and_never_rejected_by_layering(self) -> None:
        self.assertIsNone(
            catalog_provenance_reason(["_patch101.big", "ini.big"], "bfme2")
        )
        # A RotWK tree legitimately layers BFME2, so BFME2 is satisfied too.
        self.assertIsNone(
            catalog_provenance_reason(
                ["layer-0-rotwk/_patch201.big", "layer-1-bfme2/_patch103.big"], "bfme2"
            )
        )

    def test_an_unpatched_tree_is_accepted_rather_than_false_positived(self) -> None:
        # A never-patched retail install carries no patch archive at all.
        # Absence of evidence must not be treated as evidence of the wrong
        # edition, or the guard rejects legitimate retail input.
        self.assertIsNone(catalog_provenance_reason([], "rotwk"))
        self.assertIsNone(catalog_provenance_reason([], "bfme2"))
        self.assertIsNone(catalog_provenance_reason(["ini.big", "w3d.big"], "rotwk"))


class _FakeArchive:
    def __init__(self, relative_path: str) -> None:
        self.relative_path = relative_path


class _FakeCatalog:
    def __init__(self, archives: list[str], install_root: Path) -> None:
        self.archives = [_FakeArchive(item) for item in archives]
        self.install_root = install_root
        self.source_policy = None

    def stale_reasons(self) -> list[str]:
        return []


class FailClosedProducerTests(unittest.TestCase):
    def _args(self, state_root: Path, game: str) -> argparse.Namespace:
        return cli.build_parser().parse_args(
            [
                "--state-root",
                str(state_root),
                "index",
                "--game",
                game,
                "--install",
                str(state_root / "install"),
            ]
        )

    def test_a_bfme2_catalog_offered_as_rotwk_fails_closed(self) -> None:
        from unittest import mock

        with tempfile.TemporaryDirectory() as raw:
            state_root = Path(raw)
            args = self._args(state_root, "rotwk")
            catalog_path = cli._catalog_path(args)
            catalog_path.parent.mkdir(parents=True, exist_ok=True)
            catalog_path.write_text("{}", encoding="utf-8")
            install = Path(args.install).expanduser().resolve()
            fake = _FakeCatalog(["_patch101.big", "ini.big"], install)
            with mock.patch.object(
                cli.InstallCatalog, "load", return_value=fake
            ):
                with self.assertRaises(CatalogProvenanceError) as caught:
                    cli._load_or_build_catalog(args)
        diagnostic = caught.exception.diagnostic
        self.assertEqual(diagnostic["error"], "catalog-game-mismatch")
        self.assertEqual(diagnostic["game"], "rotwk")
        self.assertEqual(diagnostic["origin"], "cached")

    def test_a_rotwk_catalog_is_accepted_as_rotwk(self) -> None:
        from unittest import mock

        with tempfile.TemporaryDirectory() as raw:
            state_root = Path(raw)
            args = self._args(state_root, "rotwk")
            catalog_path = cli._catalog_path(args)
            catalog_path.parent.mkdir(parents=True, exist_ok=True)
            catalog_path.write_text("{}", encoding="utf-8")
            install = Path(args.install).expanduser().resolve()
            fake = _FakeCatalog(["layer-0-rotwk/_patch201.big"], install)
            with mock.patch.object(cli.InstallCatalog, "load", return_value=fake):
                self.assertIs(cli._load_or_build_catalog(args), fake)

    def test_a_malformed_catalog_fails_closed_instead_of_rebuilding(self) -> None:
        from unittest import mock

        with tempfile.TemporaryDirectory() as raw:
            state_root = Path(raw)
            args = self._args(state_root, "rotwk")
            catalog_path = cli._catalog_path(args)
            catalog_path.parent.mkdir(parents=True, exist_ok=True)
            catalog_path.write_text("not json at all", encoding="utf-8")
            build = mock.Mock()
            with mock.patch.object(cli.InstallCatalog, "build", build):
                with self.assertRaises(CatalogProvenanceError) as caught:
                    cli._load_or_build_catalog(args)
            # The whole point: it must NOT quietly rebuild.
            build.assert_not_called()
        diagnostic = caught.exception.diagnostic
        self.assertEqual(diagnostic["error"], "catalog-unreadable")
        self.assertEqual(diagnostic["game"], "rotwk")

    def test_a_freshly_built_catalog_from_the_wrong_install_fails_closed(self) -> None:
        from unittest import mock

        with tempfile.TemporaryDirectory() as raw:
            state_root = Path(raw)
            args = self._args(state_root, "rotwk")
            install = Path(args.install).expanduser().resolve()
            fake = _FakeCatalog(["_patch101.big", "ini.big"], install)
            save = mock.Mock()
            with (
                mock.patch.object(cli.InstallCatalog, "build", return_value=fake),
                mock.patch.object(_FakeCatalog, "save", save, create=True),
            ):
                with self.assertRaises(CatalogProvenanceError) as caught:
                    cli._load_or_build_catalog(args)
            # A rejected catalog must never be written to the rotwk slot.
            save.assert_not_called()
            self.assertFalse(cli._catalog_path(args).exists())
        self.assertEqual(caught.exception.diagnostic["origin"], "rebuilt")


class StructuredDiagnosticTests(unittest.TestCase):
    def test_json_mode_emits_the_structured_diagnostic_not_a_bare_string(self) -> None:
        import contextlib
        import io
        from unittest import mock

        with tempfile.TemporaryDirectory() as raw:
            state_root = Path(raw)
            probe = cli.build_parser().parse_args(
                ["--state-root", str(state_root), "index", "--install", "C:/Install"]
            )
            catalog_path = cli._catalog_path(probe)
            self.assertEqual(catalog_path.name, "rotwk.json")
            catalog_path.parent.mkdir(parents=True, exist_ok=True)
            catalog_path.write_text("not json", encoding="utf-8")
            stderr = io.StringIO()
            with (
                mock.patch.object(cli.InstallCatalog, "build") as build,
                contextlib.redirect_stderr(stderr),
            ):
                result = cli.main(
                    [
                        "--json",
                        "--state-root",
                        str(state_root),
                        "index",
                        "--install",
                        "C:/Install",
                    ]
                )
            build.assert_not_called()
        self.assertEqual(result, 1)
        payload = json.loads(stderr.getvalue())
        self.assertEqual(payload["error"], "catalog-unreadable")
        self.assertEqual(payload["game"], "rotwk")
        self.assertIn("--reindex", payload["message"])


if __name__ == "__main__":
    unittest.main()
