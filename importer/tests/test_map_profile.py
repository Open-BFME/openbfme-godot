from __future__ import annotations

import json
from pathlib import Path, PurePosixPath
import tempfile
import unittest

from openbfme_importer.catalog import InstallCatalog
from openbfme_importer.map_profile import FIVE_MAP_TARGETS, build_five_map_profile
from openbfme_importer.profile import ImportProfile, resolve_profile

from importer.tests.test_big import make_big
from importer.tests.test_sage_map import _synthetic_map


def _catalog(root: Path, *, omit_preview: bool = False) -> InstallCatalog:
    source, _ = _synthetic_map()
    entries: dict[str, bytes] = {
        "data/ini/terrain.ini": b"Terrain TestGrass\n Texture = testgrass.tga\nEnd\n",
        "art/terrain/testgrass.tga": b"synthetic-tga",
    }
    for index, target in enumerate(FIVE_MAP_TARGETS):
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


if __name__ == "__main__":
    unittest.main()
