from __future__ import annotations

import copy
import json
from pathlib import Path
import tempfile
import unittest

from openbfme_importer.pipeline import ImportPipeline
from openbfme_importer.profile import ImportProfile
from openbfme_importer.util import write_json_atomic


PARTICLE_SOURCE = b"""ParticleSystem WaterRipplesSmall
  Priority = AREA_EFFECT
  Shader = ALPHA
End
FXParticleSystem WaterRipplesSmall
  System
    Priority = ALWAYS_RENDER
  End
  Color = Color3
    Red = 255
  End
End
"""


def _profile() -> dict:
    return {
        "format": 1,
        "id": "particle-test",
        "pack": {"id": "particle-test-pack"},
        "resources": [
            {
                "id": "water-ripples-small",
                "kind": "data",
                "patterns": ["data/ini/particlesystem.ini"],
                "converter": "sage-particle-definition",
                "output": "effects/water-ripples-small.json",
                "limit": 1,
                "expected_count": 1,
                "options": {
                    "kind": "ParticleSystem",
                    "name": "WaterRipplesSmall",
                },
            }
        ],
    }


class SageParticlePipelineTests(unittest.TestCase):
    def test_profile_requires_one_exact_selected_definition(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            valid = root / "valid.json"
            write_json_atomic(valid, _profile())
            rule = ImportProfile.load(valid).resources[0]
            self.assertEqual(rule.converter, "sage-particle-definition")

            cases: list[tuple[str, dict, str]] = []
            extra_pattern = copy.deepcopy(_profile())
            extra_pattern["resources"][0]["patterns"].append("data/other.ini")
            cases.append(
                ("extra-pattern", extra_pattern, "requires exactly one pattern")
            )
            wrong_limit = copy.deepcopy(_profile())
            wrong_limit["resources"][0]["limit"] = 2
            cases.append(("wrong-limit", wrong_limit, "requires exactly one pattern"))
            missing_count = copy.deepcopy(_profile())
            missing_count["resources"][0].pop("expected_count")
            cases.append(
                ("missing-count", missing_count, "requires exactly one pattern")
            )
            wrong_output = copy.deepcopy(_profile())
            wrong_output["resources"][0]["output"] = "effects/{name}.json"
            cases.append(("templated-output", wrong_output, "requires a .json output"))
            wrong_kind = copy.deepcopy(_profile())
            wrong_kind["resources"][0]["options"]["kind"] = "UnknownSystem"
            cases.append(
                ("wrong-kind", wrong_kind, "unsupported particle definition kind")
            )
            unsafe_name = copy.deepcopy(_profile())
            unsafe_name["resources"][0]["options"]["name"] = "../Water"
            cases.append(
                ("unsafe-name", unsafe_name, "unsafe particle definition name")
            )
            extra_option = copy.deepcopy(_profile())
            extra_option["resources"][0]["options"]["fallback"] = True
            cases.append(("extra-option", extra_option, "exactly kind and name"))

            for name, payload, message in cases:
                with self.subTest(name=name):
                    path = root / f"{name}.json"
                    write_json_atomic(path, payload)
                    with self.assertRaisesRegex(ValueError, message):
                        ImportProfile.load(path)

    def test_converter_emits_only_the_selected_private_runtime_definition(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            source = root / "particles.ini"
            source.write_bytes(PARTICLE_SOURCE)
            first = root / "first.json"
            second = root / "second.json"
            pipeline = object.__new__(ImportPipeline)
            options = {"kind": "FXParticleSystem", "name": "WaterRipplesSmall"}

            self.assertEqual(
                pipeline._convert_sage_particle_definition(source, first, options),
                [first],
            )
            pipeline._convert_sage_particle_definition(source, second, options)
            self.assertEqual(first.read_bytes(), second.read_bytes())

            document = json.loads(first.read_text(encoding="utf-8"))
            self.assertEqual(document["schema"], "openbfme.sage-particle-definition")
            self.assertEqual(document["schemaVersion"], 0)
            self.assertEqual(document["kind"], "FXParticleSystem")
            self.assertEqual(document["name"], "WaterRipplesSmall")
            self.assertEqual(
                [entry["type"] for entry in document["entries"]], ["block", "block"]
            )
            self.assertNotIn(str(root), first.read_text(encoding="utf-8"))
            self.assertNotIn("AREA_EFFECT", first.read_text(encoding="utf-8"))

    def test_converter_rejects_missing_or_wrong_family_definition(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            source = root / "particles.ini"
            source.write_bytes(PARTICLE_SOURCE)
            pipeline = object.__new__(ImportPipeline)
            for name, options, message in (
                (
                    "missing",
                    {"kind": "ParticleSystem", "name": "Missing"},
                    "missing ParticleSystem particle definition",
                ),
                (
                    "wrong-family",
                    {"kind": "FXParticleSystem", "name": "Missing"},
                    "missing FXParticleSystem particle definition",
                ),
            ):
                with self.subTest(name=name):
                    with self.assertRaisesRegex(ValueError, message):
                        pipeline._convert_sage_particle_definition(
                            source, root / f"{name}.json", options
                        )


if __name__ == "__main__":
    unittest.main()
