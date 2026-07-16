from __future__ import annotations

import json
from pathlib import Path
import struct
import tempfile
import unittest

from importer.tests.test_sage_map import _synthetic_map
from openbfme_importer.sage_environment import (
    FORDS_MAP_INI_PATH,
    FORDS_MAP_PATH,
    GAME_DATA_INI_PATH,
    SCHEMA,
    WATER_INI_PATH,
    WATER_TEXTURES_INI_PATH,
    WEATHER_INI_PATH,
    SageEnvironmentError,
    build_fords_environment_report,
    write_fords_environment_report,
)


def _global_lighting_payload(*, first_ambient: float = 0.25) -> bytes:
    payload = struct.pack("<I", 2)
    for time_index in range(4):
        for light_index in range(9):
            ambient = first_ambient if time_index == 0 and light_index == 0 else 0.1
            payload += struct.pack(
                "<9f",
                ambient,
                0.2,
                0.3,
                0.4,
                0.5,
                0.6,
                -0.7,
                0.8,
                -0.9,
            )
    payload += struct.pack("<I", 0x40010203)
    payload += bytes(range(44))
    payload += struct.pack("<3f", 0.7, 0.8, 0.9)
    if len(payload) != 1_360:
        raise AssertionError(
            f"unexpected synthetic GlobalLighting size: {len(payload)}"
        )
    return payload


def _write(root: Path, virtual_path: str, source: bytes) -> None:
    path = root.joinpath(*virtual_path.split("/"))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(source)


def _fixture_root(
    root: Path,
    *,
    lighting_payload: bytes | None = None,
    lighting_version: int = 8,
) -> Path:
    map_source, _ = _synthetic_map(
        extra_top_record=(
            "GlobalLighting",
            lighting_version,
            _global_lighting_payload()
            if lighting_payload is None
            else lighting_payload,
        )
    )
    _write(root, FORDS_MAP_PATH, map_source)
    _write(
        root,
        FORDS_MAP_INI_PATH,
        b"""
Weather
  HardwareFogColor = R:10 G:20 B:30
  HardwareFogEnable = Yes
  HardwareFogStart = 100
  HardwareFogEnd = 900
End
WaterTransparency
  ReflectionPlaneZ = 42.0
  ReflectionOn = Yes
End
""",
    )
    _write(
        root,
        WEATHER_INI_PATH,
        b"""
Weather
  SnowEnabled = No
  IsSnowing = No
  SnowTexture = Snow.tga
  CloudTextureSize = X:10 Y:20
  CloudOffsetPerSecond = X:-0.1 Y:0.2
End
WeatherData SUNNY
  HasLightning = No
End
""",
    )
    _write(
        root,
        WATER_INI_PATH,
        b"""
WaterSet AFTERNOON
  SkyTexture = ActiveSky.tga
  WaterTexture = ActiveWater.tga
  DiffuseColor = R:1 G:2 B:3 A:4
  UScrollPerMS = 0.002
End
WaterTransparency
  TransparentWaterMinOpacity = 1.0
  TransparentWaterDepth = 3.0
  StandingWaterTexture = Surface.tga
  ReflectionPlaneZ = 5.0
  ReflectionOn = No
End
""",
    )
    _write(
        root,
        WATER_TEXTURES_INI_PATH,
        b"""
WaterTextureList WaterBumpMapTextures
  Texture = Bump.tga
End
WaterTextureList RiverTextures
  Texture = river.tga
End
""",
    )
    _write(
        root,
        GAME_DATA_INI_PATH,
        b"""
GameData
  UseCloudMap = Yes
  UseCloudPlane = Yes
  DrawSkyBox = Yes
  UseShadowVolumes = Yes
  UseShadowDecals = Yes
  UseShadowMapping = No
  DefaultCameraMinHeight = 120.0
  DefaultCameraMaxHeight = 300.0
  DefaultCameraPitchAngle = 37.5
  DefaultCameraYawAngle = 0.0
  DefaultCameraScrollSpeedScalar = 1.0
  TimeOfDay = AFTERNOON
  Weather = NORMAL
  TerrainLightingAfternoonAmbient = R:1 G:2 B:3
End
""",
    )
    for filename in (
        "ActiveSky.tga",
        "ActiveWater.tga",
        "Surface.tga",
        "Bump.tga",
        "Sky.tga",
        "water.w3d",
        "depth.tga",
        "river.tga",
        "noise.tga",
        "edge.tga",
        "sparkle.tga",
        "Snow.tga",
    ):
        _write(root, f"art/test/{filename}", ("fixture:" + filename).encode("ascii"))
    return root


