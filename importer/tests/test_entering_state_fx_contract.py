"""Typed EnteringStateFX compile: FXList stays executable; FXEvent stays deferred."""

from __future__ import annotations

from openbfme_importer.module_contracts import compile_entering_state_fx
from openbfme_importer.sage_cst import parse_sage_document


def _lineage(text: str, name: str = "FixtureObject"):
    document = parse_sage_document(
        text.encode("utf-8"),
        virtual_path="data/ini/object/fixture.ini",
    )
    objects = [obj for obj in document.objects if obj.name.casefold() == name.casefold()]
    assert objects, f"missing object {name}"
    return objects


def test_entering_state_fx_compiles_and_defers_fxevent() -> None:
    rows = compile_entering_state_fx(
        _lineage(
            """
Object FixtureObject
  Draw = W3DScriptedModelDraw ModuleTag_Draw
    DefaultModelConditionState
      Model = Base
    End
    AnimationState = DAMAGED
      Animation = Damaged
        AnimationName = SKL.DAMA
        AnimationMode = LOOP
      End
      EnteringStateFX = FX_BuildingDamaged
      FXEvent = Frame:12 Name: FX_SplatDust
    End
    AnimationState = MOVING
      Animation = Run
        AnimationName = SKL.RUNA
        AnimationMode = LOOP
      End
      FXEvent = Frame:4 Name: FX_Footstep
    End
  End
End
"""
        ),
        "FixtureObject",
    )
    assert len(rows) == 1
    row = rows[0]
    assert row["module"] == "EnteringStateFX"
    assert row["runtimeStatus"] == "executable"
    assert row["fields"]["stateKind"] == "AnimationState"
    assert row["fields"]["conditions"]["value"] == ["DAMAGED"]
    assert row["fields"]["fxList"]["value"] == "FX_BuildingDamaged"
    deferred = row["fields"]["deferredFields"]
    assert deferred[0]["name"] == "FXEvent"
    assert deferred[0]["reason"] == "compiled-as-fxevent-row"
