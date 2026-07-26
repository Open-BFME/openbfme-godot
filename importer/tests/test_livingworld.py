"""Tests for the fail-closed War-of-the-Ring (Living World) strategic profile.

The unit tests run against fixtures authored *in this file* — no retail text is
packaged.  The integration tests at the bottom read the private retail catalogs
and skip when they are unavailable, exactly like the other census tests.
"""

from __future__ import annotations

import unittest

from openbfme_importer.catalog import CatalogEntry, InstallCatalog
from openbfme_importer.livingworld import (
    _CAMPAIGN_OPENERS,
    _EFFECT_OPENERS,
    ENTRY_POINTS,
    SCHEMA,
    SCHEMA_VERSION,
    _as_milli,
    _point,
    _resolve_include,
    expand_document,
    flatten_document,
    profile_living_world,
    read_tree,
)
from openbfme_importer.paths import repo_root_from_module


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


def _reader(files: dict[str, bytes]):
    def read(virtual_path: str) -> bytes:
        try:
            return files[virtual_path.casefold()]
        except KeyError as exc:  # pragma: no cover - guarded by the tests
            raise ValueError(f"missing document: {virtual_path}") from exc

    return read


def _tree(source: bytes, *, openers=_CAMPAIGN_OPENERS, whitespace_pairs: bool = False):
    files = {"data/ini/x.ini": source}
    gaps: list = []
    lines = flatten_document(
        expand_document("data/ini/x.ini", _reader(files)),
        openers=openers,
        whitespace_pairs=whitespace_pairs,
        gaps=gaps,
    )
    parsed = read_tree(lines, openers=openers, whitespace_pairs=whitespace_pairs)
    return parsed, gaps


# A minimal but structurally faithful stand-in for the retail region graph:
# two mutually connected regions, one detour polyline, one building restriction.
REGION_SOURCE = b"""
LivingWorldRegionCampaign TestCampaign
\tRegionEffectsManagerName = TestRegionEffects
\tHeroOnlyArmyCommandPoints = 0
\tSmallArmyCommandPoints = 120
\tArmyRetreatRounds = 25
\tArmyPlacementPos = X:-85 Y:48

\tRegion North
\t\tDisplayName = LW:DisplayNameNorth
\t\tMapName = "MAP TEST North"
\t\tSubObject = North
\t\tResourceBonus = 10
\t\tCPLimit = 600
\t\tAllyCPLimit = 360
\t\tCreateAutoFort = Yes
\t\tCustomCenterPoint = Yes
\t\tCenterPoint = X:-760 Y:2000
\t\tHeroArmySpot = X:-910 Y:2075
\t\tGarrisonArmySpot = X:-760 Y:1900
\t\tConnectsTo =
\t\tConnection
\t\tRegion = South
\t\t\tDetourPoint = X:-505 Y:1860
\t\t\tDetourPoint = X:-505 Y:1683
\t\tEnd
\t\tRestrictBuildings
\t\t\tBuildings = Fortress
\t\t\tNumberAllowed = 1
\t\tEnd
\tEnd

\tRegion South
\t\tDisplayName = LW:DisplayNameSouth
\t\tMapName = "MAP TEST South"
\t\tSubObject = South
\t\tCPLimit = 480
\t\tAllyCPLimit = 360
\t\tCenterPoint = X:-355 Y:1563
\t\tConnectsTo =
\t\tConnection
\t\tRegion = North
\t\t\tDetourPoint = X:-505 Y:1683
\t\tEnd
\tEnd

\tConcurrentRegionBonus
\t\tTerritory = LW:TerritoryTest
\t\tEffectName = TestTerritory
\t\tRegions = North South
\t\tExperienceBonus = 20
\t\tLookAtCenter = X:-1121 Y:2072
\t\tLookAtZoom = 0.55
\tEnd
End
"""


