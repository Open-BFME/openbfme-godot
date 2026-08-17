"""Official RotWK map-corpus pins against the real layered catalog.

These tests exist so a future cook cannot silently reintroduce the three
defects the shipped ``goal-official-72`` catalog carried at once: WOTR battle
maps categorised as skirmish, directory-derived display names ("Fall Back 4p"
for retail's "Stonewain Valley"), and unbound map art.

They are READ-ONLY: the cached catalog JSON is loaded with
``InstallCatalog.load`` and never rebuilt or saved, and no map payload bytes
are read.  They skip cleanly on machines without the retail state root.
"""

from __future__ import annotations

from pathlib import Path
import unittest

from openbfme_importer.catalog import InstallCatalog
from openbfme_importer.map_census import (
    MAPCACHE_VIRTUAL_PATH,
    MAX_MAPCACHE_BYTES,
    load_map_display_names,
    parse_mapcache_bytes,
    resolve_map_display_name,
)
from openbfme_importer.map_profile import (
    MAP_SETS,
    discover_catalog_only_map_targets,
    discover_registry_map_targets,
)

REPO_ROOT = Path(__file__).resolve().parents[2]
CATALOG_PATH = REPO_ROOT / "workspace" / "retail-work" / "catalog" / "rotwk.json"

#: Retail authors these names; title-casing the directory produces the WRONG
#: string, not merely an ugly one.  Keyed by retail map directory.
AUTHORED_NAME_PINS = {
    "map mp fall back 4p": "Stonewain Valley",
    "map mp fall back 8p": "Nanduhirion",
    "map mp the heubris": "East Bight",
    "map mp tournament mp1": "Tournament Snow",
}


def _load_catalog_read_only() -> InstallCatalog:
    catalog = InstallCatalog.load(CATALOG_PATH)
    install_root = Path(catalog.install_root)
    if not install_root.is_dir():
        raise unittest.SkipTest(
            f"catalog install root is not present: {install_root}"
        )
    return catalog


@unittest.skipUnless(
    CATALOG_PATH.is_file(), "retail RotWK catalog state is not present"
)
class OfficialRotwkMapCorpusTests(unittest.TestCase):
    """Pins over the frozen retail registry - the facts a cook must reproduce."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.catalog = _load_catalog_read_only()
        entry = cls.catalog.resolve_exact(MAPCACHE_VIRTUAL_PATH)
        assert entry is not None
        archive = cls.catalog.open_archive_for(entry)
        cls.registry = parse_mapcache_bytes(
            archive.read_entry(
                cls.catalog.as_entry(entry), max_bytes=MAX_MAPCACHE_BYTES
            )
        )
        cls.official_mp = [
            row
            for row in cls.registry
            if bool(row["isMultiplayer"])
            and bool(row["isOfficial"])
            and not bool(row["isScenarioMp"])
        ]
        cls.display_names = load_map_display_names(cls.catalog)

    def test_official_corpus_is_72_maps_split_22_skirmish_50_wotr(self) -> None:
        targets, rejections = discover_registry_map_targets(
            self.catalog, categories=MAP_SETS["playable"]
        )
        self.assertEqual(len(self.official_mp), 72)
        self.assertEqual(len(targets), 72)
        self.assertEqual(rejections, ())
        categories = {}
        for target in targets:
            categories[target.category] = categories.get(target.category, 0) + 1
        self.assertEqual(categories, {"skirmish": 22, "wotr-battle": 50})

    def test_every_official_map_resolves_an_authored_display_name(self) -> None:
        # derivedFromDirectoryCount == 0 for the official corpus: a catalog row
        # whose name had to be invented from the directory is a regression.
        targets, _ = discover_registry_map_targets(
            self.catalog, categories=MAP_SETS["playable"]
        )
        derived = sorted(
            target.slug
            for target in targets
            if target.display_name_source != "authored"
        )
        self.assertEqual(derived, [])

    def test_the_four_directory_derivation_lies_stay_pinned(self) -> None:
        by_directory = {
            str(row["virtualPath"]).split("/")[-2]: row for row in self.official_mp
        }
        for directory, expected in AUTHORED_NAME_PINS.items():
            row = by_directory.get(directory)
            self.assertIsNotNone(row, f"registry lost {directory!r}")
            authored = resolve_map_display_name(
                self.display_names, str(row.get("displayNameKey") or "")
            )
            self.assertEqual(
                authored,
                expected,
                f"{directory!r} must resolve retail's authored name, "
                "never a directory derivation",
            )

    def test_every_official_map_ships_art_and_preview_companions(self) -> None:
        missing = []
        for row in self.official_mp:
            virtual = str(row["virtualPath"])
            stem = virtual[: -len(".map")]
            for suffix in ("_art.tga", "_pic.tga"):
                if self.catalog.resolve_exact(stem + suffix) is None:
                    missing.append(stem + suffix)
        self.assertEqual(
            missing,
            [],
            "retail ships art+preview for every official multiplayer map; "
            "an emission lane that leaves any row artless is a regression",
        )

    def test_the_two_harlindons_stay_distinct(self) -> None:
        # ``map mp harlindon`` and ``map wor harlindon`` are DIFFERENT maps
        # that both strip to "harlindon"; any lane that collapses them (map
        # ids or kind-stripped art fallbacks) hands one map the other's art.
        targets, _ = discover_registry_map_targets(
            self.catalog, categories=MAP_SETS["playable"]
        )
        harlindons = {
            target.slug: target.category
            for target in targets
            if "harlindon" in target.slug
        }
        self.assertEqual(
            harlindons,
            {"harlindon": "skirmish", "wor-harlindon": "wotr-battle"},
        )

    def test_unregistered_playable_payloads_are_exactly_the_known_two(self) -> None:
        # The layered install ships two playable-category payloads that no
        # registry row names.  A fresh full-profile cook with default
        # discovery ADMITS them (74 maps, not 72) - pin the set so a corpus
        # change is a decision, never a surprise.
        extra, _ = discover_catalog_only_map_targets(
            self.catalog, categories=MAP_SETS["playable"]
        )
        self.assertEqual(
            [(target.slug, target.category, target.display_name_source) for target in extra],
            [
                ("weather-hills", "skirmish", "derived-from-directory"),
                ("wor-gondor", "wotr-battle", "derived-from-directory"),
            ],
        )


if __name__ == "__main__":
    unittest.main()
