from __future__ import annotations

import json
import copy
from pathlib import Path
import tempfile
import unittest

from openbfme_importer.retail_men_damage_effects import (
    build_contract,
    parse_fx_lists,
    profile_fragment_document,
    write_contract,
)


REPO_ROOT = Path(__file__).resolve().parents[2]
EFFECTIVE_ROOT = REPO_ROOT / ".private" / "retail-work" / "cache" / "effective-assets"
PROFILE_PATH = (
    REPO_ROOT
    / ".private"
    / "retail-work"
    / "profiles"
    / "men-fords-v0-complete.generated.json"
)
CONTRACT_PATH = (
    REPO_ROOT / ".private" / "scratch" / "men-damage-effects" / "contract-a.json"
)
PRIVATE_READY = (
    PROFILE_PATH.is_file()
    and CONTRACT_PATH.is_file()
    and (EFFECTIVE_ROOT / ".openbfme" / "manifest.json").is_file()
)


class FxListParserTests(unittest.TestCase):
    def test_preserves_top_level_assignments_sections_and_nested_edges(self) -> None:
        payload = (
            b"FXList Root\r\n"
            b"  PlayEvenIfShrouded = Yes\r\n"
            b"  ParticleSystem\r\n"
            b"    Name = Dust\r\n"
            b"    Offset = X:0 Y:0 Z:2\r\n"
            b"  End\r\n"
            b"  FXList\r\n"
            b"    Name = Child\r\n"
            b"  End\r\n"
            b"End\r\n"
        )
        parsed = parse_fx_lists(payload)["root"]
        self.assertEqual(parsed["fxListId"], "Root")
        self.assertEqual(parsed["assignments"][0]["field"], "PlayEvenIfShrouded")
        self.assertEqual(parsed["sections"][0]["kind"], "ParticleSystem")
        self.assertEqual(parsed["sections"][1]["kind"], "FXList")
        self.assertEqual(parsed["sourceSpan"]["byteLength"], len(payload))
        self.assertEqual(parsed["sourceSpan"]["startByte"], 0)
        self.assertEqual(parsed["sourceSpan"]["endByte"], len(payload))

    def test_rejects_duplicate_fx_list_names_case_insensitively(self) -> None:
        with self.assertRaisesRegex(ValueError, "duplicate FXList"):
            parse_fx_lists(b"FXList Same\nEnd\nFXList same\nEnd\n")


