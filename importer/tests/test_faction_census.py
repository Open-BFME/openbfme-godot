from __future__ import annotations

import json
from pathlib import Path
import tempfile
import unittest

from openbfme_importer.catalog import InstallCatalog
from openbfme_importer.faction_census import census_men_faction

from importer.tests.test_big import make_big


def _player_template(*, side: str = "Men") -> bytes:
    return f"""
PlayerTemplate FactionMen
  Side = {side}
  StartingUnit0 = MenPorter
  StartingUnit1 = MenPorter
  StartingBuilding = MenFortress
  BuildableHeroesMP = HeroA
  SpellBookMp = MenSpellBook
  PurchaseScienceCommandSetMP = MenSpellStoreCommandSet
  IntrinsicSciencesMP = SCIENCE_MEN
End
""".encode("cp1252")


def _command_sets() -> bytes:
    return b"""
CommandSet MenPorterCommandSet
  1 = Command_BuildBarracks
End
CommandSet MenFortressCommandSet
  1 = Command_ConstructPorter
End
CommandSet MenBarracksCommandSet
  1 = Command_TrainSoldiers
End
CommandSet MenSpellBookCommandSet
  1 = Command_Heal
End
CommandSet MenSpellStoreCommandSet
  1 = Command_Heal
End
"""


def _command_buttons() -> bytes:
    return b"""
CommandButton Command_BuildBarracks
  Command = DOZER_CONSTRUCT
  Object = MenBarracks
  ButtonImage = BuildBarracksImage
  TextLabel = CONTROLBAR:BuildBarracks
End
CommandButton Command_ConstructPorter
  Command = UNIT_BUILD
  Object = MenPorter
End
CommandButton Command_TrainSoldiers
  Command = UNIT_BUILD
  Object = SoldierHorde
  Upgrade = Upgrade_MenTraining
  ButtonImage = TrainSoldierImage
End
CommandButton Command_Heal
  Command = SPECIAL_POWER
  SpecialPower = SpellBookHeal
  Science = SCIENCE_Heal
  ButtonImage = HealImage
  DescriptLabel = CONTROLBAR:HealDescription
End
"""


def _objects(*, duplicate_porter: bool = False) -> bytes:
    duplicate = b"Object MenPorter\nEnd\n" if duplicate_porter else b""
    return b"""
Object MenPorter
  CommandSet = MenPorterCommandSet
End
Object MenFortress
  CommandSet = MenFortressCommandSet
End
Object MenBarracks
  CommandSet = MenBarracksCommandSet
End
Object SoldierHorde
  InitialPayload = Soldier 15
  BannerCarriersAllowed = MenBanner
End
Object Soldier
  VoiceSelect = SoldierVoice
  SelectPortrait = UPSoldier
  DisplayName = OBJECT:Soldier
  Behavior = SlowDeathBehavior ModuleTag_Death
    Sound = INITIAL HumanVoiceDie
  End
End
Object MenBanner
  SelectPortrait = MissingBannerPortrait
End
Object HeroA
End
Object MenSpellBook
  CommandSet = MenSpellBookCommandSet
End
Object MenFortressCenterGeneric
End
Object MenFortressCitadel
End
Object MenFortressExpansionPadCorner
End
Object MenFortressExpansionPadSide
End
""" + duplicate


def _mapped_images() -> bytes:
    return b"""
MappedImage BuildBarracksImage
  Texture = BuildAtlas.tga
  TextureWidth = 64
  TextureHeight = 64
  Coords = Left:0 Top:0 Right:32 Bottom:32
End
MappedImage TrainSoldierImage
  Texture = TrainAtlas.tga
  TextureWidth = 64
  TextureHeight = 64
  Coords = Left:0 Top:0 Right:32 Bottom:32
End
MappedImage HealImage
  Texture = HealAtlas.tga
  TextureWidth = 64
  TextureHeight = 64
  Coords = Left:0 Top:0 Right:32 Bottom:32
End
MappedImage UPSoldier
  Texture = UnitPortrait.tga
  TextureWidth = 64
  TextureHeight = 64
  Coords = Left:0 Top:0 Right:32 Bottom:32
End
"""


def _sound_effects() -> bytes:
    return b"""
AudioEvent SoldierSelect
  Sounds = soldier_a soldier_b:50
  Type = world player
End
AudioEvent HumanVoiceDie
  Sounds = human_die
  Type = world player
End
"""


def _voice() -> bytes:
    return b"""
Multisound SoldierVoice
  Subsounds = SoldierSelect
End
"""


def _strings() -> bytes:
    return b"""
CONTROLBAR:BuildBarracks "Build barracks" END
CONTROLBAR:HealDescription "Heal description" END
OBJECT:Soldier "Synthetic soldier" END
"""


def _upgrades() -> bytes:
    return b"""
Upgrade Upgrade_MenTraining
  Type = OBJECT
  SubUpgradeTemplateNames = Upgrade_MenTrainingChild
  ButtonImage = TrainSoldierImage
  DisplayName = CONTROLBAR:BuildBarracks
  ResearchSound = SoldierVoice
  UpgradeFX = FX_MenTraining
End
Upgrade Upgrade_MenTrainingChild
  Type = OBJECT
End
"""


def _sciences() -> bytes:
    return b"""
Science SCIENCE_MEN
  IsGrantable = No
End
Science SCIENCE_Heal
  PrerequisiteSciences = SCIENCE_MEN
End
"""


def _special_powers() -> bytes:
    return b"""
SpecialPower SpellBookHeal
  Enum = SPECIAL_SPELL_BOOK_HEAL
  RequiredSciences = SCIENCE_Heal
  InitiateSound = SoldierVoice
End
"""


