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
    ResolvedProfile,
    ResolvedResource,
    ResourceRule,
)
from openbfme_importer.util import write_json_atomic


def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _profile_payload() -> dict:
    return {
        "format": 1,
        "id": "atlas-test",
        "pack": {"id": "atlas-test-pack"},
        "resources": [
            {
                "id": "command-icons",
                "kind": "ui",
                "patterns": ["art/atlas.png"],
                "converter": "texture-atlas-crops",
                "output": "assets/ui/commands",
                "limit": 1,
                "expected_count": 1,
                "options": {
                    "crops": [
                        {
                            "logicalName": "IconBeta",
                            "output": "beta.png",
                            "crop": [2, 0, 2, 2],
                        },
                        {
                            "logicalName": "IconAlpha",
                            "output": "nested/alpha.png",
                            "crop": [0, 0, 2, 2],
                        },
                    ]
                },
            }
        ],
    }


def _write_atlas(path: Path) -> None:
    image = Image.new("RGB", (4, 2))
    image.putdata(
        [
            (255, 0, 0),
            (0, 255, 0),
            (0, 0, 255),
            (255, 255, 0),
            (10, 20, 30),
            (40, 50, 60),
            (70, 80, 90),
            (100, 110, 120),
        ]
    )
    image.save(path, format="PNG", compress_level=9, optimize=False)


