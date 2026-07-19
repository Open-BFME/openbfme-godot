import hashlib
import json
from pathlib import Path
import tempfile
import unittest

try:
    from openbfme_importer.retail_fords_water_reflection_oracle import (
        _canonical_sha256,
        _dds_header,
        _named_section,
        _object_declaration,
        _reflection_values,
        _validate_declared_digest,
        compose_fords_water_reflection_oracle,
    )
    from openbfme_importer.util import write_json_atomic
except ModuleNotFoundError:  # pragma: no cover - direct discovery fallback
    from importer.openbfme_importer.retail_fords_water_reflection_oracle import (
        _canonical_sha256,
        _dds_header,
        _named_section,
        _object_declaration,
        _reflection_values,
        _validate_declared_digest,
        compose_fords_water_reflection_oracle,
    )
    from importer.openbfme_importer.util import write_json_atomic


class FordsWaterReflectionOracleUnitTests(unittest.TestCase):
    def test_declared_digest_fails_closed(self) -> None:
        document = {"schema": "fixture", "value": 3}
        document["aggregateSha256"] = _canonical_sha256(document)
        self.assertEqual(
            document["aggregateSha256"],
            _validate_declared_digest(document, "fixture"),
        )
        document["value"] = 4
        with self.assertRaisesRegex(ValueError, "mismatch"):
            _validate_declared_digest(document, "fixture")

    def test_water_transparency_parser_preserves_authored_lines(self) -> None:
        text = """; comment
WaterTransparency
 ReflectionPlaneZ = 294.0;
 ReflectionOn = Yes
End
"""
        section = _named_section(text, "WaterTransparency")
        self.assertEqual(
            {
                "reflectionOn": True,
                "reflectionOnLine": 4,
                "reflectionPlaneZ": 294.0,
                "reflectionPlaneZLine": 3,
            },
            _reflection_values(section, "fixture"),
        )
        with self.assertRaisesRegex(ValueError, "exactly one"):
            _named_section(text + text, "WaterTransparency")

    def test_reflection_object_and_dds_headers_are_strict(self) -> None:
        declaration = _object_declaration(
            """Object WaterReflectionSkydome_GapOfRohan
 Draw = W3DScriptedModelDraw ModuleTag_01
  Model = WtrSky_GRohan
 End
 EditorSorting = SYSTEM
 Browser = SKYBOXES
 KindOf = SKYBOX INERT CAN_CAST_REFLECTIONS
End
""",
            "WaterReflectionSkydome_GapOfRohan",
        )
        self.assertEqual("WtrSky_GRohan", declaration["model"])
        self.assertEqual(
            ["SKYBOX", "INERT", "CAN_CAST_REFLECTIONS"], declaration["kindOf"]
        )

        dds = bytearray(128)
        dds[:4] = b"DDS "
        dds[4:8] = (124).to_bytes(4, "little")
        dds[12:16] = (512).to_bytes(4, "little")
        dds[16:20] = (256).to_bytes(4, "little")
        dds[28:32] = (9).to_bytes(4, "little")
        dds[80:84] = (4).to_bytes(4, "little")
        dds[84:88] = b"DXT1"
        self.assertEqual(
            {
                "compressionFourCC": "DXT1",
                "height": 512,
                "mipCount": 9,
                "width": 256,
            },
            _dds_header(bytes(dds)),
        )