class ReaderTests(unittest.TestCase):
    def test_region_graph_parses_with_nested_connections(self) -> None:
        parsed, gaps = _tree(REGION_SOURCE)
        self.assertEqual(gaps, [])
        self.assertEqual(parsed.gaps, ())
        self.assertEqual(len(parsed.roots), 1)
        campaign = parsed.roots[0]
        self.assertEqual(campaign.kind, "LivingWorldRegionCampaign")
        self.assertEqual(campaign.name, "TestCampaign")
        regions = campaign.blocks("Region")
        self.assertEqual([region.name for region in regions], ["North", "South"])
        north = regions[0]
        # ``Region = South`` inside a Connection is a FIELD, not a nested block:
        # only the ``=`` distinguishes it from the ``Region North`` header.
        connection = north.blocks("Connection")[0]
        self.assertEqual(connection.value("Region"), "South")
        self.assertEqual(connection.children, ())
        self.assertEqual(len(connection.values("DetourPoint")), 2)
        # ``ConnectsTo =`` is an authored empty assignment and must not open a
        # block; if it did, the RestrictBuildings block would be misparented.
        self.assertEqual(north.values("ConnectsTo"), ("",))
        self.assertEqual(len(north.blocks("RestrictBuildings")), 1)

    def test_unknown_block_keeps_end_balance_and_reaches_the_caller(self) -> None:
        parsed, gaps = _tree(
            b"Region A\n\tFrobnicator\n\t\tValue = 1\n\tEnd\n\tMapName = M\nEnd\n"
        )
        self.assertEqual(gaps, [])
        region = parsed.roots[0]
        # The unknown block is tracked (so ``MapName`` still lands on Region),
        # and it is handed to the caller rather than swallowed by the reader.
        self.assertEqual(region.value("MapName"), "M")
        self.assertEqual([child.kind for child in region.children], ["Frobnicator"])

    def test_repeated_single_valued_field_fails_closed(self) -> None:
        parsed, _gaps = _tree(b"Region A\n\tMapName = One\n\tMapName = Two\nEnd\n")
        with self.assertRaises(ValueError):
            parsed.roots[0].value("MapName")
        self.assertEqual(parsed.roots[0].values("MapName"), ("One", "Two"))

    def test_unterminated_block_raises(self) -> None:
        files = {"data/ini/x.ini": b"Region A\n\tMapName = M\n"}
        gaps: list = []
        lines = flatten_document(
            expand_document("data/ini/x.ini", _reader(files)),
            openers=_CAMPAIGN_OPENERS,
            whitespace_pairs=False,
            gaps=gaps,
        )
        # The imbalance is caught by the per-document check before the tree
        # reader ever sees it, so the document is quarantined, not guessed at.
        self.assertEqual(lines, [])
        self.assertEqual(len(gaps), 1)
        self.assertEqual(gaps[0].reason, "unbalanced-document")

    def test_top_level_assignments_are_returned_as_tree_fields(self) -> None:
        parsed, gaps = _tree(b"SecondsPerReinforcement = 900\nStartingCashRTS = 6000\n")
        self.assertEqual(gaps, [])
        self.assertEqual(parsed.roots, ())
        self.assertEqual(
            parsed.fields,
            (("SecondsPerReinforcement", "900"), ("StartingCashRTS", "6000")),
        )

    def test_region_effects_dialect_reads_whitespace_pairs(self) -> None:
        parsed, gaps = _tree(
            b"LivingWorldRegionEffects TestEffects\n"
            b"\tRegionObject = LMR_Fill\n"
            b"\tNeutralRegionColor = R:245 G:245 B:245\n"
            b"\tBordersEffect\n"
            b"\t\tGeometry\t\tLMR_Border\n"
            b"\t\tColorIntensityControlPoint\n"
            b"\t\t\tIntensity 1.0\n"
            b"\t\t\tTime 0.0\n"
            b"\t\tEnd\n"
            b"\tEnd\n"
            b"End\n",
            openers=_EFFECT_OPENERS,
            whitespace_pairs=True,
        )
        self.assertEqual(gaps, [])
        self.assertEqual(parsed.gaps, ())
        effects = parsed.roots[0]
        borders = effects.blocks("BordersEffect")[0]
        self.assertEqual(borders.value("Geometry"), "LMR_Border")
        point = borders.blocks("ColorIntensityControlPoint")[0]
        self.assertEqual(point.value("Intensity"), "1.0")


