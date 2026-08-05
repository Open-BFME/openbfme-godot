"""Tracklist extraction from authored music INI + music-script library."""

from __future__ import annotations

import pytest

from openbfme_importer.music_import import (
    MusicImportError,
    build_music_document,
    compose_music_profile,
    music_profile_resources,
    music_script_faction_bindings,
    parse_misc_audio_music,
    parse_music_defines,
    resolve_volume,
)

# Shaped exactly like data/ini/music.ini: #define volume constants, MusicTrack
# leaves with #ADD volumes, and the "music scripting multisounds" section.
MUSIC_INI = b"""
#define CD_BA_VOLUME 58
#define CD_EX_VOLUME 46

MusicTrack Silence
  ; use this to play silence.
  Type = FAKE
End

MusicTrack BaGood04
  Filename = BaGood04_R15.mp3
  Volume = #ADD( CD_BA_VOLUME 0 )
End

MusicTrack BaGood07
  Filename = BaGood07_T17.mp3
  Volume = #ADD( CD_BA_VOLUME -2 )
End

MusicTrack BaEvil01
  Filename = BaEvil01_T05.mp3
  Volume = 55
End

MusicTrack ExGood01
  Filename = ExGood01_F13.mp3
  Volume = CD_EX_VOLUME
End

MusicTrack ExEvil01
  Filename = ExEvil01_R11.mp3
  Volume = CD_EX_VOLUME
End

MusicTrack VictoryScreenGood
  Filename = VSGood01.mp3
  Volume = 60
End

MusicTrack VictoryScreenEvil
  Filename = VSEvil01.mp3
  Volume = 60
End

MusicTrack VictoryScreenAngmar
  Filename = VSAngmar01.mp3
  Volume = 60
End

MusicTrack LoseScreenGood
  Filename = LSGood01.mp3
End

MusicTrack LoseScreenEvil
  Filename = LSEvil01.mp3
End

MusicTrack Shell2Music
  Filename = SX_BFME2_Ac1Evil02.mp3
  Volume = 55
End

MusicTrack R_BbEvil102
  Filename = R_BbEvil102.mp3
  Volume = 55
End

Multisound ShellLowLOD
  Control = PLAY_ONE LOOP
  Subsounds = R_BbEvil102
End

Multisound BaseBuildingMannishMusic
  Control = PLAY_ONE
  Subsounds = BaGood04 BaGood07
End

Multisound BaseBuildingMordorMusic
  Control = PLAY_ONE
  Subsounds = BaEvil01
End

Multisound ExploreMannishMusic
  Control = PLAY_ONE
  Subsounds = ExGood01 Silence
End

Multisound ExploreMordorMusic
  Control = PLAY_ONE
  Subsounds = ExEvil01
End

Multisound ActionGoodMusic
  Control = PLAY_ONE
  Subsounds = BaGood04
End

Multisound ActionEvilMusic
  Control = PLAY_ONE
  Subsounds = BaEvil01
End
"""

MISC_AUDIO_INI = b"""
MiscAudio
  LowLODShellMusic          = ShellLowLOD
  HighLODShellMusic         = ShellLowLOD
  ShellMapLoadMusic         = NoSound ; Shell2Music
  FullScreenSubMenuMusic    = Shell2Music
End
"""


def _condition(opcode: str, *arguments: object) -> dict[str, object]:
    return {
        "name": "OrCondition",
        "value": {
            "records": [
                {
                    "name": "Condition",
                    "value": {
                        "internalName": {"name": opcode},
                        "arguments": [_argument(value) for value in arguments],
                    },
                }
            ]
        },
    }


def _action(opcode: str, *arguments: object) -> dict[str, object]:
    return {
        "name": "ScriptAction",
        "value": {
            "internalName": {"name": opcode},
            "arguments": [_argument(value) for value in arguments],
        },
    }


def _argument(value: object) -> dict[str, object]:
    if isinstance(value, str):
        return {"text": value, "integer": 0}
    return {"text": "", "integer": value}


def _script(name: str, records: list[dict[str, object]]) -> dict[str, object]:
    return {"name": name, "payload": {"name": name, "records": records}}


def _faction_test(name: str, side: str, selector: int, good: bool) -> dict[str, object]:
    return _script(
        name,
        [
            _condition("FLAG", "/___MusicScript_Init", 1),
            _condition("SKIRMISH_PLAYER_FACTION", "<Local Player>", side),
            _action("SET_FLAG", "___MusicScript_IsGoodFaction", 1 if good else 0),
            _action("SET_COUNTER", "___MusicScript_PlayerFaction", selector),
        ],
    )


