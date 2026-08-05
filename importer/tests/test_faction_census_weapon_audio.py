"""Census routing of the retail weapon audio chain.

Retail authors weapon sounds OFF the object, on the weapon:
``Weapon BoromirSword / FireFX = FX_GondorSwordHit`` (pure retail
weapon.ini:5616-5624) resolved through ``FXList FX_GondorSwordHit / Sound /
Name = ImpactSword01`` (fxlist.ini:7584-7586).  The faction census must route
those AudioEvents as typed edges so the playable-unit lane can bind their
samples.
"""

from __future__ import annotations

from pathlib import Path
import tempfile
import unittest

from openbfme_importer.catalog import InstallCatalog
from openbfme_importer.faction_census import (
    _weapon_fire_fx_names,
    census_men_faction,
)

from importer.tests.test_big import make_big
from importer.tests.test_faction_census import (
    _command_buttons,
    _command_sets,
    _eva,
    _fx_lists,
    _mapped_images,
    _music,
    _objects,
    _player_template,
    _sciences,
    _sound_effects,
    _special_powers,
    _strings,
    _upgrades,
    _voice,
)


def _weapon_objects() -> bytes:
    return _objects().replace(
        b"Object Soldier\n",
        b"Object Soldier\n"
        b"  WeaponSet\n"
        b"    Conditions = None\n"
        b"    Weapon = PRIMARY SoldierSword\n"
        b"    Weapon = SECONDARY SoldierBow\n"
        b"    Weapon = TERTIARY UndefinedWeapon\n"
        b"  End\n",
    )


def _weapons() -> bytes:
    return b"""
Weapon SoldierSword
  MeleeWeapon = Yes
  FireFX = FX_SwordHit
  DamageNugget
    Damage = 40
    DamageType = SLASH
  End
End
Weapon SoldierBow
  FireFX = FX_BowRelease
  ProjectileDetonationFX = FX_ArrowHit
End
"""


def _weapon_fx_lists() -> bytes:
    return _fx_lists() + b"""
FXList FX_SwordHit
  Sound
    Name = ImpactSwordFx
  End
End
FXList FX_ArrowHit
  Sound
    Name = MissingImpactSoundDef
  End
End
FXList FX_BowRelease
  ParticleSystem
    Name = BowFlare
  End
End
"""


def _weapon_sound_effects() -> bytes:
    return _sound_effects() + b"""
AudioEvent ImpactSwordFx
  Sounds = impact_sword
  Type = world player
End
"""


def _catalog(root: Path, *, with_weapons: bool = True) -> InstallCatalog:
    entries = {
        "data/ini/playertemplate.ini": _player_template(),
        "data/ini/commandset.ini": _command_sets(),
        "data/ini/commandbutton.ini": _command_buttons(),
        "data/ini/object/goodfaction/men.ini": _weapon_objects(),
        "data/ini/mappedimages/aptimages/fixture.ini": _mapped_images(),
        "data/ini/soundeffects.ini": _weapon_sound_effects(),
        "data/ini/voice.ini": _voice(),
        "data/ini/fxlist.ini": _weapon_fx_lists(),
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
        "data/audio/sounds/impact_sword.wav": b"sample-impact-sword",
        "data/audio/music/battle01.mp3": b"music-battle-01",
    }
    if with_weapons:
        entries["data/ini/weapon.ini"] = _weapons()
    make_big(root / "ini.big", entries)
    return InstallCatalog.build(root)


class WeaponAudioCensusTests(unittest.TestCase):
    def test_weapon_fire_fx_parser_indexes_authored_fx_names(self) -> None:
        index = _weapon_fire_fx_names(_weapons())
        self.assertEqual(
            index["soldiersword"],
            ("SoldierSword", (("FireFX", "FX_SwordHit"),)),
        )
        self.assertEqual(
            index["soldierbow"],
            (
                "SoldierBow",
                (
                    ("FireFX", "FX_BowRelease"),
                    ("ProjectileDetonationFX", "FX_ArrowHit"),
                ),
            ),
        )

    def test_weapon_audio_chain_routes_edges_and_samples(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            report = census_men_faction(_catalog(Path(raw)))
        soldier = next(
            row
            for row in report["definitions"]["objects"]
            if row["id"] == "Soldier"
        )
        weapon_edges = [
            edge
            for edge in soldier["edges"]
            if str(edge.get("field", "")).startswith("Weapon:")
        ]
        self.assertIn(
            {
                "field": "Weapon:SoldierSword:FireFX",
                "targetKind": "audio-definition",
                "targetId": "ImpactSwordFx",
                "resolution": "resolved",
                "fxListId": "FX_SwordHit",
            },
            weapon_edges,
        )
        # An FXList Sound naming an AudioEvent retail never defines stays an
        # explicit unresolved edge, never a silent drop.
        self.assertIn(
            {
                "field": "Weapon:SoldierBow:ProjectileDetonationFX",
                "targetKind": "audio-definition",
                "targetId": "MissingImpactSoundDef",
                "resolution": "unresolved",
                "fxListId": "FX_ArrowHit",
            },
            weapon_edges,
        )
        # Sound-less FX lists route nothing (FX_BowRelease is visual-only).
        self.assertNotIn(
            "Weapon:SoldierBow:FireFX",
            {edge["field"] for edge in weapon_edges},
        )
        self.assertIn(
            "MissingImpactSoundDef",
            report["unresolved"]["missingAudioDefinitions"],
        )
        self.assertEqual(
            report["unresolved"]["missingWeaponDefinitions"],
            ["UndefinedWeapon"],
        )
        # The resolved event reaches the audio closure with its sample bound.
        events = {
            row["id"]
            for row in report["resolvedLeaves"]["audio"]["events"]
        }
        self.assertIn("ImpactSwordFx", events)
        samples = {
            row["id"]: row["virtualPath"]
            for row in report["resolvedLeaves"]["audio"]["samplePaths"]
        }
        self.assertEqual(
            samples.get("impact_sword"), "data/audio/sounds/impact_sword.wav"
        )
        # weapon.ini participates in census identity when present.
        self.assertIn(
            "data/ini/weapon.ini",
            {
                str(row["virtualPath"])
                for row in report["sourceDocuments"]
            },
        )

    def test_census_without_weapon_document_claims_no_weapon_chain(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            report = census_men_faction(_catalog(Path(raw), with_weapons=False))
        soldier = next(
            row
            for row in report["definitions"]["objects"]
            if row["id"] == "Soldier"
        )
        self.assertEqual(
            [
                edge
                for edge in soldier["edges"]
                if str(edge.get("field", "")).startswith("Weapon:")
            ],
            [],
        )
        self.assertEqual(report["unresolved"]["missingWeaponDefinitions"], [])


if __name__ == "__main__":
    unittest.main()