class IncludeTests(unittest.TestCase):
    def test_include_paths_resolve_relative_to_the_including_document(self) -> None:
        self.assertEqual(
            _resolve_include(
                "data/ini/campaigns/riskcampaign.ini", ' "Common\\Regions.inc"'
            ),
            "data/ini/campaigns/common/regions.inc",
        )
        self.assertEqual(
            _resolve_include(
                "data/ini/campaigns/scenarios/s1.inc", ' "..\\Common\\Rts.inc"'
            ),
            "data/ini/campaigns/common/rts.inc",
        )

    def test_include_escaping_the_tree_raises(self) -> None:
        with self.assertRaises(ValueError):
            _resolve_include("a.ini", ' "..\\..\\evil.inc"')
        with self.assertRaises(ValueError):
            _resolve_include("a.ini", ' "C:\\evil.inc"')

    def test_included_lines_are_spliced_in_place(self) -> None:
        files = {
            "data/ini/top.ini": b'Campaign C\n#include "sub/inner.inc"\n\tTail = 1\nEnd\n',
            "data/ini/sub/inner.inc": b"\tHead = 1\n",
        }
        gaps: list = []
        lines = flatten_document(
            expand_document("data/ini/top.ini", _reader(files)),
            openers=frozenset({"campaign"}),
            whitespace_pairs=False,
            gaps=gaps,
        )
        parsed = read_tree(lines, openers=frozenset({"campaign"}))
        self.assertEqual(gaps, [])
        self.assertEqual(
            parsed.roots[0].fields, (("Head", "1"), ("Tail", "1"))
        )

    def test_cyclic_include_raises(self) -> None:
        files = {
            "data/ini/a.inc": b'#include "b.inc"\n',
            "data/ini/b.inc": b'#include "a.inc"\n',
        }
        with self.assertRaises(ValueError):
            expand_document("data/ini/a.inc", _reader(files))

    def test_missing_include_raises_but_missing_entry_point_is_a_gap(self) -> None:
        files = {"data/ini/a.inc": b'#include "missing.inc"\n'}
        with self.assertRaises(ValueError):
            expand_document("data/ini/a.inc", _reader(files))
        report = profile_living_world(_FakeCatalog({}), "bfme2")
        unresolved = sorted(
            row["detail"] for row in report["gaps"] if row["reason"] == "unresolved-file"
        )
        self.assertEqual(unresolved, sorted(ENTRY_POINTS))
        self.assertEqual(report["regionCount"], 0)

    def test_unbalanced_include_is_quarantined_without_corrupting_siblings(self) -> None:
        # Retail ships exactly this shape: RotWK 2.01's WOTRScenario044.inc
        # authors ``SpawnArmies = HeroArmy3`` where a ``SpawnArmies`` BLOCK was
        # meant, leaving one stray ``End``.  Splicing it in would reparent every
        # later block; quarantining the file keeps the rest of the graph true.
        files = {
            "data/ini/top.ini": (
                b'Campaign C\n\tGood = 1\n#include "broken.inc"\n#include "fine.inc"\nEnd\n'
            ),
            "data/ini/broken.inc": b"\tOwnershipSet\n\t\tRegions = A\n\tEnd\n\tEnd\n",
            "data/ini/fine.inc": b"\tAlsoGood = 1\n",
        }
        gaps: list = []
        lines = flatten_document(
            expand_document("data/ini/top.ini", _reader(files)),
            openers=frozenset({"campaign", "ownershipset"}),
            whitespace_pairs=False,
            gaps=gaps,
        )
        parsed = read_tree(lines, openers=frozenset({"campaign", "ownershipset"}))
        self.assertEqual([gap.reason for gap in gaps], ["unbalanced-document"])
        self.assertEqual(gaps[0].virtual_path, "data/ini/broken.inc")
        self.assertEqual(len(parsed.roots), 1)
        self.assertEqual(
            parsed.roots[0].fields, (("Good", "1"), ("AlsoGood", "1"))
        )
        self.assertEqual(parsed.roots[0].children, ())


class ValueTests(unittest.TestCase):
    def test_points_parse_and_reject_malformed_shapes(self) -> None:
        self.assertEqual(_point("X:-505 Y:1860"), {"x": -505, "y": 1860})
        for bad in ("X:-505", "Y:1 X:2", "X:a Y:1", "-505 1860"):
            with self.assertRaises(ValueError, msg=bad):
                _point(bad)

    def test_decimals_become_exact_thousandths_never_floats(self) -> None:
        self.assertEqual(_as_milli("2.0"), 2000)
        self.assertEqual(_as_milli("0.55"), 550)
        self.assertEqual(_as_milli("-0.47"), -470)
        self.assertIsInstance(_as_milli("1.0"), int)
        # Finer than a thousandth would have to be rounded, so it fails closed.
        with self.assertRaises(ValueError):
            _as_milli("0.0005")
        with self.assertRaises(ValueError):
            _as_milli("many")


