"""Typed TransitionDamageFX compile keeps FXList / PSys; OCL stays deferred.

The eight castle gate-door blocks also author per-stage sub-object visibility
(``PristineShowSubObject`` and friends) and two Minas Tirith wall blocks author
``RubbleNeighbor``.  Both are authored retail evidence with no runtime consumer,
so they defer with a named reason instead of failing the whole object out of the
map-object corpus.
"""

from __future__ import annotations

import pytest

from openbfme_importer.module_contracts import (
    ModuleContractError,
    compile_transition_damage_fx,
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


def test_gate_door_sub_object_visibility_defers_with_its_authored_tokens() -> None:
    # Verbatim ModuleTag_hideBustedDoors from the Helm's Deep gate
    # (helmsdeepbuildings.ini:6262); the same block shape sits on the Minas
    # Tirith, Erebor, Orthanc and Rohan castle doors.
    rows = compile_transition_damage_fx(
        _lineage(
            """
Object FixtureObject
  Behavior = TransitionDamageFX ModleTag_hideBustedDoors
    PristineShowSubObject		HDG_GATER HDG_GATEL
    PristineHideSubObject		HDG_GATER_D1 HDG_GATER_D2 HDG_GATEL_D1 HDG_GATEL_D2
    DamagedShowSubObject		HDG_GATER_D1 HDG_GATEL_D1
    DamagedHideSubObject		HDG_GATER_D2 HDG_GATEL_D2 HDG_GATER HDG_GATEL
    ReallyDamagedShowSubObject	HDG_GATER_D2 HDG_GATEL_D2
    ReallyDamagedHideSubObject  HDG_GATER_D1  HDG_GATEL_D1 HDG_GATER HDG_GATEL
    DamagedFXList1 = Loc: X:0 Y:0 Z:0 FXList:FX_BasicSevereScreenShake
    RubbleFXList1 = Loc: X:0 Y:0 Z:0 FXList:FX_HelmsDeepGateRubble
  End
End
"""
        ),
        "FixtureObject",
    )
    assert len(rows) == 1
    fields = rows[0]["fields"]
    # The authored screen shake still runs; only the model visibility defers.
    assert rows[0]["runtimeStatus"] == "executable"
    assert len(fields["effects"]) == 2
    deferred = {item["name"]: item for item in fields["deferredFields"]}
    assert len(deferred) == 6
    assert {item["reason"] for item in deferred.values()} == {
        "sub-object-visibility-without-drawable-oracle"
    }
    assert deferred["PristineShowSubObject"]["subObjects"] == ["HDG_GATER", "HDG_GATEL"]
    assert deferred["ReallyDamagedHideSubObject"]["subObjects"] == [
        "HDG_GATER_D1", "HDG_GATEL_D1", "HDG_GATER", "HDG_GATEL",
    ]


def test_rubble_neighbor_defers_and_keeps_both_authored_rows() -> None:
    # ministirithbuildings.ini:369 authors RubbleNeighbor twice in one block.
    rows = compile_transition_damage_fx(
        _lineage(
            """
Object FixtureObject
  Behavior                = TransitionDamageFX ModuleTag_TransDamageFX
    RubbleNeighbor NeighborOffset: X:0 Y:-200 Z:0 SubObject:BookendLeft SubObject:BookendLeftA OCL:OCL_MinWallA_BOOKEND_Right
    RubbleNeighbor NeighborOffset: X:0 Y: 200 Z:0 SubObject:BookendRight SubObject:BookendRightA OCL:OCL_MinWallA_BOOKEND_Left
  End
End
"""
        ),
        "FixtureObject",
    )
    assert rows[0]["runtimeStatus"] == "deferred"
    deferred = rows[0]["fields"]["deferredFields"]
    assert len(deferred) == 2
    assert {item["reason"] for item in deferred} == {
        "rubble-neighbor-spawn-without-runtime-oracle"
    }
    assert "OCL:OCL_MinWallA_BOOKEND_Right" in deferred[0]["authored"]


def test_unknown_transition_field_still_fails_closed() -> None:
    with pytest.raises(ModuleContractError):
        compile_transition_damage_fx(
            _lineage(
                """
Object FixtureObject
  Behavior = TransitionDamageFX ModuleTag_FX
    PristineFXList1 = Loc: X:0 Y:0 Z:0 FXList:FX_ThatRetailNeverAuthored
  End
End
"""
            ),
            "FixtureObject",
        )