class SageEnvironmentTests(unittest.TestCase):
    def test_report_is_deterministic_and_preserves_source_precedence(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = _fixture_root(Path(temporary) / "effective")
            first = build_fords_environment_report(root)
            second = build_fords_environment_report(root)

            self.assertEqual(first, second)
            self.assertEqual(first["schema"], SCHEMA)
            self.assertEqual(
                first["timeAndWeather"]["activeTimeOfDay"]["name"], "AFTERNOON"
            )
            lighting = first["lightingAndShadows"]["mapGlobalLighting"]
            self.assertEqual(len(lighting["configurations"]), 4)
            self.assertEqual(len(lighting["configurations"]["AFTERNOON"]), 9)
            self.assertEqual(lighting["shadowColor"]["packedArgb"], 0x40010203)
            self.assertEqual(
                lighting["unresolvedVersion8Field"]["byteCount"],
                44,
            )
            self.assertEqual(
                first["fog"]["overlayEvidence"]["mapDefinedKeys"],
                [
                    "HardwareFogColor",
                    "HardwareFogEnable",
                    "HardwareFogEnd",
                    "HardwareFogStart",
                ],
            )
            self.assertEqual(
                first["water"]["transparencyOverlayEvidence"]["overriddenGlobalKeys"],
                ["ReflectionOn", "ReflectionPlaneZ"],
            )
            self.assertEqual(
                first["water"]["activeWaterSet"]["section"], "WaterSet AFTERNOON"
            )
            self.assertEqual(
                first["water"]["mapMaterialRows"]["standingWaterAreaCount"], 1
            )
            self.assertEqual(first["water"]["mapMaterialRows"]["riverAreaCount"], 1)

            output_a = Path(temporary) / "a.json"
            output_b = Path(temporary) / "b.json"
            write_fords_environment_report(root, output_a)
            write_fords_environment_report(root, output_b)
            self.assertEqual(output_a.read_bytes(), output_b.read_bytes())
            self.assertEqual(
                json.loads(output_a.read_text(encoding="utf-8"))["aggregateSha256"],
                first["aggregateSha256"],
            )

    def test_truncated_global_lighting_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = _fixture_root(
                Path(temporary) / "effective",
                lighting_payload=struct.pack("<I", 2),
            )
            with self.assertRaisesRegex(SageEnvironmentError, "truncated"):
                build_fords_environment_report(root)

    def test_unsupported_global_lighting_version_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = _fixture_root(Path(temporary) / "effective", lighting_version=7)
            with self.assertRaisesRegex(
                SageEnvironmentError,
                "unsupported GlobalLighting version: 7",
            ):
                build_fords_environment_report(root)

    def test_nonfinite_lighting_value_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = _fixture_root(
                Path(temporary) / "effective",
                lighting_payload=_global_lighting_payload(first_ambient=float("nan")),
            )
            with self.assertRaisesRegex(SageEnvironmentError, "non-finite"):
                build_fords_environment_report(root)

    def test_unterminated_selected_ini_block_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = _fixture_root(Path(temporary) / "effective")
            _write(root, FORDS_MAP_INI_PATH, b"Weather\n HardwareFogEnable = Yes\n")
            with self.assertRaisesRegex(
                SageEnvironmentError, "unterminated Weather block"
            ):
                build_fords_environment_report(root)

    def test_duplicate_effective_assignment_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = _fixture_root(Path(temporary) / "effective")
            _write(
                root,
                FORDS_MAP_INI_PATH,
                b"""
Weather
  HardwareFogEnable = Yes
End
WaterTransparency
  ReflectionOn = Yes
  reflectionon = No
End
""",
            )
            with self.assertRaisesRegex(SageEnvironmentError, "duplicate assignment"):
                build_fords_environment_report(root)

    def test_nul_in_selected_ini_source_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = _fixture_root(Path(temporary) / "effective")
            _write(root, WEATHER_INI_PATH, b"Weather\0\nEnd\n")
            with self.assertRaisesRegex(SageEnvironmentError, "contains a NUL byte"):
                build_fords_environment_report(root)


if __name__ == "__main__":
    unittest.main()
