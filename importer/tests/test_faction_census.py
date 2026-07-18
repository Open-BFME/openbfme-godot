from __future__ import annotations

import json
from pathlib import Path
import tempfile
import unittest

from openbfme_importer.catalog import InstallCatalog
from openbfme_importer.faction_census import (
    census_men_faction,
    census_playable_faction,
)

from importer.tests.test_big import make_big


def _player_template(
    *,
    side: str = "Men",
    player_template: str = "FactionMen",
    heroes: str = "HeroA",
) -> bytes:
    return f"""
PlayerTemplate {player_template}
  Side = {side}
  StartingUnit0 = MenPorter
  StartingUnit1 = MenPorter
  StartingBuilding = MenFortress
  BuildableHeroesMP = {heroes}
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


def _command_buttons(
    *,
    duplicate_soldier: bool = False,
    conflicting_soldier: bool = False,
    case_variant_image: bool = False,
    missing_secondary_sound: bool = False,
) -> bytes:
    duplicate = (
        b"""
CommandButton Command_TrainSoldiers
  Command = UNIT_BUILD
  Object = SoldierHorde
  Upgrade = Upgrade_MenTraining
  ButtonImage = TrainSoldierImage
  UnitSpecificSound = SoldierVoice HumanVoiceDie SoldierSelect
End
"""
        if duplicate_soldier
        else b""
    )
    if conflicting_soldier:
        duplicate = duplicate.replace(b"Object = SoldierHorde", b"Object = HeroA")
    source = b"""
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
  UnitSpecificSound = SoldierVoice HumanVoiceDie SoldierSelect
End
CommandButton Command_Heal
  Command = SPECIAL_POWER
  SpecialPower = SpellBookHeal
  Science = SCIENCE_Heal
  ButtonImage = HealImage
  DescriptLabel = CONTROLBAR:HealDescription
End
"""
    if case_variant_image:
        source = source.replace(
            b"ButtonImage = HealImage",
            b"ButtonImage = HealImage\n  ButtonImage = healimage",
        )
    if missing_secondary_sound:
        source = source.replace(
            b"UnitSpecificSound = SoldierVoice HumanVoiceDie SoldierSelect",
            b"UnitSpecificSound = SoldierVoice MissingToggleSound SoldierSelect",
        )
    return source + duplicate


def _objects(
    *, duplicate_porter: bool = False, missing_soldier_voice: bool = False
) -> bytes:
    duplicate = b"Object MenPorter\nEnd\n" if duplicate_porter else b""
    source = b"""
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
  InitiateVoice = SoldierVoice
End
Object Soldier
  VoiceSelect = SoldierVoice
  VoicePriority = 43
  SelectPortrait = UPSoldier
  DisplayName = OBJECT:Soldier
  Behavior = SlowDeathBehavior ModuleTag_Death
    Sound = INITIAL HumanVoiceDie
  End
End
Object MenBanner
  SelectPortrait = MissingBannerPortrait
End
Object HeroBase
  VoiceSelect = SoldierVoice
  VoiceCreated = EVA:FixtureCreated
  Sound = INITIAL
  SelectPortrait = UPSoldier
End
ChildObject HeroA HeroBase
  SelectPortrait = HealImage
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
"""
    if missing_soldier_voice:
        source = source.replace(
            b"VoiceSelect = SoldierVoice",
            b"VoiceSelect = MissingSoldierVoice",
        )
    return source + duplicate


def _mapped_images(*, missing_train_texture: bool = False) -> bytes:
    train_texture = (
        "AbsentTrainAtlas.tga" if missing_train_texture else "TrainAtlas.tga"
    )
    return f"""
MappedImage BuildBarracksImage
  Texture = BuildAtlas.tga
  TextureWidth = 64
  TextureHeight = 64
  Coords = Left:0 Top:0 Right:32 Bottom:32
