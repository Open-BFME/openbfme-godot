"""Typed FXEvent compile: Frame + Name stay executable; FireWhenSkipped is retained."""

from __future__ import annotations

from openbfme_importer.module_contracts import compile_fx_events
from openbfme_importer.sage_cst import parse_sage_document


def _lineage(text: str, name: str = "FixtureObject"):
    document = parse_sage_document(
        text.encode("utf-8"),
        virtual_path="data/ini/object/fixture.ini",
    )
    objects = [obj for obj in document.objects if obj.name.casefold() == name.casefold()]
    assert objects, f"missing object {name}"
    return objects


def test_fx_event_compiles_frame_name_and_fire_when_skipped() -> None:
    rows = compile_fx_events(
        _lineage(
            """
Object FixtureObject
  Draw = W3DScriptedModelDraw ModuleTag_Draw
    AnimationState = MOVING
      Animation = Run
        AnimationName = SKL.RUNA
        AnimationMode = LOOP
      End
      Flags = RANDOMSTART
      FXEvent = Frame:12 Name: FX_SplatDust
      FXEvent = Frame:5 FireWhenSkipped Name:FX_TrollRightFootStep
    End
  End
End
"""
        ),
        "FixtureObject",
    )
    assert len(rows) == 2
    assert all(row["module"] == "FXEvent" for row in rows)
    assert all(row["runtimeStatus"] == "executable" for row in rows)
    splat = next(row for row in rows if row["fields"]["fxList"]["value"] == "FX_SplatDust")
    step = next(row for row in rows if row["fields"]["fxList"]["value"] == "FX_TrollRightFootStep")
    assert splat["fields"]["frame"]["value"] == 12
    assert splat["fields"]["skippedCuePolicy"] == "ignore"
    assert splat["fields"]["FireWhenSkipped"]["value"] is False
    assert step["fields"]["frame"]["value"] == 5
    assert step["fields"]["FireWhenSkipped"]["value"] is True
    assert step["fields"]["skippedCuePolicy"] == "fire-when-skipped"
    assert splat["line"] != step["line"]
