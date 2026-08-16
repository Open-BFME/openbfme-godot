"""Typed AnimationState compile: AnimationName stays executable; ParticleSysBone stays deferred."""

from __future__ import annotations

from openbfme_importer.module_contracts import compile_animation_states
from openbfme_importer.sage_cst import parse_sage_document


def _lineage(text: str, name: str = "FixtureObject"):
    document = parse_sage_document(
        text.encode("utf-8"),
        virtual_path="data/ini/object/fixture.ini",
    )
    objects = [obj for obj in document.objects if obj.name.casefold() == name.casefold()]
    assert objects, f"missing object {name}"
    return objects


def test_animation_state_nested_clip_is_executable() -> None:
    rows = compile_animation_states(
        _lineage(
            """
Object FixtureObject
  Draw = W3DScriptedModelDraw ModuleTag_Draw
    IdleAnimationState
      StateName = STATE_Idle
      Flags = RANDOMSTART
      Animation = Idle
        AnimationName = SKL.IDLE
        AnimationMode = LOOP
        AnimationBlendTime = 15
        AnimationPriority = 0
        AnimationSpeedFactorRange = 0.9 1.1
      End
    End
    AnimationState = MOVING USER_1
      Animation = RunUp
        AnimationName = SKL.RUN_UPGRADE
        AnimationMode = LOOP
        AnimationBlendTime = 8
      End
      ParticleSysBone = BONE FX_Dust
    End
  End
End
"""
        ),
        "FixtureObject",
    )
    assert len(rows) == 2
    idle = next(row for row in rows if row["fields"]["stateKind"] == "IdleAnimationState")
    moving = next(row for row in rows if row["fields"]["conditions"]["value"] == ["MOVING", "USER_1"])
    assert idle["module"] == "AnimationState"
    assert idle["runtimeStatus"] == "executable"
    assert idle["fields"]["animations"][0]["animationName"] == "SKL.IDLE"
    assert idle["fields"]["animations"][0]["mode"] == "LOOP"
    assert idle["fields"]["animations"][0]["blendTime"] == 15
    assert idle["fields"]["Flags"]["value"] == ["RANDOMSTART"]
    assert moving["runtimeStatus"] == "executable"
    assert moving["fields"]["animations"][0]["animationName"] == "SKL.RUN_UPGRADE"
    assert moving["fields"]["deferredFields"][0]["name"] == "ParticleSysBone"


def test_animation_state_without_clip_stays_deferred() -> None:
    rows = compile_animation_states(
        _lineage(
            """
Object FixtureObject
  Draw = W3DScriptedModelDraw ModuleTag_Draw
    AnimationState = MOVING
      ParticleSysBone = BONE FX_Dust
    End
  End
End
"""
        ),
        "FixtureObject",
    )
    assert rows[0]["runtimeStatus"] == "deferred"
    assert rows[0]["fields"]["deferredFields"][0]["name"] == "ParticleSysBone"
