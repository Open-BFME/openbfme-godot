from __future__ import annotations

import contextlib
import hashlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

from openbfme_importer import cli
from openbfme_importer.faction_census import PlayableFaction


class CliTests(unittest.TestCase):
    def test_bootstrap_tools_returns_success_without_faction_summary(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            with mock.patch.object(
                cli, "bootstrap_tools", return_value={"ready": True}
            ):
                result = cli.main(
                    ["--state-root", raw, "bootstrap-tools"]
                )
        self.assertEqual(result, 0)

    def test_incomplete_faction_plan_writes_report_and_returns_six(self) -> None:
        plan = {
            "aggregateSha256": "a" * 64,
            "summary": {
                "ready": False,
                "objectCount": 1,
                "descriptorReadyCount": 0,
                "converterGapCount": 1,
                "unresolvedLeafCount": 0,
                "unsupportedFamilies": ["structure"],
            },
        }
        with tempfile.TemporaryDirectory() as raw:
            state_root = Path(raw) / "state"
            effective_root = state_root / "cache" / "effective-assets"
            manifest = effective_root / "manifest.json"
            manifest.parent.mkdir(parents=True)
            manifest.write_text("{}", encoding="utf-8")
            pipeline = mock.Mock()
            pipeline._effective_asset_paths.return_value = (
                effective_root,
                manifest,
                Path(raw) / "staging",
                Path(raw) / "backup",
            )
            with (
                mock.patch.object(cli, "_load_or_build_catalog", return_value=object()),
                mock.patch.object(cli, "ImportPipeline", return_value=pipeline),
                mock.patch.object(
                    cli,
                    "resolve_playable_faction",
                    return_value=PlayableFaction("FactionMen", "Men", 57),
                ),
                mock.patch.object(cli, "plan_faction_import", return_value=plan),
            ):
                result = cli.main(
                    [
                        "--state-root",
                        str(state_root),
                        "import-faction",
                        "--install",
                        "C:/BFME2",
                        "--faction",
                        "men",
                        "--plan-only",
                    ]
                )

            self.assertEqual(result, 6)
            self.assertTrue(
                (state_root / "reports" / "faction-import" / "men-plan.json").is_file()
            )

    def test_import_faction_parser_accepts_all_six_factions(self) -> None:
        parser = cli.build_parser()
        for faction in ("men", "elves", "dwarves", "isengard", "mordor", "wild"):
            args = parser.parse_args(
                [
                    "import-faction",
                    "--install",
                    "C:/BFME2",
                    "--faction",
                    faction,
                    "--plan-only",
                ]
            )
            self.assertEqual(args.faction, faction)
            self.assertTrue(args.plan_only)

        template_args = parser.parse_args(
            [
                "import-faction",
                "--install",
                "C:/RotWK",
                "--game",
                "rotwk",
                "--faction",
                "FactionAngmar",
                "--plan-only",
            ]
        )
        self.assertEqual(template_args.faction, "FactionAngmar")

    def test_census_factions_writes_discovered_rows(self) -> None:
        factions = (
            PlayableFaction("PlayableAlpha", "Alpha", 3),
            PlayableFaction("FactionBeta", "Beta", 5),
        )
        with tempfile.TemporaryDirectory() as raw:
            with (
                mock.patch.object(cli, "_load_or_build_catalog", return_value=object()),
                mock.patch.object(
                    cli, "discover_playable_factions", return_value=factions
                ),
            ):
                result = cli.main(
                    [
                        "--state-root",
                        raw,
                        "census-factions",
                        "--install",
                        "C:/BFME2",
                    ]
                )

            report = json.loads(
                (Path(raw) / "reports" / "bfme2-playable-factions.json").read_text(
                    encoding="utf-8"
                )
            )
        self.assertEqual(result, 0)
        self.assertEqual(report["factionCount"], 2)
        self.assertEqual(report["factions"][0]["name"], "PlayableAlpha")

    def test_rotwk_conversion_is_admitted_and_routes_through_the_converter(
        self,
    ) -> None:
        coverage = {
            "aggregateSha256": "a" * 64,
            "summary": {
                "conversionComplete": False,
                "objectCount": 42,
                "convertedCount": 0,
                "excludedCount": 0,
                "converterGapCount": 42,
                "unresolvedLeafCount": 861,
            },
        }
        pipeline = mock.MagicMock()
        pipeline._effective_asset_paths.return_value = (
            Path("effective"),
            Path("effective/manifest.json"),
            Path("staging"),
            Path("backup"),
        )
        with tempfile.TemporaryDirectory() as raw:
            with (
                mock.patch.object(cli, "_load_or_build_catalog", return_value=object()),
                mock.patch.object(
                    cli,
                    "resolve_playable_faction",
                    return_value=PlayableFaction("FactionAngmar", "Angmar", 1),
                ) as resolve,
                mock.patch.object(cli, "ImportPipeline", return_value=pipeline),
                mock.patch.object(
                    cli, "convert_faction_import", return_value=coverage
                ) as convert,
            ):
                result = cli.main(
                    [
                        "--state-root",
                        raw,
                        "import-faction",
                        "--install",
                        "C:/RotWK",
                        "--game",
                        "rotwk",
                        "--faction",
                        "FactionAngmar",
                        "--convert",
                    ]
                )

        # RotWK conversion is admitted: the faction is resolved and the same
        # converter entry point runs with game="rotwk" (exit 6 = fail-closed
        # incomplete coverage, not a hard rejection).
        self.assertEqual(result, 6)
        resolve.assert_called_once()
        self.assertEqual(convert.call_args.kwargs["game"], "rotwk")

    def test_unknown_game_is_rejected_by_the_cli(self) -> None:
        with self.assertRaises(SystemExit):
            cli.main(
                [
                    "import-faction",
                    "--install",
                    "C:/RotWK",
                    "--game",
                    "tiberium",
                    "--faction",
                    "FactionAngmar",
                    "--convert",
                ]
            )

    def test_non_men_census_uses_the_curated_faction_roots(self) -> None:
        report = {
            "summary": {
                "unresolvedCount": 0,
                "objectCount": 1,
                "commandSetCount": 1,
                "commandButtonCount": 1,
                "upgradeCount": 0,
                "specialPowerCount": 0,
                "mappedImageResolvedCount": 0,
                "textResolvedCount": 0,
                "audioSampleCount": 0,
            }
        }
        with tempfile.TemporaryDirectory() as raw:
            with (
                mock.patch.object(cli, "_load_or_build_catalog", return_value=object()),
                mock.patch.object(
                    cli,
                    "resolve_playable_faction",
                    return_value=PlayableFaction("FactionElves", "Elves", 1),
                ),
                mock.patch.object(
                    cli, "census_playable_faction", return_value=report
                ) as census,
            ):
                result = cli.main(
                    [
                        "--state-root",
                        raw,
                        "census-faction",
                        "--install",
                        "C:/BFME2",
                        "--faction",
                        "elves",
                    ]
                )

        self.assertEqual(result, 0)
        self.assertEqual(census.call_args.kwargs["player_template"], "FactionElves")
        self.assertTrue(census.call_args.kwargs["implicit_object_roots"])

    def test_generate_faction_profile_parser_requires_install(self) -> None:
        parser = cli.build_parser()
        args = parser.parse_args(
            ["generate-faction-profile", "--install", "C:/BFME2", "--reindex"]
        )
        self.assertEqual(args.command, "generate-faction-profile")
        self.assertEqual(args.install, "C:/BFME2")
        self.assertTrue(args.reindex)

        with contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit) as raised:
                parser.parse_args(["generate-faction-profile"])
        self.assertEqual(raised.exception.code, 2)

    def test_generate_faction_profile_writes_deterministic_private_profile(self) -> None:
        profile = {
            "format": 1,
            "id": "synthetic-private-profile",
            "pack": {"id": "synthetic-private-pack"},
            "resources": [
                {"id": "safe-resource-0"},
                {"id": "safe-resource-1"},
            ],
        }
        expected_payload = (
            json.dumps(profile, indent=2, sort_keys=True, ensure_ascii=False) + "\n"
        ).encode("utf-8")

        with tempfile.TemporaryDirectory() as raw:
            state_root = Path(raw) / "external-state"
            stdout = io.StringIO()
            with (
                mock.patch.object(cli, "_load_or_build_catalog", return_value=object()),
                mock.patch.object(
                    cli, "build_men_leaf_profile", return_value=profile
                ) as build_profile,
                contextlib.redirect_stdout(stdout),
            ):
                result = cli.main(
                    [
                        "--json",
                        "--state-root",
                        str(state_root),
                        "generate-faction-profile",
                        "--install",
                        "C:/BFME2",
                    ]
                )

            self.assertEqual(result, 0)
            build_profile.assert_called_once()
            output = json.loads(stdout.getvalue())
            expected_path = (
                state_root / "profiles" / "men-command-leaves.generated.json"
            )
            self.assertEqual(
                output,
                {
                    "ready": True,
                    "profile": str(expected_path),
                    "profile_sha256": hashlib.sha256(expected_payload).hexdigest(),
                    "resource_count": 2,
                },
            )
            self.assertEqual(expected_path.read_bytes(), expected_payload)

    def test_generate_map_profile_writes_deterministic_private_profile(self) -> None:
        profile = {
            "format": 1,
            "id": "synthetic-map-profile",
            "pack": {"id": "synthetic-map-pack"},
            "resources": [{"id": "map-resource"}],
            "runtime_data": {"data/maps.json": {"maps": [{"id": "map-one"}]}},
        }
        expected_payload = (
            json.dumps(profile, indent=2, sort_keys=True, ensure_ascii=False) + "\n"
        ).encode("utf-8")
        with tempfile.TemporaryDirectory() as raw:
            state_root = Path(raw) / "private-state"
            stdout = io.StringIO()
            with (
                mock.patch.object(cli, "_load_or_build_catalog", return_value=object()),
                mock.patch.object(cli, "build_five_map_profile", return_value=profile),
                contextlib.redirect_stdout(stdout),
            ):
                result = cli.main(
                    [
                        "--json",
                        "--state-root",
                        str(state_root),
                        "generate-map-profile",
                        "--install",
                        "C:/BFME2",
                    ]
                )
            self.assertEqual(result, 0)
            output = json.loads(stdout.getvalue())
            expected_path = state_root / "profiles" / "five-maps.generated.json"
            self.assertEqual(output["map_count"], 1)
            self.assertEqual(output["resource_count"], 1)
            self.assertEqual(
                output["profile_sha256"], hashlib.sha256(expected_payload).hexdigest()
            )
            self.assertEqual(expected_path.read_bytes(), expected_payload)


if __name__ == "__main__":
    unittest.main()