class TextureAtlasCropTests(unittest.TestCase):
    def test_multiple_crops_are_canonical_rgba_and_byte_deterministic(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            profile_path = root / "profile.json"
            write_json_atomic(profile_path, _profile_payload())
            rule = ImportProfile.load(profile_path).resources[0]
            self.assertEqual(
                [item["logicalName"] for item in rule.options["crops"]],
                ["IconAlpha", "IconBeta"],
            )

            source = root / "atlas.png"
            _write_atlas(source)
            pipeline = object.__new__(ImportPipeline)
            first_root = root / "first"
            second_root = root / "second"
            first = pipeline._convert_texture_atlas_crops(
                source, rule.output, rule.options, first_root
            )
            second = pipeline._convert_texture_atlas_crops(
                source, rule.output, rule.options, second_root
            )

            self.assertEqual(
                [path.relative_to(first_root).as_posix() for path in first],
                [
                    "assets/ui/commands/nested/alpha.png",
                    "assets/ui/commands/beta.png",
                ],
            )
            self.assertEqual([_sha256(path) for path in first], [_sha256(path) for path in second])
            with Image.open(first[0]) as alpha:
                self.assertEqual(alpha.mode, "RGBA")
                self.assertEqual(alpha.size, (2, 2))
                self.assertEqual(alpha.getpixel((0, 0)), (255, 0, 0, 255))
            with Image.open(first[1]) as beta:
                self.assertEqual(beta.mode, "RGBA")
                self.assertEqual(beta.getpixel((0, 0)), (0, 0, 255, 255))

    def test_profile_rejects_unbounded_unsafe_duplicate_and_malformed_crops(self) -> None:
        base = _profile_payload()
        cases: list[tuple[str, dict, str]] = []

        missing_expected = copy.deepcopy(base)
        missing_expected["resources"][0].pop("expected_count")
        cases.append(("missing-expected", missing_expected, "requires expected_count=1"))

        missing_output = copy.deepcopy(base)
        missing_output["resources"][0].pop("output")
        cases.append(("missing-output", missing_output, "requires an output directory"))

        unsupported = copy.deepcopy(base)
        unsupported["resources"][0]["options"]["guess"] = True
        cases.append(("unsupported", unsupported, "unsupported option"))

        empty = copy.deepcopy(base)
        empty["resources"][0]["options"]["crops"] = []
        cases.append(("empty", empty, "array of 1..64"))

        unbounded = copy.deepcopy(base)
        unbounded["resources"][0]["options"]["crops"] = [
            {"logicalName": f"Icon{index}", "output": f"{index}.png", "crop": [0, 0, 1, 1]}
            for index in range(65)
        ]
        cases.append(("unbounded", unbounded, "array of 1..64"))

        escaping = copy.deepcopy(base)
        escaping["resources"][0]["options"]["crops"][0]["output"] = "../escape.png"
        cases.append(("escaping", escaping, "unsafe texture atlas crop output"))

        duplicate_logical = copy.deepcopy(base)
        duplicate_logical["resources"][0]["options"]["crops"][1]["logicalName"] = "iconbeta"
        cases.append(("duplicate-logical", duplicate_logical, "duplicate texture atlas logicalName"))

        duplicate_output = copy.deepcopy(base)
        duplicate_output["resources"][0]["options"]["crops"][1]["output"] = "BETA.PNG"
        cases.append(("duplicate-output", duplicate_output, "duplicate texture atlas crop outputs"))

        boolean_coordinate = copy.deepcopy(base)
        boolean_coordinate["resources"][0]["options"]["crops"][0]["crop"][0] = True
        cases.append(("boolean-coordinate", boolean_coordinate, "nonnegative integer coordinates"))

        zero_width = copy.deepcopy(base)
        zero_width["resources"][0]["options"]["crops"][0]["crop"][2] = 0
        cases.append(("zero-width", zero_width, "positive dimensions"))

        extra_field = copy.deepcopy(base)
        extra_field["resources"][0]["options"]["crops"][0]["source"] = "guess"
        cases.append(("extra-field", extra_field, "contain exactly"))

        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            for name, payload, message in cases:
                with self.subTest(name=name):
                    profile_path = root / f"{name}.json"
                    write_json_atomic(profile_path, payload)
                    with self.assertRaisesRegex(ValueError, message):
                        ImportProfile.load(profile_path)

    def test_out_of_bounds_crop_fails_before_writing_any_output(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            source = root / "atlas.png"
            _write_atlas(source)
            pipeline = object.__new__(ImportPipeline)
            options = {
                "crops": [
                    {"logicalName": "Good", "output": "good.png", "crop": [0, 0, 1, 1]},
                    {"logicalName": "Outside", "output": "outside.png", "crop": [3, 1, 2, 1]},
                ]
            }
            with self.assertRaisesRegex(ValueError, "exceeds source image bounds"):
                pipeline._convert_texture_atlas_crops(
                    source, "assets/ui", options, root / "pack"
                )
            self.assertFalse((root / "pack").exists())

    def test_pipeline_records_every_crop_once_on_the_single_source(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            source = root / "atlas.png"
            _write_atlas(source)
            entry = CatalogEntry("synthetic.big", "art/atlas.png", 1, source.stat().st_size, 0)
            loaded_rule = _profile_payload()["resources"][0]
            rule = ResourceRule(
                id=loaded_rule["id"],
                kind=loaded_rule["kind"],
                patterns=("art/atlas.png",),
                required=True,
                converter="texture-atlas-crops",
                output=loaded_rule["output"],
                limit=1,
                expected_count=1,
                options=loaded_rule["options"],
            )
            profile = ImportProfile(
                source_sha256="a" * 64,
                id="synthetic-atlas",
                title="Synthetic Atlas",
                pack_id="synthetic-atlas-pack",
                pack_version="0.1.0",
                pack_metadata={"id": "synthetic-atlas-pack"},
                resources=(rule,),
                runtime_data={},
            )
            resolved = ResolvedProfile(
                profile,
                (ResolvedResource(rule, (entry,), (), None),),
            )
            extracted = {
                (entry.archive.casefold(), entry.name.casefold()): {
                    "source_path": source,
                    "source_sha256": _sha256(source),
                    "cache_key": "synthetic-cache-key",
                }
            }
            pipeline = object.__new__(ImportPipeline)
            pipeline.packs_root = root / "packs"
            with (
                mock.patch.object(pipeline, "_attest_source_archives", return_value=[]),
                mock.patch.object(pipeline, "_verify_required_tools"),
                mock.patch.object(pipeline, "extract_sources", return_value=extracted),
                mock.patch.object(pipeline, "_canonical_tool_report", return_value={}),
                mock.patch("openbfme_importer.pipeline.audit_pack", return_value={"valid": True}),
            ):
                pack = pipeline.build(resolved)

            provenance = json.loads(
                (pack / "provenance" / "manifest.json").read_text(encoding="utf-8")
            )
            outputs = provenance["entries"][0]["outputs"]
            self.assertEqual(len(outputs), 2)
            self.assertEqual(
                [item["path"] for item in outputs],
                [
                    "assets/ui/commands/nested/alpha.png",
                    "assets/ui/commands/beta.png",
                ],
            )


if __name__ == "__main__":
    unittest.main()
