from __future__ import annotations

import json
from pathlib import Path, PurePosixPath
import tempfile
import unittest

from openbfme_importer.catalog import InstallCatalog
from openbfme_importer.map_profile import (
    FIVE_MAP_TARGETS,
    MapTarget,
    build_five_map_profile,
    build_map_profile,
)
from openbfme_importer.profile import ImportProfile, resolve_profile

from importer.tests.test_big import make_big
from importer.tests.test_sage_map import _synthetic_map


def _catalog(
    root: Path,
    *,
    omit_preview: bool = False,
    targets: tuple[MapTarget, ...] = FIVE_MAP_TARGETS,
) -> InstallCatalog:
    source, _ = _synthetic_map()
    entries: dict[str, bytes] = {
        "data/ini/terrain.ini": b"Terrain TestGrass\n Texture = testgrass.tga\nEnd\n",
        "art/terrain/testgrass.tga": b"synthetic-tga",
    }
    for index, target in enumerate(targets):
        path = PurePosixPath(target.virtual_path)
        entries[target.virtual_path] = source
        entries[(path.parent / "map.ini").as_posix()] = f"map-{index}".encode()
        entries[(path.parent / f"{path.stem}_art.tga").as_posix()] = b"art"
        if not (omit_preview and index == 0):
            entries[(path.parent / f"{path.stem}_pic.tga").as_posix()] = b"preview"
    make_big(root / "maps.big", entries)
    return InstallCatalog.build(root)


class FiveMapProfileTests(unittest.TestCase):
    def test_generated_profile_is_deterministic_resolved_and_schema_valid(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            catalog = _catalog(root)
            first = build_five_map_profile(catalog)
            second = build_five_map_profile(catalog)
            self.assertEqual(
                json.dumps(first, sort_keys=True), json.dumps(second, sort_keys=True)
            )
            profile_path = root / "profile.json"
            profile_path.write_text(
                json.dumps(first, indent=2, sort_keys=True) + "\n", encoding="utf-8"
            )
            loaded = ImportProfile.load(profile_path)
            resolved = resolve_profile(loaded, catalog)

        self.assertFalse(resolved.missing_required)
        self.assertEqual(len(first["resources"]), 21)
        self.assertEqual(len(first["runtime_data"]["data/maps.json"]["maps"]), 5)
        self.assertEqual(
            [row["playerCount"] for row in first["runtime_data"]["data/maps.json"]["maps"]],
            [1, 1, 1, 1, 1],
        )
        map_resources = [
            row for row in first["resources"] if row["converter"] == "sage-map"
        ]
        self.assertEqual(len(map_resources), 5)
        self.assertTrue(
            all(row["options"]["expected"]["terrainTextureCount"] == 1 for row in map_resources)
        )
        terrain_resources = [
            row
            for row in first["resources"]
            if row["converter"] == "sage-terrain-materials"
        ]
        self.assertEqual(len(terrain_resources), 1)
        self.assertEqual(terrain_resources[0]["patterns"], [
            "data/ini/terrain.ini",
            "art/terrain/testgrass.tga",
        ])

    def test_missing_exact_companion_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            with self.assertRaisesRegex(ValueError, "map preview"):
                build_five_map_profile(_catalog(Path(raw), omit_preview=True))

    def test_five_map_wrapper_keeps_its_historical_identity(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            profile = build_five_map_profile(_catalog(Path(raw)))
        self.assertEqual("bfme2-five-maps-106-generated", profile["id"])
        self.assertEqual("bfme2-five-maps-106-private", profile["pack"]["id"])
        self.assertEqual(
            "maps/fords-of-isen-ii/map.json", profile["pack"]["files"]["entryMap"]
        )
        materials = [
            row
            for row in profile["resources"]
            if row["converter"] == "sage-terrain-materials"
        ]
        self.assertEqual("five-maps-terrain-materials", materials[0]["id"])
        self.assertEqual("assets/terrain/five-maps", materials[0]["output"])
        self.assertEqual(
            "assets/terrain/five-maps/terrain-materials.json",
            profile["runtime_data"]["data/maps.json"]["maps"][0]["terrainMaterials"],
        )


class GeneralizedMapProfileTests(unittest.TestCase):
    _TARGET = MapTarget(
        "test-map",
        "Test Map",
        "maps/map mp test map/map mp test map.map",
    )

    def _build(self, catalog: InstallCatalog) -> dict:
        return build_map_profile(
            catalog,
            (self._TARGET,),
            profile_id="bfme2-test-map-generated",
            title="Test map generated pack",
            pack_id="bfme2-test-map-private",
            priority=904,
            terrain_materials_label="test-label",
        )

    def test_single_custom_target_profile_is_parameterized_and_resolves(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            catalog = _catalog(root, targets=(self._TARGET,))
            profile = self._build(catalog)
            profile_path = root / "profile.json"
            profile_path.write_text(
                json.dumps(profile, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )
            resolved = resolve_profile(ImportProfile.load(profile_path), catalog)

        self.assertFalse(resolved.missing_required)
        self.assertEqual("bfme2-test-map-generated", profile["id"])
        self.assertEqual("bfme2-test-map-private", profile["pack"]["id"])
        self.assertEqual(
            "maps/test-map/map.json", profile["pack"]["files"]["entryMap"]
        )
        map_resources = [
            row for row in profile["resources"] if row["converter"] == "sage-map"
        ]
        self.assertEqual(1, len(map_resources))
        self.assertEqual("map-test-map-binary", map_resources[0]["id"])
        self.assertEqual(
            "bfme2.map.test-map",
            map_resources[0]["options"]["metadata"]["id"],
        )
        self.assertEqual(
            "assets/terrain/test-label/terrain-materials.json",
            map_resources[0]["options"]["metadata"]["terrainMaterials"],
        )
        materials = [
            row
            for row in profile["resources"]
            if row["converter"] == "sage-terrain-materials"
        ]
        self.assertEqual("test-label-terrain-materials", materials[0]["id"])
        self.assertEqual("assets/terrain/test-label", materials[0]["output"])
        catalog_rows = profile["runtime_data"]["data/maps.json"]["maps"]
        self.assertEqual(["bfme2.map.test-map"], [row["id"] for row in catalog_rows])

    def test_target_validation_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            catalog = _catalog(Path(raw), targets=(self._TARGET,))
            with self.assertRaisesRegex(ValueError, "at least one"):
                build_map_profile(
                    catalog,
                    (),
                    profile_id="x",
                    title="x",
                    pack_id="x",
                    priority=904,
                    terrain_materials_label="x",
                )
            with self.assertRaisesRegex(ValueError, "duplicate slugs"):
                build_map_profile(
                    catalog,
                    (self._TARGET, self._TARGET),
                    profile_id="x",
                    title="x",
                    pack_id="x",
                    priority=904,
                    terrain_materials_label="x",
                )
            with self.assertRaisesRegex(ValueError, "invalid map target slug"):
                build_map_profile(
                    catalog,
                    (MapTarget("Bad Slug", "Bad", self._TARGET.virtual_path),),
                    profile_id="x",
                    title="x",
                    pack_id="x",
                    priority=904,
                    terrain_materials_label="x",
                )
            with self.assertRaisesRegex(ValueError, "entry slug"):
                build_map_profile(
                    catalog,
                    (self._TARGET,),
                    profile_id="x",
                    title="x",
                    pack_id="x",
                    priority=904,
                    terrain_materials_label="x",
                    entry_slug="absent-map",
                )


if __name__ == "__main__":
    unittest.main()
