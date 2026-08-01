from __future__ import annotations

import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

from openbfme_importer.catalog import InstallCatalog
from openbfme_importer.map_census import census_multiplayer_maps, parse_mapcache_bytes

from importer.tests.test_big import make_big


def _encoded_path(value: str) -> str:
    return "".join(
        character if character.isalnum() else f"_{ord(character):02X}"
        for character in value
    )


def _record(path: str, *, multiplayer: bool, players: int) -> str:
    return "\n".join(
        [
            f"MapCache {_encoded_path(path)}",
            f"  isMultiplayer = {'Yes' if multiplayer else 'No'}",
            "  isOfficial = Yes",
            "  isScenarioMP = No",
            f"  numPlayers = {players}",
            "End",
        ]
    )


def _facts(source: bytes) -> dict:
    return {
        "sourceBytes": len(source),
        "sourceSha256": "a" * 64,
        "bodySha256": "b" * 64,
        "envelopeKind": "uncompressed",
        "chunks": [
            {
                "name": "HeightMapData",
                "version": 5,
                "occurrences": 1,
                "payloadBytes": 10,
                "probeStatus": "parsed",
                "probeRejections": [],
            }
        ],
        "features": {
            "height": {"width": 3, "height": 2, "borderWidth": 1},
            "terrain": {},
            "objects": {},
            "standingWaterCount": 0,
            "riverCount": 0,
            "triggerAreaCount": 0,
            "standingWaveAreaCount": 0,
            "waypointPathCount": 0,
            "scriptListCount": 0,
            "nonemptyScriptListCount": 0,
        },
        "strictCook": {"accepted": True, "reason": None},
    }


def _parsed(
    *,
    declared_players: int = 4,
    duplicate_team_name: str | None = None,
    waypoint_edge_count: int = 0,
) -> SimpleNamespace:
    duplicate_teams = []
    if duplicate_team_name is not None:
        duplicate_teams.append(
            {"name": duplicate_team_name, "teamIndices": [3, 5]}
        )
    cross_checks = {
        "libraryPlayerCountMatches": True,
        "scriptPlayerCountMatches": True,
        "teamOwnersResolved": True,
        "defaultTeamsResolved": True,
        "teamNamesUnique": duplicate_team_name is None,
        "playerStartsContiguous": True,
        "startCountWithinLobbySlots": True,
        "startCountWithinScenarioPlayers": True,
        "waypointEdgeEndpointsResolved": True,
    }
    return SimpleNamespace(
        setup={
            "declaredPlayerCount": declared_players,
            "lobbySlotCount": 8,
            "scenarioPlayerCount": 14,
            "teamCount": 16,
            "extraTeamCount": 2,
            "duplicateTeamNames": duplicate_teams,
            "scriptListCount": 14,
            "nonemptyScriptListCount": 1,
            "libraryMapLists": [
                {
                    "index": 0,
                    "references": [
                        {
                            "identitySha256": "c" * 64,
                            "normalized": "source-name-not-for-census",
                        }
                    ],
                }
            ],
            "libraryDependencyStatus": "references-preserved-not-flattened",
            "libraryCycleValidationStatus": "pending-external-dependency-graph",
            "crossChecks": cross_checks,
        },
        waypoints=[{"id": index} for index in range(6)],
        player_starts={
            f"Player_{index + 1}_Start": {"playerIndex": index + 1}
            for index in range(declared_players)
        },
        waypoint_edges=[
            {"startId": index, "endId": index + 1}
            for index in range(waypoint_edge_count)
        ],
    )


