from __future__ import annotations

import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

from openbfme_importer.catalog import InstallCatalog
from openbfme_importer.faction_census import (
    _is_playable_template,
    PlayableFaction,
    census_men_faction,
    census_playable_faction,
    object_audio_definition_routes,
    resolve_playable_faction,
)
from openbfme_importer.sage_ini import IniBlock

from importer.tests.test_big import make_big


def test_object_audio_schema_distinguishes_silence_eva_sound_and_alias() -> None:
    assert object_audio_definition_routes("Cow", "VoiceSelect", "NoSound") == ()
    assert object_audio_definition_routes(
        "CaveTroll_Slaved", "VoiceCreated", "EVA:CaveTrollCreated"
    ) == ()
    assert object_audio_definition_routes(
        "BarrowWight", "VoiceCreated", "+SOUND:BarrowWightVoxCreated"
    ) == (("BarrowWightVoxCreated", "BarrowWightVoxCreated"),)
    assert object_audio_definition_routes(
        "MordorBatteringRam", "Sound", "INITIAL UrukVoiceDie"
    ) == (("OrcVoiceDie", "UrukVoiceDie"),)


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
    *,
    duplicate_porter: bool = False,
    missing_soldier_voice: bool = False,
    respawn_portrait: bool = False,
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
    if respawn_portrait:
        # Retail heroes author their recruitment portrait at the Object top
        # level while RespawnUpdate modules carry their own respawn icon.
        source = source.replace(
            b"  Sound = INITIAL\n  SelectPortrait = UPSoldier\nEnd",
            b"  Sound = INITIAL\n  SelectPortrait = UPSoldier\n"
            b"  ButtonImage = HIHeroBase\n"
            b"  Behavior = RespawnUpdate ModuleTag_RespawnUpdate\n"
            b"    ButtonImage = HIHeroBaseRespawn\n"
            b"  End\n"
            b"End",
        ).replace(
            b"ChildObject HeroA HeroBase\n  SelectPortrait = HealImage\nEnd",
            b"ChildObject HeroA HeroBase\n  SelectPortrait = HealImage\n"
            b"  Behavior = RespawnUpdate ModuleTag_RespawnUpdate\n"
            b"    ButtonImage = HIHeroARespawn\n"
            b"  End\n"
            b"End",
        )
    if missing_soldier_voice:
        source = source.replace(
            b"VoiceSelect = SoldierVoice",
            b"VoiceSelect = MissingSoldierVoice",
        )
    return source + duplicate


def _additive_voice_objects() -> bytes:
    return _objects().replace(
        b"  VoiceSelect = SoldierVoice\n  VoicePriority = 43",
        b"  VoiceSelect = SoldierVoice\n"
        b"  VoiceCreated = NoSound\n"
        b"  VoiceCreated = +SOUND:SoldierSelect\n"
        b"  VoicePriority = 43",
    )


def _spell_fx_objects() -> bytes:
    return _objects().replace(
        b"Object MenSpellBook\n  CommandSet = MenSpellBookCommandSet\nEnd",
        b"Object MenSpellBook\n"
        b"  CommandSet = MenSpellBookCommandSet\n"
        b"  Behavior = SpecialPowerModule ModuleTag_Heal\n"
        b"    SpecialPowerTemplate = SpellBookHeal\n"
        b"    TriggerFX = FX_HealSpell\n"
        b"  End\n"
        b"End",
    )


def _fx_lists() -> bytes:
    return b"""
FXList FX_HealSpell
  ParticleSystem
    Name = HealFlare
  End
  Sound
    Name = HealSoundFx
  End
End
"""


def _spell_fx_sound_effects() -> bytes:
    return _sound_effects() + b"""
AudioEvent HealSoundFx
  Sounds = heal_fx
  Type = world player
End
"""


