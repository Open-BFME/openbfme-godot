from __future__ import annotations

import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

from openbfme_importer.asset_census import ASSET_FAMILIES, census_assets
from openbfme_importer.catalog import CatalogEntry, InstallCatalog

from importer.tests.test_big import make_big


def _fixture_catalog(root: Path) -> InstallCatalog:
    make_big(
        root / "ini.big",
        {
            r"Maps\Arena\Arena.MAP": b"map",
            r"Art\W3D\Hero.W3D": b"w3d!",
            r"ART\CompiledTextures\Hero.DDS": b"texture",
            r"DATA\Audio\Sounds\Voice.WAV": b"voice!",
            r"DATA\Audio\Tracks\Theme.MP3": b"track!!",
            r"DATA\APT\Shell.APT": b"apt!!!!!",
            r"DATA\UI\Layout.INI": b"ui-layout",
            r"DATA\INI\Game.INI": b"data!!!!!!",
            r"Misc\Blob.BIN": b"other!!!!!!",
            r"Misc\README": b"",
            r"Data\Thing.INI": b"base",
        },
    )
    make_big(root / "_patch106.big", {"data/thing.ini": b"patch-winner"})
    make_big(
        root / "music.big",
        {r"DATA\Audio\Unknown\Score.WAV": b"archive-music"},
    )
    return InstallCatalog.build(root)


class AssetCensusTests(unittest.TestCase):
    def test_case_insensitive_precedence_winners_and_totals_are_exact(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            catalog = _fixture_catalog(Path(raw))
            report = census_assets(catalog)

        by_path = {item.virtual_path.casefold(): item for item in report.entries}
        winner = by_path["data/thing.ini"]
        self.assertEqual(winner.archive, "_patch106.big")
        self.assertEqual(winner.size, len(b"patch-winner"))
        self.assertEqual(report.catalog_entry_count, 13)
        self.assertEqual(report.winner_file_count, 12)
        self.assertEqual(report.shadowed_entry_count, 1)

        family_counts = {item.family: item.file_count for item in report.families}
        self.assertEqual(
            family_counts,
            {
                "map": 1,
                "w3d": 1,
                "texture": 1,
                "sound": 1,
                "music": 2,
                "ui": 2,
                "data": 2,
                "other": 2,
            },
        )
        self.assertEqual(sum(family_counts.values()), report.winner_file_count)
        self.assertEqual(
            sum(item.total_bytes for item in report.families), report.winner_bytes
        )
        self.assertEqual(
            sum(item.file_count for item in report.extensions),
            report.winner_file_count,
        )
        self.assertEqual(
            sum(item.total_bytes for item in report.extensions), report.winner_bytes
        )

        neutral = report.neutral()
        self.assertEqual(neutral["totals"]["accountedWinnerFileCount"], 12)
        self.assertEqual(neutral["totals"]["unclassifiedWinnerFileCount"], 0)
        self.assertEqual(neutral["totals"]["otherFileCount"], 2)
        self.assertEqual(neutral["selection"]["payloadBytesRead"], 0)
        self.assertEqual(
            neutral["totals"]["catalogEntryBytes"]
            - neutral["totals"]["winnerBytes"],
            len(b"base"),
        )

    def test_classification_is_case_insensitive_path_aware_and_explicit(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            report = census_assets(_fixture_catalog(Path(raw)))

        by_path = {item.virtual_path.casefold(): item for item in report.entries}
        expected = {
            "maps/arena/arena.map": ("map", ".map"),
            "art/w3d/hero.w3d": ("w3d", ".w3d"),
            "art/compiledtextures/hero.dds": ("texture", ".dds"),
            "data/audio/sounds/voice.wav": ("sound", ".wav"),
            "data/audio/tracks/theme.mp3": ("music", ".mp3"),
            "data/audio/unknown/score.wav": ("music", ".wav"),
            "data/apt/shell.apt": ("ui", ".apt"),
            "data/ui/layout.ini": ("ui", ".ini"),
            "data/ini/game.ini": ("data", ".ini"),
            "misc/blob.bin": ("other", ".bin"),
            "misc/readme": ("other", ""),
        }
        for path, classification in expected.items():
            with self.subTest(path=path):
                item = by_path[path]
                self.assertEqual((item.family, item.extension), classification)
                self.assertIn("/", item.virtual_path)
                self.assertNotIn("\\", item.virtual_path)

        self.assertEqual(
            by_path["data/audio/tracks/theme.mp3"].classification_rule,
            "audio-path:music-or-tracks",
        )
        self.assertEqual(
            by_path["data/audio/unknown/score.wav"].classification_rule,
            "audio-archive:music.big",
        )
        self.assertEqual(
            by_path["misc/blob.bin"].classification_rule, "explicit-fallback"
        )
        self.assertEqual(tuple(item.family for item in report.families), ASSET_FAMILIES)
        extensions = {item.extension: item for item in report.extensions}
        self.assertEqual(extensions[""].file_count, 1)
        self.assertEqual(extensions[".ini"].file_count, 3)

    def test_neutral_output_is_order_independent_json_ready_and_payload_free(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            catalog = _fixture_catalog(Path(raw))
            reordered = InstallCatalog(
                catalog.install_root,
                tuple(reversed(catalog.archives)),
                tuple(reversed(catalog.entries)),
            )
            with mock.patch.object(
                catalog,
                "open_archive_for",
                side_effect=AssertionError("asset census opened a payload"),
            ):
                first = census_assets(catalog).neutral()
            second = census_assets(reordered).neutral()

            self.assertEqual(first, second)
            self.assertEqual(
                json.dumps(first, sort_keys=True, separators=(",", ":")),
                json.dumps(second, sort_keys=True, separators=(",", ":")),
            )
            self.assertNotIn(str(catalog.install_root), json.dumps(first))

    def test_unsafe_catalog_paths_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            catalog = InstallCatalog(
                Path(raw),
                (),
                (CatalogEntry("ini.big", "../escape.dds", 0, 1, 0),),
            )
            with self.assertRaisesRegex(ValueError, "unsafe catalog virtual path"):
                census_assets(catalog)


if __name__ == "__main__":
    unittest.main()
