from __future__ import annotations

from collections import Counter
import json
from pathlib import Path, PurePosixPath
import tempfile
import unittest

from importer.tests.test_sage_scb import _fixture
from importer.tests.test_sage_map import _synthetic_map

from openbfme_importer.pipeline import ImportPipeline
from openbfme_importer.map_profile import discover_registry_map_targets
from openbfme_importer.sage_scb import SageScbError
from openbfme_importer.sage_scripts import (
    _INHERITANCE_TACTICAL_MARKER_TYPES,
    _build_world,
    _marker_only_team_attested,
    MAP_SCRIPTS_COMPOSITE_SCHEMA_VERSION,
    MAP_SCRIPTS_SCHEMA,
    MAP_SCRIPTS_SCHEMA_VERSION,
    RECORDED_ACTION_OPCODES,
    RECORDED_CONDITION_OPCODES,
    SEMANTIC_ACTION_OPCODES,
    SEMANTIC_CONDITION_OPCODES,
    compose_map_scripts_document,
    map_scripts_document,
)


ROOT = Path(__file__).resolve().parents[2]
RUNTIME_GDSCRIPT = ROOT / "game/src/retail_slice/retail_map_scripts.gd"
CATALOG = ROOT / ".private/retail-work/catalog/rotwk.json"
BFME2_CATALOG = ROOT / ".private/retail-work/catalog/bfme2.json"
CONTRACT = (
    ROOT
    / ".private/retail-work/reports/skirmish-script-contract"
    / "skirmish_script_contract.json"
)

# One real skirmish map and one real libraries.big SCB from the tier-1
# skirmish contract scope (both present in the RotWK 2.01 catalog).
REAL_MAP = "maps/map mp adorn river/map mp adorn river.map"
REAL_SCB = "libraries/power restrictions.scb"
# One real campaign mission (RotWK Angmar campaign, mission 2).
REAL_CAMPAIGN_MAP = "maps/map ang fornost/map ang fornost.map"


def _pipeline() -> ImportPipeline:
    return object.__new__(ImportPipeline)