def _mapped_images(
    *, missing_train_texture: bool = False, respawn_portrait: bool = False
) -> bytes:
    train_texture = (
        "AbsentTrainAtlas.tga" if missing_train_texture else "TrainAtlas.tga"
    )
    respawn = (
        """
MappedImage HIHeroBase
  Texture = HeroBaseIcon.tga
  TextureWidth = 64
  TextureHeight = 64
  Coords = Left:0 Top:0 Right:32 Bottom:32
End
MappedImage HIHeroBaseRespawn
  Texture = HeroBaseRespawn.tga
  TextureWidth = 64
  TextureHeight = 64
  Coords = Left:0 Top:0 Right:32 Bottom:32
End
MappedImage HIHeroARespawn
  Texture = HeroARespawn.tga
  TextureWidth = 64
  TextureHeight = 64
  Coords = Left:0 Top:0 Right:32 Bottom:32
End
"""
        if respawn_portrait
        else ""
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
{respawn}""".encode("cp1252")


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


def _music() -> bytes:
    return b"""
MusicTrack BattleTrack01
  Filename = battle01.mp3
End
Multisound Shell2Music
  Subsounds = BattleTrack01
End
Multisound Shell2MusicForLoadScreen
  Subsounds = BattleTrack01
End
"""


def _eva() -> bytes:
    return b"""
NewEvaEvent FixtureCreated
  Priority = 5
  SideSound
    Side = Men
    Sound = SoldierSelect
  End
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
    additive_voice: bool = False,
    missing_barracks_command_set: bool = False,
    spell_fx: bool = False,
    respawn_portrait: bool = False,
) -> InstallCatalog:
    if additive_voice:
        object_source = _additive_voice_objects()
    elif spell_fx:
        object_source = _spell_fx_objects()
    else:
        object_source = _objects(
            duplicate_porter=duplicate_porter,
            missing_soldier_voice=missing_soldier_voice,
            respawn_portrait=respawn_portrait,
        )
    command_set_source = _command_sets()
    if missing_barracks_command_set:
        command_set_source = command_set_source.replace(
            b"CommandSet MenBarracksCommandSet\n  1 = Command_TrainSoldiers\nEnd\n",
            b"",
        )
    make_big(
        root / "ini.big",
        {
            "data/ini/playertemplate.ini": _player_template(
                side=side, player_template=player_template, heroes=heroes
            ),
            "data/ini/commandset.ini": command_set_source,
            "data/ini/commandbutton.ini": _command_buttons(
                duplicate_soldier=duplicate_soldier_button,
                conflicting_soldier=conflicting_soldier_button,
                case_variant_image=case_variant_image,
                missing_secondary_sound=missing_secondary_sound,
            ),
            "data/ini/object/goodfaction/men.ini": object_source,
            "data/ini/mappedimages/aptimages/fixture.ini": _mapped_images(
                missing_train_texture=missing_train_texture,
                respawn_portrait=respawn_portrait,
            ),
            "data/ini/soundeffects.ini": (
                _spell_fx_sound_effects() if spell_fx else _sound_effects()
            ),
            "data/ini/voice.ini": _voice(),
            "data/ini/fxlist.ini": _fx_lists(),
            "data/ini/eva.ini": _eva(),
            "data/ini/music.ini": _music(),
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
            "data/audio/sounds/heal_fx.wav": b"sample-heal-fx",
            "data/audio/music/battle01.mp3": b"music-battle-01",
            **(
                {
                    "art/compiledtextures/he/herobaseicon.dds": b"texture-hero-base",
                    "art/compiledtextures/he/herobaserespawn.dds": b"texture-hero-base-respawn",
                    "art/compiledtextures/he/heroarespawn.dds": b"texture-hero-a-respawn",
                }
                if respawn_portrait
                else {}
            ),
        },
    )
    return InstallCatalog.build(root)


