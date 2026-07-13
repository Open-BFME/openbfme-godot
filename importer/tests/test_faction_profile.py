from __future__ import annotations

import copy
import json
from pathlib import Path
import tempfile
import unittest

from openbfme_importer.catalog import InstallCatalog
from openbfme_importer.faction_census import census_men_faction
from openbfme_importer.faction_profile import (
    PACK_ID,
    PROFILE_ID,
    build_men_leaf_profile,
    build_men_leaf_profile_from_report,
)
from openbfme_importer.profile import ImportProfile, resolve_profile
from openbfme_importer.util import write_json_atomic

from importer.tests.test_big import make_big


IMAGE_COUNT = 65
SAMPLE_COUNT = 257


def _player_template() -> bytes:
    return b"""
PlayerTemplate FactionMen
  Side = Men
  StartingUnit0 = MenPorter
  StartingUnit1 = MenPorter
  StartingBuilding = MenFortress
  BuildableHeroesMP = HeroA
  SpellBookMp = MenSpellBook
  PurchaseScienceCommandSetMP = MenSpellStoreCommandSet
  IntrinsicSciencesMP = SCIENCE_MEN
End
"""


def _command_sets() -> bytes:
    porter_buttons = "\n".join(
        f"  {index + 1} = Command_Test{index:03d}" for index in range(IMAGE_COUNT)
    )
    return f"""
CommandSet MenPorterCommandSet
{porter_buttons}
End
CommandSet MenFortressCommandSet
End
CommandSet MenSpellStoreCommandSet
End
""".encode("cp1252")


def _command_buttons() -> bytes:
    blocks: list[str] = []
    for index in range(IMAGE_COUNT):
        target = "SoldierHorde" if index == 0 else "MenBarracks"
        text = "\n  TextLabel = CONTROLBAR:Build" if index == 0 else ""
        blocks.append(
            f"""CommandButton Command_Test{index:03d}
  Command = UNIT_BUILD
  Object = {target}
  ButtonImage = Icon'{index:03d}{text}
End
"""
        )
    return "\n".join(blocks).encode("cp1252")


def _objects() -> bytes:
    return b"""
Object MenPorter
  CommandSet = MenPorterCommandSet
End
Object MenFortress
  CommandSet = MenFortressCommandSet
End
Object MenBarracks
End
Object SoldierHorde
  InitialPayload = Soldier 15
  BannerCarriersAllowed = MenBanner
End
Object Soldier
  VoiceSelect = SoldierVoice
End
Object MenBanner
End
Object HeroA
End
Object MenSpellBook
End
Object MenFortressCenterGeneric
End
Object MenFortressCitadel
End
Object MenFortressExpansionPadCorner
End
Object MenFortressExpansionPadSide
End
"""


