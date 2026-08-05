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
    compile_playable_unit_descriptor,
    prepare_playable_unit_compiler,
    validate_playable_unit_descriptor,
)
from openbfme_importer.spellbook_import import spellbook_source_documents
from tests.test_playable_unit_compiler import _hero_ability_documents


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
    OCL = INITIAL OCL_FixtureCosmetic
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
    def test_multiple_auras_and_activation_fields_are_preserved(self) -> None:
        documents = _documents()
        documents["data/ini/object/units/fixture.ini"] = OBJECTS_INI.replace(
            b"  LocomotorSet\n    Locomotor = FixtureLocomotor\n    Speed = 40\n  End\nEnd\n",
            b"  LocomotorSet\n    Locomotor = FixtureLocomotor\n    Speed = 40\n  End\n"
            b"  Behavior = AttributeModifierAuraUpdate ModuleTag_First\n"
            b"    StartsActive = Yes\n    BonusName = FixtureAuraOne\n"
            b"    RefreshDelay = 1000\n    Range = 100\n    ObjectFilter = ANY +INFANTRY\n"
            b"  End\n"
            b"  Behavior = AttributeModifierAuraUpdate ModuleTag_Second\n"
            b"    StartsActive = No\n    TriggeredBy = Upgrade_ObjectLevel10\n"
            b"    BonusName = FixtureAuraTwo\n    RefreshDelay = 2000\n"
            b"    Range = 200\n    ObjectFilter = ANY +INFANTRY\n    TargetEnemy = Yes\n"
            b"  End\nEnd\n",
        )
        documents["data/ini/attributemodifier.ini"] = b"""
ModifierList FixtureAuraOne
  Category = LEADERSHIP
  Modifier = ARMOR 20%
  Duration = 3000
End
ModifierList FixtureAuraTwo
  Category = DEBUFF
  Modifier = DAMAGE_MULT 80%
  Duration = 3000
End

ObjectCreationList OCL_FixtureCosmetic
  CreateObject
    ObjectNames = FixtureSummonEgg
    Count = 1
  End
End
"""
        closure = _closure(documents)
        assert closure is not None
        member = next(row for row in closure["objects"] if row["id"] == "FixtureGhost")
        assert "aura" not in member
        assert [row["modifier"] for row in member["auras"]] == [
            "FixtureAuraOne",
            "FixtureAuraTwo",
        ]
        assert member["auras"][1]["startsActive"] == "No"
        assert member["auras"][1]["triggeredBy"] == ["Upgrade_ObjectLevel10"]

    def test_disabled_aura_without_upgrade_edge_is_fail_closed(self) -> None:
        documents = _documents()
        documents["data/ini/object/units/fixture.ini"] = OBJECTS_INI.replace(
            b"  LocomotorSet\n    Locomotor = FixtureLocomotor\n    Speed = 40\n  End\nEnd\n",
            b"  LocomotorSet\n    Locomotor = FixtureLocomotor\n    Speed = 40\n  End\n"
            b"  Behavior = AttributeModifierAuraUpdate ModuleTag_Disabled\n"
            b"    StartsActive = No\n    BonusName = FixtureAura\n"
            b"    RefreshDelay = 1000\n    Range = 100\n    ObjectFilter = ANY\n"
            b"  End\nEnd\n",
        )
        documents["data/ini/attributemodifier.ini"] = b"""
ModifierList FixtureAura
  Category = LEADERSHIP
  Modifier = ARMOR 20%
  Duration = 3000
End
"""
        closure = _closure(documents)
        assert closure is not None
        member = next(row for row in closure["objects"] if row["id"] == "FixtureGhost")
        assert "AttributeModifierAuraUpdate" in member["unconvertedBehaviors"]
        assert "auras" not in member

    def test_real_loader_collects_define_from_top_level_inc(self, tmp_path) -> None:
        for relative, payload in _documents().items():
            path = tmp_path / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(payload)
        for relative in (
            "data/ini/armor.ini",
            "data/ini/experiencelevels.ini",
            "data/ini/science.ini",
            "data/ini/specialpower.ini",
            "data/ini/createaherospecialpowers.ini",
            "data/ini/emotions.ini",
        ):
            path = tmp_path / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"\n")
        inc = tmp_path / "data/ini/createaherogamedata.inc"
        inc.write_bytes(b"#define CREATE_A_HERO_REINFORCEMENT_LIFETIME 90000\n")
        object_path = tmp_path / "data/ini/object/units/fixture.ini"
        object_path.write_bytes(
            OBJECTS_INI.replace(
                b"MinLifetime = 60000\n    MaxLifetime = 60000",
                b"MinLifetime = CREATE_A_HERO_REINFORCEMENT_LIFETIME\n"
                b"    MaxLifetime = CREATE_A_HERO_REINFORCEMENT_LIFETIME",
            )
        )
        loaded = spellbook_source_documents(tmp_path)
        assert "data/ini/createaherogamedata.inc" in loaded
        closure = _closure(loaded)
        assert closure is not None
        horde = next(
            row for row in closure["objects"] if row["id"] == "FixtureGhostHorde"
        )
        assert horde["lifetime"]["minMs"] == 90000
        assert horde["lifetime"]["maxMs"] == 90000

    def test_lifetime_macro_resolves_from_inc_document(self) -> None:
        documents = _documents()
        documents["data/ini/createaherogamedata.inc"] = (
            b"#define CREATE_A_HERO_REINFORCEMENT_LIFETIME 90000\n"
        )
        documents["data/ini/object/units/fixture.ini"] = OBJECTS_INI.replace(
            b"MinLifetime = 60000\n    MaxLifetime = 60000",
            b"MinLifetime = CREATE_A_HERO_REINFORCEMENT_LIFETIME\n"
            b"    MaxLifetime = CREATE_A_HERO_REINFORCEMENT_LIFETIME",
        )
        closure = _closure(documents)
        assert closure is not None
        horde = next(
            row for row in closure["objects"] if row["id"] == "FixtureGhostHorde"
        )
        assert horde["lifetime"] == {
            "minMs": 90000,
            "maxMs": 90000,
            "deathType": "FADED",
        }
        assert "LifetimeUpdate" not in horde.get("unconvertedBehaviors", [])

    def test_unresolved_lifetime_macro_is_recorded_fail_closed(self) -> None:
        documents = _documents()
        documents["data/ini/object/units/fixture.ini"] = OBJECTS_INI.replace(
            b"MinLifetime = 60000\n    MaxLifetime = 60000",
            b"MinLifetime = NO_SUCH_LIFETIME_DEFINE\n"
            b"    MaxLifetime = NO_SUCH_LIFETIME_DEFINE",
        )
        closure = _closure(documents)
        assert closure is not None
        horde = next(
            row for row in closure["objects"] if row["id"] == "FixtureGhostHorde"
        )
        assert "lifetime" not in horde
        assert "LifetimeUpdate" in horde["unconvertedBehaviors"]

    def test_mixed_unresolved_min_lifetime_is_atomic_gap(self) -> None:
        documents = _documents()
        documents["data/ini/object/units/fixture.ini"] = OBJECTS_INI.replace(
            b"MinLifetime = 60000", b"MinLifetime = NO_SUCH_LIFETIME_DEFINE"
        )
        closure = _closure(documents)
        assert closure is not None
        horde = next(
            row for row in closure["objects"] if row["id"] == "FixtureGhostHorde"
        )
        assert "lifetime" not in horde
        assert "LifetimeUpdate" in horde["unconvertedBehaviors"]

    def test_mixed_unresolved_max_lifetime_is_atomic_gap(self) -> None:
        documents = _documents()
        documents["data/ini/object/units/fixture.ini"] = OBJECTS_INI.replace(
            b"MaxLifetime = 60000", b"MaxLifetime = NO_SUCH_LIFETIME_DEFINE"
        )
        closure = _closure(documents)
        assert closure is not None
        horde = next(
            row for row in closure["objects"] if row["id"] == "FixtureGhostHorde"
        )
        assert "lifetime" not in horde
        assert "LifetimeUpdate" in horde["unconvertedBehaviors"]

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


