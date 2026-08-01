"""Hero summon abilities resolve their egg -> hatch -> horde -> member chain.

Retail hero summons never name the units they create.  The ability's
ObjectCreationList creates a deliberately invisible, one-frame egg
(``data/ini/object/goodfaction/goodfactionsubobjects.ini``
``AragornArmyofTheDeadSmallEgg``: ``Model = None``, ``MaxLifetime = 0.0``)
whose ``SlowDeathBehavior`` fires ``OCL = MIDPOINT
SUPERWEAPON_SpawnAragornOathbreakers`` after ``DestructionDelay = 4000``, and
only THAT list names ``AragornOathbreakerHordeSmall``.  A summon effect that
stops at the egg therefore names an object no playable-unit document describes,
and the power spawns nothing at all.

The fixture below is that exact shape reduced to one egg, one hatch list, one
horde and one member.
"""

from __future__ import annotations

from openbfme_importer.playable_unit_compiler import (
    _summon_leaf_closure,
    prepare_playable_unit_compiler,
)


OBJECTS_INI = b"""
Object FixtureSummonEgg
  Draw = W3DScriptedModelDraw ModuleTag_Draw
    DefaultModelConditionState
      Model = None
    End
  End
  KindOf = INERT IMMOBILE UNATTACKABLE
  Body = ActiveBody ModuleTag_Body
    MaxHealth = 1
  End
  Behavior = LifetimeUpdate ModuleTag_HatchTrigger
    MinLifetime = 0.0
    MaxLifetime = 0.0
  End
  Behavior = SlowDeathBehavior ModuleTag_HatchProcess
    DestructionDelay = 4000
    OCL = MIDPOINT OCL_FixtureHatch
  End
End

Object FixtureGhostHorde
  KindOf = SELECTABLE CAN_ATTACK INFANTRY HORDE SUMMONED
  CommandPoints = 0
  Body = ImmortalBody ModuleTag_Body
    MaxHealth = 1
  End
  Behavior = LifetimeUpdate ModuleTag_Lifetime
    MinLifetime = 60000
    MaxLifetime = 60000
    DeathType = FADED
  End
  Behavior = HordeContain ModuleTag_HordeContain
    InitialPayload = FixtureGhost 4
    Slots = 4
    RankInfo = RankNumber:1 UnitType:FixtureGhost Position:X:0 Y:0
  End
End

Object FixtureGhost
  Draw = W3DScriptedModelDraw ModuleTag_Draw
    DefaultModelConditionState
      Model = FixtureGhost_SKN
    End
  End
  KindOf = INFANTRY SELECTABLE
  Body = ActiveBody ModuleTag_Body
    MaxHealth = 300
  End
  WeaponSet
    Weapon = PRIMARY FixtureGhostSword
  End
  Behavior = LockWeaponCreate ModuleTag_LockWeapon
    SlotToLock = PRIMARY
  End
  LocomotorSet
    Locomotor = FixtureLocomotor
    Speed = 40
  End
End
"""


OCL_INI = b"""
ObjectCreationList OCL_FixtureSummonEgg
  CreateObject
    ObjectNames = FixtureSummonEgg
    Count = 1
    Disposition = LIKE_EXISTING
  End
End

ObjectCreationList OCL_FixtureHatch
  CreateObject
    ObjectNames = FixtureGhostHorde
    Count = 1
    FadeIn = Yes
  End
End
"""


WEAPON_INI = b"""
Weapon FixtureGhostSword
  AttackRange = 30
  DelayBetweenShots = 1000
  DamageNugget
    Damage = 50
    Radius = 0
    DamageType = SLASH
    DelayTime = 0
  End
End
"""


LOCOMOTOR_INI = b"""
Locomotor FixtureLocomotor
  Surfaces = GROUND
  Speed = 40
  Acceleration = 100
  Braking = 100
  TurnRate = 720
End
"""


def _documents() -> dict[str, bytes]:
    return {
        "data/ini/object/units/fixture.ini": OBJECTS_INI,
        "data/ini/objectcreationlist.ini": OCL_INI,
        "data/ini/weapon.ini": WEAPON_INI,
        "data/ini/locomotor.ini": LOCOMOTOR_INI,
        "data/ini/fxlist.ini": b"\n",
        "data/ini/fxparticlesystem.ini": b"\n",
        "data/ini/attributemodifier.ini": b"\n",
        "data/ini/upgrade.ini": b"\n",
        "data/ini/gamedata.ini": b"\n",
        "data/ini/commandset.ini": b"\n",
        "data/ini/commandbutton.ini": b"\n",
        "data/ini/playertemplate.ini": b"\n",
    }


def _closure(documents: dict[str, bytes]) -> dict[str, object] | None:
    prepared = prepare_playable_unit_compiler(documents)
    return _summon_leaf_closure(
        "OCL_FixtureSummonEgg",
        prepared.objects,
        documents,
        prepared.numeric_defines,
    )


class TestSummonLeafClosure:
    def test_chain_reaches_the_units_the_egg_hatches(self) -> None:
        closure = _closure(_documents())
        assert closure is not None
        assert [row["id"] for row in closure["objectCreationLists"]] == [
            "OCL_FixtureHatch",
            "OCL_FixtureSummonEgg",
        ]
        # Without the hop the effect names only FixtureSummonEgg, which no
        # playable-unit document describes.
        assert [row["id"] for row in closure["objects"]] == [
            "FixtureGhost",
            "FixtureGhostHorde",
            "FixtureSummonEgg",
        ]
        assert [row["id"] for row in closure["weapons"]] == ["FixtureGhostSword"]

    def test_egg_hatch_carries_the_authored_rise_delay(self) -> None:
        closure = _closure(_documents())
        assert closure is not None
        egg = next(
            row for row in closure["objects"] if row["id"] == "FixtureSummonEgg"
        )
        assert egg["hatch"] == {
            "trigger": "MIDPOINT",
            "ocl": "OCL_FixtureHatch",
            "destructionDelayMs": 4000,
        }
        # The egg is authored with no art at all; nothing is invented for it.
        assert "draw" not in egg

    def test_hatched_member_carries_the_stats_and_art_the_runtime_needs(
        self,
    ) -> None:
        closure = _closure(_documents())
        assert closure is not None
        member = next(
            row for row in closure["objects"] if row["id"] == "FixtureGhost"
        )
        assert member["maxHealth"] == 300
        assert member["weaponId"] == "FixtureGhostSword"
        assert member["weaponSlot"] == "PRIMARY"
        assert member["permanentWeaponLocks"][0]["slot"] == "PRIMARY"
        assert member["permanentWeaponLocks"][0]["state"] == "LOCKED_PERMANENTLY"
        assert "LockWeaponCreate" not in member.get("unconvertedBehaviors", [])
        assert member["locomotor"]["speed"] == 40
        assert member["draw"] == [
            {
                "drawModule": "W3DScriptedModelDraw",
                "conditions": [],
                "models": ["FixtureGhost_SKN"],
            }
        ]

    def test_incomplete_document_view_leaves_the_effect_unchanged(self) -> None:
        documents = _documents()
        del documents["data/ini/fxparticlesystem.ini"]
        assert _closure(documents) is None

    def test_unresolvable_chain_is_never_partially_invented(self) -> None:
        documents = _documents()
        documents["data/ini/objectcreationlist.ini"] = OCL_INI.replace(
            b"ObjectNames = FixtureGhostHorde", b"ObjectNames = NoSuchObject"
        )
        assert _closure(documents) is None
