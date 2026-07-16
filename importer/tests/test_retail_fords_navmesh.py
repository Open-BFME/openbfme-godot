from __future__ import annotations

import hashlib
import json
from pathlib import Path
import tempfile
import unittest

try:
    from openbfme_importer.retail_fords_navmesh import (
        _canonical_sha256,
        _cell_is_impassable,
        _formation_rows,
        _geometry,
        _object_block,
        build_fords_navigation_contract,
        main,
    )
except ModuleNotFoundError:  # pragma: no cover - direct discovery fallback
    from importer.openbfme_importer.retail_fords_navmesh import (
        _canonical_sha256,
        _cell_is_impassable,
        _formation_rows,
        _geometry,
        _object_block,
        build_fords_navigation_contract,
        main,
    )


class RetailFordsNavmeshUnitTests(unittest.TestCase):
    def test_canonical_digest_is_key_order_independent(self) -> None:
        self.assertEqual(
            _canonical_sha256({"b": 2, "a": [1, 3]}),
            _canonical_sha256({"a": [1, 3], "b": 2}),
        )

    def test_lsb_first_passability_respects_padded_rows(self) -> None:
        payload = bytes([0b10000001, 0b00000001, 0b00000010, 0])
        self.assertTrue(_cell_is_impassable(payload, 2, 0, 0))
        self.assertTrue(_cell_is_impassable(payload, 2, 7, 0))
        self.assertTrue(_cell_is_impassable(payload, 2, 8, 0))
        self.assertFalse(_cell_is_impassable(payload, 2, 9, 0))
        self.assertTrue(_cell_is_impassable(payload, 2, 1, 1))

    def test_geometry_parser_preserves_exact_active_declarations(self) -> None:
        text = """Object Fixture
  Geometry = BOX
  GeometryMajorRadius = 30.0
  GeometryMinorRadius = 45.0
  GeometryHeight = 20
  GeometryOffset = X:-2 Y:3 Z:0
  AdditionalGeometry = CYLINDER
  GeometryName = TERRAIN_BOUNDS
  GeometryMajorRadius = 80
  GeometryMinorRadius = 10
  GeometryHeight = 0.8
  GeometryActive = No
  ;AdditionalGeometry = BOX
  ;GeometryMajorRadius = 999
End
"""
        rows = _geometry(_object_block(text, "Fixture"))
        self.assertEqual(2, len(rows))
        self.assertEqual("BOX", rows[0]["shape"])
        self.assertEqual({"x": -2.0, "y": 3.0, "z": 0.0}, rows[0]["offset"])
        self.assertEqual("TERRAIN_BOUNDS", rows[1]["name"])
        self.assertFalse(rows[1]["active"])

    def test_rank_rows_are_preserved_without_selecting_a_runtime_formation(
        self,
    ) -> None:
        text = """Object FixtureHorde
  RankInfo = RankNumber:1 UnitType:Fixture Position:X:10 Y:0 Position:X:10 Y:20
  RankInfo = RankNumber:2 UnitType:Fixture Position:X:-10 Y:0 Leader 1 0
End
"""
        rows = _formation_rows(_object_block(text, "FixtureHorde"))
        self.assertEqual([1, 2], [row["rankNumber"] for row in rows])
        self.assertEqual(3, sum(len(row["positions"]) for row in rows))
        self.assertEqual("Fixture", rows[0]["unitType"])


class RetailFordsNavmeshPrivateIntegrationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.repo = Path(__file__).resolve().parents[2]
        cls.map_root = (
            cls.repo
            / ".private/content-packs/bfme2-five-maps-106-private/maps/fords-of-isen-ii"
        )
        cls.effective_root = cls.repo / ".private/retail-work/cache/effective-assets"
        cls.runtime_source = cls.repo / "game/src/retail_slice/retail_map_data.gd"
        if not all(
            path.exists()
            for path in (cls.map_root, cls.effective_root, cls.runtime_source)
        ):
            raise unittest.SkipTest("private Fords retail evidence is unavailable")
        cls.contract = build_fords_navigation_contract(
            cls.map_root,
            cls.effective_root,
            runtime_source=cls.runtime_source,
        )

    def test_exact_static_source_contract(self) -> None:
        terrain = self.contract["terrainPassability"]
        self.assertEqual((415, 353), (terrain["width"], terrain["height"]))
        self.assertEqual(20, terrain["borderWidth"])
        self.assertEqual(18_325, terrain["impassableCount"])
        self.assertEqual(
            "11e911c6ba50a0d8dcf7fc3a71242b013b5dfdce1169ae86c939d2ddd5e654b9",
            terrain["sha256"],
        )
        self.assertEqual(
            [[20, 20], [395, 333]],
            [
                terrain["playableGridMinInclusive"],
                terrain["playableGridMaxInclusive"],
            ],
        )
        self.assertEqual(2, len(self.contract["playerStarts"]))
        self.assertEqual(3, len(self.contract["waterAndFords"]["namedFords"]))
        self.assertEqual(
            [38, 43, 46],
            sorted(row["id"] for row in self.contract["waterAndFords"]["namedFords"]),
        )

    def test_routing_and_buildability_fail_closed(self) -> None:
        routing = self.contract["routing"]
        self.assertEqual(0, routing["authoredWaypointEdgeCount"])
        connectivity = routing["passabilityOnlyConnectivity"]
        self.assertEqual(39, connectivity["componentCount"])
        self.assertEqual(104_166, connectivity["walkableCellCount"])
        self.assertEqual(103_702, connectivity["componentSizesDescending"][0])
        self.assertTrue(connectivity["playerStartsShareComponent"])
        self.assertEqual(
            [[303, 69], [70, 237]],
            [row["gridCell"] for row in connectivity["startCells"]],
        )
        self.assertFalse(routing["parityReady"])
        self.assertFalse(self.contract["sourceBuildability"]["parityReady"])
        self.assertFalse(self.contract["summary"]["sourceBuildabilityGridPresent"])
        self.assertFalse(self.contract["summary"]["parityReady"])
        blocker_ids = {row["id"] for row in self.contract["blockers"]}
        self.assertIn("source-buildability-grid-absent", blocker_ids)
        self.assertIn(
            "route-cost-neighbors-tie-break-and-smoothing-unproven", blocker_ids
        )
        vectors = {row["id"]: row for row in self.contract["behavioralTestVectors"]}
        self.assertTrue(
            vectors["passability-reviewed-ford2-cell"]["expectedImpassable"]
        )
        self.assertEqual(
            [
                ("ford1", [332, 203], False),
                ("ford2", [197, 166], False),
                ("ford3", [62, 120], False),
            ],
            [
                (
                    name,
                    vectors[f"passability-{name}-middle-source-section"]["gridCell"],
                    vectors[f"passability-{name}-middle-source-section"][
                        "expectedImpassable"
                    ],
                )
                for name in ("ford1", "ford2", "ford3")
            ],
        )

    def test_exact_men_footprint_declarations_are_present(self) -> None:
        roster = self.contract["rosterFootprints"]
        self.assertEqual(
            (4, 4, 5),
            (
                len(roster["units"]),
                len(roster["hordes"]),
                len(roster["buildings"]),
            ),
        )
        units = {row["name"]: row for row in roster["units"]}
        self.assertEqual(8.0, units["GondorFighter"]["geometry"][0]["majorRadius"])
        self.assertEqual(8.0, units["GondorCavalry"]["geometry"][0]["majorRadius"])
        hordes = {row["name"]: row for row in roster["hordes"]}
        self.assertIn(
            "LARGE_RECTANGLE_PATHFIND", hordes["GondorFighterHorde"]["kindOf"]
        )
        self.assertEqual(
            (30.0, 45.0),
            (
                hordes["GondorFighterHorde"]["geometry"][0]["majorRadius"],
                hordes["GondorFighterHorde"]["geometry"][0]["minorRadius"],
            ),
        )
        buildings = {row["buildingId"]: row for row in roster["buildings"]}
        farm_names = [row["name"] for row in buildings["men-farm"]["definitions"]]
        self.assertEqual(["FarmInterface", "GondorFarm"], farm_names)
        self.assertGreater(len(buildings["men-farm"]["definitions"][0]["geometry"]), 1)

    def test_generation_and_cli_outputs_are_byte_identical(self) -> None:
        second = build_fords_navigation_contract(
            self.map_root,
            self.effective_root,
            runtime_source=self.runtime_source,
        )
        self.assertEqual(self.contract, second)
        payload = dict(self.contract)
        declared = payload.pop("aggregateSha256")
        self.assertEqual(declared, _canonical_sha256(payload))
        with tempfile.TemporaryDirectory() as raw:
            first = Path(raw) / "first.json"
            other = Path(raw) / "other.json"
            base_args = [
                "--map-dir",
                str(self.map_root),
                "--effective-assets",
                str(self.effective_root),
                "--runtime-source",
                str(self.runtime_source),
            ]
            self.assertEqual(0, main([*base_args, "--output", str(first)]))
            self.assertEqual(0, main([*base_args, "--output", str(other)]))
            self.assertEqual(first.read_bytes(), other.read_bytes())
            self.assertEqual(
                hashlib.sha256(first.read_bytes()).hexdigest(),
                hashlib.sha256(other.read_bytes()).hexdigest(),
            )
            loaded = json.loads(first.read_text("utf-8"))
            self.assertEqual(
                self.contract["aggregateSha256"], loaded["aggregateSha256"]
            )


if __name__ == "__main__":
    unittest.main()