SCENARIO_SOURCE = b"""LivingWorldCampaign TestScenario
\tIsEvilCampaign = No
\tScenario
\t\tDisplayName = LWScenario:Test
\t\tRegionCampaign = TestCampaign
\t\tMaxPlayers = 6
\t\tDefaultStartSpots = North South
\t\tPlayerDefeatCondition
\t\t\tTeams = 1 2
\t\t\tLoseIfCapitalLost = Yes
\t\t\tNumControlledRegionsLessOrEqualTo = 0
\t\tEnd
\t\tOwnershipSet
\t\t\tRegions = North
\t\t\tStartRegion = North
\t\t\tSpawnArmies
\t\t\t\tArmies = HeroArmy1
\t\t\t\tRegion = North
\t\t\tEnd
\t\tEnd
\tEnd
\tAct One
\t\tSpawnArmy
\t\t\tScriptingName = HeroArmy1
\t\t\tSpawnForTemplates = PlayerTest
\t\t\tHeroTemplateName = TestHero
\t\t\tPlayerArmy = TestHeroArmy
\t\t\tIcon = HeroTestIcon
\t\tEnd
\tEnd
End
"""


def _campaign_catalog(**overrides: bytes) -> _FakeCatalog:
    """Build a complete synthetic Living World install for the profile."""

    files: dict[str, bytes] = {
        "data/ini/campaigns/riskcampaign.ini": (
            b'#include "Common/Regions.inc"\n'
            b'#include "Scenarios/Test.inc"\n'
            b"LivingWorldPlayerArmy\n"
            b"\tName = TestHeroArmy\n"
            b"\tDisplayNameTag = LWA:TestHeroArmy\n"
            b"\tArmyEntry\n"
            b"\t\tThingTemplate = TestHero\n"
            b"\t\tQuantity = 1\n"
            b"\tEnd\n"
            b"End\n"
        ),
        "data/ini/campaigns/common/regions.inc": REGION_SOURCE,
        "data/ini/campaigns/scenarios/test.inc": SCENARIO_SOURCE,
        "data/ini/livingworldregions.ini": b"",
        "data/ini/livingworldregioneffects.ini": (
            b"LivingWorldRegionEffects TestRegionEffects\n"
            b"\tRegionObject = LMR_Fill\n"
            b"\tRegionBorderColor = R:30 G:6 B:6\n"
            b"\tBordersEffect\n"
            b"\t\tGeometry\t\tLMR_Border\n"
            b"\tEnd\n"
            b"End\n"
        ),
        "data/ini/livingworldplayers.ini": (
            b"LivingWorldPlayerTemplate PlayerTest\n"
            b"\tFaction = FactionTest\n"
            b"\tStartingWorldCP = 1500\n"
            b"\tMaxWorldCP = 4500\n"
            b"\tStartingHeroCP = 450\n"
            b"\tMaxHeroCP = 450\n"
            b"End\n"
        ),
        "data/ini/campaigns/common/livingworldcities.inc": (
            b"SpawnArmy\n"
            b"\tPlayerArmy = North_PlayerArmy\n"
            b"\tIcon = City_Large\n"
            b"\tPalantirMovie = Palantir_001\n"
            b"\tPosition = X:-850 Y:1300\n"
            b"\tIsCity = Yes\n"
            b"End\n"
        ),
        "data/ini/campaigns/common/livingworlddefaultrtssettings.inc": (
            b"SecondsPerReinforcement = 900\n"
            b"StartingCashRTS = 6000\n"
            b"StartingCashRTSWithFort = 1000\n"
            b"InitialRevivalCostMultiplier = 2.0\n"
            b"InitialRevivalTimeMultiplier = 1.0\n"
        ),
    }
    files.update({path.casefold(): source for path, source in overrides.items()})
    return _FakeCatalog(files)


