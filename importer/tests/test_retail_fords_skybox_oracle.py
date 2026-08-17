import hashlib
import json
from pathlib import Path
import unittest
from tests.retail_inputs import retail_file

try:
    from openbfme_importer.retail_fords_skybox_oracle import (
        _ascii_offsets,
        _selection_contract,
        compose_fords_skybox_oracle_contract,
    )
except ModuleNotFoundError:  # pragma: no cover - direct discovery fallback
    from importer.openbfme_importer.retail_fords_skybox_oracle import (
        _ascii_offsets,
        _selection_contract,
        compose_fords_skybox_oracle_contract,
    )


def _set_fixture(*, complete: bool) -> dict[str, object]:
    return {
        "complete": complete,
        "faces": [
            {
                "requestedName": f"Face{face}.tga",
                "selectedVirtualPath": f"face-{face}.dds" if complete else None,
            }
            for face in "NESWT"
        ],
        "name": "DefaultSky",
    }


class FordsSkyboxOracleUnitTests(unittest.TestCase):
    def test_ascii_offsets_require_exact_nul_terminated_literals(self) -> None:
        payload = b"X\0Face\0FaceX\0Face\0"
        self.assertEqual({"Face": [2, 13]}, _ascii_offsets(payload, ("Face",)))

    def test_no_override_never_promotes_executable_morning_literals(self) -> None:
        offsets = {f"TSMorning{face}.tga": [index] for index, face in enumerate("NESWT")}
        result = _selection_contract(
            map_has_settings=False,
            map_ini_has_override=False,
            default_set=_set_fixture(complete=False),
            executable_offsets=offsets,
        )
        self.assertFalse(result["proven"])
        self.assertIsNone(result["selectedTextureSet"])
        self.assertEqual([], result["candidateAssetClosure"])
        self.assertEqual(
            [
                "no-map-authored-world-sky-selection",
                "environment-default-semantics-do-not-state-selection",
                "default-sky-leaves-absent",
                "executable-morning-literals-are-not-active-selection-proof",
            ],
            [value["id"] for value in result["blockers"]],
        )

    def test_complete_default_set_still_does_not_prove_selection(self) -> None:
        result = _selection_contract(
            map_has_settings=False,
            map_ini_has_override=False,
            default_set=_set_fixture(complete=True),
            executable_offsets={},
        )
        self.assertFalse(result["proven"])
        self.assertNotIn(
            "default-sky-leaves-absent",
            [value["id"] for value in result["blockers"]],
        )


class FordsSkyboxOraclePrivateIntegrationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.repo = Path(__file__).resolve().parents[2]
        cls.paths = {
            "effective_assets_root": cls.repo
            / "workspace/retail-work/cache/effective-assets",
            "manifest_path": cls.repo
            / "workspace/retail-work/cache/effective-assets/.openbfme/manifest.json",
            "catalog_path": cls.repo / "workspace/retail-work/catalog/bfme2.json",
            "game_dat_path": retail_file("game.dat"),
        }
        if not all(path.exists() for path in cls.paths.values()):
            raise unittest.SkipTest("private BFME2 retail skybox evidence is unavailable")
        cls.contract = compose_fords_skybox_oracle_contract(**cls.paths)

    def test_exact_fail_closed_world_sky_summary(self) -> None:
        self.assertEqual(
            {
                "blockerCount": 4,
                "defaultSkyResolvedFaceCount": 0,
                "mapSkyboxOverridePresent": False,
                "morningResolvedFaceCount": 5,
                "reflectionClosureFileCount": 4,
                "worldSkySelectionProven": False,
            },
            self.contract["summary"],
        )
        selection = self.contract["worldSky"]["selection"]
        self.assertEqual([], selection["candidateAssetClosure"])
        self.assertIsNone(selection["selectedTextureSet"])

    def test_map_has_no_skybox_override_and_model_has_exact_placeholders(self) -> None:
        self.assertFalse(self.contract["map"]["skyboxSettingsChunkPresent"])
        self.assertEqual([], self.contract["map"]["mapIniSkyboxTokens"])
        self.assertEqual(20, self.contract["map"]["topLevelChunkCount"])
        self.assertEqual(
            [
                "SkyBox_01.tga",
                "SkyBox_02.tga",
                "SkyBox_03.tga",
                "SkyBox_04.tga",
            ],
            self.contract["worldSky"]["model"]["textureReferences"],
        )

    def test_game_dat_evidence_is_presence_only(self) -> None:
        executable = self.contract["evidence"]["executable"]
        self.assertEqual(10_969_600, executable["byteCount"])
        self.assertEqual(
            "f008b587570bad693981dc7218588c81d192a1e064b0f7f861539c51156a7640",
            executable["sha256"],
        )
        self.assertEqual([8_270_748], executable["asciiLiteralOffsets"]["SkyboxTextureSet"])
        self.assertEqual(
            [8_270_728], executable["asciiLiteralOffsets"]["TSMorningN.tga"]
        )
        self.assertIn("do not prove", executable["scope"])

    def test_reflection_skydome_is_separate_and_exactly_closed(self) -> None:
        reflection = self.contract["reflectionSkydome"]
        self.assertTrue(reflection["proven"])
        self.assertEqual(
            "placed-water-reflection-skydome-not-world-sky",
            reflection["classification"],
        )
        self.assertEqual(1, reflection["placementCount"])
        self.assertEqual(
            "0b253fb3b2153e12274133431ae3a1c007ffe80e430d9713ab94779ae739e226",
            reflection["assetClosureSha256"],
        )

    def test_contract_and_declared_digest_are_deterministic(self) -> None:
        second = compose_fords_skybox_oracle_contract(**self.paths)
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


if __name__ == "__main__":
    unittest.main()