@unittest.skipUnless(PRIVATE_READY, "private BFME2 effective tree is unavailable")
class PrivateMenDamageEffectsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.profile = json.loads(PROFILE_PATH.read_text(encoding="utf-8"))
        cls.manifest = json.loads(
            (EFFECTIVE_ROOT / ".openbfme" / "manifest.json").read_text(encoding="utf-8")
        )
        cls.contract = json.loads(CONTRACT_PATH.read_text(encoding="utf-8"))
        cls.current_contract = build_contract(EFFECTIVE_ROOT, cls.profile, cls.manifest)

    def test_accounts_for_all_authored_bindings_and_state_blocks(self) -> None:
        summary = self.contract["summary"]
        self.assertEqual(summary["menObjectCount"], 5)
        self.assertEqual(summary["enteringStateFxBindingCount"], 17)
        self.assertEqual(summary["particleAttachmentCount"], 88)
        self.assertEqual(summary["particleBearingStateBlockCount"], 29)
        self.assertEqual(len(self.contract["bindings"]["particleStateBlocks"]), 29)
        indexes = sorted(
            index
            for block in self.contract["bindings"]["particleStateBlocks"]
            for index in block["attachmentIndexes"]
        )
        self.assertEqual(indexes, list(range(88)))

    def test_seals_full_fx_and_particle_definition_closure(self) -> None:
        summary = self.contract["summary"]
        self.assertEqual(summary["uniqueFxListCount"], 7)
        self.assertEqual(summary["directParticleSystemIdCount"], 14)
        self.assertEqual(summary["fxListParticleSystemIdCount"], 7)
        self.assertEqual(summary["uniqueParticleSystemIdCount"], 21)
        self.assertEqual(summary["particleDefinitionCandidateCount"], 31)
        self.assertEqual(summary["duplicateFamilySystemCount"], 10)
        definitions = {
            row["particleSystemId"]: row for row in self.contract["particleDefinitions"]
        }
        self.assertIn("PCTFortressDust", definitions)
        self.assertIn("BuildingDamagedBig", definitions)
        self.assertIn("FortressExplosion", definitions)
        self.assertEqual(
            definitions["FireBuildingMedium"]["familyResolution"]["status"],
            "unresolved-cross-family-precedence",
        )
        self.assertEqual(
            definitions["MenFortressSpray"]["familyResolution"]["selectedKind"],
            "FXParticleSystem",
        )

    def test_seals_exact_render_leaves_and_profile_delta(self) -> None:
        summary = self.contract["summary"]
        coverage = self.contract["profileCoverage"]
        self.assertEqual(summary["textureLeafCount"], 13)
        self.assertEqual(summary["w3dLeafCount"], 0)
        self.assertEqual(coverage["missingDefinitionResourceCount"], 18)
        self.assertEqual(coverage["missingTextureResourceCount"], 6)
        self.assertEqual(coverage["missingW3dResourceCount"], 0)
        self.assertEqual(coverage["resourceDeltaCount"], 24)
        self.assertEqual(
            coverage["missingFxListIds"],
            [
                "FX_BoilingOilAttack",
                "FX_FortressCollapse",
                "FX_FortressDamaged",
                "FX_FortressReallyDamaged",
            ],
        )
        self.assertTrue(
            all(leaf["source"]["sha256"] for leaf in self.contract["renderLeaves"])
        )

    def test_definition_provenance_and_control_fields_are_exact(self) -> None:
        candidates = [
            candidate
            for row in self.contract["particleDefinitions"]
            for candidate in row["definitionCandidates"]
        ]
        self.assertEqual(len(candidates), 31)
        for candidate in candidates:
            self.assertEqual(candidate["source"]["source"]["archive"], "ini.big")
            self.assertEqual(candidate["source"]["source"]["precedence"], 91)
            self.assertGreater(candidate["sourceSpan"]["byteLength"], 0)
            self.assertTrue(candidate["sourceSpan"]["sha256"])
            self.assertTrue(candidate["emissionAndControlAssignments"])

    def test_profile_fragment_is_minimal_and_writes_byte_identically(self) -> None:
        fragment = self.contract["profileFragmentProposal"]
        self.assertEqual(fragment["integrationStatus"], "proposal-only-not-integrated")
        self.assertEqual(len(fragment["resources"]), 24)
        self.assertEqual(
            len(fragment["runtimeDataPatch"]["definitionRegistryAppend"]), 18
        )
        self.assertEqual(
            fragment["runtimeDataPatch"][
                "unresolvedDuplicateIdentifierSystemIdsAppend"
            ],
            [
                "FireBuildingLarge",
                "FireBuildingMedium",
                "FireBuildingSmall",
                "SmokeBuildingMedium",
            ],
        )
        sealed_fragment = profile_fragment_document(self.contract)
        self.assertEqual(
            sealed_fragment["sourceContractAggregateSha256"],
            self.contract["aggregateSha256"],
        )
        self.assertEqual(len(sealed_fragment["resources"]), 24)
        with tempfile.TemporaryDirectory() as raw:
            first = Path(raw) / "first.json"
            second = Path(raw) / "second.json"
            write_contract(first, self.contract)
            write_contract(second, self.contract)
            self.assertEqual(first.read_bytes(), second.read_bytes())

    def test_integrated_profile_now_owns_the_sealed_delta(self) -> None:
        coverage = self.current_contract["profileCoverage"]
        self.assertEqual(coverage["missingDefinitionResourceCount"], 0)
        self.assertEqual(coverage["missingTextureResourceCount"], 0)
        self.assertEqual(coverage["missingFxListIds"], [])
        self.assertEqual(coverage["resourceDeltaCount"], 0)
        self.assertEqual(self.current_contract["summary"], self.contract["summary"])

    def test_proven_fx_manager_family_propagates_to_new_damage_duplicates(self) -> None:
        profile = copy.deepcopy(self.profile)
        family = profile["runtime_data"][
            "effects/fords-particle-bindings.json"
        ]["familyResolution"]
        new_damage_duplicates = {
            "FireBuildingLarge",
            "FireBuildingMedium",
            "FireBuildingSmall",
            "SmokeBuildingMedium",
        }
        duplicate_ids = [
            value
            for value in family["duplicateIdentifierSystemIds"]
            if value not in new_damage_duplicates
        ]
        family.update(
            {
                "status": "proven-effective-fx-manager-family",
                "noGeneralPrecedenceRule": False,
                "duplicateSemantics": {
                    "crossFamilyPrecedence": "proven-fx-manager-only",
                    "legacySubsystemActive": False,
                    "repeatedFxParticleSystemSyntax": "proven-last-definition-wins",
                },
                "runtimeSelections": [
                    {
                        "particleSystemId": system_id,
                        "status": "proven-effective-fx-manager-family",
                        "selectedKind": "FXParticleSystem",
                        "crossFamilyPrecedenceProven": True,
                        "generalizesToOtherDuplicateIdentifiers": True,
                    }
                    for system_id in duplicate_ids
                ],
                "unresolvedDuplicateIdentifierSystemIds": [],
                "duplicateIdentifierSystemIds": duplicate_ids,
            }
        )
        contract = build_contract(EFFECTIVE_ROOT, profile, self.manifest)
        definitions = {
            row["particleSystemId"]: row for row in contract["particleDefinitions"]
        }
        self.assertEqual(
            definitions["FireBuildingMedium"]["familyResolution"],
            {
                "status": "proven-effective-fx-manager-family",
                "selectedKind": "FXParticleSystem",
                "crossFamilyPrecedenceProven": True,
                "generalizesToOtherDuplicateIdentifiers": True,
            },
        )
        patch = contract["profileFragmentProposal"]["runtimeDataPatch"]
        self.assertEqual(patch["unresolvedDuplicateIdentifierSystemIdsAppend"], [])
        self.assertEqual(
            patch["provenFxDuplicateIdentifierSystemIdsAppend"],
            [
                "FireBuildingLarge",
                "FireBuildingMedium",
                "FireBuildingSmall",
                "SmokeBuildingMedium",
            ],
        )


if __name__ == "__main__":
    unittest.main()
