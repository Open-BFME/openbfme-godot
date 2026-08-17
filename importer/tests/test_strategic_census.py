"""Tests for the fail-closed War-of-the-Ring / Create-a-Hero strategic census."""

from __future__ import annotations

import unittest

from openbfme_importer.catalog import CatalogEntry, InstallCatalog
from openbfme_importer.paths import repo_root_from_module
from openbfme_importer.strategic_census import (
    EXPECTED_FILES,
    census_strategic_surface,
    _count_top_level_blocks,
)


class _FakeCatalog:
    """Duck-typed InstallCatalog carrying synthetic effective entries."""

    def __init__(self, files: dict[str, bytes]) -> None:
        self._files = {path.casefold(): source for path, source in files.items()}
        self.entries = [
            CatalogEntry(
                archive="test.big",
                name=path,
                offset=0,
                size=len(source),
                precedence=0,
            )
            for path, source in files.items()
        ]

    def resolve_exact(self, virtual_path: str):
        for entry in self.entries:
            if entry.key == virtual_path.casefold():
                return entry
        return None

    def open_archive_for(self, entry):
        return self

    def as_entry(self, entry):
        return entry

    def read_entry(self, entry, max_bytes=None):
        return self._files[entry.key]


class StrategicCensusClassifierTests(unittest.TestCase):
    def test_known_families_are_accepted_and_totaled(self) -> None:
        catalog = _FakeCatalog(
            {
                "data/ini/livingworld.ini": (
                    b"LivingWorldArmyIcon City_Large\n"
                    b"\tObject = Icon1\n"
                    b"End\n"
                    b"LivingWorldArmyIcon City_Small\n"
                    b"\tObject = Icon2\n"
                    b"End\n"
                ),
                "data/ini/livingworldautoresolvearmor.ini": (
                    b"AutoResolveArmor Armor_One\n"
                    b"\tArmor = DEFAULT 100%\n"
                    b"End\n"
                ),
                "data/ini/strategichud.ini": (
                    b"StrategicHUD\n"
                    b"\tChecklist\n"
                    b"\t\tItemVerticalSpacing = 0.0\n"
                    b"\tEnd\n"
                    b"End\n"
                ),
                "data/ini/createaherospecialpowers.ini": (
                    b"SpecialPower Power_One\n"
                    b"\tEnum = SPECIAL_GENERIC\n"
                    b"End\n"
                    b"SpecialPower Power_Two\n"
                    b"\tEnum = SPECIAL_GENERIC\n"
                    b"\tEnd\n"
                ),
            }
        )
        report = census_strategic_surface(catalog, "bfme2")
        self.assertEqual(report["fileCount"], 4)
        self.assertEqual(report["surfaceFileCounts"], {"wotr": 3, "cah": 1})
        self.assertEqual(
            report["familyTotals"],
            {"livingWorld": 3, "autoResolve": 1, "cah": 2},
        )
        self.assertEqual(report["unclassifiedBlocks"], [])
        rows = {row["virtualPath"]: row for row in report["files"]}
        self.assertEqual(
            rows["data/ini/livingworld.ini"]["blockCounts"],
            {"LivingWorldArmyIcon": 2},
        )
        # The anonymous StrategicHUD singleton has no name token for the flat
        # reader and must still be counted through the header scanner.
        self.assertEqual(
            rows["data/ini/strategichud.ini"]["blockCounts"],
            {"StrategicHUD": 1},
        )
        # The second SpecialPower closes with an indented End (a retail
        # authoring quirk) and must still count.
        self.assertEqual(
            rows["data/ini/createaherospecialpowers.ini"]["blockCounts"],
            {"SpecialPower": 2},
        )
        self.assertEqual(rows["data/ini/livingworld.ini"]["archive"], "test.big")

    def test_unknown_block_type_lands_in_unclassified_blocks(self) -> None:
        catalog = _FakeCatalog(
            {
                "data/ini/livingworld.ini": (
                    b"LivingWorldFrobnicator Widget\n"
                    b"\tValue = 1\n"
                    b"End\n"
                    b"LivingWorldSound Wind\n"
                    b"\tSound = WindLoop\n"
                    b"End\n"
                ),
            }
        )
        report = census_strategic_surface(catalog, "bfme2")
        self.assertEqual(
            report["unclassifiedBlocks"],
            [
                {
                    "virtualPath": "data/ini/livingworld.ini",
                    "blockType": "LivingWorldFrobnicator",
                    "count": 1,
                }
            ],
        )
        # Unclassified blocks are enumerated, never dropped: they stay in the
        # per-file counts but never contribute to family totals.
        row = report["files"][0]
        self.assertEqual(
            row["blockCounts"],
            {"LivingWorldFrobnicator": 1, "LivingWorldSound": 1},
        )
        self.assertEqual(
            report["familyTotals"],
            {"livingWorld": 1, "autoResolve": 0, "cah": 0},
        )

    def test_missing_expected_files_land_in_unresolved_files(self) -> None:
        report = census_strategic_surface(_FakeCatalog({}), "bfme2")
        expected_total = len(EXPECTED_FILES["bfme2"]["wotr"]) + len(
            EXPECTED_FILES["bfme2"]["cah"]
        )
        self.assertEqual(report["fileCount"], 0)
        self.assertEqual(len(report["unresolvedFiles"]), expected_total)
        unresolved = {row["virtualPath"]: row["surface"] for row in report["unresolvedFiles"]}
        self.assertEqual(unresolved["data/ini/strategichud.ini"], "wotr")
        self.assertEqual(unresolved["maps/createahero/map.ini"], "cah")
        self.assertEqual(
            unresolved["data/ini/createaherospecialpowers.ini"], "cah"
        )

    def test_expected_file_counts_match_measured_ground_truth(self) -> None:
        self.assertEqual(len(EXPECTED_FILES["bfme2"]["wotr"]), 27)
        self.assertEqual(len(EXPECTED_FILES["bfme2"]["cah"]), 6)
        self.assertEqual(len(EXPECTED_FILES["rotwk"]["wotr"]), 29)
        self.assertEqual(len(EXPECTED_FILES["rotwk"]["cah"]), 6)

    def test_unterminated_block_fails_closed(self) -> None:
        with self.assertRaises(ValueError):
            _count_top_level_blocks(b"LivingWorldSound Wind\n\tSound = X\n")

    def test_unsupported_game_fails_closed(self) -> None:
        with self.assertRaises(ValueError):
            census_strategic_surface(_FakeCatalog({}), "bfme1")


