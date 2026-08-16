"""Typed FXEvent compile: Frame + Name stay executable; FireWhenSkipped is retained."""

from __future__ import annotations

from openbfme_importer.module_contracts import (
    compile_fx_events,
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
    assert splat["line"] == splat["fields"]["frame"]["line"]
    assert step["line"] == step["fields"]["frame"]["line"]
    validate_module_contracts(rows, label="playable-unit")


def test_fx_event_parses_obsolete_bare_frame_name_and_keeps_collision_lines() -> None:
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
      FXEvent = 1 FX_OrcFletcherDust
      FXEvent = 14 FX_OrcFletcherStep
      FXEvent = Frame:9 ExtraToken Name:FX_StillUnparsed
    End
  End
End
"""
        ),
        "FixtureObject",
    )
    assert len(rows) == 3
    executable = [row for row in rows if row["runtimeStatus"] == "executable"]
    deferred = [row for row in rows if row["runtimeStatus"] == "deferred"]
    assert len(executable) == 2
    assert len(deferred) == 1
    dust = next(row for row in executable if row["fields"]["fxList"]["value"] == "FX_OrcFletcherDust")
    step = next(row for row in executable if row["fields"]["fxList"]["value"] == "FX_OrcFletcherStep")
    assert dust["fields"]["frame"]["value"] == 1
    assert step["fields"]["frame"]["value"] == 14
    assert dust["fields"]["skippedCuePolicy"] == "ignore"
    assert dust["line"] != step["line"]
    assert dust["line"] == dust["fields"]["frame"]["line"]
    assert step["line"] == step["fields"]["frame"]["line"]
    assert deferred[0]["fields"]["deferredFields"][0]["reason"] == "unparsed-fxevent-line"
    assert "FX_StillUnparsed" in str(deferred[0]["fields"]["deferredFields"][0]["authored"])
    validate_module_contracts(executable, label="playable-unit")
