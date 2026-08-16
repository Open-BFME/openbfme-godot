"""Typed AnimationSoundClientBehavior compile keeps clip and frame tokens."""

from __future__ import annotations

from openbfme_importer.module_contracts import compile_animation_sound_client_behaviors
from openbfme_importer.sage_cst import parse_sage_document


def _lineage(text: str, name: str = "FixtureObject"):
    document = parse_sage_document(
        text.encode("utf-8"),
        virtual_path="data/ini/object/fixture.ini",
    )
    objects = [obj for obj in document.objects if obj.name.casefold() == name.casefold()]
    assert objects, f"missing object {name}"
    return objects


def test_animation_sound_client_behavior_is_executable() -> None:
    rows = compile_animation_sound_client_behaviors(
        _lineage(
            """
Object FixtureObject
  ClientBehavior = AnimationSoundClientBehavior ModuleTag_AnimAudio
    MaxUpdateRangeCap = 800
    AnimationSound = Sound:BodyFallGeneric1 Animation:GUHero_SKL.GUHero_DTHA Frames:8
    AnimationSound = Sound:BodyFallGenericNoArmor Animation:GUHero_SKL.GUHero_DTHB Frames:12
  End
End
"""
        ),
        "FixtureObject",
    )
    assert len(rows) == 1
    assert rows[0]["module"] == "AnimationSoundClientBehavior"
    assert rows[0]["runtimeStatus"] == "executable"
    sounds = rows[0]["fields"]["AnimationSound"]
    assert sounds[0]["eventId"] == "BodyFallGeneric1"
    assert sounds[0]["animation"] == "GUHero_SKL.GUHero_DTHA"
    assert sounds[0]["frames"] == [8]
    assert sounds[1]["eventId"] == "BodyFallGenericNoArmor"
    assert sounds[1]["frames"] == [12]


def test_empty_animation_sound_module_stays_deferred() -> None:
    rows = compile_animation_sound_client_behaviors(
        _lineage(
            """
Object FixtureObject
  ClientBehavior = AnimationSoundClientBehavior ModuleTag_AnimAudio
  End
End
"""
        ),
        "FixtureObject",
    )
    assert rows[0]["runtimeStatus"] == "deferred"
    assert rows[0]["fields"]["AnimationSound"] == []