def _mapped_images() -> bytes:
    blocks: list[str] = []
    for index in range(IMAGE_COUNT):
        left = (index % 16) * 8
        top = (index // 16) * 8
        blocks.append(
            f"""MappedImage Icon'{index:03d}
  Texture = SharedAtlas.tga
  TextureWidth = 128
  TextureHeight = 64
  Coords = Left:{left} Top:{top} Right:{left + 8} Bottom:{top + 8}
End
"""
        )
    return "\n".join(blocks).encode("cp1252")


def _sound_effects() -> bytes:
    samples = " ".join(f"sample{index:03d}" for index in range(SAMPLE_COUNT))
    return f"""
AudioEvent SoldierSelect
  Sounds = {samples}
  Type = world player
End
Multisound SoldierVoice
  Subsounds = SoldierSelect
End
""".encode("cp1252")


def _catalog(root: Path) -> InstallCatalog:
    files: dict[str, bytes] = {
        "data/ini/playertemplate.ini": _player_template(),
        "data/ini/commandset.ini": _command_sets(),
        "data/ini/commandbutton.ini": _command_buttons(),
        "data/ini/object/goodfaction/men.ini": _objects(),
        "data/ini/mappedimages/aptimages/fixture.ini": _mapped_images(),
        "data/ini/soundeffects.ini": _sound_effects(),
        "data/ini/upgrade.ini": b"",
        "data/ini/science.ini": b"Science SCIENCE_MEN\n  IsGrantable = No\nEnd\n",
        "data/ini/specialpower.ini": b"",
        "data/lotr.str": b'CONTROLBAR:Build "Build me" END\n',
        "art/compiledtextures/sh/sharedatlas.dds": b"synthetic-atlas-payload",
    }
    for index in range(SAMPLE_COUNT):
        extension = "mp3" if index % 2 else "wav"
        files[f"data/audio/sounds/sample{index:03d}.{extension}"] = (
            f"sample-payload-{index:03d}".encode("ascii")
        )
    make_big(root / "ini.big", files)
    return InstallCatalog.build(root)


class FactionProfileTests(unittest.TestCase):
    def test_generated_profile_is_bounded_deterministic_and_resolves_exactly(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            catalog = _catalog(root)
            first = build_men_leaf_profile(catalog)
            second = build_men_leaf_profile(catalog)
            self.assertEqual(
                json.dumps(first, sort_keys=True, ensure_ascii=False),
                json.dumps(second, sort_keys=True, ensure_ascii=False),
            )

            profile_path = root / "generated-profile.json"
            write_json_atomic(profile_path, first)
            loaded = ImportProfile.load(profile_path)
            resolved = resolve_profile(loaded, catalog)

            self.assertEqual(loaded.id, PROFILE_ID)
            self.assertEqual(loaded.pack_id, PACK_ID)
            self.assertEqual(resolved.missing_required, ())
            self.assertFalse(loaded.pack_metadata["vertical_slice_complete"])
            self.assertFalse(loaded.pack_metadata["full_faction_complete"])
            self.assertFalse(loaded.pack_metadata["asset_conversion_complete"])
            self.assertFalse(loaded.pack_metadata["oracle_parity_complete"])
            self.assertFalse(loaded.pack_metadata["dataPolicy"]["redistributable"])
            self.assertEqual(
                loaded.pack_metadata["files"],
                {
                    "uiManifest": "data/ui_manifest.json",
                    "strings": "data/strings.json",
                    "audioEvents": "data/audio_events.json",
                },
            )

            semantic = [item for item in first["resources"] if item["converter"] == "hash-only"]
            self.assertTrue(semantic)
            semantic_paths = {
                path for resource in semantic for path in resource["patterns"]
            }
            self.assertIn("data/lotr.str", semantic_paths)
            self.assertIn("data/ini/commandbutton.ini", semantic_paths)
            self.assertTrue(
                all(len(resource["patterns"]) <= 256 for resource in semantic)
            )

            ui_rules = [
                item
                for item in first["resources"]
                if item["converter"] == "texture-atlas-crops"
            ]
            self.assertEqual(len(ui_rules), 2)
            self.assertEqual(
                sorted(len(item["options"]["crops"]) for item in ui_rules), [1, 64]
            )
            self.assertTrue(
                all(
                    item["patterns"]
                    == ["art/compiledtextures/sh/sharedatlas.dds"]
                    for item in ui_rules
                )
            )

            audio_rules = [
                item for item in first["resources"] if item["converter"] == "audio"
            ]
            self.assertEqual(len(audio_rules), 2)
            self.assertEqual(
                sorted(len(item["patterns"]) for item in audio_rules), [1, 256]
            )
            self.assertTrue(
                all(item["output"] == "assets/audio/men/{stem}.wav" for item in audio_rules)
            )
            self.assertTrue(all(item["options"] == {"force_pcm": True} for item in audio_rules))

            runtime = first["runtime_data"]
            ui_manifest = runtime["data/ui_manifest.json"]
            strings = runtime["data/strings.json"]
            audio = runtime["data/audio_events.json"]
            self.assertEqual(ui_manifest["schema"], "openbfme.ui-manifest")
            self.assertEqual(ui_manifest["schemaVersion"], 0)
            self.assertFalse(ui_manifest["complete"])
            self.assertEqual(len(ui_manifest["images"]), IMAGE_COUNT)
            self.assertEqual(strings["schema"], "openbfme.localized-strings")
            self.assertEqual(strings["locale"], "en")
            self.assertEqual(strings["strings"], {"CONTROLBAR:Build": "Build me"})
            self.assertEqual(audio["schema"], "openbfme.audio-events")
            self.assertEqual(audio["schemaVersion"], 1)
            self.assertEqual(audio["rootIds"], ["SoldierVoice"])
            self.assertIn("SoldierSelect", audio["events"])
            self.assertIn("SoldierVoice", audio["multisounds"])
            self.assertEqual(len(audio["samples"]), SAMPLE_COUNT)

            ui_outputs = [
                f"{rule['output']}/{crop['output']}".casefold()
                for rule in ui_rules
                for crop in rule["options"]["crops"]
            ]
            audio_outputs = [path.casefold() for path in audio["samples"].values()]
            self.assertEqual(len(ui_outputs), len(set(ui_outputs)))
            self.assertEqual(len(audio_outputs), len(set(audio_outputs)))
            self.assertFalse(set(ui_outputs) & set(audio_outputs))

            serialized = json.dumps(first, ensure_ascii=False)
            self.assertNotIn(str(catalog.install_root), serialized)
            self.assertNotIn("Object MenPorter", serialized)
            self.assertNotIn("sample-payload-000", serialized)
            self.assertNotIn("synthetic-atlas-payload", serialized)
            self.assertIn("Build me", serialized)

    def test_missing_and_malformed_census_reports_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            catalog = _catalog(Path(raw))
            report = census_men_faction(catalog)

            with self.assertRaisesRegex(ValueError, "report is required"):
                build_men_leaf_profile_from_report(catalog, None)

            cases: list[tuple[str, dict, str]] = []
            wrong_schema = copy.deepcopy(report)
            wrong_schema["schema"] = "wrong"
            cases.append(("wrong-schema", wrong_schema, "unsupported schema"))

            incomplete = copy.deepcopy(report)
            incomplete["unresolved"]["missingObjects"] = ["MissingObject"]
            incomplete["summary"]["unresolvedCount"] = 1
            cases.append(("unresolved", incomplete, "unresolved dependencies"))

            missing_strings = copy.deepcopy(report)
            missing_strings["sourceDocuments"] = [
                item
                for item in missing_strings["sourceDocuments"]
                if item["virtualPath"].casefold() != "data/lotr.str"
            ]
            cases.append(("missing-lotr", missing_strings, "missing required semantic"))

            wrong_text_digest = copy.deepcopy(report)
            wrong_text_digest["resolvedLeaves"]["localization"]["records"][0][
                "utf8Sha256"
            ] = "0" * 64
            cases.append(("wrong-text", wrong_text_digest, "no longer matches lotr.str"))

            missing_atlas = copy.deepcopy(report)
            missing_atlas["resolvedLeaves"]["mappedImages"][0][
                "compiledTextureVirtualPath"
            ] = "art/compiledtextures/no/missing.dds"
            cases.append(("missing-atlas", missing_atlas, "missing from the install catalog"))

            for name, malformed, message in cases:
                with self.subTest(name=name):
                    with self.assertRaisesRegex(ValueError, message):
                        build_men_leaf_profile_from_report(catalog, malformed)


if __name__ == "__main__":
    unittest.main()