def _catalog(root: Path, *, side: str = "Men", duplicate_porter: bool = False) -> InstallCatalog:
    make_big(
        root / "ini.big",
        {
            "data/ini/playertemplate.ini": _player_template(side=side),
            "data/ini/commandset.ini": _command_sets(),
            "data/ini/commandbutton.ini": _command_buttons(),
            "data/ini/object/goodfaction/men.ini": _objects(
                duplicate_porter=duplicate_porter
            ),
            "data/ini/mappedimages/aptimages/fixture.ini": _mapped_images(),
            "data/ini/soundeffects.ini": _sound_effects(),
            "data/ini/voice.ini": _voice(),
            "data/lotr.str": _strings(),
            "data/ini/upgrade.ini": _upgrades(),
            "data/ini/science.ini": _sciences(),
            "data/ini/specialpower.ini": _special_powers(),
            "art/compiledtextures/bu/buildatlas.dds": b"texture-build",
            "art/compiledtextures/tr/trainatlas.dds": b"texture-train",
            "art/compiledtextures/he/healatlas.dds": b"texture-heal",
            "art/compiledtextures/un/unitportrait.dds": b"texture-portrait",
            "data/audio/sounds/soldier_a.wav": b"sample-a",
            "data/audio/sounds/soldier_b.wav": b"sample-b",
            "data/audio/sounds/human_die.wav": b"sample-death",
        },
    )
    return InstallCatalog.build(root)


class FactionCensusTests(unittest.TestCase):
    def test_command_reachable_census_is_deterministic_and_payload_free(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            catalog = _catalog(Path(raw))
            first = census_men_faction(catalog)
            second = census_men_faction(catalog)
        self.assertEqual(json.dumps(first, sort_keys=True), json.dumps(second, sort_keys=True))
        self.assertEqual(first["target"]["patch"], "1.06")
        self.assertEqual(first["schema"], "openbfme.faction-command-leaf-census")
        self.assertEqual(first["summary"]["unresolvedCount"], 0)
        object_ids = {item["id"] for item in first["definitions"]["objects"]}
        self.assertTrue(
            {
                "MenPorter",
                "MenFortress",
                "MenBarracks",
                "SoldierHorde",
                "Soldier",
                "MenBanner",
                "HeroA",
                "MenSpellBook",
                "MenFortressCenterGeneric",
            }.issubset(object_ids)
        )
        self.assertEqual(
            first["dependencies"]["upgrades"],
            ["Upgrade_MenTraining", "Upgrade_MenTrainingChild"],
        )
        self.assertEqual(first["dependencies"]["specialPowers"], ["SpellBookHeal"])
        self.assertEqual(first["dependencies"]["spellbookSpecialPowers"], ["SpellBookHeal"])
        self.assertEqual(first["dependencies"]["sciences"], ["SCIENCE_Heal", "SCIENCE_MEN"])
        self.assertEqual(first["dependencies"]["spellbookSciences"], ["SCIENCE_Heal"])
        self.assertIn("TrainSoldierImage", first["dependencies"]["mappedImages"])
        self.assertEqual(first["summary"]["mappedImageResolvedCount"], 4)
        self.assertEqual(first["summary"]["mappedImageTextureCount"], 4)
        self.assertEqual(first["summary"]["mappedImageReferenceCount"], 5)
        self.assertEqual(first["summary"]["mappedImageSourceNullCount"], 1)
        self.assertEqual(
            first["dependencies"]["sourceNullMappedImages"],
            ["MissingBannerPortrait"],
        )
        self.assertEqual(first["summary"]["textResolvedCount"], 3)
        self.assertEqual(first["summary"]["audioRootCount"], 2)
        self.assertEqual(first["summary"]["audioEventCount"], 2)
        self.assertEqual(first["summary"]["audioMultisoundCount"], 1)
        self.assertEqual(first["summary"]["audioSampleCount"], 3)
        self.assertEqual(first["summary"]["resolvedUpgradeDefinitionCount"], 2)
        self.assertEqual(first["summary"]["resolvedScienceDefinitionCount"], 2)
        self.assertEqual(first["summary"]["resolvedSpecialPowerDefinitionCount"], 1)
        self.assertEqual(first["dependencies"]["fxLists"], ["FX_MenTraining"])
        self.assertEqual(
            first["dependencies"]["audioRootIds"], ["HumanVoiceDie", "SoldierVoice"]
        )
        soldier = next(
            item for item in first["definitions"]["objects"] if item["id"] == "Soldier"
        )
        self.assertIn(
            {
                "field": "Sound",
                "targetKind": "audio-definition",
                "targetId": "HumanVoiceDie",
            },
            soldier["edges"],
        )
        self.assertEqual(
            len(first["resolvedLeaves"]["localization"]["records"]), 3
        )
        self.assertEqual(
            len(first["resolvedLeaves"]["audio"]["samplePaths"]), 3
        )
        serialized = json.dumps(first)
        self.assertNotIn("DOZER_CONSTRUCT\n", serialized)
        self.assertNotIn("Build barracks", serialized)
        self.assertNotIn("sample-a", serialized)
        self.assertNotIn(str(catalog.install_root), serialized)

    def test_duplicate_object_is_reported_as_ambiguous_not_silently_selected(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            report = census_men_faction(
                _catalog(Path(raw), duplicate_porter=True)
            )
        self.assertIn("MenPorter", report["unresolved"]["ambiguousObjects"])
        self.assertGreater(report["summary"]["unresolvedCount"], 0)

    def test_wrong_player_template_side_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            with self.assertRaisesRegex(ValueError, "Side must be Men"):
                census_men_faction(_catalog(Path(raw), side="Elves"))


if __name__ == "__main__":
    unittest.main()