class FactionCensusTests(unittest.TestCase):
    def test_playable_template_predicate_is_exact_and_fail_closed(self) -> None:
        def block(*assignments: tuple[str, str]) -> IniBlock:
            return IniBlock("PlayerTemplate", "Fixture", None, assignments)

        self.assertTrue(_is_playable_template(block(("PlayableSide", "Yes"))))
        self.assertFalse(_is_playable_template(block(("PlayableSide", "No"))))
        self.assertFalse(_is_playable_template(block()))
        with self.assertRaisesRegex(ValueError, "unsupported PlayableSide"):
            _is_playable_template(block(("PlayableSide", "Maybe")))
        with self.assertRaisesRegex(ValueError, "ambiguous PlayableSide"):
            _is_playable_template(
                block(("PlayableSide", "Yes"), ("PlayableSide", "No"))
            )

    def test_faction_aliases_resolve_against_discovered_templates(self) -> None:
        factions = (
            PlayableFaction("FactionAngmar", "Angmar", 89),
            PlayableFaction("PlayableMen", "Men", 10),
        )
        with mock.patch(
            "openbfme_importer.faction_census.discover_playable_factions",
            return_value=factions,
        ):
            self.assertEqual(
                resolve_playable_faction(mock.Mock(), "FactionAngmar"), factions[0]
            )
            self.assertEqual(resolve_playable_faction(mock.Mock(), "angmar"), factions[0])
            self.assertEqual(resolve_playable_faction(mock.Mock(), "Men"), factions[1])

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

    def test_module_scoped_button_image_never_shadows_top_level_portrait(self) -> None:
        # Retail MordorFellBeast shape: the child authors only a RespawnUpdate
        # module ButtonImage while the parent's top level authors the
        # recruitment portrait.  The module value is a genuine reference but
        # must not shadow the inherited top-level one.
        with tempfile.TemporaryDirectory() as raw:
            report = census_men_faction(_catalog(Path(raw), respawn_portrait=True))
        self.assertEqual(report["summary"]["unresolvedCount"], 0)
        hero = next(
            item for item in report["definitions"]["objects"] if item["id"] == "HeroA"
        )
        self.assertIn(
            {
                "field": "ButtonImage",
                "targetKind": "mapped-image",
                "targetId": "HIHeroBase",
                "sourceObjectId": "HeroBase",
            },
            hero["edges"],
        )
        self.assertIn(
            {
                "field": "ButtonImage",
                "targetKind": "mapped-image",
                "targetId": "HIHeroARespawn",
            },
            hero["edges"],
        )
        self.assertIn("HIHeroBase", report["dependencies"]["mappedImages"])
        self.assertIn("HIHeroARespawn", report["dependencies"]["mappedImages"])
        self.assertIn(
            {
                "field": "ButtonImage",
                "targetKind": "mapped-image",
                "targetId": "HIHeroBaseRespawn",
                "sourceObjectId": "HeroBase",
            },
            hero["edges"],
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

    def test_additive_sound_prefix_resolves_the_namespaced_identifier(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            report = census_men_faction(_catalog(Path(raw), additive_voice=True))
        self.assertEqual(report["summary"]["unresolvedCount"], 0)
        soldier = next(
            item for item in report["definitions"]["objects"] if item["id"] == "Soldier"
        )
        self.assertIn(
            {
                "field": "VoiceCreated",
                "targetKind": "audio-definition",
                "targetId": "SoldierSelect",
                "resolution": "resolved",
            },
            soldier["edges"],
        )
        self.assertFalse(
            any(
                edge["targetId"] in {"SOUND", "+SOUND", "NoSound"}
                for item in report["definitions"]["objects"]
                for edge in item["edges"]
                if edge["targetKind"] == "audio-definition"
            )
        )

    def test_spellbook_fx_sound_nuggets_become_audio_roots(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            report = census_men_faction(_catalog(Path(raw), spell_fx=True))
        self.assertEqual(report["summary"]["unresolvedCount"], 0)
        self.assertEqual(report["unresolved"]["missingFxLists"], [])
        self.assertIn("FX_HealSpell", report["dependencies"]["fxLists"])
        self.assertIn("HealSoundFx", report["dependencies"]["audioRootIds"])
        spellbook = next(
            item
            for item in report["definitions"]["objects"]
            if item["id"] == "MenSpellBook"
        )
        self.assertIn(
            {
                "field": "TriggerFX",
                "targetKind": "fx-list",
                "targetId": "FX_HealSpell",
            },
            spellbook["edges"],
        )
        self.assertIn(
            {
                "field": "FXList:FX_HealSpell",
                "targetKind": "audio-definition",
                "targetId": "HealSoundFx",
                "resolution": "resolved",
            },
            spellbook["edges"],
        )

    def test_source_null_texture_policy_covers_only_declared_retail_gaps(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            catalog = _catalog(Path(raw), missing_train_texture=True)
            report = census_playable_faction(
                catalog,
                player_template="FactionMen",
                expected_side="Men",
                source_null_mapped_image_textures=(
                    ("AbsentTrainAtlas.tga", "retail placeholder atlas"),
                ),
            )
        self.assertEqual(report["summary"]["unresolvedCount"], 0)
        self.assertNotIn("missingMappedImageTextures", report["unresolved"])
        self.assertEqual(
            report["dependencies"]["sourceNullMappedImageTextures"],
            [
                {
                    "texture": "AbsentTrainAtlas.tga",
                    "reason": "retail placeholder atlas",
                    "mappedImages": ["TrainSoldierImage"],
                }
            ],
        )
        self.assertEqual(report["summary"]["mappedImageTextureSourceNullCount"], 1)
        image = next(
            item
            for item in report["resolvedLeaves"]["mappedImages"]
            if item["id"] == "TrainSoldierImage"
        )
        self.assertEqual(image["compiledTextureResolution"], "source-null")

    def test_resolved_texture_declared_source_null_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            with self.assertRaisesRegex(ValueError, "source-null MappedImage texture"):
                census_playable_faction(
                    _catalog(Path(raw)),
                    player_template="FactionMen",
                    expected_side="Men",
                    source_null_mapped_image_textures=(
                        ("TrainAtlas.tga", "stale policy entry"),
                    ),
                )

    def test_source_null_command_set_policy_covers_only_declared_retail_gaps(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as raw:
            catalog = _catalog(Path(raw), missing_barracks_command_set=True)
            uncovered = census_playable_faction(
                catalog,
                player_template="FactionMen",
                expected_side="Men",
            )
            self.assertEqual(
                uncovered["unresolved"]["missingCommandSets"],
                ["MenBarracksCommandSet"],
            )
            covered = census_playable_faction(
                catalog,
                player_template="FactionMen",
                expected_side="Men",
                source_null_command_sets=(
                    ("MenBarracksCommandSet", "retail placeholder command set"),
                ),
            )
        self.assertEqual(covered["summary"]["unresolvedCount"], 0)
        self.assertEqual(
            covered["dependencies"]["sourceNullCommandSets"],
            [
                {
                    "id": "MenBarracksCommandSet",
                    "reason": "retail placeholder command set",
                }
            ],
        )
        self.assertEqual(covered["summary"]["commandSetSourceNullCount"], 1)

    def test_resolved_command_set_declared_source_null_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            with self.assertRaisesRegex(ValueError, "source-null CommandSet"):
                census_playable_faction(
                    _catalog(Path(raw)),
                    player_template="FactionMen",
                    expected_side="Men",
                    source_null_command_sets=(
                        ("MenBarracksCommandSet", "stale policy entry"),
                    ),
                )


if __name__ == "__main__":
    unittest.main()


class FactionCensusEvaTests(unittest.TestCase):
    def test_eva_voice_prefix_routes_side_announcer_sounds(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            report = census_men_faction(_catalog(Path(raw)))
        self.assertEqual(report["summary"]["unresolvedCount"], 0)
        self.assertEqual(report["unresolved"]["missingEvaEvents"], [])
        hero = next(
            item for item in report["definitions"]["objects"] if item["id"] == "HeroA"
        )
        self.assertIn(
            {
                "field": "VoiceCreated",
                "targetKind": "eva-event",
                "targetId": "FixtureCreated",
                "resolution": "resolved",
                "sourceObjectId": "HeroBase",
            },
            hero["edges"],
        )
        self.assertIn(
            {
                "field": "EvaEvent:FixtureCreated",
                "targetKind": "audio-definition",
                "targetId": "SoldierSelect",
                "resolution": "resolved",
                "sourceObjectId": "HeroBase",
            },
            hero["edges"],
        )

    def test_missing_eva_event_is_an_explicit_gap(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            catalog_root = Path(raw)
            catalog = _catalog(catalog_root)
            # Rewrite eva.ini without the referenced event.
            import zipfile  # noqa: F401  (documents the big archive rewrite below)
            from importer.tests.test_big import make_big as _make
            _make(
                catalog_root / "ini.big",
                {
                    "data/ini/playertemplate.ini": _player_template(),
                    "data/ini/commandset.ini": _command_sets(),
                    "data/ini/commandbutton.ini": _command_buttons(),
                    "data/ini/object/goodfaction/men.ini": _objects(),
                    "data/ini/mappedimages/aptimages/fixture.ini": _mapped_images(),
                    "data/ini/soundeffects.ini": _sound_effects(),
                    "data/ini/voice.ini": _voice(),
                    "data/ini/fxlist.ini": _fx_lists(),
                    "data/ini/eva.ini": b"\n",
                    "data/ini/music.ini": _music(),
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
                    "data/audio/sounds/heal_fx.wav": b"sample-heal-fx",
                    "data/audio/music/battle01.mp3": b"music-battle-01",
                },
            )
            report = census_men_faction(InstallCatalog.build(catalog_root))
        self.assertEqual(report["unresolved"]["missingEvaEvents"], ["FixtureCreated"])


class FactionCensusMusicTests(unittest.TestCase):
    def test_declared_music_roots_resolve_through_music_tracks(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            report = census_playable_faction(
                _catalog(Path(raw)),
                player_template="FactionMen",
                expected_side="Men",
                music_roots=(
                    ("Shell2Music", "shell loop"),
                    ("Shell2MusicForLoadScreen", "load loop"),
                ),
            )
        self.assertEqual(report["summary"]["unresolvedCount"], 0)
        self.assertEqual(
            report["dependencies"]["musicRootIds"],
            ["Shell2Music", "Shell2MusicForLoadScreen"],
        )
        self.assertEqual(report["summary"]["musicRootCount"], 2)
        self.assertIn("Shell2Music", report["dependencies"]["audioRootIds"])
        self.assertIn(
            "battle01",
            {row["id"].casefold() for row in report["resolvedLeaves"]["audio"]["samplePaths"]},
        )
        self.assertIn(
            {
                "sourceField": "shell loop",
                "id": "Shell2Music",
                "edgeKind": "engine-music-root",
            },
            report["roots"],
        )

    def test_undeclared_or_missing_music_root_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            with self.assertRaisesRegex(ValueError, "music root"):
                census_playable_faction(
                    _catalog(Path(raw)),
                    player_template="FactionMen",
                    expected_side="Men",
                    music_roots=(("AbsentShellMusic", "missing loop"),),
                )