End
MappedImage TrainSoldierImage
  Texture = {train_texture}
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
""".encode("cp1252")


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


def _catalog(
    root: Path,
    *,
    side: str = "Men",
    player_template: str = "FactionMen",
    duplicate_porter: bool = False,
    duplicate_soldier_button: bool = False,
    conflicting_soldier_button: bool = False,
    missing_train_texture: bool = False,
    case_variant_image: bool = False,
    heroes: str = "HeroA",
    missing_soldier_voice: bool = False,
    missing_secondary_sound: bool = False,
) -> InstallCatalog:
    make_big(
        root / "ini.big",
        {
            "data/ini/playertemplate.ini": _player_template(
                side=side, player_template=player_template, heroes=heroes
            ),
            "data/ini/commandset.ini": _command_sets(),
            "data/ini/commandbutton.ini": _command_buttons(
                duplicate_soldier=duplicate_soldier_button,
                conflicting_soldier=conflicting_soldier_button,
                case_variant_image=case_variant_image,
                missing_secondary_sound=missing_secondary_sound,
            ),
            "data/ini/object/goodfaction/men.ini": _objects(
                duplicate_porter=duplicate_porter,
                missing_soldier_voice=missing_soldier_voice,
            ),
            "data/ini/mappedimages/aptimages/fixture.ini": _mapped_images(
                missing_train_texture=missing_train_texture
            ),
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
        self.assertEqual(
            json.dumps(first, sort_keys=True), json.dumps(second, sort_keys=True)
        )
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
        self.assertEqual(
            first["dependencies"]["spellbookSpecialPowers"], ["SpellBookHeal"]
        )
        self.assertEqual(
            first["dependencies"]["sciences"], ["SCIENCE_Heal", "SCIENCE_MEN"]
        )
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
        self.assertEqual(first["summary"]["audioRootCount"], 3)
        self.assertEqual(first["summary"]["audioEventCount"], 2)
        self.assertEqual(first["summary"]["audioMultisoundCount"], 1)
        self.assertEqual(first["summary"]["audioSampleCount"], 3)
        self.assertEqual(first["summary"]["resolvedUpgradeDefinitionCount"], 2)
        self.assertEqual(first["summary"]["resolvedScienceDefinitionCount"], 2)
        self.assertEqual(first["summary"]["resolvedSpecialPowerDefinitionCount"], 1)
        self.assertEqual(first["dependencies"]["fxLists"], ["FX_MenTraining"])
        self.assertEqual(
            first["dependencies"]["audioRootIds"],
            ["HumanVoiceDie", "SoldierSelect", "SoldierVoice"],
        )
        train = next(
            item
            for item in first["definitions"]["commandButtons"]
            if item["id"] == "Command_TrainSoldiers"
        )
        self.assertEqual(
            [route["targetId"] for route in train["audioRoutes"]],
            ["SoldierVoice", "HumanVoiceDie", "SoldierSelect"],
        )
        soldier = next(
            item for item in first["definitions"]["objects"] if item["id"] == "Soldier"
        )
        soldier_horde = next(
            item
            for item in first["definitions"]["objects"]
            if item["id"] == "SoldierHorde"
        )
        self.assertIn(
            {
                "field": "InitiateVoice",
                "targetKind": "audio-definition",
                "targetId": "SoldierVoice",
                "resolution": "resolved",
            },
            soldier_horde["edges"],
        )
        self.assertIn(
            {
                "field": "Sound",
                "targetKind": "audio-definition",
                "targetId": "HumanVoiceDie",
                "resolution": "resolved",
            },
            soldier["edges"],
        )
        self.assertEqual(len(first["resolvedLeaves"]["localization"]["records"]), 3)
        self.assertEqual(len(first["resolvedLeaves"]["audio"]["samplePaths"]), 3)
        serialized = json.dumps(first)
        self.assertNotIn("DOZER_CONSTRUCT\n", serialized)
        self.assertNotIn("Build barracks", serialized)
        self.assertNotIn("sample-a", serialized)
        self.assertNotIn(str(catalog.install_root), serialized)

    def test_duplicate_object_is_reported_as_ambiguous_not_silently_selected(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as raw:
            report = census_men_faction(_catalog(Path(raw), duplicate_porter=True))
        self.assertIn("MenPorter", report["unresolved"]["ambiguousObjects"])
        self.assertGreater(report["summary"]["unresolvedCount"], 0)

    def test_semantically_identical_command_button_duplicate_is_one_definition(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as raw:
            report = census_men_faction(
                _catalog(Path(raw), duplicate_soldier_button=True)
            )
        self.assertEqual(report["summary"]["unresolvedCount"], 0)
        self.assertEqual(
            sum(
                item["id"] == "Command_TrainSoldiers"
                for item in report["definitions"]["commandButtons"]
            ),
            1,
        )

    def test_conflicting_command_button_duplicate_remains_ambiguous(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            report = census_men_faction(
                _catalog(
                    Path(raw),
                    duplicate_soldier_button=True,
                    conflicting_soldier_button=True,
                )
            )
        self.assertEqual(
            report["unresolved"]["ambiguousCommandButtons"],
            ["Command_TrainSoldiers"],
        )

    def test_missing_retail_ui_atlas_is_an_explicit_graph_gap(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            report = census_men_faction(_catalog(Path(raw), missing_train_texture=True))
        self.assertEqual(
            report["unresolved"]["missingMappedImageTextures"],
            ["AbsentTrainAtlas.tga"],
        )
        image = next(
            item
            for item in report["resolvedLeaves"]["mappedImages"]
            if item["id"] == "TrainSoldierImage"
        )
        self.assertEqual(image["compiledTextureResolution"], "missing")
        self.assertNotIn("compiledTextureVirtualPath", image)

    def test_case_variant_mapped_image_references_share_one_identity(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            report = census_men_faction(_catalog(Path(raw), case_variant_image=True))
        self.assertEqual(report["summary"]["unresolvedCount"], 0)
        self.assertEqual(
            sum(
                item["id"].casefold() == "healimage"
                for item in report["resolvedLeaves"]["mappedImages"]
            ),
            1,
        )

    def test_wrong_player_template_side_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            with self.assertRaisesRegex(ValueError, "Side must be Men"):
                census_men_faction(_catalog(Path(raw), side="Elves"))

    def test_non_men_template_uses_source_side_and_intrinsic_science(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            catalog = _catalog(Path(raw), side="Elves", player_template="FactionElves")
            report = census_playable_faction(
                catalog,
                player_template="FactionElves",
                expected_side="Elves",
            )
        self.assertEqual(report["target"]["faction"], "Elves")
        self.assertEqual(report["target"]["playerTemplate"], "FactionElves")
        self.assertNotIn("SCIENCE_MEN", report["dependencies"]["spellbookSciences"])
        self.assertFalse(
            any(
                root["edgeKind"] == "engine-implicit-object" for root in report["roots"]
            )
        )
        self.assertEqual(
            report["playerTemplateRosters"]["entries"],
            [
                {
                    "sourceField": "BuildableHeroesMP",
                    "assignmentOrdinal": 4,
                    "tokenOrdinal": 0,
                    "rosterOrdinal": 0,
                    "id": "HeroA",
                }
            ],
        )
        hero = next(
            item for item in report["definitions"]["objects"] if item["id"] == "HeroA"
        )
        self.assertIn(
            {
                "field": "VoiceSelect",
                "targetKind": "audio-definition",
                "targetId": "SoldierVoice",
                "resolution": "resolved",
                "sourceObjectId": "HeroBase",
            },
            hero["edges"],
        )
        self.assertIn(
            {
                "field": "SelectPortrait",
                "targetKind": "mapped-image",
                "targetId": "HealImage",
            },
            hero["edges"],
        )
        self.assertFalse(
            any(
                edge["field"].casefold() == "selectportrait"
                and edge["targetId"] == "UPSoldier"
                for edge in hero["edges"]
            )
        )

    def test_missing_authored_audio_target_remains_an_unresolved_edge(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            report = census_men_faction(_catalog(Path(raw), missing_soldier_voice=True))
        soldier = next(
            item for item in report["definitions"]["objects"] if item["id"] == "Soldier"
        )
        self.assertIn(
            {
                "field": "VoiceSelect",
                "targetKind": "audio-definition",
                "targetId": "MissingSoldierVoice",
                "resolution": "unresolved",
            },
            soldier["edges"],
        )
        self.assertEqual(
            report["unresolved"]["missingAudioDefinitions"],
            ["MissingSoldierVoice"],
        )
        self.assertFalse(
            any(
                edge["targetId"] in {"43", "INITIAL", "EVA"}
                for item in report["definitions"]["objects"]
                for edge in item["edges"]
                if edge["targetKind"] == "audio-definition"
            )
        )

    def test_missing_secondary_command_audio_is_not_dropped(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            report = census_men_faction(
                _catalog(Path(raw), missing_secondary_sound=True)
            )
        train = next(
            item
            for item in report["definitions"]["commandButtons"]
            if item["id"] == "Command_TrainSoldiers"
        )
        self.assertEqual(
            train["audioRoutes"][1],
            {
                "field": "UnitSpecificSound",
                "targetId": "MissingToggleSound",
                "tokenOrdinal": 1,
                "resolution": "unresolved",
            },
        )
        self.assertIn(
            "MissingToggleSound",
            report["unresolved"]["missingAudioDefinitions"],
        )

    def test_generic_identity_binds_template_and_explicit_implicit_roots(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            catalog = _catalog(Path(raw), side="Elves", player_template="FactionElves")
            plain = census_playable_faction(catalog, player_template="FactionElves")
            rooted = census_playable_faction(
                catalog,
                player_template="FactionElves",
                implicit_object_roots=(("MenFortressCenterGeneric", "fixture-root"),),
            )
        with tempfile.TemporaryDirectory() as men_raw:
            legacy_men = census_men_faction(_catalog(Path(men_raw), side="Men"))
        self.assertNotEqual(plain["inputSetSha256"], rooted["inputSetSha256"])
        self.assertNotEqual(
            plain["summary"]["objectSetSha256"],
            legacy_men["summary"]["objectSetSha256"],
        )
        self.assertIn(
            {
                "sourceField": "fixture-root",
                "id": "MenFortressCenterGeneric",
                "edgeKind": "engine-implicit-object",
            },
            rooted["roots"],
        )

    def test_implicit_root_order_is_stable_and_case_collisions_fail(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            catalog = _catalog(Path(raw), side="Elves", player_template="FactionElves")
            forward = census_playable_faction(
                catalog,
                player_template="FactionElves",
                implicit_object_roots=(
                    ("MenFortressCitadel", "second"),
                    ("MenFortressCenterGeneric", "first"),
                ),
            )
            reverse = census_playable_faction(
                catalog,
                player_template="FactionElves",
                implicit_object_roots=(
                    ("MenFortressCenterGeneric", "first"),
                    ("MenFortressCitadel", "second"),
                ),
            )
            with self.assertRaisesRegex(
                ValueError, "case-colliding implicit object roots"
            ):
                census_playable_faction(
                    catalog,
                    player_template="FactionElves",
                    implicit_object_roots=(
                        ("MenFortressCitadel", "first"),
                        ("menfortresscitadel", "second"),
                    ),
                )
        self.assertEqual(forward, reverse)

    def test_generic_hero_roster_preserves_authored_order_and_duplicates(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            report = census_playable_faction(
                _catalog(
                    Path(raw),
                    side="Elves",
                    player_template="FactionElves",
                    heroes="MenPorter HeroA MenPorter",
                ),
                player_template="FactionElves",
            )
        entries = report["playerTemplateRosters"]["entries"]
        self.assertEqual(
            [item["id"] for item in entries], ["MenPorter", "HeroA", "MenPorter"]
        )
        self.assertEqual([item["tokenOrdinal"] for item in entries], [0, 1, 2])
        self.assertEqual([item["rosterOrdinal"] for item in entries], [0, 1, 2])


if __name__ == "__main__":
    unittest.main()