def _weapon_ocl_ability_documents(*, mixed_payload: bool) -> dict[str, bytes]:
    """A hero whose special ability fires an OCL-only SpecialWeapon.

    This is Aragorn's Army of the Dead in RotWK: aragorn.ini:854-866 authors
    ``WeaponFireSpecialAbilityUpdate`` with ``SpecialWeapon =
    AragornPersonalOathbreakers``, and weapon.ini:7806-7810 gives that weapon
    exactly one ``WeaponOCLNugget`` naming ``OCL_GondorArmyofTheDeadEggSmall``
    -- no DamageNugget and no warhead anywhere in the chain.

    ``mixed_payload`` adds a DamageNugget beside the OCL nugget: a weapon that
    both damages and spawns is NOT a summon and must stay a recorded gap.
    """

    documents = _hero_ability_documents()
    units_path = "data/ini/object/units/test_units.ini"
    documents[units_path] = (
        documents[units_path].replace(
            b"SpecialWeapon = MissingWeapon",
            b"SpecialWeapon = FixtureOathbreakerSummon",
        )
        + OBJECTS_INI
    )
    summon_weapon = (
        b"\nWeapon FixtureOathbreakerSummon\n"
        b"  WeaponOCLNugget\n"
        b"    WeaponOCLName = OCL_FixtureSummonEgg\n"
        b"  End\n"
    )
    if mixed_payload:
        summon_weapon += (
            b"  DamageNugget\n"
            b"    Damage = 100\n"
            b"    Radius = 10\n"
            b"    DamageType = MAGIC\n"
            b"  End\n"
        )
    summon_weapon += b"End\n"
    documents["data/ini/weapon.ini"] += WEAPON_INI + summon_weapon
    documents["data/ini/objectcreationlist.ini"] += OCL_INI
    documents["data/ini/locomotor.ini"] = LOCOMOTOR_INI
    # The shared leaf resolver refuses to run unless the whole effective INI
    # view it needs is present (import-unit lane stays byte-identical).
    for path in ("data/ini/fxlist.ini", "data/ini/fxparticlesystem.ini", "data/ini/upgrade.ini"):
        documents.setdefault(path, b"\n")
    return documents