class FordsWaterReflectionOraclePrivateIntegrationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.repo = Path(__file__).resolve().parents[2]
        cls.paths = {
            "effective_assets_root": cls.repo
            / ".private/retail-work/cache/effective-assets",
            "manifest_path": cls.repo
            / ".private/retail-work/cache/effective-assets/.openbfme/manifest.json",
            "catalog_path": cls.repo / ".private/retail-work/catalog/bfme2.json",
            "environment_report_path": cls.repo
            / ".private/retail-work/reports/retail-fords-environment-c1f300fcf6fed6f2.json",
            "visual_closure_path": cls.repo
            / ".private/retail-work/reports/retail-visual-closure-0d51ad8d31ca6e6c.json",
            "static_prop_plan_path": cls.repo
            / ".private/retail-work/reports/retail-static-prop-plan-0d51ad8d31ca6e6c.json",
            "cooked_map_directory": cls.repo
            / ".private/retail-work/packs/bfme2-men-vslice/maps/fords-of-isen-ii",
        }
        required = list(cls.paths.values())[:-1] + [
            cls.paths["cooked_map_directory"] / "objects.json",
            cls.paths["cooked_map_directory"] / "water.json",
        ]
        if not all(path.exists() for path in required):
            raise unittest.SkipTest("private Fords retail reflection evidence unavailable")
        cls.contract = compose_fords_water_reflection_oracle(**cls.paths)

    def test_exact_reflection_settings_areas_and_placement(self) -> None:
        self.assertEqual(
            {
                "reflectionEnabled": True,
                "reflectionPlaneSageZ": 294.0,
                "reflectionSkydomePlacementCount": 1,
                "rendererBlockerCount": 4,
                "standingWaterAreaCount": 4,
                "standingWaterPointCount": 25,
                "staticNativeAssetClosureResolved": True,
                "worldSkySeparated": True,
            },
            self.contract["summary"],
        )
        self.assertEqual(
            [(1, 294, 13), (3, 364, 4), (4, 365, 4), (5, 245, 4)],
            [
                (area["id"], area["waterHeight"], len(area["sagePoints"]))
                for area in self.contract["standingWater"]["areas"]
            ],
        )
        self.assertEqual([1], self.contract["standingWater"]["planeMatchingAreaIds"])
        placement = self.contract["reflectionSkydome"]["placement"]
        self.assertEqual(6, placement["index"])
        self.assertEqual(
            [1955.4375, 291.9921875, -1806.58154296875],
            placement["godotPosition"],
        )
        self.assertEqual("Skybox", placement["properties"]["objectLayer"])

    def test_exact_model_texture_and_world_sky_separation(self) -> None:
        skydome = self.contract["reflectionSkydome"]
        self.assertEqual(
            ["SKYBOX", "INERT", "CAN_CAST_REFLECTIONS"],
            skydome["objectDeclaration"]["kindOf"],
        )
        self.assertEqual(
            {
                "attributes": 0,
                "faceCount": 98,
                "identifier": "WTRSKY_GROHAN",
                "materialCount": 1,
                "meshName": "WTRSKY_GROHAN",
                "vertexCount": 57,
                "versionMajor": 5,
                "versionMinor": 0,
            },
            skydome["retailModel"]["mesh"],
        )
        self.assertEqual(
            "WtrSkydome_GapOfRohan.tga",
            skydome["retailModel"]["shaderProperties"]["Texture_0"],
        )
        self.assertEqual(
            ("DXT1", 512, 512, 10),
            (
                skydome["retailTexture"]["compressionFourCC"],
                skydome["retailTexture"]["width"],
                skydome["retailTexture"]["height"],
                skydome["retailTexture"]["mipCount"],
            ),
        )
        distinction = self.contract["worldSkyDistinction"]
        self.assertEqual(
            "art/w3d/ne/new_skybox.w3d", distinction["worldSkyModel"]
        )
        self.assertNotEqual(
            distinction["worldSkyModel"], distinction["waterReflectionModel"]
        )
        self.assertEqual(
            "unresolved-in-effective-tree",
            self.contract["standingWater"]["skyEnv"]["status"],
        )

    def test_renderer_semantics_remain_explicitly_unresolved(self) -> None:
        renderer = self.contract["godotRenderer"]
        self.assertFalse(renderer["parityReady"])
        self.assertIsNone(renderer["techniqueSelected"])
        self.assertEqual(
            [
                "reflection-pass-camera-and-clip-semantics",
                "reflection-only-skydome-visibility-semantics",
                "single-plane-to-four-water-area-compositing",
                "standing-water-skyenv-input",
            ],
            [row["id"] for row in renderer["blockers"]],
        )

    def test_generation_and_pretty_output_are_byte_identical(self) -> None:
        second = compose_fords_water_reflection_oracle(**self.paths)
        self.assertEqual(self.contract, second)
        payload = dict(self.contract)
        declared = payload.pop("aggregateSha256")
        canonical = json.dumps(
            payload,
            ensure_ascii=False,
            sort_keys=True,
            separators=(",", ":"),
            allow_nan=False,
        ).encode("utf-8")
        self.assertEqual(declared, hashlib.sha256(canonical).hexdigest())
        self.assertEqual(
            "c8e5df5a1e8f19817e639300bd977bfbf7ab5eeda8b63eedc634baa4c8d0b22a",
            declared,
        )
        with tempfile.TemporaryDirectory() as raw:
            first = Path(raw) / "first.json"
            other = Path(raw) / "other.json"
            write_json_atomic(first, self.contract)
            write_json_atomic(other, second)
            self.assertEqual(first.read_bytes(), other.read_bytes())


if __name__ == "__main__":
    unittest.main()