def _phase_play(
    name: str, selector: int, flag: str, playlist: str
) -> dict[str, object]:
    return _script(
        name,
        [
            _condition("COUNTER", "___MusicScript_PlayerFaction", 2, selector),
            _condition("FLAG", flag, 1),
            _action(
                "MUSIC_SCRIPT_PLAY_TRACK_FINITE_TIMES_AND_NOTIFY",
                playlist,
                1,
                0,
                0,
                "___MusicScript_Level0MusicDone",
            ),
        ],
    )


SCRIPT_DOCUMENT: dict[str, object] = {
    "scripts": [
        _faction_test("___MusicScript_TestMen", "Men", 1, True),
        _faction_test("___MusicScript_TestMordor", "Mordor", 5, False),
        # RotWK's real authored quirk: Angmar reuses Mordor's music slot.
        _faction_test("___MusicScript_TestAngmar", "Angmar", 5, False),
        _phase_play(
            "___MusicScript_DoBaseBuildMen",
            1,
            "___MusicScript_InPhaseBaseBuilding",
            "BaseBuildingMannishMusic",
        ),
        _phase_play(
            "___MusicScript_DoExploreMen",
            1,
            "___MusicScript_InPhaseExplore",
            "ExploreMannishMusic",
        ),
        _phase_play(
            "___MusicScript_DoBaseBuildMordor",
            5,
            "___MusicScript_InPhaseBaseBuilding",
            "BaseBuildingMordorMusic",
        ),
        _phase_play(
            "___MusicScript_DoExploreMordor",
            5,
            "___MusicScript_InPhaseExplore",
            "ExploreMordorMusic",
        ),
        _script(
            "___MusicScript_DoActionGood",
            [
                _condition("COUNTER", "___MusicScript_CurrentLevel1MusicType", 2, 1),
                _action(
                    "MUSIC_SCRIPT_PUSH_TRACK_FINITE_TIMES_AND_NOTIFY",
                    "ActionGoodMusic",
                    1,
                    1,
                    0,
                    "___MusicScript_Level1MusicDone",
                ),
            ],
        ),
        _script(
            "___MusicScript_DoActionEvil",
            [
                _condition("COUNTER", "___MusicScript_CurrentLevel1MusicType", 2, 1),
                _action(
                    "MUSIC_SCRIPT_PUSH_TRACK_FINITE_TIMES_AND_NOTIFY",
                    "ActionEvilMusic",
                    1,
                    1,
                    0,
                    "___MusicScript_Level1MusicDone",
                ),
            ],
        ),
    ]
}


def _document() -> dict[str, object]:
    return build_music_document(
        music_ini=MUSIC_INI,
        misc_audio_ini=MISC_AUDIO_INI,
        script_document=SCRIPT_DOCUMENT,
        provenance={"musicIni": "data/ini/music.ini"},
    )


def test_volume_defines_and_macros_resolve() -> None:
    defines = parse_music_defines(MUSIC_INI)
    assert defines["CD_BA_VOLUME"] == 58
    assert resolve_volume("#ADD( CD_BA_VOLUME 0 )", defines) == 58
    assert resolve_volume("#ADD( CD_BA_VOLUME -2 )", defines) == 56
    assert resolve_volume("#SUBTRACT( CD_BA_VOLUME 8 )", defines) == 50
    assert resolve_volume("CD_EX_VOLUME", defines) == 46
    assert resolve_volume("55", defines) == 55
    # Unknown forms stay honest rather than inventing a level.
    assert resolve_volume("#MULTIPLY( CD_BA_VOLUME 2 )", defines) is None
    assert resolve_volume(None, defines) is None


def test_faction_bindings_come_from_the_script_library() -> None:
    bindings = music_script_faction_bindings(SCRIPT_DOCUMENT)
    assert bindings["sides"]["Men"] == {
        "selector": 1,
        "alignment": "good",
        "scriptName": "___MusicScript_TestMen",
    }
    assert bindings["sides"]["Angmar"]["selector"] == 5
    assert bindings["sides"]["Angmar"]["alignment"] == "evil"
    assert bindings["phases"]["1"] == {
        "basebuilding": "BaseBuildingMannishMusic",
        "explore": "ExploreMannishMusic",
    }
    assert bindings["level1"]["good"]["action"] == "ActionGoodMusic"
    assert bindings["level1"]["evil"]["action"] == "ActionEvilMusic"


def test_misc_audio_shell_declarations() -> None:
    misc = parse_misc_audio_music(MISC_AUDIO_INI)
    assert misc["shellLowLod"] == "ShellLowLOD"
    assert misc["shellHighLod"] == "ShellLowLOD"
    assert misc["fullScreenSubMenu"] == "Shell2Music"
    # NoSound is a declaration of silence, not a track to ship.
    assert "shellMapLoad" not in misc


