"""Typed ParticleSysBone compile: bone + FXParticleSystem stay executable."""

from __future__ import annotations

from openbfme_importer.module_contracts import (
    compile_particle_sys_bones,
    validate_module_contracts,
)
from openbfme_importer.sage_cst import parse_sage_document


def _lineage(text: str, name: str = "FixtureObject"):
    document = parse_sage_document(
        text.encode("utf-8"),
        virtual_path="data/ini/object/fixture.ini",
    )
    objects = [obj for obj in document.objects if obj.name.casefold() == name.casefold()]
    assert objects, f"missing object {name}"
    return objects


def test_particle_sys_bone_animation_and_model_states_compile() -> None:
    rows = compile_particle_sys_bones(
        _lineage(
            """
Object FixtureObject
  Draw = W3DScriptedModelDraw ModuleTag_Draw
    DefaultModelConditionState
      Model = Base
    End
    ModelConditionState = DAMAGED
      ParticleSysBone = FIREBONE FireBuildingSmall
    End
    AnimationState = MOVING
      Animation = Run
        AnimationName = SKL.RUNA
        AnimationMode = LOOP
      End
      ParticleSysBone = BONE FX_Dust FollowBone:Yes
    End
  End
End
"""
        ),
        "FixtureObject",
    )
    assert len(rows) == 2
    assert all(row["module"] == "ParticleSysBone" for row in rows)
    assert all(row["runtimeStatus"] == "executable" for row in rows)
    damaged = next(row for row in rows if row["fields"]["stateKind"] == "ModelConditionState")
    moving = next(row for row in rows if row["fields"]["stateKind"] == "AnimationState")
    assert damaged["fields"]["conditions"]["value"] == ["DAMAGED"]
    assert damaged["fields"]["bone"]["value"] == "FIREBONE"
    assert damaged["fields"]["particleSystem"]["value"] == "FireBuildingSmall"
    assert moving["fields"]["conditions"]["value"] == ["MOVING"]
    assert moving["fields"]["FollowBone"]["value"] is True


def test_particle_sys_bone_multiple_assignments_in_one_state_keep_distinct_lines() -> None:
    """transportship.ini authors several ParticleSysBone rows per ModelConditionState."""

    rows = compile_particle_sys_bones(
        _lineage(
            """
Object FixtureObject
  Draw = W3DScriptedModelDraw ModuleTag_Draw
    ModelConditionState = DAMAGED
      ParticleSysBone = FireFront FireBoatSmall FollowBone:Yes
      ParticleSysBone = FireBeam01 FireBoatBeam FollowBone:Yes
      ParticleSysBone = FireLeft FireBoatSmall FollowBone:Yes
    End
  End
End
"""
        ),
        "FixtureObject",
    )
    assert len(rows) == 3
    assert [row["fields"]["bone"]["value"] for row in rows] == [
        "FireFront",
        "FireBeam01",
        "FireLeft",
    ]
    lines = [row["line"] for row in rows]
    assert len(set(lines)) == 3
    assert all(row["line"] == row["fields"]["bone"]["line"] for row in rows)
    validate_module_contracts(rows, label="playable-unit")


def test_particle_sys_bone_follow_bone_strips_authored_space() -> None:
    """treeberd.ini authors FollowBone: no with a space after the colon."""

    rows = compile_particle_sys_bones(
        _lineage(
            """
Object FixtureObject
  Draw = W3DScriptedModelDraw ModuleTag_Draw
    DefaultModelConditionState
      ParticleSysBone = BAT_SPINE1 TreeBeardIdleLeaves FollowBone: no
    End
  End
End
"""
        ),
        "FixtureObject",
    )
    assert len(rows) == 1
    assert rows[0]["runtimeStatus"] == "executable"
    assert rows[0]["fields"]["FollowBone"]["value"] is False
    validate_module_contracts(rows, label="playable-unit")