class ProfileTests(unittest.TestCase):
    def test_profile_carries_the_whole_strategic_surface(self) -> None:
        report = profile_living_world(_campaign_catalog(), "bfme2")
        self.assertEqual(report["schema"], SCHEMA)
        self.assertEqual(report["schemaVersion"], SCHEMA_VERSION)
        self.assertEqual(report["game"], "bfme2")
        self.assertEqual(report["gaps"], [])

        self.assertEqual(report["regionCount"], 2)
        self.assertEqual(report["connectionCount"], 2)
        campaign = report["regionCampaigns"][0]
        self.assertEqual(campaign["name"], "TestCampaign")
        self.assertEqual(campaign["armyIconCommandPoints"], {"heroOnly": 0, "small": 120})
        self.assertEqual(campaign["armyRetreatRounds"], 25)
        self.assertEqual(campaign["armyPlacementOffsets"], [{"x": -85, "y": 48}])

        north = campaign["regions"][0]
        self.assertEqual(north["id"], "North")
        self.assertEqual(north["mapName"], "MAP TEST North")
        self.assertEqual(north["cpLimit"], 600)
        self.assertEqual(north["allyCpLimit"], 360)
        self.assertTrue(north["createAutoFort"])
        self.assertEqual(north["centerPoint"], {"x": -760, "y": 2000})
        self.assertEqual(north["bonuses"]["resource"], 10)
        self.assertEqual(
            north["connections"],
            [
                {
                    "region": "South",
                    "detourPoints": [{"x": -505, "y": 1860}, {"x": -505, "y": 1683}],
                }
            ],
        )
        self.assertEqual(
            north["restrictBuildings"],
            [{"buildings": ["Fortress"], "numberAllowed": 1}],
        )

        territory = report["territoryBonuses"][0]
        self.assertEqual(territory["effectName"], "TestTerritory")
        self.assertEqual(territory["regions"], ["North", "South"])
        self.assertEqual(territory["bonuses"]["experience"], 20)
        # Decimals are exact thousandths so the Godot layer can hash them.
        self.assertEqual(territory["lookAtZoomMilli"], 550)

        self.assertEqual(
            report["rtsSettings"],
            {
                "secondsPerReinforcement": 900,
                "startingCashRts": 6000,
                "startingCashRtsWithFort": 1000,
                "initialRevivalCostMilli": 2000,
                "initialRevivalTimeMilli": 1000,
            },
        )

        scenario = report["scenarios"][0]
        self.assertEqual(scenario["maxPlayers"], 6)
        self.assertEqual(scenario["regionCampaign"], "TestCampaign")
        self.assertEqual(scenario["defaultStartSpots"], ["North", "South"])
        self.assertEqual(scenario["ownershipSets"][0]["startRegion"], "North")
        self.assertEqual(
            scenario["ownershipSets"][0]["spawnArmies"],
            [{"armies": ["HeroArmy1"], "region": "North"}],
        )
        self.assertEqual(scenario["playerDefeatConditions"][0]["teams"], [1, 2])

        self.assertEqual(len(report["defaultArmies"]), 1)
        self.assertEqual(report["defaultArmies"][0]["heroTemplateName"], "TestHero")
        self.assertEqual(len(report["cities"]), 1)
        self.assertTrue(report["cities"][0]["isCity"])
        self.assertEqual(
            report["playerArmies"][0]["entries"],
            [{"thingTemplate": "TestHero", "quantity": 1}],
        )
        self.assertEqual(
            report["playerTemplates"],
            [
                {
                    "name": "PlayerTest",
                    "faction": "FactionTest",
                    "factionIcon": "",
                    "factionDozerTemplateName": "",
                    "factionInnUnitTemplateName": "",
                    "defaultArmyIconName": "",
                    "buildPlotIconName": "",
                    "buildPlotSelectionPortraitName": "",
                    "garrisonSelectionPortraitName": "",
                    "garrisonDisplayNameTag": "",
                    "music": "",
                    "autoResolveLoop": "",
                    "startingWorldCp": 1500,
                    "maxWorldCp": 4500,
                    "startingHeroCp": 450,
                    "maxHeroCp": 450,
                    "scenarioStartResources": -1,
                }
            ],
        )
        self.assertEqual(report["regionEffects"][0]["name"], "TestRegionEffects")
        self.assertEqual(
            report["regionEffects"][0]["colors"],
            {"regionBorder": {"r": 30, "g": 6, "b": 6}},
        )

    def test_profile_is_deterministic(self) -> None:
        first = profile_living_world(_campaign_catalog(), "bfme2")
        second = profile_living_world(_campaign_catalog(), "bfme2")
        self.assertEqual(first, second)

    def test_unknown_region_field_is_recorded_not_dropped(self) -> None:
        source = REGION_SOURCE.replace(
            b"\t\tResourceBonus = 10\n", b"\t\tResourceBonus = 10\n\t\tFrobnicator = 7\n"
        )
        report = profile_living_world(
            _campaign_catalog(**{"data/ini/campaigns/common/regions.inc": source}),
            "bfme2",
        )
        gaps = [row for row in report["gaps"] if row["detail"] == "Frobnicator"]
        self.assertEqual(len(gaps), 1)
        self.assertEqual(gaps[0]["reason"], "unknown-field")
        self.assertEqual(gaps[0]["scope"], "Region")
        # The rest of the region survives the unknown field untouched.
        north = report["regionCampaigns"][0]["regions"][0]
        self.assertEqual(north["bonuses"]["resource"], 10)
        self.assertEqual(report["regionCount"], 2)

    def test_unmodelled_block_is_recorded_not_dropped(self) -> None:
        source = REGION_SOURCE.replace(
            b"\t\tResourceBonus = 10\n",
            b"\t\tResourceBonus = 10\n\t\tMysteryBlock\n\t\t\tValue = 1\n\t\tEnd\n",
        )
        report = profile_living_world(
            _campaign_catalog(**{"data/ini/campaigns/common/regions.inc": source}),
            "bfme2",
        )
        gaps = [row for row in report["gaps"] if row["detail"] == "MysteryBlock"]
        self.assertEqual(len(gaps), 1)
        self.assertEqual(gaps[0]["reason"], "unmodelled-block")
        self.assertEqual(report["regionCount"], 2)

    def test_bonus_macro_is_kept_verbatim_and_gapped(self) -> None:
        source = REGION_SOURCE.replace(
            b"\t\tResourceBonus = 10\n",
            b"\t\tFertileTerritoryBonus = FERTILE_TERRITORY_BONUS\n",
        )
        report = profile_living_world(
            _campaign_catalog(**{"data/ini/campaigns/common/regions.inc": source}),
            "bfme2",
        )
        north = report["regionCampaigns"][0]["regions"][0]
        self.assertEqual(
            north["bonusMacros"], {"fertileTerritory": "FERTILE_TERRITORY_BONUS"}
        )
        self.assertEqual(north["bonuses"]["fertileTerritory"], 0)
        self.assertEqual(
            [row["reason"] for row in report["gaps"]], ["unexpanded-macro"]
        )

    def test_dangling_and_one_way_connections_are_gapped(self) -> None:
        source = REGION_SOURCE.replace(b"\t\tRegion = South\n", b"\t\tRegion = Nowhere\n")
        report = profile_living_world(
            _campaign_catalog(**{"data/ini/campaigns/common/regions.inc": source}),
            "bfme2",
        )
        reasons = {row["reason"] for row in report["gaps"]}
        self.assertIn("dangling-connection", reasons)
        self.assertIn("one-way-connection", reasons)

    def test_scenario_referencing_an_unknown_region_is_gapped(self) -> None:
        report = profile_living_world(
            _campaign_catalog(
                **{
                    "data/ini/campaigns/scenarios/test.inc": SCENARIO_SOURCE.replace(
                        b"StartRegion = North", b"StartRegion = Atlantis"
                    )
                }
            ),
            "bfme2",
        )
        gaps = [
            row
            for row in report["gaps"]
            if row["reason"] == "unresolved-region-reference"
        ]
        self.assertEqual([row["detail"] for row in gaps], ["Atlantis"])

    def test_unsupported_game_fails_closed(self) -> None:
        with self.assertRaises(ValueError):
            profile_living_world(_campaign_catalog(), "bfme1")


