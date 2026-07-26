import hashlib
import json
from pathlib import Path
import struct
import unittest
from tests.retail_inputs import retail_file

try:
    from openbfme_importer.retail_fords_world_sky_trace import (
        _occurrences,
        _raw_to_va,
        _relative_call_sites,
        compose_fords_world_sky_trace_contract,
    )
except ModuleNotFoundError:  # pragma: no cover - direct discovery fallback
    from importer.openbfme_importer.retail_fords_world_sky_trace import (
        _occurrences,
        _raw_to_va,
        _relative_call_sites,
        compose_fords_world_sky_trace_contract,
    )


class FordsWorldSkyTraceUnitTests(unittest.TestCase):
    def test_occurrences_include_overlaps(self) -> None:
        self.assertEqual([0, 1], _occurrences(b"aaa", b"aa"))

    def test_raw_to_va_uses_section_mapping(self) -> None:
        sections = [
            {
                "name": ".text",
                "rawOffset": 0x400,
                "rawSize": 0x80,
                "rva": 0x1000,
                "virtualSize": 0x80,
            }
        ]
        self.assertEqual(0x401020, _raw_to_va(0x420, 0x400000, sections))
        self.assertIsNone(_raw_to_va(0x500, 0x400000, sections))

    def test_relative_call_sites_resolve_targets(self) -> None:
        code_va = 0x401000
        target = 0x402000
        relative = target - (code_va + 2 + 5)
        code = b"\x90\x90\xe8" + struct.pack("<i", relative) + b"\xc3"
        self.assertEqual([0x401002], _relative_call_sites(code, code_va, target))


class FordsWorldSkyTracePrivateIntegrationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.repo = Path(__file__).resolve().parents[2]
        cls.paths = {
            "effective_assets_root": cls.repo
            / ".private/retail-work/cache/effective-assets",
            "manifest_path": cls.repo
            / ".private/retail-work/cache/effective-assets/.openbfme/manifest.json",
            "catalog_path": cls.repo / ".private/retail-work/catalog/bfme2.json",
            "game_dat_path": retail_file("game.dat"),
            "skybox_oracle_path": cls.repo
            / ".private/scratch/fords-skybox-oracle/contract-a.json",
            "water_reflection_contract_path": cls.repo
            / ".private/scratch/fords-water-reflection/contract-a.json",
            "opensage_root": cls.repo
            / ".private/scratch/fords-skybox-oracle/OpenSAGE",
        }
        if not all(path.exists() for path in cls.paths.values()):
            raise unittest.SkipTest("private Fords world-sky evidence is unavailable")
        cls.contract = compose_fords_world_sky_trace_contract(**cls.paths)

    def test_trace_stays_fail_closed(self) -> None:
        self.assertEqual(
            {
                "blockerCount": 4,
                "globalDrawSkyboxEnabled": True,
                "morningConstructorDefaultFieldCount": 5,
                "selectedFaceClosureCount": 0,
                "waterReflectionSkydomeSeparated": True,
                "worldSkySelectionProven": False,
            },
            self.contract["summary"],
        )
        selection = self.contract["worldSkySelection"]
        self.assertFalse(selection["proven"])
        self.assertIsNone(selection["selectedTextureSet"])
        self.assertEqual([], selection["selectedFaceClosure"])
        self.assertEqual(["Morning", "DefaultSky"], selection["candidatePromotionRejected"])

    def test_constructor_defaults_are_parser_record_fields(self) -> None:
        constructor = self.contract["evidence"]["executable"]["constructor"]
        self.assertEqual(0x0060D4E3, constructor["va"])
        self.assertEqual([0x0060D755], constructor["callSites"])
        self.assertEqual(
            [
                ("N", 0x04, "TSMorningN.tga", 0x00BE3F88, 0x0060D516),
                ("E", 0x08, "TSMorningE.tga", 0x00BE3F78, 0x0060D524),
                ("S", 0x0C, "TSMorningS.tga", 0x00BE3F68, 0x0060D530),
                ("W", 0x10, "TSMorningW.tga", 0x00BE3F58, 0x0060D53C),
                ("T", 0x14, "TSMorningT.tga", 0x00BE3F48, 0x0060D549),
            ],
            [
                (
                    value["face"],
                    value["fieldOffset"],
                    value["literal"],
                    value["literalVa"],
                    value["pushInstructionVa"],
                )
                for value in constructor["fieldDefaults"]
            ],
        )
        self.assertIn("not a selected named texture set", constructor["meaning"])

    def test_parser_registry_has_no_direct_renderer_consumer(self) -> None:
        executable = self.contract["evidence"]["executable"]
        self.assertEqual(0x0060D6F5, executable["parserDescriptor"]["callbackVa"])
        self.assertEqual(0x00DB9850, executable["parserDescriptor"]["descriptorVa"])
        registry = executable["registry"]
        self.assertEqual(0x00DFF494, registry["address"])
        self.assertEqual(0, registry["rendererConsumerReferenceCount"])
        self.assertEqual(
            [
                ("parser-create-or-find-record", 0x0060D720),
                ("static-registry-initialization", 0x00BAE6E6),
                ("static-registry-teardown-thunk", 0x00BB7AE1),
            ],
            [
                (value["classification"], value["instructionVa"])
                for value in registry["absoluteTextReferences"]
            ],
        )
        self.assertEqual(
            {"Morning": [], "DefaultSky": [], "new_skybox": []},
            executable["namedSelectionTokenOffsets"],
        )

    def test_draw_flag_and_water_skydome_do_not_select_world_faces(self) -> None:
        self.assertEqual(
            {"authoredValue": True, "line": 6483},
            {
                "authoredValue": self.contract["globalDrawSkybox"]["authoredValue"],
                "line": self.contract["globalDrawSkybox"]["line"],
            },
        )
        self.assertFalse(
            self.contract["waterReflectionBoundary"]["maySatisfyWorldSkySelection"]
        )
        self.assertEqual(2, self.contract["runtimeTraceRequired"]["minimumRuns"])

    def test_opensage_is_parser_only_secondary_evidence(self) -> None:
        opensage = self.contract["evidence"]["opensage"]
        self.assertEqual(
            "588ac477367a0022adf29f20a084e8873014e6ce", opensage["commit"]
        )
        self.assertEqual(0, opensage["rendererReferenceCount"])
        self.assertGreater(opensage["productionReferenceCount"], 0)
        self.assertIn("not retail behavior proof", opensage["scope"])

    def test_contract_and_declared_digest_are_deterministic(self) -> None:
        second = compose_fords_world_sky_trace_contract(**self.paths)
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
