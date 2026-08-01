import hashlib
import json
from pathlib import Path
import tempfile
import unittest

try:
    from openbfme_importer.catalog import InstallCatalog
    from openbfme_importer.profile import ImportProfile, resolve_profile
    from openbfme_importer.retail_fords_environment_profile import (
        RUNTIME_DATA_PATH,
        _canonical_sha256,
        _resolve_texture_name,
        _skybox_sets,
        _validate_declared_digest,
        compose_fords_environment_plan,
    )
except ModuleNotFoundError:  # pragma: no cover - direct discovery fallback
    from importer.openbfme_importer.catalog import InstallCatalog
    from importer.openbfme_importer.profile import ImportProfile, resolve_profile
    from importer.openbfme_importer.retail_fords_environment_profile import (
        RUNTIME_DATA_PATH,
        _canonical_sha256,
        _resolve_texture_name,
        _skybox_sets,
        _validate_declared_digest,
        compose_fords_environment_plan,
    )


class FordsEnvironmentProfileUnitTests(unittest.TestCase):
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

    def test_texture_resolution_prefers_exact_name_then_compiled_stem(self) -> None:
        files = {
            "art/textures/Face.tga": {"sha256": "a" * 64, "size": 1},
            "art/compiledtextures/fa/face.dds": {
                "sha256": "b" * 64,
                "size": 2,
            },
            "art/compiledtextures/on/only.dds": {
                "sha256": "c" * 64,
                "size": 3,
            },
        }
        exact = _resolve_texture_name("Face.tga", files)
        self.assertEqual("art/textures/Face.tga", exact["selectedVirtualPath"])
        self.assertEqual("one-exact-filename-candidate", exact["status"])
        compiled = _resolve_texture_name("Only.tga", files)
        self.assertEqual(
            "art/compiledtextures/on/only.dds", compiled["selectedVirtualPath"]
        )
        self.assertEqual("one-compiled-dds-stem-counterpart", compiled["status"])

    def test_skybox_set_requires_all_five_named_faces(self) -> None:
        payload = b"""SkyboxTextureSet Test
 SkyboxTextureN = North.tga
 SkyboxTextureE = East.tga
 SkyboxTextureS = South.tga
 SkyboxTextureW = West.tga
 SkyboxTextureT = Top.tga
End
"""
        files = {
            f"art/compiledtextures/fi/{name.casefold()}.dds": {
                "sha256": str(index) * 64,
                "size": index,
            }
            for index, name in enumerate(
                ("North", "East", "South", "West", "Top"), start=1
            )
        }
        sets = _skybox_sets(payload, files)
        self.assertEqual(1, len(sets))
        self.assertTrue(sets[0]["complete"])
        self.assertEqual(["N", "E", "S", "W", "T"], [
            row["face"] for row in sets[0]["faces"]
        ])
        with self.assertRaisesRegex(ValueError, "exactly five faces"):
            _skybox_sets(payload.replace(b" SkyboxTextureT = Top.tga\n", b""), files)


class FordsEnvironmentProfilePrivateIntegrationTests(unittest.TestCase):
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
        }
        if not all(path.exists() for path in cls.paths.values()):
            raise unittest.SkipTest("private BFME2 retail environment evidence is unavailable")
        cls.plan = compose_fords_environment_plan(**cls.paths)

    def test_exact_resource_and_candidate_census(self) -> None:
        self.assertEqual(
            {
                "blockerCount": 4,
                "catalogSelectedEntryCount": 3,
                "completeSkyboxTextureSetCount": 8,
                "profileResourceCount": 3,
                "resolvedSkyboxFaceAssignmentCount": 40,
                "skyboxTextureSetCount": 11,
                "uniqueResolvedSkyboxFaceFileCount": 37,
                "worldSkySelectionResolved": False,
            },
            self.plan["summary"],
        )
        self.assertEqual(
            RUNTIME_DATA_PATH,
            next(iter(self.plan["profileFragment"]["runtimeDataMerge"])),
        )

    def test_exact_fog_light_shadow_and_fail_closed_sky_contract(self) -> None:
        runtime = self.plan["runtimeEnvironment"]
        self.assertEqual({"name": "AFTERNOON", "wireValue": 2}, runtime["activeTimeOfDay"])
        self.assertEqual({"name": "NORMAL", "wireValue": 0}, runtime["mapWeather"])
        self.assertEqual([220, 226, 235], runtime["fog"]["colorRgb8"])
        self.assertEqual((350, 2000), (runtime["fog"]["start"], runtime["fog"]["end"]))
        self.assertEqual(9, len(runtime["lighting"]["activeSourceRows"]))
        self.assertIsNone(runtime["lighting"]["godotBasisTransform"])
        self.assertTrue(runtime["shadows"]["useShadowVolumes"])
        self.assertTrue(runtime["shadows"]["useShadowDecals"])
        self.assertFalse(runtime["shadows"]["useShadowMapping"])
        self.assertIsNone(runtime["worldSky"]["selectedTextureSet"])
        self.assertFalse(runtime["parityReady"])

    def test_profile_fragment_resolves_with_zero_missing(self) -> None:
        resources = self.plan["profileFragment"]["resources"]
        document = {
            "format": 1,
            "id": "retail-fords-environment-test",
            "title": "Retail Fords environment test",
            "pack": {"id": "bfme2-men-vslice", "version": "1.06-v0"},
            "resources": resources,
        }
        with tempfile.TemporaryDirectory() as raw:
            path = Path(raw) / "profile.json"
            path.write_text(json.dumps(document), encoding="utf-8")
            profile = ImportProfile.load(path)
        resolved = resolve_profile(profile, InstallCatalog.load(self.paths["catalog_path"]))
        self.assertEqual((), resolved.missing_required)
        self.assertEqual(3, len(resolved.selected_entries))

    def test_generation_and_declared_digest_are_deterministic(self) -> None:
        second = compose_fords_environment_plan(**self.paths)
        self.assertEqual(self.plan, second)
        payload = dict(self.plan)
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
