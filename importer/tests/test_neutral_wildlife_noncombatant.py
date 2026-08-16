"""Retail's map-live wildlife must stay noncombatant-closed.

Cow / Sheep / Goat / Wolf / Crow / Dove are the passive Squish wildlife slice:
no weapon, no damage route, `NO_COLLIDE`/`INERT` KindOf, and the fieldless
`SquishCollide` marker.  The compiler proves that shape by requiring every
authored module contract to be one it knows cannot create a damage route.

An animation contract is not a damage route.  When the animation lane began
compiling `AnimationState` rows these six objects silently stopped classifying
as noncombatant, their simulation went back to `unresolved` with the whole
`combat.*` list missing, and the neutral-mob catalog deferred them.  These
tests are the fast oracle for that boundary; the retail one is
`test_retail_map_placed_passive_units_have_closed_scenario_simulation`.
"""

from __future__ import annotations

import pytest

from openbfme_importer.neutral_mob_catalog import (
    compile_neutral_mob_catalog,
    neutral_unit_passive_runtime_ready,
    validate_neutral_mob_catalog,
)


_WILDLIFE_INI = """
Object FixtureCow
  Draw = W3DScriptedModelDraw ModuleTag_01
    DefaultModelConditionState
      Model = CUCow_SKN
    End
    AnimationState = MOVING PANICKING
      Animation = RUNA
        AnimationName = CUCow_SKL.CUCow_RUNA
        AnimationMode = LOOP
      End
      Flags = RANDOMSTART
    End
  End
  EditorSorting = MISC_NATURAL
  ArmorSet
    Conditions = None
    Armor = NoArmor
    DamageFX = None
  End
  VisionRange = 121
  DisplayName = OBJECT:Prop
  CrushableLevel = 0
  KindOf = PRELOAD NO_COLLIDE IGNORED_IN_GUI INFANTRY PATH_THROUGH_EACH_OTHER NO_BASE_CAPTURE
  Body = ActiveBody ModuleTag_02
    MaxHealth = 50.0
  End
  Behavior = AnimalAIUpdate ModuleTagWanderAround
    FleeRange = 50
    WanderPercentage = 5
    MaxWanderDistance = 10
    MaxWanderRadius = 40
  End
  LocomotorSet
    Locomotor = HumanLocomotor
    Condition = SET_NORMAL
    Speed = 9
  End
  Behavior = PhysicsBehavior ModuleTag_04
  End
  Behavior = SlowDeathBehavior ModuleTag_05
    DeathTypes = ALL -FADED
    SinkDelay = 3000
    SinkRate = 0.40
    DestructionDelay = 8000
  End
  Behavior = SquishCollide ModuleTag_06
  End
  Geometry = CYLINDER
  GeometryMajorRadius = 0.8
  GeometryHeight = 0.8
  GeometryIsSmall = Yes
End
"""


def _documents(*, with_animation_state: bool = True) -> dict[str, bytes]:
    body = _WILDLIFE_INI
    if not with_animation_state:
        # The same object as retail authors it minus the animation graph, which
        # is what the compiler used to see before AnimationState was typed.
        body = body.replace(
            """    AnimationState = MOVING PANICKING
      Animation = RUNA
        AnimationName = CUCow_SKL.CUCow_RUNA
        AnimationMode = LOOP
      End
      Flags = RANDOMSTART
    End
""",
            "",
        )
    return {
        "data/ini/object/nature/naturefixtures.ini": body.encode("utf-8"),
        "data/ini/commandset.ini": b"",
        "data/ini/commandbutton.ini": b"",
        "data/ini/locomotor.ini": b"""
Locomotor HumanLocomotor
  Surfaces = GROUND
  Speed = 9
  Acceleration = 100
  Braking = 100
  MinTurnSpeed = 0
  TurnRate = 360
End
""",
        "data/ini/armor.ini": b"""
Armor NoArmor
  Armor = DEFAULT 100%
End
""",
    }


def _wildlife_row(*, with_animation_state: bool) -> dict[str, object]:
    catalog = compile_neutral_mob_catalog(
        _documents(with_animation_state=with_animation_state)
    )
    validate_neutral_mob_catalog(catalog)
    rows = {str(row["objectId"]): row for row in catalog["neutralMobs"]}
    assert set(rows) == {"FixtureCow"}
    return rows["FixtureCow"]


@pytest.mark.parametrize("with_animation_state", [True, False])
def test_passive_wildlife_is_noncombatant_ready(with_animation_state: bool) -> None:
    row = _wildlife_row(with_animation_state=with_animation_state)
    assert row["runtimeStatus"] == "descriptor-ready"
    assert "deferredReason" not in row
    descriptor = row["descriptor"]
    simulation = descriptor["gameplay"]["simulation"]
    assert simulation["missing"] == []
    assert simulation["status"] == "ready"
    resolved = simulation["resolved"]
    assert resolved["combat"] == {
        "disposition": "noncombatant",
        "evidence": "no-effective-weapon-or-damage-route",
        "kindOfEvidence": ["NO_COLLIDE"],
    }
    assert neutral_unit_passive_runtime_ready(descriptor) is True


def test_animation_contract_is_the_only_difference() -> None:
    # Guards against the fixture proving nothing: the AnimationState variant has
    # to actually reach the compiler as a contract.
    with_animation = _wildlife_row(with_animation_state=True)
    without_animation = _wildlife_row(with_animation_state=False)
    modules_with = {
        str(contract["module"])
        for contract in with_animation["descriptor"]["gameplay"]["simulation"][
            "resolved"
        ]["moduleContracts"]
    }
    modules_without = {
        str(contract["module"])
        for contract in without_animation["descriptor"]["gameplay"]["simulation"][
            "resolved"
        ]["moduleContracts"]
    }
    assert "AnimationState" in modules_with
    assert modules_with - modules_without == {"AnimationState"}


def test_a_weapon_bearing_creep_is_still_refused_noncombatant_status() -> None:
    # The widening must not turn a hostile creep into a noncombatant: the same
    # object with CAN_ATTACK/CREEP KindOf keeps its combat gap.
    documents = _documents()
    documents["data/ini/object/nature/naturefixtures.ini"] = documents[
        "data/ini/object/nature/naturefixtures.ini"
    ].replace(
        b"KindOf = PRELOAD NO_COLLIDE IGNORED_IN_GUI INFANTRY",
        b"KindOf = PRELOAD NO_COLLIDE CREEP CAN_ATTACK IGNORED_IN_GUI INFANTRY",
    )
    catalog = compile_neutral_mob_catalog(documents)
    row = next(
        value for value in catalog["neutralMobs"] if value["objectId"] == "FixtureCow"
    )
    simulation = row["descriptor"]["gameplay"]["simulation"]
    assert simulation["status"] == "unresolved"
    assert "combat.weapon" in simulation["missing"]
    assert "combat" not in simulation["resolved"]