def test_document_binds_every_faction_to_a_non_empty_tracklist() -> None:
    document = _document()
    assert document["schema"] == "openbfme.music"
    factions = document["factions"]
    assert set(factions) == {"Men", "Mordor", "Angmar"}
    playlists = document["playlists"]
    for side, entry in factions.items():
        assert entry["phases"], side
        for playlist_id in entry["phases"].values():
            assert playlists[playlist_id]["tracks"], (side, playlist_id)
    assert factions["Men"]["phases"]["basebuilding"] == "BaseBuildingMannishMusic"
    assert factions["Mordor"]["phases"]["explore"] == "ExploreMordorMusic"
    # Angmar shares Mordor's slot because RotWK authors it that way.
    assert factions["Angmar"]["phases"] == factions["Mordor"]["phases"]
    assert factions["Men"]["level1"]["action"] == "ActionGoodMusic"
    assert factions["Angmar"]["level1"]["action"] == "ActionEvilMusic"


def test_screen_tracks_are_bound_and_marked_name_derived() -> None:
    factions = _document()["factions"]
    assert factions["Men"]["screens"] == {
        "defeat": "LoseScreenGood",
        "victory": "VictoryScreenGood",
    }
    assert factions["Mordor"]["screens"]["victory"] == "VictoryScreenEvil"
    # RotWK ships a dedicated Angmar victory leaf; the side beats the alignment.
    assert factions["Angmar"]["screens"]["victory"] == "VictoryScreenAngmar"
    assert factions["Angmar"]["screensDerivation"] == "name-bound"
    assert factions["Angmar"]["derivation"] == "script-bound"


def test_track_rows_carry_file_volume_and_control() -> None:
    tracks = _document()["tracks"]
    assert tracks["BaGood04"]["file"] == "audio/music/tracks/bagood04_r15.mp3"
    assert tracks["BaGood04"]["source"] == "data/audio/tracks/bagood04_r15.mp3"
    assert tracks["BaGood04"]["volume"] == 58
    assert tracks["BaGood07"]["volume"] == 56
    # A FAKE (fileless) leaf is recorded, never shipped as bytes.
    assert "Silence" not in tracks
    assert _document()["silentTracks"] == ["Silence"]
    assert _document()["playlists"]["ExploreMannishMusic"]["tracks"] == [
        "ExGood01",
        "Silence",
    ]


def test_shell_playlist_is_carried_for_the_menu() -> None:
    document = _document()
    assert document["shell"]["shellLowLod"] == "ShellLowLOD"
    assert document["playlists"]["ShellLowLOD"]["control"] == ["play_one", "loop"]
    # A MiscAudio slot naming a bare MusicTrack becomes a one-leaf playlist.
    assert document["playlists"]["Shell2Music"] == {
        "id": "Shell2Music",
        "control": ["play_one"],
        "tracks": ["Shell2Music"],
        "kind": "track",
    }


def test_only_referenced_tracks_become_resources() -> None:
    document = _document()
    resources = music_profile_resources(document)
    outputs = {resource["output"] for resource in resources}
    assert "assets/audio/music/tracks/bagood04_r15.mp3" in outputs
    assert len(resources) == len(document["tracks"])
    # Resource ids are bounded lowercase slugs: no underscores may survive.
    assert {resource["id"] for resource in resources} >= {
        "music-track-r-bbevil102",
        "music-track-sx-bfme2-ac1evil02",
    }
    for resource in resources:
        assert resource["kind"] == "music"
        assert resource["converter"] == "copy"
        assert resource["limit"] == 1
        assert resource["patterns"][0].startswith("data/audio/tracks/")


def test_profile_declares_the_music_document_and_pack_file() -> None:
    profile = compose_music_profile(
        _document(), pack_id="rotwk-music-vslice", game="rotwk", catalog_identity="ab" * 32
    )
    assert profile["pack"]["id"] == "rotwk-music-vslice"
    assert profile["pack"]["files"] == {"music": "data/music.json"}
    assert profile["pack"]["sourceCatalogIdentitySha256"] == "ab" * 32
    assert profile["runtime_data"]["data/music.json"]["schema"] == "openbfme.music"
    assert profile["resources"]


def test_missing_playlist_refuses_rather_than_shipping_a_silent_faction() -> None:
    broken = {
        "scripts": [
            _faction_test("___MusicScript_TestMen", "Men", 1, True),
            _phase_play(
                "___MusicScript_DoBaseBuildElves",
                2,
                "___MusicScript_InPhaseBaseBuilding",
                "BaseBuildingElvenMusic",
            ),
        ]
    }
    with pytest.raises(MusicImportError, match="binds no playlist"):
        build_music_document(
            music_ini=MUSIC_INI,
            misc_audio_ini=MISC_AUDIO_INI,
            script_document=broken,
        )