class MultiplayerMapCensusTests(unittest.TestCase):
    def test_registry_selects_shipped_multiplayer_and_reports_stale_record(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            shipped = "maps/map wor shipped/map wor shipped.map"
            missing = "maps/map mp missing/map mp missing.map"
            solo = "maps/map solo/map solo.map"
            mapcache = "\n".join(
                [
                    _record(shipped, multiplayer=True, players=4),
                    _record(missing, multiplayer=True, players=2),
                    _record(solo, multiplayer=False, players=1),
                ]
            ).encode("cp1252")
            make_big(
                root / "maps.big",
                {
                    "maps/mapcache.ini": mapcache,
                    shipped: b"synthetic-map",
                    solo: b"solo-map",
                },
            )
            catalog = InstallCatalog.build(root)
            with mock.patch(
                "openbfme_importer.map_census.census_sage_map_bytes",
                side_effect=_facts,
            ), mock.patch(
                "openbfme_importer.map_census.parse_sage_map_bytes",
                return_value=_parsed(),
            ):
                first = census_multiplayer_maps(catalog)
                second = census_multiplayer_maps(catalog)

            self.assertEqual(first["scope"]["registryRecordCount"], 3)
            self.assertEqual(first["scope"]["registryMultiplayerRecordCount"], 2)
            self.assertEqual(first["scope"]["selectedMapCount"], 1)
            self.assertEqual(first["scope"]["staleMissingPayloadCount"], 1)
            self.assertEqual(first["maps"][0]["virtualPath"], shipped)
            self.assertEqual(first["maps"][0]["registry"]["playerCount"], 4)
            self.assertEqual(
                first["staleRegistryRecords"][0]["status"],
                "registry-stale-missing-payload",
            )
            setup = first["maps"][0]["setupNavigation"]
            self.assertEqual(setup["registryPlayerCapacity"], 4)
            self.assertEqual(setup["declaredPlayerCount"], 4)
            self.assertEqual(setup["lobbySlotCount"], 8)
            self.assertEqual(setup["scenarioPlayerCount"], 14)
            self.assertEqual(setup["teamCount"], 16)
            self.assertEqual(setup["extraTeamCount"], 2)
            self.assertEqual(setup["playerStartCount"], 4)
            self.assertEqual(setup["waypointCount"], 6)
            self.assertEqual(setup["waypointEdgeCount"], 0)
            self.assertEqual(setup["scriptListCount"], 14)
            self.assertEqual(setup["nonemptyScriptListCount"], 1)
            self.assertEqual(setup["libraryListCount"], 1)
            self.assertEqual(setup["libraryReferenceCount"], 1)
            self.assertEqual(
                setup["libraryDependencyStatus"],
                "references-preserved-not-flattened",
            )
            self.assertEqual(
                setup["navigationMeshStatus"],
                "not-generated-or-validated-by-census",
            )
            summary = first["summary"]["setupNavigation"]
            self.assertEqual(summary["validatedMapCount"], 1)
            self.assertEqual(summary["unavailableMapCount"], 0)
            self.assertEqual(summary["registryCapacityMismatchMapCount"], 0)
            self.assertEqual(summary["libraryDependencies"]["referenceCount"], 1)
            self.assertEqual(summary["navigation"]["waypointEdgeCount"], 0)
            self.assertEqual(
                json.dumps(first, sort_keys=True), json.dumps(second, sort_keys=True)
            )

    def test_duplicate_team_name_quirk_remains_a_neutral_census_fact(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            shipped = "maps/map mp gap of rohan/map mp gap of rohan.map"
            mapcache = _record(shipped, multiplayer=True, players=2).encode("cp1252")
            make_big(
                root / "maps.big",
                {"maps/mapcache.ini": mapcache, shipped: b"synthetic-map"},
            )
            catalog = InstallCatalog.build(root)
            with mock.patch(
                "openbfme_importer.map_census.census_sage_map_bytes",
                side_effect=_facts,
            ), mock.patch(
                "openbfme_importer.map_census.parse_sage_map_bytes",
                return_value=_parsed(
                    declared_players=2,
                    duplicate_team_name="SOURCE_TEAM_NAME_MUST_NOT_LEAK",
                ),
            ):
                report = census_multiplayer_maps(catalog)

            setup = report["maps"][0]["setupNavigation"]
            self.assertEqual(setup["duplicateTeamNameCount"], 1)
            self.assertEqual(setup["duplicateTeamEntryCount"], 2)
            self.assertFalse(setup["sourceCrossChecks"]["allPassed"])
            self.assertFalse(setup["sourceCrossChecks"]["teamNamesUnique"])
            self.assertEqual(
                setup["sourceCrossChecks"]["failed"], ["teamNamesUnique"]
            )
            self.assertEqual(
                report["summary"]["setupNavigation"]["duplicateTeamNameMapCount"],
                1,
            )
            self.assertNotIn("SOURCE_TEAM_NAME_MUST_NOT_LEAK", json.dumps(report))

    def test_mapcache_parser_rejects_unknown_or_duplicate_fields(self) -> None:
        base = _record("maps/test/test.map", multiplayer=True, players=2)
        cases = [
            base.replace("End", "Mystery = 1\nEnd"),
            base.replace("End", "numPlayers = 2\nEnd"),
        ]
        for text in cases:
            with self.subTest(text=text), self.assertRaises(ValueError):
                parse_mapcache_bytes(text.encode("cp1252"))


if __name__ == "__main__":
    unittest.main()
