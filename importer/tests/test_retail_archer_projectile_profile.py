from __future__ import annotations

import copy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from openbfme_importer.catalog import InstallCatalog
from openbfme_importer.paths import repo_root_from_module
from openbfme_importer.profile import ImportProfile, resolve_profile
from openbfme_importer.retail_archer_projectile_profile import (
    ARCHER_PROJECTILE_BINDING_SCHEMA,
    ARCHER_PROJECTILE_PLAN_SCHEMA,
    RUNTIME_DATA_PATH,
    build_retail_archer_projectile_plan,
    generated_import_profile,
)


class RetailArcherProjectileProfileTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.repo = repo_root_from_module()
        cls.effective = cls.repo / "workspace" / "retail-work" / "cache" / "effective-assets"
        cls.manifest_path = cls.effective / ".openbfme" / "manifest.json"
        cls.catalog_path = cls.repo / "workspace" / "retail-work" / "catalog" / "bfme2.json"
        cls.base_path = (
            cls.repo
            / "workspace"
            / "retail-work"
            / "profiles"
            / "men-fords-v0-complete.generated.json"
        )
        cls.report_path = (
            cls.repo / "workspace" / "scratch" / "combat-visual-parity" / "REPORT.md"
        )
        required = (
            cls.manifest_path,
            cls.catalog_path,
            cls.base_path,
            cls.report_path,
        )
        if not all(path.is_file() for path in required):
            raise unittest.SkipTest("private BFME2 Archer closure inputs are unavailable")
        cls.manifest = json.loads(cls.manifest_path.read_text(encoding="utf-8"))
        cls.base = json.loads(cls.base_path.read_text(encoding="utf-8"))
        cls.report = cls.report_path.read_bytes()
        cls.catalog = InstallCatalog.load(cls.catalog_path)
        catalog_sha = hashlib.sha256(cls.catalog_path.read_bytes()).hexdigest()
        cls.first = build_retail_archer_projectile_plan(
            cls.manifest,
            cls.base,
            cls.report,
            cls.effective,
            cls.catalog,
            catalog_sha256=catalog_sha,
        )
        cls.second = build_retail_archer_projectile_plan(
            cls.manifest,
            cls.base,
            cls.report,
            cls.effective,
            cls.catalog,
            catalog_sha256=catalog_sha,
        )

    def test_real_closure_is_deterministic_exact_and_zero_missing(self) -> None:
        self.assertEqual(self.first, self.second)
        self.assertEqual(self.first["schema"], ARCHER_PROJECTILE_PLAN_SCHEMA)
        self.assertEqual(self.first["summary"]["exactClosureSourceFileCount"], 109)
        self.assertEqual(self.first["summary"]["newSelectedSourceFileCount"], 106)
        self.assertEqual(self.first["summary"]["reusedExactLeafCount"], 3)
        self.assertEqual(self.first["summary"]["profileFragmentResourceCount"], 4)
        self.assertEqual(self.first["summary"]["audioLeafCount"], 100)
        self.assertEqual(self.first["summary"]["fireAudioLeafCount"], 32)
        self.assertEqual(self.first["summary"]["impactAudioLeafCount"], 68)
        self.assertEqual(self.first["catalogResolution"]["missingRequired"], [])

    def test_runtime_binding_preserves_streak_impact_and_uncertainty(self) -> None:
        fragment = self.first["profileFragment"]
        self.assertEqual(fragment["runtimeDataPath"], RUNTIME_DATA_PATH)
        runtime = fragment["runtimeData"]
        self.assertEqual(runtime["schema"], ARCHER_PROJECTILE_BINDING_SCHEMA)
        self.assertEqual(runtime["weapon"]["projectileTemplateId"], "GondorArcherArrow")
        self.assertEqual(runtime["weapon"]["warheadTemplateId"], "GondorArcherBowWarhead")
        self.assertEqual(runtime["projectilePresentation"]["kind"], "W3DStreakDraw")
        self.assertIsNone(runtime["projectilePresentation"]["model"])
        self.assertEqual(
            runtime["projectilePresentation"]["texture"],
            "assets/textures/combat/archer/exarrowstreak01.png",
        )
        self.assertEqual(runtime["impactPresentation"]["attachedModelId"], "g_arrow")
        self.assertEqual(runtime["impactPresentation"]["soundEventId"], "ImpactArrow")
        self.assertEqual(len(runtime["damage"]["majorFxMappings"]), 4)
        self.assertEqual(len(runtime["unresolvedEngineSemantics"]), 5)

    def test_generated_profile_resolves_against_real_catalog(self) -> None:
        document = generated_import_profile(self.first)
        with tempfile.TemporaryDirectory() as raw:
            path = Path(raw) / "profile.json"
            path.write_text(json.dumps(document), encoding="utf-8")
            profile = ImportProfile.load(path)
        resolved = resolve_profile(profile, self.catalog)
        self.assertEqual(resolved.missing_required, ())
        self.assertEqual(len(profile.resources), 7)
        self.assertEqual(len(resolved.selected_entries), 199)
        self.assertIn(RUNTIME_DATA_PATH, profile.runtime_data)
        impact = next(
            resource
            for resource in document["resources"]
            if resource["id"] == "archer-projectile-impact-model"
        )
        self.assertEqual(impact["converter"], "w3d-bundle")
        self.assertEqual(impact["options"]["model"], "g_arrow.w3d")
        self.assertEqual(impact["options"]["animations"], ["g_arrow.w3d"])
        self.assertEqual(
            self.first["sourceEvidence"]["impactModel"][
                "conversionClassification"
            ],
            "self-contained-embedded-animation",
        )

    def test_plan_digest_tampering_fails_closed(self) -> None:
        tampered = copy.deepcopy(self.first)
        tampered["summary"]["audioLeafCount"] = 99
        with self.assertRaisesRegex(ValueError, "digest mismatch"):
            generated_import_profile(tampered)

    def test_combat_report_finding_is_required(self) -> None:
        with self.assertRaisesRegex(ValueError, "missing required finding"):
            build_retail_archer_projectile_plan(
                self.manifest,
                self.base,
                b"not the sealed combat report",
                self.effective,
                self.catalog,
                catalog_sha256=hashlib.sha256(self.catalog_path.read_bytes()).hexdigest(),
            )


if __name__ == "__main__":
    unittest.main()
