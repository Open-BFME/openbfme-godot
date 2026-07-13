from __future__ import annotations

import copy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

from PIL import Image

from openbfme_importer.catalog import CatalogEntry
from openbfme_importer.pipeline import ImportPipeline
from openbfme_importer.profile import (
    ImportProfile,
    MAX_TERRAIN_MATERIAL_SYMBOLS,
    ResolvedProfile,
    ResolvedResource,
    ResourceRule,
)
from openbfme_importer.terrain_materials import (
    TerrainMaterialReference,
    convert_terrain_materials,
    resolve_terrain_material_references,
)
from openbfme_importer.util import write_json_atomic


def _write_tga(path: Path, size: tuple[int, int], color: tuple[int, int, int, int]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    Image.new("RGBA", size, color).save(path, format="TGA")


def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _write_ini(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="cp1252", newline="\n")


class TerrainMaterialTests(unittest.TestCase):
    def test_resolves_ordered_material_references_from_bytes(self) -> None:
        source = b"""
Terrain GrassA
  Texture = Grass_A.TGA
End
Terrain Rock
  Texture = rock.tga
End
"""
        self.assertEqual(
            resolve_terrain_material_references(source, ["rock", "GRASSA"]),
            (
                TerrainMaterialReference("rock", "Rock", "rock.tga"),
                TerrainMaterialReference("GRASSA", "GrassA", "Grass_A.TGA"),
            ),
        )
        with self.assertRaisesRegex(ValueError, "unresolved terrain material"):
            resolve_terrain_material_references(source, ["Missing"])

    def test_converts_exact_case_insensitive_closure_deterministically(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            ini = root / "sources" / "terrain.ini"
            grass = root / "sources" / "grass_a.tga"
            rock = root / "sources" / "ROCK.TGA"
            _write_ini(
                ini,
                """
                ; Synthetic definitions only.
                Terrain GrassA
                  Texture = Grass_A.TGA
                  Class = Grass
                End

                Terrain Rock
                  Texture = rock.tga // case-insensitive source match
                End
                """,
            )
            _write_tga(grass, (3, 2), (10, 20, 30, 255))
            _write_tga(rock, (2, 4), (40, 50, 60, 128))
            sources = [
                ("art/terrain/grass_a.tga", grass),
                ("data/ini/terrain.ini", ini),
                ("art/terrain/ROCK.TGA", rock),
            ]

            first = root / "first"
            second = root / "second"
            first_paths = convert_terrain_materials(
                sources, first, ["rock", "GRASSA"]
            )
            second_paths = convert_terrain_materials(
                list(reversed(sources)), second, ["rock", "GRASSA"]
            )

            self.assertEqual(
                [path.relative_to(first).as_posix() for path in first_paths],
                ["textures/0000.png", "textures/0001.png", "terrain-materials.json"],
            )
            self.assertEqual(
                {
                    path.relative_to(first).as_posix(): _sha256(path)
                    for path in first_paths
                },
                {
                    path.relative_to(second).as_posix(): _sha256(path)
                    for path in second_paths
                },
            )
            self.assertFalse(any(first.rglob("*.ini")))
            self.assertFalse(any(first.rglob("*.tga")))

            manifest = json.loads(
                (first / "terrain-materials.json").read_text(encoding="utf-8")
            )
            self.assertEqual(manifest["schema"], "openbfme.sage-terrain-materials")
            self.assertEqual(manifest["schemaVersion"], 0)
            self.assertEqual(manifest["tableOrder"], "options.symbols")
            self.assertEqual(manifest["symbolCount"], 2)
            self.assertEqual(manifest["textureCount"], 2)
            self.assertFalse(manifest["definitionSource"]["packaged"])
            self.assertEqual(manifest["definitionSource"]["sha256"], _sha256(ini))
            self.assertEqual(
                [item["symbol"] for item in manifest["materials"]],
                ["rock", "GRASSA"],
            )
            self.assertEqual(
                [item["tableIndex"] for item in manifest["materials"]], [0, 1]
            )
            self.assertEqual(
                [item["png"] for item in manifest["materials"]],
                ["textures/0000.png", "textures/0001.png"],
            )
            self.assertEqual(
                [(item["width"], item["height"]) for item in manifest["materials"]],
                [(2, 4), (3, 2)],
            )
            for item in manifest["textures"]:
                png = first / item["png"]
                self.assertEqual(item["pngSha256"], _sha256(png))
                self.assertEqual(len(item["sourceSha256"]), 64)
                self.assertEqual(item["mode"], "RGBA")
                png.resolve().relative_to(first.resolve())
                with Image.open(png) as opened:
                    self.assertEqual(opened.format, "PNG")
                    self.assertEqual(opened.mode, "RGBA")

    def test_rejects_unresolved_ambiguous_and_non_exact_closures(self) -> None:
        def run_case(
            case_root: Path,
            ini_text: str,
            symbols: list[str],
            selected_names: list[str],
            *,
            duplicate_basename: bool = False,
            disguised_png: bool = False,
        ) -> None:
            ini = case_root / "terrain.ini"
            _write_ini(ini, ini_text)
            sources: list[tuple[str, Path]] = [("data/terrain.ini", ini)]
            for index, name in enumerate(selected_names):
                source = case_root / f"source-{index}" / name
                source.parent.mkdir(parents=True, exist_ok=True)
                if disguised_png:
                    Image.new("RGBA", (1, 1), (1, 2, 3, 255)).save(
                        source, format="PNG"
                    )
                elif source.suffix.casefold() == ".tga":
                    _write_tga(source, (1, 1), (index, 0, 0, 255))
                else:
                    source.write_bytes(b"synthetic")
                virtual_parent = "b" if duplicate_basename and index else "a"
                sources.append((f"art/{virtual_parent}/{name}", source))
            convert_terrain_materials(sources, case_root / "out", symbols)

        base_ini = "Terrain Grass\n Texture = grass.tga\nEnd\n"
        cases = [
            (
                "unresolved",
                base_ini,
                ["Missing"],
                ["grass.tga"],
                {},
                "unresolved",
            ),
            (
                "duplicate-definition",
                base_ini + "Terrain gRaSs\n Texture = grass.tga\nEnd\n",
                ["Grass"],
                ["grass.tga"],
                {},
                "duplicate terrain definition",
            ),
            (
                "ambiguous-texture",
                "Terrain Grass\n Texture = grass.tga\n Texture = other.tga\nEnd\n",
                ["Grass"],
                ["grass.tga"],
                {},
                "exactly one Texture",
            ),
            (
                "missing",
                base_ini + "Terrain Rock\n Texture = rock.tga\nEnd\n",
                ["Grass", "Rock"],
                ["grass.tga"],
                {},
                "missing selected",
            ),
            (
                "extra",
                base_ini,
                ["Grass"],
                ["grass.tga", "extra.tga"],
                {},
                "extra selected",
            ),
            (
                "unsafe-definition-name",
                "Terrain Grass\n Texture = ../grass.tga\nEnd\n",
                ["Grass"],
                ["grass.tga"],
                {},
                "unsafe Texture filename",
            ),
            (
                "duplicate-basename",
                base_ini,
                ["Grass"],
                ["grass.tga", "grass.tga"],
                {"duplicate_basename": True},
                "duplicate terrain material source basename",
            ),
            (
                "unsupported-source",
                base_ini,
                ["Grass"],
                ["grass.png"],
                {},
                "unsupported terrain material source format",
            ),
            (
                "unsupported-payload",
                base_ini,
                ["Grass"],
                ["grass.tga"],
                {"disguised_png": True},
                "unsupported image payload",
            ),
        ]
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            for name, ini_text, symbols, selected, arguments, message in cases:
                with self.subTest(name=name):
                    with self.assertRaisesRegex(ValueError, message):
                        run_case(
                            root / name,
                            ini_text,
                            symbols,
                            selected,
                            **arguments,
                        )

    def test_ignores_duplicate_unrequested_retail_definitions(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            ini = root / "terrain.ini"
            grass = root / "grass.tga"
            _write_ini(
                ini,
                "Terrain Unused\n Texture = old.tga\nEnd\n"
                "Terrain unused\n Texture = newer.tga\nEnd\n"
                "Terrain AlsoUnused\n Texture = ignored.tga\n"
                "Terrain Unsupported Form WithTokens\n Texture = ignored2.tga\nEnd\n"
                "Terrain Grass\n Texture = grass.tga\nEnd\n",
            )
            _write_tga(grass, (1, 1), (1, 2, 3, 255))
            paths = convert_terrain_materials(
                [
                    ("data/ini/terrain.ini", ini),
                    ("art/terrain/grass.tga", grass),
                ],
                root / "out",
                ["Grass"],
            )
            self.assertTrue((root / "out" / "terrain-materials.json").is_file())
            self.assertEqual(len(paths), 2)

    def test_profile_requires_bounded_explicit_symbols_and_output(self) -> None:
        base = {
            "format": 1,
            "id": "terrain-test",
            "pack": {"id": "terrain-test-pack"},
            "resources": [
                {
                    "id": "terrain-materials",
                    "kind": "texture",
                    "patterns": ["data/terrain.ini", "art/terrain/*.tga"],
                    "converter": "sage-terrain-materials",
                    "output": "maps/test/terrain-materials",
                    "limit": 3,
                    "options": {"symbols": ["Grass", "Rock"]},
                }
            ],
        }
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            valid_path = root / "valid.json"
            write_json_atomic(valid_path, base)
            profile = ImportProfile.load(valid_path)
            self.assertEqual(profile.resources[0].converter, "sage-terrain-materials")

            cases: list[tuple[str, dict, str]] = []
            missing = copy.deepcopy(base)
            missing["resources"][0]["options"] = {}
            cases.append(("missing", missing, "options.symbols"))
            duplicate = copy.deepcopy(base)
            duplicate["resources"][0]["options"]["symbols"] = ["Grass", "grass"]
            cases.append(("duplicate", duplicate, "duplicate terrain material symbols"))
            unsafe = copy.deepcopy(base)
            unsafe["resources"][0]["options"]["symbols"] = ["../Grass"]
            cases.append(("unsafe", unsafe, "unsafe terrain material symbol"))
            unbounded = copy.deepcopy(base)
            unbounded["resources"][0]["options"]["symbols"] = [
                f"T{index}" for index in range(MAX_TERRAIN_MATERIAL_SYMBOLS + 1)
            ]
            cases.append(("unbounded", unbounded, "options.symbols"))
            extra_option = copy.deepcopy(base)
            extra_option["resources"][0]["options"]["guess"] = True
            cases.append(("extra-option", extra_option, "unsupported option"))
            missing_output = copy.deepcopy(base)
            missing_output["resources"][0].pop("output")
            cases.append(("missing-output", missing_output, "requires an output directory"))
            for name, value, message in cases:
                with self.subTest(name=name):
                    path = root / f"{name}.json"
                    write_json_atomic(path, value)
                    with self.assertRaisesRegex(ValueError, message):
                        ImportProfile.load(path)

    def test_pipeline_runs_terrain_material_conversion_once_for_all_entries(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            ini = root / "sources" / "terrain.ini"
            grass = root / "sources" / "grass.tga"
            rock = root / "sources" / "rock.tga"
            _write_ini(
                ini,
                "Terrain Grass\n Texture = grass.tga\nEnd\n"
                "Terrain Rock\n Texture = rock.tga\nEnd\n",
            )
            _write_tga(grass, (1, 2), (1, 2, 3, 255))
            _write_tga(rock, (2, 1), (4, 5, 6, 255))
            entries = (
                CatalogEntry("synthetic.big", "data/terrain.ini", 1, ini.stat().st_size, 0),
                CatalogEntry("synthetic.big", "art/grass.tga", 2, grass.stat().st_size, 0),
                CatalogEntry("synthetic.big", "art/rock.tga", 3, rock.stat().st_size, 0),
            )
            rule = ResourceRule(
                id="terrain-materials",
                kind="texture",
                patterns=tuple(entry.name for entry in entries),
                required=True,
                converter="sage-terrain-materials",
                output="maps/test/terrain-materials",
                limit=3,
                expected_count=3,
                options={"symbols": ["Grass", "Rock"]},
            )
            profile = ImportProfile(
                source_sha256="a" * 64,
                id="synthetic-terrain",
                title="Synthetic Terrain",
                pack_id="synthetic-terrain-pack",
                pack_version="0.1.0",
                pack_metadata={"id": "synthetic-terrain-pack"},
                resources=(rule,),
                runtime_data={},
            )
            resolved = ResolvedProfile(
                profile,
                (ResolvedResource(rule, entries, (), None),),
            )
            source_paths = [ini, grass, rock]
            extracted = {
                (entry.archive.casefold(), entry.name.casefold()): {
                    "source_path": source_path,
                    "source_sha256": _sha256(source_path),
                    "cache_key": f"cache-{index}",
                }
                for index, (entry, source_path) in enumerate(zip(entries, source_paths))
            }

            pipeline = object.__new__(ImportPipeline)
            pipeline.packs_root = root / "packs"
            with (
                mock.patch.object(pipeline, "_attest_source_archives", return_value=[]),
                mock.patch.object(pipeline, "_verify_required_tools"),
                mock.patch.object(pipeline, "extract_sources", return_value=extracted),
                mock.patch.object(pipeline, "_canonical_tool_report", return_value={}),
                mock.patch.object(
                    pipeline,
                    "_convert_terrain_material_bundle",
                    wraps=pipeline._convert_terrain_material_bundle,
                ) as converted,
                mock.patch("openbfme_importer.pipeline.audit_pack", return_value={"valid": True}),
            ):
                pack = pipeline.build(resolved)

            self.assertEqual(converted.call_count, 1)
            provenance = json.loads(
                (pack / "provenance" / "manifest.json").read_text(encoding="utf-8")
            )
            outputs_per_source = [len(item["outputs"]) for item in provenance["entries"]]
            self.assertEqual(outputs_per_source, [3, 0, 0])
            self.assertTrue((pack / "maps/test/terrain-materials/terrain-materials.json").is_file())
            self.assertFalse(any(pack.rglob("*.ini")))
            self.assertFalse(any(pack.rglob("*.tga")))


if __name__ == "__main__":
    unittest.main()