class SageScriptsConverterTests(unittest.TestCase):
    def test_marker_only_attestation_requires_exact_complete_source_rows(
        self,
    ) -> None:
        def source_object(
            type_name: str,
            *,
            owner: str = "PlyrCivilian/Player_1_Inherit",
            name: str = "",
        ) -> dict:
            return {
                "typeName": type_name,
                "godotPosition": [1.0, 2.0, 3.0],
                "godotYawRadians": 0.0,
                "properties": {
                    "originalOwner": owner,
                    "objectName": name,
                },
            }

        world = _build_world(
            sides={"players": []},
            teams={
                "teams": [
                    {
                        "index": 0,
                        "name": "Player_1_Inherit",
                        "owner": "PlyrCivilian",
                        "properties": [],
                    },
                    {
                        "index": 1,
                        "name": "Player_2_Inherit",
                        "owner": "PlyrCivilian",
                        "properties": [],
                    },
                    {
                        "index": 2,
                        "name": "OrdinaryTeam",
                        "owner": "PlyrCivilian",
                        "properties": [],
                    },
                ]
            },
            objects=[
                source_object("Center1", name="Center"),
                source_object("Backdoor3"),
                # The same local team spelling under another player is a
                # distinct qualified identity and must not contaminate the
                # PlyrCivilian team's enumeration.
                source_object(
                    "RealCombatUnit",
                    owner="Player_1/Player_1_Inherit",
                ),
                source_object(
                    "RealCombatUnit",
                    owner="PlyrCivilian/Player_2_Inherit",
                ),
                source_object(
                    "Center1",
                    owner="PlyrCivilian/OrdinaryTeam",
                ),
            ],
            waypoints=[],
            waypoint_edges=[],
            trigger_areas=[],
        )
        rows = {row["name"]: row for row in world["teams"]}

        self.assertTrue(rows["Player_1_Inherit"]["markerOnly"])
        self.assertEqual(rows["Player_1_Inherit"]["objectCount"], 2)
        self.assertEqual(rows["Player_1_Inherit"]["namedMembers"], ["Center"])
        self.assertNotIn("markerOnly", rows["Player_2_Inherit"])
        self.assertNotIn("markerOnly", rows["OrdinaryTeam"])

        mismatched = dict(rows["Player_1_Inherit"])
        mismatched["objectCount"] += 1
        self.assertFalse(
            _marker_only_team_attested(mismatched, world["objects"])
        )

    def test_empty_inheritance_team_is_completely_enumerated(self) -> None:
        world = _build_world(
            sides={"players": []},
            teams={
                "teams": [
                    {
                        "index": 0,
                        "name": "Player_8_Inherit",
                        "owner": "PlyrCivilian",
                        "properties": [],
                    }
                ]
            },
            objects=[],
            waypoints=[],
            waypoint_edges=[],
            trigger_areas=[],
        )

        self.assertEqual(
            world["teams"][0],
            {
                "index": 0,
                "name": "Player_8_Inherit",
                "owner": "PlyrCivilian",
                "objectCount": 0,
                "namedMembers": [],
                "units": [],
                "markerOnly": True,
            },
        )

    def test_composite_preserves_per_player_library_template(self) -> None:
        map_document = {
            "schema": MAP_SCRIPTS_SCHEMA,
            "schemaVersion": MAP_SCRIPTS_SCHEMA_VERSION,
            "source": {"container": "map", "sourceSha256": "1" * 64},
            "world": {
                "available": True,
                "players": [
                    {"index": 0, "name": ""},
                    {"index": 1, "name": "PlyrCivilian"},
                    {"index": 2, "name": "Player_1"},
                ],
                "teams": [
                    {
                        "index": 0,
                        "name": "Player_1_Inherit",
                        "owner": "PlyrCivilian",
                        "objectCount": 0,
                        "namedMembers": [],
                    }
                ],
                "namedObjects": [],
                "waypoints": [],
                "waypointPaths": [],
                "triggerAreas": [],
            },
            "scripts": [],
        }
        library = {
            "schema": MAP_SCRIPTS_SCHEMA,
            "schemaVersion": MAP_SCRIPTS_SCHEMA_VERSION,
            "source": {"container": "map", "sourceSha256": "2" * 64},
            "world": {
                "available": True,
                "players": [
                    {"index": 0, "name": ""},
                    {"index": 1, "name": "Player"},
                ],
                "teams": [
                    {
                        "index": 0,
                        "name": "teamPlayer",
                        "owner": "Player",
                        "objectCount": 0,
                        "namedMembers": [],
                    },
                    {
                        "index": 1,
                        "name": "AI Base Team",
                        "owner": "Player",
                        "objectCount": 0,
                        "namedMembers": [],
                    },
                ],
                "namedObjects": [],
                "waypoints": [],
                "waypointPaths": [],
                "triggerAreas": [],
            },
            "scripts": [
                {
                    "playerIndex": 1,
                    "payload": {
                        "name": "inherit",
                        "isActive": True,
                        "records": [],
                    },
                }
            ],
        }

        composite = compose_map_scripts_document(map_document, [library])

        self.assertEqual(
            composite["schemaVersion"], MAP_SCRIPTS_COMPOSITE_SCHEMA_VERSION
        )
        self.assertEqual(composite["world"], map_document["world"])
        self.assertEqual(composite["scripts"], [])
        self.assertEqual(
            composite["libraryTemplates"][0]["playerPlaceholder"], "Player"
        )
        self.assertEqual(
            composite["libraryTemplates"][0]["scripts"][0]["payload"]["name"],
            "inherit",
        )
        # The library remains a template rather than being falsely attributed
        # to one concrete Player_N at conversion time.
        self.assertNotIn("Player_1", composite["libraryTemplates"][0])

    def test_composite_refuses_ambiguous_or_duplicate_library_templates(
        self,
    ) -> None:
        map_document = {
            "schema": MAP_SCRIPTS_SCHEMA,
            "schemaVersion": MAP_SCRIPTS_SCHEMA_VERSION,
            "source": {"container": "map", "sourceSha256": "1" * 64},
            "world": {
                "available": True,
                "players": [],
                "teams": [],
                "namedObjects": [],
            },
            "scripts": [],
        }
        library = {
            "schema": MAP_SCRIPTS_SCHEMA,
            "schemaVersion": MAP_SCRIPTS_SCHEMA_VERSION,
            "source": {"container": "map", "sourceSha256": "2" * 64},
            "world": {
                "available": True,
                "players": [{"index": 1, "name": "Player"}],
                "teams": [],
                "namedObjects": [],
            },
            "scripts": [],
        }
        with self.assertRaisesRegex(ValueError, "duplicate library"):
            compose_map_scripts_document(map_document, [library, library])

        ambiguous = json.loads(json.dumps(library))
        ambiguous["world"]["players"].append({"index": 2, "name": "Player"})
        with self.assertRaisesRegex(ValueError, "exactly one Player"):
            compose_map_scripts_document(map_document, [ambiguous])

        foreign = json.loads(json.dumps(library))
        foreign["scripts"] = [{"playerIndex": 99, "payload": {}}]
        with self.assertRaisesRegex(ValueError, "outside the Player"):
            compose_map_scripts_document(map_document, [foreign])

    def test_synthetic_scb_round_trips_through_the_converter(self) -> None:
        source_bytes = _fixture()
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            source = root / "fixture.scb"
            source.write_bytes(source_bytes)
            target = root / "fixture.scripts.json"

            outputs = _pipeline()._convert_sage_scripts(source, target, {})
            self.assertEqual(outputs, [target])

            document = json.loads(target.read_text(encoding="utf-8"))
            self.assertEqual(document["schema"], MAP_SCRIPTS_SCHEMA)
            self.assertEqual(
                document["schemaVersion"], MAP_SCRIPTS_SCHEMA_VERSION
            )
            self.assertEqual(document["source"]["container"], "scb")
            self.assertEqual(
                document["source"]["sourceBytes"], len(source_bytes)
            )
            self.assertEqual(len(document["source"]["sourceSha256"]), 64)

            # The fixture carries one grouped and one top-level copy of the
            # same script: 1 condition + 2 actions (true/false lanes) each.
            self.assertEqual(document["counts"]["scripts"], 2)
            self.assertEqual(document["counts"]["actionSlots"], 4)
            self.assertEqual(document["counts"]["conditionSlots"], 2)
            self.assertEqual(document["actionOpcodes"], {"internalKey": 4})
            self.assertEqual(document["conditionOpcodes"], {"internalKey": 2})

            rows = document["scripts"]
            self.assertEqual(
                [row["groupPath"] for row in rows], ["fixture-group", ""]
            )
            self.assertEqual(
                {row["name"] for row in rows}, {"duplicate-script"}
            )
            for row in rows:
                self.assertEqual(row["actionSlots"], 2)
                self.assertEqual(row["conditionSlots"], 1)
                payload = row["payload"]
                self.assertEqual(payload["name"], "duplicate-script")
                self.assertIn("records", payload)

    def test_synthetic_map_container_extracts_only_player_scripts(self) -> None:
        # The synthetic SCB body is also a valid CkMp map blob for the map
        # lane, which parses PlayerScriptsList and skips every other chunk.
        document = map_scripts_document(_fixture(), container="map")
        self.assertEqual(document["source"]["container"], "map")
        self.assertEqual(document["counts"]["scripts"], 2)
        self.assertEqual(document["counts"]["actionSlots"], 4)
        self.assertEqual(document["counts"]["conditionSlots"], 2)

    def test_one_cell_structural_map_decodes_for_script_extraction(self) -> None:
        source, _elevations = _synthetic_map(height_dimensions=(1, 1))

        document = map_scripts_document(source, container="map")

        self.assertEqual(document["schema"], MAP_SCRIPTS_SCHEMA)
        self.assertEqual(document["schemaVersion"], MAP_SCRIPTS_SCHEMA_VERSION)
        self.assertEqual(document["source"]["container"], "map")
        self.assertEqual(document["counts"]["scripts"], 0)
        self.assertTrue(document["world"]["available"])
        self.assertEqual(document["counts"]["players"], 1)
        self.assertEqual(document["counts"]["teams"], 1)

    def test_two_runs_are_byte_identical(self) -> None:
        source_bytes = _fixture()
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            source = root / "fixture.scb"
            source.write_bytes(source_bytes)
            first = root / "first.scripts.json"
            second = root / "second.scripts.json"

            _pipeline()._convert_sage_scripts(source, first, {})
            _pipeline()._convert_sage_scripts(source, second, {})
            self.assertEqual(first.read_bytes(), second.read_bytes())
            self.assertNotIn(
                raw.encode("utf-8", "ignore"), first.read_bytes()
            )

    def test_corrupt_input_fails_closed(self) -> None:
        source_bytes = _fixture()
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            target = root / "out.scripts.json"

            truncated = root / "truncated.scb"
            truncated.write_bytes(source_bytes[: len(source_bytes) // 2])
            with self.assertRaises(SageScbError):
                _pipeline()._convert_sage_scripts(truncated, target, {})
            self.assertFalse(target.exists())

            garbage = root / "garbage.map"
            garbage.write_bytes(b"\x00" * 64)
            with self.assertRaises((SageScbError, ValueError)):
                _pipeline()._convert_sage_scripts(garbage, target, {})
            self.assertFalse(target.exists())

            flipped = bytearray(source_bytes)
            flipped[-1] ^= 0xFF
            corrupted = root / "corrupted.scb"
            corrupted.write_bytes(bytes(flipped))
            with self.assertRaises((SageScbError, ValueError)):
                _pipeline()._convert_sage_scripts(corrupted, target, {})
            self.assertFalse(target.exists())

    def test_converter_contract_is_strict(self) -> None:
        source_bytes = _fixture()
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            source = root / "fixture.scb"
            source.write_bytes(source_bytes)

            with self.assertRaisesRegex(ValueError, "scripts.json"):
                _pipeline()._convert_sage_scripts(
                    source, root / "fixture.json", {}
                )
            with self.assertRaisesRegex(ValueError, "no options"):
                _pipeline()._convert_sage_scripts(
                    source, root / "fixture.scripts.json", {"extra": True}
                )
            wrong_suffix = root / "fixture.ini"
            wrong_suffix.write_bytes(source_bytes)
            with self.assertRaisesRegex(ValueError, ".map or .scb"):
                _pipeline()._convert_sage_scripts(
                    wrong_suffix, root / "fixture.scripts.json", {}
                )

    def test_unsupported_container_label_is_rejected(self) -> None:
        with self.assertRaisesRegex(ValueError, "unsupported"):
            map_scripts_document(_fixture(), container="ini")


def _gdscript_opcode_set(source: str, constant: str) -> frozenset[str]:
    """Read one ``const NAME := { "OPCODE": true, ... }`` block from GDScript."""

    marker = f"const {constant} := {{"
    start = source.index(marker) + len(marker)
    end = source.index("}", start)
    body = source[start:end]
    return frozenset(
        line.split('"')[1]
        for line in body.splitlines()
        if line.strip().startswith('"')
    )


@unittest.skipUnless(
    RUNTIME_GDSCRIPT.is_file(), "runtime interpreter source is not present"
)
class RuntimeOpcodeContractTests(unittest.TestCase):
    """The census may never claim coverage the runtime does not implement.

    ``sage_scripts.py`` declares which opcodes are honoured, and the coverage
    section of every converted document is computed from those declarations.
    The Godot interpreter is the thing that actually honours them, so the two
    declarations are asserted identical here; drift fails the build rather
    than silently inflating reported campaign coverage.
    """

    @classmethod
    def setUpClass(cls) -> None:
        cls.source = RUNTIME_GDSCRIPT.read_text(encoding="utf-8")

    def test_semantic_condition_tables_match(self) -> None:
        self.assertEqual(
            _gdscript_opcode_set(self.source, "SEMANTIC_CONDITIONS"),
            SEMANTIC_CONDITION_OPCODES,
        )

    def test_semantic_action_tables_match(self) -> None:
        self.assertEqual(
            _gdscript_opcode_set(self.source, "SEMANTIC_ACTIONS"),
            SEMANTIC_ACTION_OPCODES,
        )

    def test_recorded_action_tables_match(self) -> None:
        self.assertEqual(
            _gdscript_opcode_set(self.source, "RECORDED_ACTIONS"),
            RECORDED_ACTION_OPCODES,
        )

    def test_no_opcode_is_both_semantic_and_recorded(self) -> None:
        self.assertEqual(
            SEMANTIC_ACTION_OPCODES & RECORDED_ACTION_OPCODES, frozenset()
        )
        self.assertEqual(
            SEMANTIC_CONDITION_OPCODES & RECORDED_CONDITION_OPCODES,
            frozenset(),
        )

    def test_every_declared_action_has_a_runtime_branch(self) -> None:
        # Semantic actions must appear in the action match statement; recorded
        # actions are dispatched through the RECORDED_ACTIONS fallback.
        body = self.source[self.source.index("func _execute_action") :]
        # Archived runtime ends the action match before _compare helpers.
        end_marker = "\nfunc _compare"
        if end_marker not in body:
            end_marker = "\nfunc _set_counter"
        body = body[: body.index(end_marker)]
        for opcode in SEMANTIC_ACTION_OPCODES:
            self.assertIn(f'"{opcode}"', body, f"{opcode} has no runtime branch")

    def test_every_declared_condition_has_a_runtime_branch(self) -> None:
        body = self.source[self.source.index("func _evaluate_condition(") :]
        body = body[: body.index("\nfunc _execute_action")]
        for opcode in SEMANTIC_CONDITION_OPCODES:
            self.assertIn(f'"{opcode}"', body, f"{opcode} has no runtime branch")


@unittest.skipUnless(
    CATALOG.is_file() and CONTRACT.is_file(),
    "RotWK catalog or skirmish script contract is not present",
)
class SageScriptsRealCorpusTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        from openbfme_importer.catalog import InstallCatalog

        cls.catalog = InstallCatalog.load(CATALOG)
        stale = cls.catalog.stale_reasons()
        if stale:
            raise unittest.SkipTest(f"stale RotWK catalog: {stale}")
        cls.winners = {
            rows[0].name.casefold(): rows[0]
            for rows in cls.catalog._by_key.values()
        }
        cls.contract = json.loads(CONTRACT.read_text(encoding="utf-8"))
        cls.contract_sources = {
            row["path"].casefold(): row for row in cls.contract["sources"]
        }

    def _convert_real_entry(self, virtual_path: str) -> dict:
        entry = self.winners[virtual_path.casefold()]
        archive = self.catalog.open_archive_for(entry)
        payload = archive.read_entry(
            self.catalog.as_entry(entry), max_bytes=max(entry.size, 1)
        )
        suffix = PurePosixPath(virtual_path).suffix
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            source = root / f"entry{suffix}"
            source.write_bytes(payload)
            target = root / "entry.scripts.json"
            _pipeline()._convert_sage_scripts(source, target, {})
            return json.loads(target.read_text(encoding="utf-8"))

    def _assert_matches_contract(self, virtual_path: str) -> None:
        expected = self.contract_sources[virtual_path.casefold()]
        document = self._convert_real_entry(virtual_path)

        self.assertEqual(
            document["counts"]["scripts"], expected["scriptCount"]
        )
        self.assertEqual(document["actionOpcodes"], expected["actionOpcodes"])
        self.assertEqual(
            document["conditionOpcodes"], expected["conditionOpcodes"]
        )
        self.assertEqual(
            document["counts"]["actionSlots"],
            sum(row["actionSlots"] for row in expected["scripts"]),
        )
        self.assertEqual(
            document["counts"]["conditionSlots"],
            sum(row["conditionSlots"] for row in expected["scripts"]),
        )
        self.assertEqual(
            [row["name"] for row in document["scripts"]],
            [row["name"] for row in expected["scripts"]],
        )

    def test_real_skirmish_map_matches_the_contract(self) -> None:
        self._assert_matches_contract(REAL_MAP)

    def test_real_library_scb_matches_the_contract(self) -> None:
        self._assert_matches_contract(REAL_SCB)

    def test_real_campaign_map_extracts_a_resolvable_script_world(self) -> None:
        entry = self.winners.get(REAL_CAMPAIGN_MAP.casefold())
        if entry is None:
            self.skipTest(f"{REAL_CAMPAIGN_MAP} is not in the catalog")
        document = self._convert_real_entry(REAL_CAMPAIGN_MAP)

        world = document["world"]
        self.assertTrue(world["available"])
        # A campaign mission's scripts are unrunnable without these; the
        # skirmish-era document carried none of them.
        for section in ("players", "teams", "waypoints", "triggerAreas"):
            self.assertGreater(len(world[section]), 0, section)
        self.assertTrue(
            all(path["waypointIds"] for path in world["waypointPaths"])
        )

        # Scripts stay attributed to their owning SidesList player.
        player_count = len(world["players"])
        indices = {row["playerIndex"] for row in document["scripts"]}
        self.assertTrue(indices)
        self.assertTrue(all(0 <= index < player_count for index in indices))

        # Coverage partitions the census exactly; nothing is unaccounted for.
        for kind, histogram_key, slot_key in (
            ("actions", "actionOpcodes", "actionSlots"),
            ("conditions", "conditionOpcodes", "conditionSlots"),
        ):
            coverage = document["coverage"][kind]
            self.assertEqual(
                coverage["totalSlots"], document["counts"][slot_key]
            )
            self.assertEqual(
                coverage["semanticSlots"]
                + coverage["recordedSlots"]
                + coverage["unsupportedSlots"],
                coverage["totalSlots"],
            )
            missing = coverage["unsupportedByFrequency"]
            self.assertEqual(len(missing), coverage["distinctUnsupported"])
            self.assertEqual(
                sum(row["slots"] for row in missing),
                coverage["unsupportedSlots"],
            )
            # Descending frequency order survives the key-sorting JSON writer.
            self.assertEqual(
                [row["slots"] for row in missing],
                sorted((row["slots"] for row in missing), reverse=True),
            )
            names = {row["opcode"] for row in missing}
            self.assertEqual(
                names
                & (
                    SEMANTIC_ACTION_OPCODES
                    if kind == "actions"
                    else SEMANTIC_CONDITION_OPCODES
                ),
                set(),
            )
            self.assertTrue(names <= set(document[histogram_key]))


@unittest.skipUnless(
    BFME2_CATALOG.is_file() and CATALOG.is_file(),
    "BFME2 or RotWK catalog is not present",
)
class InheritanceMarkerRealCorpusTests(unittest.TestCase):
    EXPECTED = {
        "BFME2": {
            "catalog": BFME2_CATALOG,
            "maps": 8,
            "rejections": 5,
            "inheritMaps": {
                "maps/map mp fords of isen ii/map mp fords of isen ii.map": 2,
                "maps/map mp harlindon/map mp harlindon.map": 3,
            },
            "teams": 5,
            "objects": 12,
            "types": {
                "Backdoor1": 2,
                "Backdoor2": 2,
                "Center1": 2,
                "Center2": 2,
                "Flank1": 2,
                "Flank2": 2,
            },
        },
        "RotWK": {
            "catalog": CATALOG,
            "maps": 22,
            "rejections": 0,
            "inheritMaps": {
                "maps/map mp adorn river/map mp adorn river.map": 8,
                "maps/map mp anfalas/map mp anfalas.map": 6,
                "maps/map mp argonath/map mp argonath.map": 2,
                "maps/map mp brown lands/map mp brown lands.map": 8,
                "maps/map mp fords of isen ii/map mp fords of isen ii.map": 2,
                "maps/map mp harlindon/map mp harlindon.map": 3,
                "maps/map mp weathertop/map mp weathertop.map": 4,
            },
            "teams": 33,
            "objects": 113,
            "types": {
                "Backdoor1": 16,
                "Backdoor2": 16,
                "Backdoor3": 5,
                "Center1": 16,
                "Center2": 17,
                "Center3": 5,
                "Flank1": 16,
                "Flank2": 16,
                "Flank3": 6,
            },
        },
    }

    def test_effective_official_skirmish_inheritance_teams_are_attested(
        self,
    ) -> None:
        from openbfme_importer.catalog import InstallCatalog

        for tree, expected in self.EXPECTED.items():
            catalog = InstallCatalog.load(expected["catalog"])
            stale = catalog.stale_reasons()
            if stale:
                self.skipTest(f"stale {tree} catalog: {stale}")
            targets, rejections = discover_registry_map_targets(catalog)
            map_team_counts: Counter = Counter()
            type_counts: Counter = Counter()
            team_count = 0
            object_count = 0

            self.assertEqual(len(targets), expected["maps"], tree)
            self.assertEqual(len(rejections), expected["rejections"], tree)
            for target in targets:
                entry = catalog.resolve_exact(target.virtual_path)
                self.assertIsNotNone(entry, target.virtual_path)
                archive = catalog.open_archive_for(entry)
                payload = archive.read_entry(
                    catalog.as_entry(entry),
                    max_bytes=max(entry.size, 1),
                )
                world = map_scripts_document(
                    payload, container="map"
                )["world"]
                objects_by_team: dict[str, list[dict]] = {}
                for object_row in world["objects"]:
                    objects_by_team.setdefault(
                        object_row["team"], []
                    ).append(object_row)
                for team_row in world["teams"]:
                    is_inheritance = (
                        team_row["owner"] == "PlyrCivilian"
                        and team_row["name"]
                        in {
                            f"Player_{index}_Inherit"
                            for index in range(1, 9)
                        }
                    )
                    if not is_inheritance:
                        self.assertNotIn("markerOnly", team_row)
                        continue
                    members = objects_by_team.get(team_row["name"], [])
                    self.assertEqual(
                        team_row["objectCount"],
                        len(members),
                        (tree, target.virtual_path, team_row["name"]),
                    )
                    self.assertTrue(
                        team_row.get("markerOnly", False),
                        (tree, target.virtual_path, team_row["name"]),
                    )
                    self.assertTrue(
                        {
                            row["typeName"] for row in members
                        }
                        <= _INHERITANCE_TACTICAL_MARKER_TYPES
                    )
                    map_team_counts[target.virtual_path] += 1
                    team_count += 1
                    object_count += len(members)
                    type_counts.update(
                        row["typeName"] for row in members
                    )

            self.assertEqual(
                dict(sorted(map_team_counts.items())),
                expected["inheritMaps"],
                tree,
            )
            self.assertEqual(team_count, expected["teams"], tree)
            self.assertEqual(object_count, expected["objects"], tree)
            self.assertEqual(
                dict(sorted(type_counts.items())), expected["types"], tree
            )


if __name__ == "__main__":
    unittest.main()