class _RealCatalogTestsBase(unittest.TestCase):
    game = ""

    @classmethod
    def setUpClass(cls) -> None:
        if not cls.game:
            raise unittest.SkipTest("abstract base")
        path = (
            repo_root_from_module()
            / ".private"
            / "retail-work"
            / "catalog"
            / f"{cls.game}.json"
        )
        if not path.is_file():
            raise unittest.SkipTest(f"private {cls.game} catalog is unavailable")
        cls.report = profile_living_world(InstallCatalog.load(path), cls.game)

    def _campaign(self, name: str) -> dict:
        for row in self.report["regionCampaigns"]:
            if row["name"] == name:
                return row
        self.fail(f"missing region campaign {name}")


class Bfme2LivingWorldIntegrationTests(_RealCatalogTestsBase):
    game = "bfme2"

    def test_measured_wotr_graph(self) -> None:
        default = self._campaign("DefaultCampaign")
        self.assertEqual(len(default["regions"]), 38)
        self.assertEqual(sum(len(r["connections"]) for r in default["regions"]), 158)
        self.assertEqual(len(default["territoryBonuses"]), 6)
        self.assertEqual(default["regionEffectsManagerName"], "WotRRegionEffects")
        # Seven of the 38 WOTR regions defend themselves without a built fort.
        self.assertEqual(
            sum(1 for region in default["regions"] if region["createAutoFort"]), 7
        )

    def test_measured_rts_handoff_settings(self) -> None:
        self.assertEqual(
            self.report["rtsSettings"],
            {
                "secondsPerReinforcement": 900,
                "startingCashRts": 6000,
                "startingCashRtsWithFort": 1000,
                "initialRevivalCostMilli": 2000,
                "initialRevivalTimeMilli": 1000,
            },
        )

    def test_measured_command_point_economy(self) -> None:
        templates = {row["name"]: row for row in self.report["playerTemplates"]}
        self.assertEqual(len(templates), 7)
        self.assertEqual(templates["PlayerMen"]["startingWorldCp"], 1500)
        self.assertEqual(templates["PlayerMen"]["maxWorldCp"], 4500)
        self.assertEqual(templates["PlayerMen"]["startingHeroCp"], 450)
        self.assertEqual(templates["PlayerMen"]["maxHeroCp"], 450)
        # The observer template carries no economy at all.
        self.assertEqual(templates["PlayerObserver"]["startingWorldCp"], -1)

    def test_measured_scenario_surface(self) -> None:
        self.assertEqual(self.report["scenarioCount"], 44)
        # WOTR caps at six players even though skirmish allows eight.
        self.assertEqual(
            max(row["maxPlayers"] for row in self.report["scenarios"]), 6
        )
        self.assertEqual(len(self.report["cities"]), 16)
        self.assertEqual(len(self.report["defaultArmies"]), 32)

    def test_every_gap_is_accounted_for(self) -> None:
        reasons = sorted({row["reason"] for row in self.report["gaps"]})
        self.assertEqual(
            reasons,
            [
                "presentation-only",  # EyeTowerPoints camera fluff
                "unknown-field",  # tutorial's scripted-campaign fields
                "unmodelled-block",  # tutorial AddPlayer / Tutorial blocks
                "unsupported-preprocessor",  # gamedata.ini building macros
            ],
        )