def test_ocl_only_special_weapon_compiles_to_a_summon() -> None:
    documents = _weapon_ocl_ability_documents(mixed_payload=False)

    descriptor = compile_playable_unit_descriptor("AbilityHero", documents)

    validate_playable_unit_descriptor(descriptor)
    ability = next(
        row
        for row in descriptor["abilities"]  # type: ignore[index]
        if row["id"] == "Command_FixtureBroken"
    )
    assert ability["implementation"]["status"] == "implemented"
    effect = ability["effect"]
    assert effect["kind"] == "summon"
    assert effect["oclId"] == "OCL_FixtureSummonEgg"
    assert effect["weaponId"] == "FixtureOathbreakerSummon"
    assert [row["id"] for row in effect["objects"]] == ["FixtureSummonEgg"]
    # The egg names nothing playable on its own; the closure must reach the
    # hatched horde and its members.
    assert [row["id"] for row in effect["leaves"]["objects"]] == [
        "FixtureGhost",
        "FixtureGhostHorde",
        "FixtureSummonEgg",
    ]


def test_weapon_that_both_damages_and_spawns_is_never_a_summon() -> None:
    # A payload that also damages is not the Army-of-the-Dead shape.  The
    # OCL-only rule is deliberately narrow: this weapon keeps the authored
    # damage lane exactly as it compiled before the rule existed.
    documents = _weapon_ocl_ability_documents(mixed_payload=True)

    descriptor = compile_playable_unit_descriptor("AbilityHero", documents)

    validate_playable_unit_descriptor(descriptor)
    ability = next(
        row
        for row in descriptor["abilities"]  # type: ignore[index]
        if row["id"] == "Command_FixtureBroken"
    )
    assert ability["effect"]["kind"] == "weapon-blast"
    assert ability["effect"]["damage"] == 100


def test_ocl_only_weapon_naming_an_unresolvable_list_stays_unimplemented() -> None:
    documents = _weapon_ocl_ability_documents(mixed_payload=False)
    documents["data/ini/weapon.ini"] = documents["data/ini/weapon.ini"].replace(
        b"WeaponOCLName = OCL_FixtureSummonEgg",
        b"WeaponOCLName = OCL_NoSuchSummon",
    )

    descriptor = compile_playable_unit_descriptor("AbilityHero", documents)

    validate_playable_unit_descriptor(descriptor)
    ability = next(
        row
        for row in descriptor["abilities"]  # type: ignore[index]
        if row["id"] == "Command_FixtureBroken"
    )
    assert ability["effect"] == {"kind": "none"}
    assert ability["implementation"]["status"] == "unimplemented"
