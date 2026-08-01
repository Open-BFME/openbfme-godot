from __future__ import annotations

import unittest

try:
    from openbfme_importer.sage_audio import (
        parse_sage_audio_definitions,
        resolve_audio_sample_paths,
        resolve_audio_sample_paths_partial,
        resolve_sage_audio_closure,
    )
except ModuleNotFoundError as exc:
    if exc.name != "openbfme_importer":
        raise
    from importer.openbfme_importer.sage_audio import (
        parse_sage_audio_definitions,
        resolve_audio_sample_paths,
        resolve_audio_sample_paths_partial,
        resolve_sage_audio_closure,
    )


SOURCE = b"""
AudioEvent SoldierSelect
  Sounds = voice_a voice_b:125
  Type = world player
  Volume = 90
End
AudioEvent SoldierAttack
  Sounds = attack_a
  Type = world player
End
Multisound SoldierSelectOrAttack
  Subsounds = SoldierSelect SoldierAttack:50
  Control = random
End
"""


class SageAudioTests(unittest.TestCase):
    def test_soundeffects_and_voice_documents_form_one_exact_namespace(self) -> None:
        soundeffects = b"""
AudioEvent UnitSelect
  Sounds = unit_select_a unit_select_b
End
AudioEvent HumanVoiceDie
  Sounds = human_die
End
"""
        voice = b"""
Multisound UnitVoiceSelect
  Subsounds = UnitSelect
End
"""
        definitions = parse_sage_audio_definitions(soundeffects + b"\n" + voice)
        closure = resolve_sage_audio_closure(
            definitions, ["UnitVoiceSelect", "HumanVoiceDie"]
        )
        self.assertEqual(closure.root_ids, ("HumanVoiceDie", "UnitVoiceSelect"))
        self.assertEqual(
            tuple(item.id for item in closure.events), ("HumanVoiceDie", "UnitSelect")
        )
        self.assertEqual(
            tuple(item.id for item in closure.multisounds), ("UnitVoiceSelect",)
        )
        self.assertEqual(
            closure.sample_ids, ("human_die", "unit_select_a", "unit_select_b")
        )

    def test_parse_and_resolve_typed_closure_deterministically(self) -> None:
        definitions = parse_sage_audio_definitions(SOURCE)
        closure = resolve_sage_audio_closure(definitions, ["soldierselectorattack"])
        self.assertEqual([item.id for item in closure.events], ["SoldierAttack", "SoldierSelect"])
        self.assertEqual(
            [item.id for item in closure.multisounds], ["SoldierSelectOrAttack"]
        )
        self.assertEqual(closure.sample_ids, ("attack_a", "voice_a", "voice_b"))
        self.assertEqual(closure.events[1].sounds[1].weight, 125)
        self.assertEqual(closure.multisounds[0].parameters, (("Control", "random"),))
        self.assertEqual(
            resolve_sage_audio_closure(definitions, ["SoldierSelectOrAttack"]),
            closure,
        )
        serialized = str(closure.neutral())
        self.assertNotIn("AudioEvent SoldierSelect", serialized)
        self.assertNotIn("C:\\", serialized)

    def test_closure_collapses_retail_case_variants_of_one_sample(self) -> None:
        definitions = parse_sage_audio_definitions(
            b"AudioEvent Move\n Sounds = EUUruPi_voimovb\nEnd\n"
            b"AudioEvent Formation\n Sounds = EUUruPi_voiMovb\nEnd\n"
        )
        closure = resolve_sage_audio_closure(
            definitions, ["Move", "Formation"]
        )
        self.assertEqual(closure.sample_ids, ("EUUruPi_voiMovb",))
        self.assertEqual(
            resolve_audio_sample_paths(
                closure.sample_ids,
                ["data/audio/sounds/euurupi_voimovb.wav"],
            ),
            {"EUUruPi_voiMovb": "data/audio/sounds/euurupi_voimovb.wav"},
        )

    def test_sample_resolution_is_exact_and_path_safe(self) -> None:
        resolved = resolve_audio_sample_paths(
            ["voice_a", "voice_b"],
            [
                "data/audio/sounds/voice_b.wav",
                "data/audio/sounds/voice_a.mp3",
                "data/text/voice_a.wav",
            ],
        )
        self.assertEqual(
            resolved,
            {
                "voice_a": "data/audio/sounds/voice_a.mp3",
                "voice_b": "data/audio/sounds/voice_b.wav",
            },
        )

    def test_partial_sample_resolution_reports_missing_and_ambiguous(self) -> None:
        resolved, missing, ambiguous = resolve_audio_sample_paths_partial(
            ["voice", "missing", "duplicate"],
            [
                "data/audio/voice.wav",
                "data/audio/a/duplicate.wav",
                "data/audio/b/duplicate.mp3",
            ],
        )

        self.assertEqual(resolved, {"voice": "data/audio/voice.wav"})
        self.assertEqual(missing, ("missing",))
        self.assertEqual(ambiguous, ("duplicate",))
        with self.assertRaisesRegex(ValueError, "unresolved audio sample"):
            resolve_audio_sample_paths(["missing"], ["data/audio/sounds/voice.wav"])
        with self.assertRaisesRegex(ValueError, "ambiguous audio sample"):
            resolve_audio_sample_paths(
                ["voice"],
                ["data/audio/a/voice.wav", "data/audio/b/voice.mp3"],
            )
        with self.assertRaisesRegex(ValueError, "relative path traversal"):
            resolve_audio_sample_paths(["voice"], ["../data/audio/voice.wav"])

    def test_rejects_duplicate_ambiguous_and_missing_definitions(self) -> None:
        with self.assertRaisesRegex(ValueError, "duplicate AudioEvent definition"):
            parse_sage_audio_definitions(
                SOURCE + b"AudioEvent SOLDIERSELECT\n Sounds = other\nEnd\n"
            )
        with self.assertRaisesRegex(ValueError, "ambiguous audio definition kind"):
            parse_sage_audio_definitions(
                b"AudioEvent Same\n Sounds = a\nEnd\n"
                b"Multisound same\n Subsounds = Same\nEnd\n"
            )
        definitions = parse_sage_audio_definitions(SOURCE)
        with self.assertRaisesRegex(ValueError, "unresolved audio definition"):
            resolve_sage_audio_closure(definitions, ["Missing"])

    def test_rejects_malformed_references_and_cycles(self) -> None:
        with self.assertRaisesRegex(ValueError, "unsafe AudioEvent.*reference"):
            parse_sage_audio_definitions(
                b"AudioEvent Bad\n Sounds = ../escape\nEnd\n"
            )
        definitions = parse_sage_audio_definitions(
            b"Multisound A\n Subsounds = B\nEnd\n"
            b"Multisound B\n Subsounds = A\nEnd\n"
        )
        with self.assertRaisesRegex(ValueError, "dependency cycle"):
            resolve_sage_audio_closure(definitions, ["A"])

    def test_rejects_encoding_nul_unclosed_and_duplicate_requests(self) -> None:
        with self.assertRaisesRegex(ValueError, "NUL"):
            parse_sage_audio_definitions(SOURCE + b"\0")
        with self.assertRaisesRegex(ValueError, "unterminated AudioEvent"):
            parse_sage_audio_definitions(b"AudioEvent Broken\n Sounds = one\n")
        definitions = parse_sage_audio_definitions(SOURCE)
        with self.assertRaisesRegex(ValueError, "duplicate audio root"):
            resolve_sage_audio_closure(definitions, ["SoldierSelect", "soldierselect"])
        with self.assertRaisesRegex(ValueError, "duplicate audio sample"):
            resolve_audio_sample_paths(["one", "ONE"], [])

    def test_music_tracks_resolve_through_multisounds_to_track_samples(self) -> None:
        definitions = parse_sage_audio_definitions(
            b"MusicTrack BattleGood01\n Filename = bagood01_t05.mp3\n Volume = 58\nEnd\n"
            b"MusicTrack BattleGood02\n Filename = BaGood02_F12.mp3\nEnd\n"
            b"MusicTrack Silence\n Type = FAKE\nEnd\n"
            b"Multisound ShellMusic\n Subsounds = BattleGood01:2000 BattleGood02 Silence\nEnd\n"
        )
        closure = resolve_sage_audio_closure(definitions, ["shellmusic"])
        self.assertEqual(closure.root_ids, ("ShellMusic",))
        self.assertEqual(
            tuple(item.id for item in closure.tracks),
            ("BattleGood01", "BattleGood02", "Silence"),
        )
        self.assertEqual(
            closure.sample_ids, ("bagood01_t05", "BaGood02_F12")
        )
        self.assertEqual(
            closure.tracks[2].neutral()["filename"], None
        )
        self.assertEqual(
            resolve_audio_sample_paths(
                closure.sample_ids,
                [
                    "data/audio/tracks/bagood01_t05.mp3",
                    "data/audio/tracks/bagood02_f12.mp3",
                ],
            ),
            {
                "bagood01_t05": "data/audio/tracks/bagood01_t05.mp3",
                "BaGood02_F12": "data/audio/tracks/bagood02_f12.mp3",
            },
        )
        direct = resolve_sage_audio_closure(definitions, ["BattleGood01"])
        self.assertEqual(direct.root_ids, ("BattleGood01",))
        self.assertEqual(direct.sample_ids, ("bagood01_t05",))

    def test_rejects_ambiguous_unsafe_and_duplicate_music_tracks(self) -> None:
        with self.assertRaisesRegex(ValueError, "ambiguous audio definition kind"):
            parse_sage_audio_definitions(
                b"AudioEvent Same\n Sounds = a\nEnd\n"
                b"MusicTrack same\n Filename = same.mp3\nEnd\n"
            )
        with self.assertRaisesRegex(ValueError, "ambiguous audio definition kind"):
            parse_sage_audio_definitions(
                b"Multisound Same\n Subsounds = Other\nEnd\n"
                b"MusicTrack SAME\n Filename = same.mp3\nEnd\n"
            )
        with self.assertRaisesRegex(ValueError, "duplicate MusicTrack definition"):
            parse_sage_audio_definitions(
                b"MusicTrack A\n Filename = a.mp3\nEnd\n"
                b"MusicTrack a\n Filename = b.mp3\nEnd\n"
            )
        with self.assertRaisesRegex(ValueError, "multiple Filename values"):
            parse_sage_audio_definitions(
                b"MusicTrack A\n Filename = a.mp3\n Filename = b.mp3\nEnd\n"
            )
        for unsafe in (b"../escape.mp3", b"dir/nested.mp3", b"noextension", b"bad.aif"):
            with self.assertRaisesRegex(ValueError, "unsafe MusicTrack"):
                parse_sage_audio_definitions(
                    b"MusicTrack A\n Filename = " + unsafe + b"\nEnd\n"
                )


if __name__ == "__main__":
    unittest.main()
