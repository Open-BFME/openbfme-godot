"""Typed TransitionDamageFX compile keeps FXList / PSys; OCL stays deferred."""

from __future__ import annotations

from openbfme_importer.module_contracts import compile_transition_damage_fx
from openbfme_importer.sage_cst import parse_sage_document


def _lineage(text: str, name: str = "FixtureObject"):
    document = parse_sage_document(
        text.encode("utf-8"),
        virtual_path="data/ini/object/fixture.ini",
    )
    objects = [obj for obj in document.objects if obj.name.casefold() == name.casefold()]
    assert objects, f"missing object {name}"
    return objects


def test_transition_damage_fx_lists_are_executable() -> None:
    rows = compile_transition_damage_fx(
        _lineage(
            """
Object FixtureObject
  Behavior = TransitionDamageFX ModuleTag_FX
    DamagedFXList1 = Loc: X:0 Y:0 Z:0 FXList:FX_BasicSevereScreenShake
    ReallyDamagedFXList1 = Loc: X:0 Y:0 Z:0 FXList:FX_GondorTrebuchetTransitionMedium
    ReallyDamagedParticleSystem1 = Bone:None RandomBone:No PSys:FireBuildingLarge
    RubbleFXList1 = Loc: X:0 Y:0 Z:0 FXList:FX_HelmsDeepGateRubble
  End
End
"""
        ),
        "FixtureObject",
    )
    assert len(rows) == 1
    assert rows[0]["module"] == "TransitionDamageFX"
    assert rows[0]["runtimeStatus"] == "executable"
    kinds = {(row["stage"], row["kind"], row.get("fxList") or row.get("particleSystem")) for row in rows[0]["fields"]["effects"]}
    assert ("Damaged", "FXList", "FX_BasicSevereScreenShake") in kinds
    assert ("ReallyDamaged", "ParticleSystem", "FireBuildingLarge") in kinds


def test_transition_damage_fx_ocl_stays_deferred() -> None:
    rows = compile_transition_damage_fx(
        _lineage(
            """
Object FixtureObject
  Behavior = TransitionDamageFX ModuleTag_FX
    ReallyDamagedOCL1 = Loc: X:0 Y:0 Z:0 OCL:OCL_FireFieldSmall
  End
End
"""
        ),
        "FixtureObject",
    )
    assert rows[0]["runtimeStatus"] == "deferred"
    assert rows[0]["fields"]["deferredFields"][0]["name"] == "ReallyDamagedOCL1"
