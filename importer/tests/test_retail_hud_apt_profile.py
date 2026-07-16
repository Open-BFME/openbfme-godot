from __future__ import annotations

from copy import deepcopy
import json
from pathlib import Path
import tempfile
import unittest

from openbfme_importer.catalog import InstallCatalog
from openbfme_importer.pipeline import ImportPipeline
from openbfme_importer.profile import ImportProfile, resolve_profile
from openbfme_importer.retail_hud_apt_profile import (
    build_retail_hud_apt_plan,
    generated_import_profile,
    write_generated_import_profile,
    write_retail_hud_apt_plan,
)


REPO = Path(__file__).resolve().parents[2]
ROOT = REPO / ".private" / "retail-work" / "cache" / "effective-assets"
MANIFEST = ROOT / ".openbfme" / "manifest.json"
CATALOG = REPO / ".private" / "retail-work" / "catalog" / "bfme2.json"
ORACLE = REPO / ".private" / "scratch" / "hud-apt-oracle" / "REPORT.md"
EXTERNAL_MOVIES = (
    REPO / ".private" / "scratch" / "hud-external-movies" / "contract-a.json"
)


@unittest.skipUnless(
    ROOT.is_dir()
    and MANIFEST.is_file()
    and CATALOG.is_file()
    and ORACLE.is_file()
    and EXTERNAL_MOVIES.is_file(),
    "private BFME2 HUD retail closure is unavailable",
)
class RetailHudAptProfileTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
        cls.catalog = InstallCatalog.load(CATALOG)
        cls.oracle = ORACLE.read_bytes()
        cls.external_movies = json.loads(EXTERNAL_MOVIES.read_text(encoding="utf-8"))
        cls.plan = build_retail_hud_apt_plan(
            ROOT, cls.manifest, cls.catalog, cls.oracle, cls.external_movies
        )

    def test_exact_closure_composite_ids_and_fail_closed_semantics(self) -> None:
        summary = self.plan["summary"]
        self.assertEqual(summary["aptBundleCount"], 9)
        self.assertEqual(summary["aptClosureFileCount"], 260)
        self.assertEqual(summary["aptClosurePayloadBytes"], 10_410_403)
        self.assertEqual(summary["atlasCount"], 24)
        self.assertEqual(summary["geometryFileCount"], 209)
        self.assertEqual(summary["wndWindowCount"], 87)
        self.assertEqual(summary["profileResourceCount"], 2)
        self.assertEqual(summary["profileSelectedSourceCount"], 262)
        self.assertEqual(summary["fontResourceCount"], 1)
        self.assertEqual(summary["sourceAttestationResourceCount"], 0)
        self.assertEqual(summary["textureConversionResourceCount"], 0)
        self.assertEqual(summary["runtimeBundleResourceCount"], 1)
        replacement = self.plan["legacyAssumptionReplacement"]
        self.assertEqual(replacement["rejectedMappedImageId"], "SGCommandBar")
        self.assertEqual(replacement["requiredSceneId"], "bfme2.ui.palantir")
        output_ids = {row["id"] for row in self.plan["compositeOutputs"]}
        self.assertIn("bfme2.ui.palantir.symbols", output_ids)
        self.assertIn("bfme2.ui.palantir.timelines", output_ids)
        self.assertIn("bfme2.ui.controlbar.layout.800x600", output_ids)
        outputs = {row["id"]: row for row in self.plan["compositeOutputs"]}
        self.assertEqual(
            outputs["bfme2.ui.palantir.atlases"]["status"],
            "runtime-bundle-atlases-ready",
        )
        self.assertIn(
            "fail-closed",
            outputs["bfme2.ui.palantir.timelines"]["status"],
        )
        unsupported = self.plan["sceneContract"]["unsupportedSemantics"]
        self.assertIn("never-evaluated", unsupported["actionScript"])
        self.assertIn("fail-closed", unsupported["unknownCallbacks"])
        frame_ids = self.plan["sceneContract"]["frameIds"]
        self.assertEqual(
            frame_ids["goodSingle"], "bfme2.ui.palantir.frame.good.single"
        )
        bindings = self.plan["sceneContract"]["runtimeAssetBindings"]
        self.assertTrue(bindings["sourceVirtualPathsAreEvidenceOnly"])
        self.assertEqual(len(bindings["atlasTextures"]), 24)
        self.assertTrue(
            all(row["png"].endswith(".png") for row in bindings["atlasTextures"])
        )
        self.assertEqual(
            bindings["externalFonts"],
            [
                {
                    "fontId": "palantir:63",
                    "fontName": "Albertus MT",
                    "resourceId": "men-hud-font-albertus-mt",
                    "sourceVirtualPath": "albertusmt.otf",
                    "cookedFont": (
                        "assets/ui/palantir/fonts/albertusmt-6a1990e17f14.otf"
                    ),
                    "sourceSha256": (
                        "6a1990e17f14ce5be199dde10f56dac3efd66aaa8e91d46119952cf55a9d9ba0"
                    ),
                }
            ],
        )
        self.assertEqual(
            self.plan["sourceEvidence"]["effectiveAssets"]["albertusMtFont"],
            {
                "archive": "_patch103.big",
                "offset": 2510,
                "path": "albertusmt.otf",
                "precedence": 0,
                "sha256": (
                    "6a1990e17f14ce5be199dde10f56dac3efd66aaa8e91d46119952cf55a9d9ba0"
                ),
                "size": 24_712,
            },
        )
        self.assertEqual(
            [
                (row["loadOrder"], row["movie"], row["resolvesTo"], row["target"])
                for row in self.plan["sceneContract"]["externalMovieEdges"]
            ],
            [
                (0, "InGameSpellBook.swf", "InGameSpellBook", "SpellBookUI"),
                (
                    1,
                    "InGameSideCommandBar.swf",
                    "InGameSideCommandBar",
                    "SideCommandBar",
                ),
                (2, "InGameHelpBox.swf", "InGameHelpBox", "helpBox"),
                (3, "InGameHeroSelect.swf", "InGameHeroSelect", "HeroSelectUI"),
                (
                    4,
                    "InGamePlanningMode.swf",
                    "InGamePlanningMode",
                    "planningModeUI",
                ),
            ],
        )
        self.assertEqual(summary["unresolvedExternalMovieCount"], 0)

    def test_generation_is_deterministic_and_payload_free(self) -> None:
        second = build_retail_hud_apt_plan(
            ROOT,
            self.manifest,
            self.catalog,
            self.oracle,
            self.external_movies,
        )
        self.assertEqual(second, self.plan)
        self.assertEqual(second["aggregateSha256"], self.plan["aggregateSha256"])
        serialized = json.dumps(self.plan, sort_keys=True)
        self.assertNotIn(str(ROOT), serialized)
        self.assertNotIn("TRUEVISION-XFILE", serialized)
        self.assertNotIn("Apt constant file", serialized)
        self.assertNotIn("FILE_VERSION = 2", serialized)
        self.assertNotIn("SGCommandBar.tga", serialized)

    def test_generated_import_profile_has_zero_missing_real_catalog_entries(self) -> None:
        profile = generated_import_profile(self.plan)
        with tempfile.TemporaryDirectory(prefix="openbfme-hud-apt-test-") as raw:
            path = Path(raw) / "profile.json"
            write_generated_import_profile(path, profile)
            loaded = ImportProfile.load(path)
            resolved = resolve_profile(loaded, self.catalog)
            plan_path = Path(raw) / "plan.json"
            write_retail_hud_apt_plan(plan_path, self.plan)
            self.assertEqual(json.loads(plan_path.read_text(encoding="utf-8")), self.plan)
        self.assertEqual(resolved.missing_required, ())
        self.assertEqual(len(resolved.selected_entries), 262)
        resource_ids = [resource.rule.id for resource in resolved.resources]
        self.assertEqual(
            resource_ids,
            ["men-hud-apt-runtime-bundle", "men-hud-font-albertus-mt"],
        )
        self.assertEqual(
            sum(
                resource.rule.converter == "sage-apt-runtime"
                for resource in resolved.resources
            ),
            1,
        )
        font = resolved.resources[1]
        bundle = resolved.resources[0]
        self.assertEqual(len(bundle.entries), 261)
        self.assertEqual(
            bundle.rule.options["expectedSourceAggregateSha256"],
            "f62347fb78065726715618ed9c73f152c678fec5646ddf7b0855825d1cb23599",
        )
        self.assertEqual(
            bundle.rule.options["externalFonts"],
            self.plan["sceneContract"]["runtimeAssetBindings"]["externalFonts"],
        )
        self.assertEqual(font.rule.converter, "copy")
        self.assertEqual(font.rule.output, "assets/ui/palantir/fonts/albertusmt-6a1990e17f14.otf")
        self.assertEqual([entry.name for entry in font.entries], ["albertusmt.otf"])
        pipeline = object.__new__(ImportPipeline)
        with self.assertRaisesRegex(
            ValueError,
            "requires lowercase expectedSourceAggregateSha256",
        ):
            pipeline._convert_hud_apt_runtime_bundle(
                None,  # type: ignore[arg-type]
                {},
                "data/ui/palantir/scene-contract.json",
                {
                    "expectedSourceAggregateSha256": "invalid",
                    "externalFonts": bundle.rule.options["externalFonts"],
                },
                Path(tempfile.gettempdir()),
            )

    def test_manifest_or_oracle_drift_is_rejected(self) -> None:
        changed = deepcopy(self.manifest)
        target = next(
            row for row in changed["files"] if row["path"] == "Palantir.apt"
        )
        target["sha256"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "manifest changed"):
            build_retail_hud_apt_plan(
                ROOT,
                changed,
                self.catalog,
                self.oracle,
                self.external_movies,
            )
        with self.assertRaisesRegex(ValueError, "oracle report lacks"):
            build_retail_hud_apt_plan(
                ROOT,
                self.manifest,
                self.catalog,
                b"not the sealed oracle",
                self.external_movies,
            )
        changed_external = deepcopy(self.external_movies)
        changed_external["summary"]["newSourceCount"] = 71
        with self.assertRaisesRegex(ValueError, "oracle contract changed"):
            build_retail_hud_apt_plan(
                ROOT,
                self.manifest,
                self.catalog,
                self.oracle,
                changed_external,
            )


if __name__ == "__main__":
    unittest.main()
