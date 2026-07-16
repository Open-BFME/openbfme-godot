import json
from pathlib import Path
import tempfile
import unittest

try:
    from openbfme_importer.catalog import InstallCatalog
    from openbfme_importer.profile import ImportProfile, resolve_profile
    from openbfme_importer.retail_fords_ambient_audio_profile import (
        EXPECTED_PLACEMENT_COUNTS,
        ROOT_IDS,
        TYPE_EVENT_IDS,
        _fragment_profile,
        _resolve_parameter_value,
        _resolve_sample_records,
        _runtime_mapping,
        compose_fords_ambient_audio_plan,
    )
except ModuleNotFoundError:  # pragma: no cover - direct discovery fallback
    from importer.openbfme_importer.catalog import InstallCatalog
    from importer.openbfme_importer.profile import ImportProfile, resolve_profile
    from importer.openbfme_importer.retail_fords_ambient_audio_profile import (
        EXPECTED_PLACEMENT_COUNTS,
        ROOT_IDS,
        TYPE_EVENT_IDS,
        _fragment_profile,
        _resolve_parameter_value,
        _resolve_sample_records,
        _runtime_mapping,
        compose_fords_ambient_audio_plan,
    )


class FordsAmbientAudioUnitTests(unittest.TestCase):
    def test_numeric_macro_resolution_is_exact_and_case_insensitive(self) -> None:
        macros = {"amb_min_range": "300", "amb_max_range": "800"}
        self.assertEqual("300", _resolve_parameter_value(" AMB_MIN_RANGE ", macros))
        self.assertEqual("800", _resolve_parameter_value("amb_MAX_range", macros))
        self.assertEqual("400", _resolve_parameter_value(" 400 ", macros))

    def test_runtime_mapping_requires_the_exact_seven_rows(self) -> None:
        rows = "\n".join(
            f'\t"{key}": "{value}",'
            for key, value in TYPE_EVENT_IDS.items()
        )
        source = (
            "const FORDS_AMBIENT_TYPE_EVENT_IDS: Dictionary = {\n"
            f"{rows}\n"
            "}\n"
        )
        self.assertEqual(TYPE_EVENT_IDS, _runtime_mapping(source))
        with self.assertRaisesRegex(ValueError, "mapping changed"):
            _runtime_mapping(source.replace("Amb_MTBirds1Loop", "WrongEvent", 1))

    def test_sample_resolution_fails_closed_on_ambiguous_stems(self) -> None:
        files = {
            "data/audio/a/same.wav": {"sha256": "a" * 64, "size": 1},
            "data/audio/b/same.mp3": {"sha256": "b" * 64, "size": 2},
        }
        with self.assertRaisesRegex(ValueError, "ambiguous audio sample"):
            _resolve_sample_records(["same"], files)


class FordsAmbientAudioPrivateIntegrationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.repo = Path(__file__).resolve().parents[2]
        cls.paths = {
            "effective_assets_root": cls.repo
            / ".private/retail-work/cache/effective-assets",
            "manifest_path": cls.repo
            / ".private/retail-work/cache/effective-assets/.openbfme/manifest.json",
            "catalog_path": cls.repo / ".private/retail-work/catalog/bfme2.json",
            "map_objects_path": cls.repo
            / ".private/retail-work/packs/bfme2-men-vslice/maps/fords-of-isen-ii/objects.json",
            "complete_profile_path": cls.repo
            / ".private/retail-work/profiles/men-fords-v0-full.generated.json",
            "runtime_audio_path": cls.repo
            / "game/src/retail_slice/retail_slice_audio.gd",
        }
        if not all(path.exists() for path in cls.paths.values()):
            raise unittest.SkipTest("private BFME2 retail evidence is not available")
        cls.plan = compose_fords_ambient_audio_plan(**cls.paths)

    def test_exact_private_closure_and_placement_contract(self) -> None:
        self.assertEqual(
            {
                "ambientStreamCount": 1,
                "audioEventCount": 6,
                "catalogSelectedEntryCount": 57,
                "mapPlacementCount": 50,
                "mp3SampleCount": 1,
                "multisoundCount": 0,
                "profileResourceCount": 2,
                "reusedSampleCount": 0,
                "rootCount": 7,
                "sampleCount": 57,
                "wavSampleCount": 56,
            },
            self.plan["summary"],
        )
        self.assertEqual(
            EXPECTED_PLACEMENT_COUNTS,
            self.plan["scope"]["placementEvidence"]["countsByType"],
        )
        self.assertEqual(sorted(ROOT_IDS, key=str.casefold), self.plan["scope"]["rootIds"])
        self.assertEqual(
            57, len(self.plan["runtimeAudioRegistryAddition"]["samples"])
        )

    def test_generation_is_deterministic(self) -> None:
        second = compose_fords_ambient_audio_plan(**self.paths)
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
        import hashlib

        self.assertEqual(declared, hashlib.sha256(canonical).hexdigest())

    def test_profile_fragment_resolves_real_catalog_with_zero_missing(self) -> None:
        resources = self.plan["profileFragment"]["resources"]
        profile = _fragment_profile(resources)
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "profile.json"
            path.write_text(json.dumps(profile), encoding="utf-8")
            parsed = ImportProfile.load(path)
        resolved = resolve_profile(parsed, InstallCatalog.load(self.paths["catalog_path"]))
        self.assertEqual((), resolved.missing_required)
        self.assertEqual(57, len(resolved.selected_entries))


if __name__ == "__main__":
    unittest.main()