class _RealCatalogTestsBase(unittest.TestCase):
    game = ""

    @classmethod
    def setUpClass(cls) -> None:
        if not cls.game:
            raise unittest.SkipTest("abstract base")
        path = (
            repo_root_from_module()
            / "workspace"
            / "retail-work"
            / "catalog"
            / f"{cls.game}.json"
        )
        if not path.is_file():
            raise unittest.SkipTest(f"private {cls.game} catalog is unavailable")
        cls.report = census_strategic_surface(InstallCatalog.load(path), cls.game)


class Bfme2StrategicCensusIntegrationTests(_RealCatalogTestsBase):
    game = "bfme2"

    def test_measured_surface(self) -> None:
        report = self.report
        self.assertEqual(report["surfaceFileCounts"], {"wotr": 27, "cah": 6})
        self.assertEqual(report["fileCount"], 33)
        self.assertEqual(report["unresolvedFiles"], [])
        self.assertEqual(report["unclassifiedBlocks"], [])
        rows = {row["virtualPath"]: row for row in report["files"]}
        self.assertEqual(
            rows["data/ini/createaherospecialpowers.ini"]["blockCounts"],
            {"SpecialPower": 143},
        )
        self.assertEqual(
            rows["data/ini/mappedimages/aptimages/strategicimages.ini"][
                "blockCounts"
            ],
            {"MappedImage": 72},
        )
        self.assertEqual(
            rows["maps/createahero/map.ini"]["blockCounts"],
            {"Object": 2, "ShadowMap": 1, "Weather": 1},
        )
        self.assertEqual(
            rows["data/ini/strategichud.ini"]["blockCounts"], {"StrategicHUD": 1}
        )


class RotwkStrategicCensusIntegrationTests(_RealCatalogTestsBase):
    game = "rotwk"

    def test_measured_surface(self) -> None:
        report = self.report
        # The canonical rotwk catalog is the layered expansion+base install
        # (the engine mounts the base game's archives at runtime), so the
        # base-only CaH map document resolves and archives carry their
        # layer-<n> install prefix.
        self.assertEqual(report["surfaceFileCounts"], {"wotr": 29, "cah": 6})
        self.assertEqual(report["fileCount"], 35)
        self.assertEqual(report["unresolvedFiles"], [])
        self.assertEqual(report["unclassifiedBlocks"], [])
        rows = {row["virtualPath"]: row for row in report["files"]}
        special = rows["data/ini/createaherospecialpowers.ini"]
        self.assertEqual(special["blockCounts"], {"SpecialPower": 147})
        # The 2.01 patch archive must win precedence for this document.
        self.assertEqual(
            special["archive"].rsplit("/", 1)[-1], "_patch201ini.big"
        )
        self.assertEqual(
            rows["data/ini/mappedimages/aptimages/strategicimages.ini"][
                "blockCounts"
            ],
            {"MappedImage": 77},
        )
        self.assertIn(
            "data/ini/livingworldicons/angmaricons.ini", rows
        )


if __name__ == "__main__":
    unittest.main()