class RotwkLivingWorldIntegrationTests(_RealCatalogTestsBase):
    game = "rotwk"

    def test_expansion_grows_the_region_graph(self) -> None:
        default = self._campaign("DefaultCampaign")
        self.assertEqual(len(default["regions"]), 52)
        self.assertEqual(len(default["territoryBonuses"]), 7)

    def test_expansion_changes_the_rts_handoff(self) -> None:
        self.assertEqual(self.report["rtsSettings"]["secondsPerReinforcement"], 300)
        self.assertEqual(self.report["rtsSettings"]["startingCashRtsWithFort"], 1500)

    def test_expansion_adds_angmar_and_shared_victory_types(self) -> None:
        names = {row["name"] for row in self.report["playerTemplates"]}
        self.assertIn("PlayerAngmar", names)
        self.assertEqual(len(self.report["victoryTypes"]), 7)

    def test_retail_authoring_bug_is_quarantined_and_named(self) -> None:
        # RotWK 2.01 authors ``SpawnArmies = HeroArmy3`` in WOTRScenario044.inc
        # where a block was intended, leaving a stray ``End``.  The document is
        # dropped whole and named, rather than silently reparenting the rest.
        unbalanced = [
            row for row in self.report["gaps"] if row["reason"] == "unbalanced-document"
        ]
        self.assertEqual(
            [row["virtualPath"] for row in unbalanced],
            ["data/ini/campaigns/scenarios/wotrscenario044.inc"],
        )
        self.assertEqual(self.report["scenarioCount"], 14)


if __name__ == "__main__":
    unittest.main()
