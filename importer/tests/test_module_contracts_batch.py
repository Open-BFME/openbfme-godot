"""Focused tests for the Class-B / death module contract batch."""

from __future__ import annotations

from collections import Counter
from copy import deepcopy
import hashlib
import json
from pathlib import Path

import pytest
import openbfme_importer.module_contracts as module_contracts_subject

from openbfme_importer.module_contracts import (
    ModuleContractError,
    compile_ai_update_interfaces,
    compile_auto_ability_behaviors,
    compile_all_module_contracts,
    compile_attribute_modifier_aura_updates,
    compile_attribute_modifier_upgrades,
    compile_create_object_die,
    compile_castle_member_behaviors,
    compile_dual_weapon_behaviors,
    compile_emotion_tracker_updates,
    compile_fire_weapon_updates,
    compile_deletion_updates,
    compile_siege_docking_behaviors,
    compile_refund_die_behaviors,
    compile_fire_spread_updates,
    compile_invisibility_updates,
    compile_attach_updates,
    compile_clearance_testing_slow_death_behaviors,
    compile_production_updates,
    compile_squish_collides,
    compile_getting_built_behaviors,
    compile_ai_special_power_updates,
    compile_building_behaviors,
    compile_queue_production_exit_updates,
    compile_rebuild_hole_expose_dies,
    compile_rebuild_hole_behaviors,
    compile_salvage_crate_collides,
    compile_horde_member_collides,
    compile_banner_carrier_updates,
    compile_bezier_projectile_behaviors,
    compile_respawn_bodies,
    compile_notify_crushing_updates,
    compile_foundation_ai_updates,
    compile_monitor_condition_updates,
    compile_give_upgrade_updates,
    compile_gate_open_close_behaviors,
    compile_ai_gate_updates,
    compile_fake_pathfind_portals,
    compile_stealth_detector_updates,
    compile_slaved_updates,
    compile_castle_upgrades,
    compile_delayed_death_bodies,
    compile_dynamic_portal_behaviours,
    compile_flammable_updates,
    compile_spawn_behaviors,
    compile_stealth_updates,
    compile_object_creation_upgrades,
    compile_ocl_updates,
    compile_transport_contains,
    compile_tunnel_contains,
    compile_garrison_contains,
    compile_horde_garrison_contains,
    compile_large_group_bonus_updates,
    compile_production_queue_horde_contains,
    compile_siege_engine_contains,
    compile_large_group_audio_updates,
    compile_hit_reaction_behaviors,
    compile_animal_ai_updates,
    compile_threat_finder_updates,
    compile_model_condition_sound_selectors,
    compile_random_sound_selectors,
    compile_upgrade_sound_selectors,
    compile_radiate_fear_updates,
    compile_poisoned_behaviors,
    compile_damage_field_updates,
    compile_spawn_unit_behaviors,
    compile_replace_self_upgrades,
    compile_citadel_slaughter_horde_contains,
    compile_wall_hub_behaviors,
    compile_activate_module_special_powers,
    compile_weapon_mode_special_power_updates,
    compile_dominate_enemy_special_powers,
    compile_grab_passenger_special_powers,
    compile_fling_passenger_special_ability_updates,
    compile_temporarily_defect_update_default,
    compile_repair_special_powers,
    compile_horde_dispatch_special_powers,
    compile_stop_special_powers,
    compile_siege_deploy_special_powers,
    compile_deflect_special_powers,
    compile_split_horde_special_powers,
    compile_deploy_style_ai_updates,
    compile_toggle_deploy_special_ability_updates,
    compile_special_disguise_updates,
    compile_unleash_special_powers,
    compile_special_enemy_sense_updates,
    compile_scavenger_special_powers,
    compile_fire_weapon_when_dead_behaviors,
    compile_geometry_upgrades,
    compile_horde_transport_contains,
    compile_horde_contains,
    compile_horde_ai_updates,
    compile_inactive_bodies,
    compile_keep_object_die,
    compile_lifetime_updates,
    compile_physics_behaviors,
    compile_pickup_stuff_updates,
    compile_respawn_updates,
    compile_ship_slow_death_behaviors,
    compile_slow_death_behaviors,
    compile_stances_behaviors,
    compile_spawn_point_production_exits,
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


def test_attribute_modifier_upgrade_extracts_trigger_and_modifier() -> None:
    lineage = _lineage(
        """
Object FixtureObject
  Behavior = AttributeModifierUpgrade ModuleTag_Bonus
    TriggeredBy = Upgrade_EasyAISinglePlayer
    AttributeModifier = EasyAISinglePlayer_Bonus
  End
End
"""
    )
    rows = compile_attribute_modifier_upgrades(lineage, "FixtureObject")
    assert len(rows) == 1
    assert rows[0]["module"] == "AttributeModifierUpgrade"
    assert rows[0]["fields"]["TriggeredBy"]["value"] == ["Upgrade_EasyAISinglePlayer"]
    assert rows[0]["fields"]["AttributeModifier"]["value"] == "EasyAISinglePlayer_Bonus"
    assert rows[0]["runtimeStatus"] == "executable"


def test_geometry_upgrade_extracts_show_hide() -> None:
    lineage = _lineage(
        """
Object FixtureObject
  Behavior = GeometryUpgrade Geom_ModuleTag_HideAll
    TriggeredBy = Upgrade_StructureLevel1
    ShowGeometry = Geom_Orig
    HideGeometry = Geom_V2
  End
End
"""
    )
    rows = compile_geometry_upgrades(lineage, "FixtureObject")
    assert rows[0]["fields"]["ShowGeometry"]["value"] == ["Geom_Orig"]
    assert rows[0]["fields"]["HideGeometry"]["value"] == ["Geom_V2"]
    assert rows[0]["runtimeStatus"] == "executable"


def test_geometry_upgrade_mesh_and_custom_animation_shape_stays_deferred() -> None:
    lineage = _lineage(
        """
Object FixtureObject
  Behavior = GeometryUpgrade Geom_ModuleTag_Wall
    TriggeredBy = Upgrade_Wall
    ShowGeometry = Geom_Wall
    WallBoundsMesh = P1
    CustomAnimAndDuration = USER_1 500
  End
End
"""
    )
    row = compile_geometry_upgrades(lineage, "FixtureObject")[0]
    assert row["runtimeStatus"] == "deferred"
    assert [field["name"] for field in row["fields"]["deferredFields"]] == [
        "CustomAnimAndDuration",
        "WallBoundsMesh",
    ]


def test_inactive_body_is_presence_policy() -> None:
    lineage = _lineage(
        """
Object FixtureObject
  Body = InactiveBody ModuleTag_Body
  End
End
"""
    )
    rows = compile_inactive_bodies(lineage, "FixtureObject")
    assert rows[0]["module"] == "InactiveBody"
    assert rows[0]["fields"]["indestructible"] is True


def test_spawn_point_production_exit_requires_bone() -> None:
    lineage = _lineage(
        """
Object FixtureObject
  Behavior = SpawnPointProductionExitUpdate ModuleTag_ProductionExit
    SpawnPointBoneName = ARCHER
  End
End
"""
    )
    rows = compile_spawn_point_production_exits(lineage, "FixtureObject")
    assert rows[0]["fields"]["SpawnPointBoneName"]["value"] == "ARCHER"


def test_keep_object_die_defaults_all_and_does_not_destroy() -> None:
    lineage = _lineage(
        """
Object FixtureObject
  Behavior = KeepObjectDie ModuleTag_IWantRubble
  End
End
"""
    )
    rows = compile_keep_object_die(lineage, "FixtureObject")
    assert rows[0]["fields"]["deathTypes"] == "ALL"
    assert rows[0]["fields"]["destroyOnDeath"] is False
    assert rows[0]["runtimeStatus"] == "executable"
    assert rows[0]["extraction"] == "typed"


def test_create_object_die_requires_creation_list() -> None:
    lineage = _lineage(
        """
Object FixtureObject
  Behavior = CreateObjectDie ModuleTag_DropTheRing
    CreationList = OCL_TheOneRing
    DeathTypes = ALL
  End
End
"""
    )
    rows = compile_create_object_die(lineage, "FixtureObject")
    assert rows[0]["fields"]["CreationList"]["value"] == "OCL_TheOneRing"
    assert rows[0]["fields"]["deathTypes"] == "ALL"
    assert rows[0]["runtimeStatus"] == "executable"
    assert rows[0]["extraction"] == "typed"


def test_physics_behavior_extracts_combat_motion_and_timer_fields() -> None:
    lineage = _lineage(
        """
Object FixtureObject
  Behavior = PhysicsBehavior ModuleTag_Physics
    GravityMult = 1.0
    AllowBouncing = Yes
    OrientToFlightPath = No
    KillWhenRestingOnGround = Yes
    ShockStunnedTimeLow = 1400 //msec
    ShockStunnedTimeHigh = 2400
    ShockStandingTime = 666
    FirstHeight = 0
    SecondHeight = 0.0
  End
End
"""
    )
    rows = compile_physics_behaviors(lineage, "FixtureObject")
    assert len(rows) == 1
    assert rows[0]["module"] == "PhysicsBehavior"
    fields = rows[0]["fields"]
    assert fields["GravityMult"]["value"] == 1.0
    assert fields["AllowBouncing"]["value"] is True
    assert fields["OrientToFlightPath"]["value"] is False
    assert fields["KillWhenRestingOnGround"]["value"] is True
    assert fields["ShockStunnedTimeLow"]["milliseconds"] == 1400
    # The CST strips SAGE inline comments before preserving authored values.
    assert fields["ShockStunnedTimeLow"]["authored"] == "1400"
    assert fields["ShockStunnedTimeHigh"]["milliseconds"] == 2400
    assert fields["ShockStandingTime"]["milliseconds"] == 666
    assert fields["FirstHeight"]["value"] == 0
    assert fields["SecondHeight"]["value"] == 0.0
    assert rows[0]["runtimeStatus"] == "executable"
    assert rows[0]["extraction"] == "typed"


def test_physics_behavior_rejects_unknown_and_invalid_authored_fields() -> None:
    unknown = _lineage(
        """
Object FixtureObject
  Behavior = PhysicsBehavior ModuleTag_Physics
    InventedForce = 3
  End
End
"""
    )
    with pytest.raises(ModuleContractError, match="unsupported fields"):
        compile_physics_behaviors(unknown, "FixtureObject")

    invalid_timer = _lineage(
        """
Object FixtureObject
  Behavior = PhysicsBehavior ModuleTag_Physics
    ShockStandingTime = instantly
  End
End
"""
    )
    with pytest.raises(ModuleContractError, match="ShockStandingTime"):
        compile_physics_behaviors(invalid_timer, "FixtureObject")


def test_fire_weapon_when_dead_extracts_timer_mux_and_offset() -> None:
    lineage = _lineage(
        """
Object FixtureObject
  Behavior = FireWeaponWhenDeadBehavior ModuleTag_DeathWeapon
    DeathTypes = NONE +CRUSHED
    RequiredStatus = DEATH_1 DEPLOYED
    ExemptStatus = SOLD
    StartsActive = Yes
    ActiveDuringConstruction = No
    DelayTime = 2466
    DeathWeapon = CastleWallDeath
    WeaponOffset = X:-40 Y:0 Z:2.5
  End
End
"""
    )
    rows = compile_fire_weapon_when_dead_behaviors(lineage, "FixtureObject")
    assert len(rows) == 1
    fields = rows[0]["fields"]
    assert fields["deathTypes"] == "NONE"
    assert fields["includedDeathTypes"] == ["CRUSHED"]
    assert fields["RequiredStatus"]["value"] == ["DEATH_1", "DEPLOYED"]
    assert fields["ExemptStatus"]["value"] == ["SOLD"]
    assert fields["StartsActive"]["value"] is True
    assert fields["ActiveDuringConstruction"]["value"] is False
    assert fields["DelayTime"]["milliseconds"] == 2466
    assert fields["DeathWeapon"]["value"] == "CastleWallDeath"
    assert fields["WeaponOffset"]["value"] == {"x": -40.0, "y": 0.0, "z": 2.5}
    assert rows[0]["runtimeStatus"] == "executable"
    assert rows[0]["extraction"] == "typed"


def test_fire_weapon_when_dead_fails_closed_on_unknown_field() -> None:
    lineage = _lineage(
        """
Object FixtureObject
  Behavior = FireWeaponWhenDeadBehavior ModuleTag_DeathWeapon
    StartsActive = Yes
    DeathWeapon = CastleWallDeath
    UnknownDelay = 1
  End
End
"""
    )
    with pytest.raises(ModuleContractError, match="unsupported fields"):
        compile_fire_weapon_when_dead_behaviors(lineage, "FixtureObject")


def test_ship_slow_death_extracts_sink_timers_rate_sound_and_death_mux() -> None:
    lineage = _lineage(
        """
Object FixtureObject
  Behavior = ShipSlowDeathBehavior ModuleTag_Sink
    DeathTypes = ALL -FADED
    SinkDelay = 250
    SinkRate = 12.0
    DestructionDelay = 10000
    Sound = INITIAL GoodShipTransportSinkMS
  End
End
"""
    )
    rows = compile_ship_slow_death_behaviors(lineage, "FixtureObject")
    fields = rows[0]["fields"]
    assert fields["deathTypes"] == "ALL"
    assert fields["excludedDeathTypes"] == ["FADED"]
    assert fields["SinkDelay"]["milliseconds"] == 250
    assert fields["SinkRate"]["value"] == 12.0
    assert fields["DestructionDelay"]["milliseconds"] == 10000
    assert fields["Sound"]["phase"] == "INITIAL"
    assert fields["Sound"]["event"] == "GoodShipTransportSinkMS"
    assert rows[0]["runtimeStatus"] == "executable"
    assert rows[0]["extraction"] == "typed"


def test_slow_death_extracts_ordered_phases_provenance_and_deferred_receipts() -> None:
    lineage = _lineage(
        """
Object FixtureObject
  Behavior = SlowDeathBehavior ModuleTag_Death
    DeathTypes = NONE +KNOCKBACK
    DeathFlags = DEATH_1 DEATH_3
    ProbabilityModifier = 50
    SinkDelay = 3000
    SinkRate = -2.0
    DestructionDelay = 8000
    FadeDelay = 250
    FadeTime = 3500
    DecayBeginTime = 1200
    ShadowWhenDead = No
    FX = INITIAL FX_First
    FX = INITIAL FX_Second FX_Third
    Sound = FINAL UnitVoiceDie
    OCL = MIDPOINT OCL_First
    OCL = FINAL OCL_Second
    Weapon = FINAL DeathWeapon
  End
End
"""
    )
    row = compile_slow_death_behaviors(lineage, "FixtureObject")[0]
    fields = row["fields"]
    assert fields["deathTypes"] == "NONE"
    assert fields["includedDeathTypes"] == ["KNOCKBACK"]
    assert fields["DeathFlags"]["value"] == ["DEATH_1", "DEATH_3"]
    assert fields["DeathFlags"]["runtimeStatus"] == "deferred"
    assert fields["ProbabilityModifier"]["value"] == 50
    assert fields["SinkDelay"]["milliseconds"] == 3000
    assert fields["SinkRate"]["value"] == -2.0
    assert fields["DestructionDelay"]["milliseconds"] == 8000
    assert fields["FX"] == [
        {
            "phase": "INITIAL", "references": ["FX_First"],
            "authored": "INITIAL FX_First", "sourceIni": "data/ini/object/fixture.ini",
            "line": 14, "runtimeStatus": "deferred",
            "deferredReason": "presentation-system-not-bound",
        },
        {
            "phase": "INITIAL", "references": ["FX_Second", "FX_Third"],
            "authored": "INITIAL FX_Second FX_Third", "sourceIni": "data/ini/object/fixture.ini",
            "line": 15, "runtimeStatus": "deferred",
            "deferredReason": "presentation-system-not-bound",
        },
    ]
    assert fields["OCL"][0]["phase"] == "MIDPOINT"
    assert fields["Weapon"][0]["references"] == ["DeathWeapon"]
    assert row["runtimeStatus"] == "deferred"
    graph = row["effectGraph"]
    assert graph["probabilityWeight"] == 50
    assert graph["sinkRatePerSecond"] == -2.0
    assert graph["phaseOrder"] == ["INITIAL", "MIDPOINT", "FINAL"]
    assert graph["phaseEffectOrder"] == ["FX", "OCL", "Weapon"]
    assert graph["executionEligibility"] == {
        "status": "deferred",
        "blockers": ["OCL", "Weapon"],
        "runtimeStatus": "deferred",
        "deferredReason": "slow-death-gameplay-variant-not-runtime-bound",
    }


def test_slow_death_promotes_only_closed_core_shape_with_presentation_receipts() -> None:
    lineage = _lineage(
        """
Object FixtureObject
  Behavior = SlowDeathBehavior ModuleTag_Death
    DeathTypes = ALL -FADED
    DestructionDelay = 4000
    FX = INITIAL FX_Unsummon
    Sound = INITIAL UnitVoiceDie
  End
End
"""
    )
    row = compile_slow_death_behaviors(lineage, "FixtureObject")[0]
    assert row["runtimeStatus"] == "executable"
    assert row["effectGraph"]["executionEligibility"] == {
        "status": "evidence-closed-core",
        "blockers": [],
        "runtimeStatus": "executable",
    }
    assert all(
        phase["runtimeStatus"] == "deferred"
        for key in ("FX", "Sound")
        for phase in row["fields"][key]
    )
    integrated = compile_all_module_contracts(lineage, "FixtureObject")
    assert [(item["module"], item["tag"]) for item in integrated] == [
        ("SlowDeathBehavior", "ModuleTag_Death")
    ]


def test_slow_death_executable_claim_fails_closed_when_row_or_graph_is_tampered() -> None:
    lineage = _lineage(
        """
Object FixtureObject
  Behavior = SlowDeathBehavior ModuleTag_Death
    DeathTypes = ALL
    DestructionDelay = 4000
  End
End
"""
    )
    row = compile_slow_death_behaviors(lineage, "FixtureObject")[0]
    validate_module_contracts([row], label="closed SlowDeathBehavior")

    graph_tamper = dict(row)
    graph_tamper["effectGraph"] = dict(row["effectGraph"])
    graph_tamper["effectGraph"]["executionEligibility"] = {
        "status": "deferred", "blockers": [], "runtimeStatus": "executable",
    }
    with pytest.raises(ModuleContractError, match="lacks closed runtime evidence"):
        validate_module_contracts([graph_tamper], label="tampered graph")

    field_tamper = dict(row)
    field_tamper["fields"] = dict(row["fields"])
    field_tamper["fields"]["Weapon"] = [
        {"phase": "FINAL", "references": ["InventedWeapon"]}
    ]
    with pytest.raises(ModuleContractError, match="lacks closed runtime evidence"):
        validate_module_contracts([field_tamper], label="tampered fields")


@pytest.mark.parametrize(
    ("authored", "message"),
    [
        ("Invented = Yes", "unsupported fields"),
        ("SinkDelay = 3\n    SinkDelay = 4", "duplicate scalar"),
        ("SinkDelay = -1", "non-negative integer milliseconds"),
        ("DestructionDelay = MISSING_DELAY", "define is unresolved"),
        ("ProbabilityModifier = 1.5", "integer"),
        ("DeathFlags = DEATH_5", "DeathFlags malformed"),
        ("FX = UNKNOWN FX_Bad", "PHASE plus reference"),
        ("OCL = FINAL", "PHASE plus reference"),
        ("ShadowWhenDead = Maybe", "must be Yes or No"),
    ],
)
def test_slow_death_fails_closed_on_malformed_or_unknown_shape(
    authored: str, message: str
) -> None:
    lineage = _lineage(
        f"""
Object FixtureObject
  Behavior = SlowDeathBehavior ModuleTag_Death
    {authored}
  End
End
"""
    )
    with pytest.raises(ModuleContractError, match=message):
        compile_slow_death_behaviors(lineage, "FixtureObject")


def test_slow_death_exact_canonical_retail_corpora() -> None:
    from openbfme_importer.catalog import InstallCatalog
    from openbfme_importer.module_census import census_catalog_paths, read_catalog_documents

    expected = {
        "bfme2-retail": {
            "rows": 1244, "authoredDeathTypes": 639,
            "eligible": 1192, "deferred": 52,
            "fieldRows": {
                "DeathFlags": 165, "DecayBeginTime": 26,
                "DestructionDelay": 1217, "FadeDelay": 63, "FadeTime": 64,
                "FX": 68, "OCL": 45, "ProbabilityModifier": 31,
                "ShadowWhenDead": 13, "SinkDelay": 518, "SinkRate": 517,
                "Sound": 325, "Weapon": 11,
            },
            "phaseRows": {"FX": 73, "OCL": 46, "Sound": 325, "Weapon": 11},
            "phases": {"FINAL": 50, "HIT_GROUND": 1, "INITIAL": 395, "MIDPOINT": 9},
        },
        "rotwk-retail": {
            "rows": 1459, "authoredDeathTypes": 803,
            "eligible": 1394, "deferred": 65,
            "fieldRows": {
                "DeathFlags": 196, "DecayBeginTime": 27,
                "DestructionDelay": 1428, "DoNotRandomizeMidpoint": 2,
                "FadeDelay": 85, "FadeTime": 86, "FX": 91, "OCL": 55,
                "ProbabilityModifier": 32, "ShadowWhenDead": 14,
                "SinkDelay": 637, "SinkRate": 636, "Sound": 433, "Weapon": 13,
            },
            "phaseRows": {"FX": 97, "OCL": 57, "Sound": 433, "Weapon": 13},
            "phases": {"FINAL": 64, "HIT_GROUND": 3, "INITIAL": 522, "MIDPOINT": 11},
        },
    }
    paths = census_catalog_paths()
    if not all(path.is_file() for path in paths.values()):
        pytest.skip("retail catalogs are not available")
    actual: dict[str, dict[str, object]] = {}
    for label, path in paths.items():
        rows: list[dict[str, object]] = []
        for virtual_path, source in read_catalog_documents(InstallCatalog.load(path)):
            if (
                not virtual_path.casefold().startswith("data/ini/object/")
                or b"slowdeathbehavior" not in source.lower()
            ):
                continue
            document = parse_sage_document(source, virtual_path=virtual_path)
            for obj in document.objects:
                rows.extend(compile_slow_death_behaviors([obj], obj.name))
        validate_module_contracts(rows, label=f"{label} SlowDeathBehavior")
        field_rows = Counter(
            key for row in rows for key in row["fields"] if key[:1].isupper()
        )
        phase_rows = {
            key: sum(len(row["fields"].get(key, [])) for row in rows)
            for key in ("FX", "OCL", "Sound", "Weapon")
        }
        phases = Counter(
            phase_row["phase"]
            for row in rows
            for key in ("FX", "OCL", "Sound", "Weapon")
            for phase_row in row["fields"].get(key, [])
        )
        eligibility = Counter(
            row["effectGraph"]["executionEligibility"]["status"] for row in rows
        )
        actual[label] = {
            "rows": len(rows),
            "authoredDeathTypes": sum(
                "deathTypesAuthored" in row["fields"] for row in rows
            ),
            "eligible": eligibility["evidence-closed-core"],
            "deferred": eligibility["deferred"],
            "fieldRows": dict(field_rows),
            "phaseRows": phase_rows,
            "phases": dict(sorted(phases.items())),
        }
        assert Counter(row["runtimeStatus"] for row in rows) == {
            "executable": expected[label]["eligible"],
            "deferred": expected[label]["deferred"],
        }
        assert all(row["extraction"] == "typed" for row in rows)
    assert actual == expected


def test_horde_transport_extracts_capacity_status_timers_and_payload() -> None:
    lineage = _lineage(
        """
Object FixtureObject
  Behavior = HordeTransportContain ModuleTag_Transport
    ObjectStatusOfContained = UNSELECTABLE ENCLOSED
    Slots = 2
    EnterSound = GarrisonEnter
    ExitSound = GarrisonExit
    DamagePercentToUnits = 0%
    PassengerFilter = ANY +INFANTRY +CAVALRY
    AllowOwnPlayerInsideOverride = Yes
    AllowAlliesInside = No
    AllowEnemiesInside = No
    AllowNeutralInside = No
    ExitDelay = 250
    NumberOfExitPaths = 2
    ForceOrientationContainer = No
    PassengerBonePrefix = PassengerBone:B_CARGO0 KindOf:INFANTRY
    PassengerBonePrefix = PassengerBone:B_BANNER KindOf:BANNER
    ShowPips = Yes
    KillPassengersOnDeath = No
    EjectPassengersOnDeath = Yes
    FadeFilter = ALL
    FadePassengerOnEnter = Yes
    EnterFadeTime = 3000
    FadePassengerOnExit = Yes
    ExitFadeTime = 1000
    InitialPayload = InternalShipGoodArcher 2
  End
End
"""
    )
    rows = compile_horde_transport_contains(lineage, "FixtureObject")
    fields = rows[0]["fields"]
    assert fields["ObjectStatusOfContained"]["value"] == [
        "UNSELECTABLE",
        "ENCLOSED",
    ]
    assert fields["Slots"]["value"] == 2
    assert fields["DamagePercentToUnits"]["ratio"] == 0.0
    assert fields["PassengerFilter"]["value"] == ["ANY", "+INFANTRY", "+CAVALRY"]
    assert fields["ExitDelay"]["milliseconds"] == 250
    assert fields["NumberOfExitPaths"]["value"] == 2
    assert fields["PassengerBonePrefix"] == [
        {
            "passengerBone": "B_CARGO0",
            "kindOf": "INFANTRY",
            "authored": "PassengerBone:B_CARGO0 KindOf:INFANTRY",
            "sourceIni": "data/ini/object/fixture.ini",
            "line": 17,
        },
        {
            "passengerBone": "B_BANNER",
            "kindOf": "BANNER",
            "authored": "PassengerBone:B_BANNER KindOf:BANNER",
            "sourceIni": "data/ini/object/fixture.ini",
            "line": 18,
        },
    ]
    assert fields["EjectPassengersOnDeath"]["value"] is True
    assert fields["EnterFadeTime"]["milliseconds"] == 3000
    assert fields["InitialPayload"]["objectId"] == "InternalShipGoodArcher"
    assert fields["InitialPayload"]["count"] == 2
    assert rows[0]["runtimeStatus"] == "executable"


@pytest.mark.parametrize("kind", ["ShipSlowDeathBehavior", "HordeTransportContain"])
def test_ship_contracts_fail_closed_on_unknown_field(kind: str) -> None:
    lineage = _lineage(
        f"""
Object FixtureObject
  Behavior = {kind} ModuleTag_Fixture
    InventedShipRule = Yes
  End
End
"""
    )
    compiler = (
        compile_ship_slow_death_behaviors
        if kind == "ShipSlowDeathBehavior"
        else compile_horde_transport_contains
    )
    with pytest.raises(ModuleContractError, match="unsupported fields"):
        compiler(lineage, "FixtureObject")


def test_attribute_modifier_aura_extracts_complete_authored_contract() -> None:
    lineage = _lineage(
        """
Object FixtureObject
  Behavior = AttributeModifierAuraUpdate ModuleTag_Aura
    StartsActive = No
    BonusName = GenericHeroLeadership
    TriggeredBy = Upgrade_Leadership Upgrade_ObjectLevel4
    ConflictsWith = Upgrade_Disabled
    RefreshDelay = 2000
    Range = LEADERSHIP_RADIUS
    ObjectFilter = ANY +INFANTRY +CAVALRY -HERO
    TargetEnemy = Yes
    MaxActiveRank = 1
    AntiCategory = LEADERSHIP BUFF
    AllowSelf = Yes
    RunWhileDead = No
    RequiredConditions = TAINT ELVEN_WOOD
    AffectContainedOnly = Yes
  End
End
"""
    )
    row = compile_attribute_modifier_aura_updates(lineage, "FixtureObject")[0]
    fields = row["fields"]
    assert fields["StartsActive"]["value"] is False
    assert fields["BonusName"]["value"] == "GenericHeroLeadership"
    assert fields["TriggeredBy"]["value"] == [
        "Upgrade_Leadership",
        "Upgrade_ObjectLevel4",
    ]
    assert fields["RefreshDelay"]["milliseconds"] == 2000
    assert fields["Range"]["expression"] == "LEADERSHIP_RADIUS"
    assert "value" not in fields["Range"]
    assert fields["ObjectFilter"]["value"][-1] == "-HERO"
    assert fields["TargetEnemy"]["value"] is True
    assert fields["MaxActiveRank"]["value"] == 1
    assert fields["AntiCategory"]["value"] == ["LEADERSHIP", "BUFF"]
    assert fields["RequiredConditions"]["value"] == ["TAINT", "ELVEN_WOOD"]
    assert row["runtimeStatus"] == "executable"
    assert row["extraction"] == "typed"


def test_upgrade_gated_aura_uses_proven_omitted_starts_active_default() -> None:
    lineage = _lineage(
        """
Object FixtureObject
  Behavior = AttributeModifierAuraUpdate ModuleTag_UpgradeAura
    TriggeredBy = Upgrade_FixtureAura
    BonusName = FixtureAuraModifier
    RefreshDelay = 0
    AllowSelf = Yes
  End
End
"""
    )

    row = compile_attribute_modifier_aura_updates(lineage, "FixtureObject")[0]

    assert row["fields"]["StartsActive"] == {
        "authored": None,
        "value": False,
        "defaulted": True,
        "defaultSource": "AttributeModifierAuraUpdate-module-data-bool-zero",
        "sourceIni": "data/ini/object/fixture.ini",
        "line": 3,
    }
    assert row["fields"]["TriggeredBy"]["value"] == ["Upgrade_FixtureAura"]


def test_ungated_aura_cannot_omit_starts_active() -> None:
    lineage = _lineage(
        """
Object FixtureObject
  Behavior = AttributeModifierAuraUpdate ModuleTag_Aura
    BonusName = FixtureAuraModifier
  End
End
"""
    )

    with pytest.raises(
        ModuleContractError,
        match="omitted StartsActive requires an authored TriggeredBy gate",
    ):
        compile_attribute_modifier_aura_updates(lineage, "FixtureObject")


def test_lifetime_update_extracts_literal_and_define_timer_expressions() -> None:
    literal = _lineage(
        """
Object FixtureObject
  Behavior = LifetimeUpdate ModuleTag_Lifetime
    MinLifetime = 1000
    MaxLifetime = 2000.0
    DeathType = FADED
  End
End
"""
    )
    fields = compile_lifetime_updates(literal, "FixtureObject")[0]["fields"]
    assert fields["MinLifetime"] == {
        "expression": "1000",
        "milliseconds": 1000,
        "sourceIni": "data/ini/object/fixture.ini",
        "line": 4,
    }
    assert fields["MaxLifetime"]["milliseconds"] == 2000
    assert fields["DeathType"]["value"] == "FADED"

    defined = _lineage(
        """
Object FixtureObject
  Behavior = LifetimeUpdate ModuleTag_Lifetime
    MinLifetime = SUMMON_LIFETIME
    MaxLifetime = SUMMON_LIFETIME
  End
End
"""
    )
    fields = compile_lifetime_updates(defined, "FixtureObject")[0]["fields"]
    assert fields["MinLifetime"]["expression"] == "SUMMON_LIFETIME"
    assert "milliseconds" not in fields["MinLifetime"]


@pytest.mark.parametrize(
    ("kind", "field", "value"),
    [
        ("AttributeModifierAuraUpdate", "RefreshDelay", "soon"),
        ("AttributeModifierAuraUpdate", "InventedAuraField", "Yes"),
        ("LifetimeUpdate", "MinLifetime", "-1"),
        ("LifetimeUpdate", "InventedLifetimeField", "1"),
    ],
)
def test_aura_and_lifetime_contracts_fail_closed(
    kind: str, field: str, value: str
) -> None:
    lineage = _lineage(
        f"""
Object FixtureObject
  Behavior = {kind} ModuleTag_Fixture
    {field} = {value}
  End
End
"""
    )
    compiler = (
        compile_attribute_modifier_aura_updates
        if kind == "AttributeModifierAuraUpdate"
        else compile_lifetime_updates
    )
    with pytest.raises(ModuleContractError):
        compiler(lineage, "FixtureObject")


def test_ai_update_interface_extracts_complete_scalar_and_turret_contract() -> None:
    lineage = _lineage(
        """
Object FixtureObject
  Behavior = AIUpdateInterface ModuleTag_AI
    AutoAcquireEnemiesWhenIdle = Yes ATTACK_BUILDINGS STEALTHED
    CanAttackWhileContained = Yes
    AILuaEventsList = FixtureFunctions
    AttackPriority = AttackPriority_Infantry
    FadeOnPortals = No
    MoodAttackCheckRate = 500
    HoldGroundCloseRangeDistance = 40
    MinCowerTime = 3000
    MaxCowerTime = 5000
    RampageTime = 4470
    TimeToEjectPassengersOnRampage = 2300
    StopChaseDistance = 562
    BurningDeathTime = BURNINGDEATH_DURATION_INFANTRY
    RampageRequiresAflame = Yes
    SpecialContactPoints = Bomb
    Turret
      TurretTurnRate = 90
      ControlledWeaponSlots = PRIMARY SECONDARY
    End
  End
End
"""
    )
    row = compile_ai_update_interfaces(lineage, "FixtureObject")[0]
    fields = row["fields"]
    assert fields["AutoAcquireEnemiesWhenIdle"]["enabled"] is True
    assert fields["AutoAcquireEnemiesWhenIdle"]["flags"] == [
        "ATTACK_BUILDINGS",
        "STEALTHED",
    ]
    assert fields["MoodAttackCheckRate"]["milliseconds"] == 500
    assert fields["BurningDeathTime"]["expression"] == (
        "BURNINGDEATH_DURATION_INFANTRY"
    )
    assert "milliseconds" not in fields["BurningDeathTime"]
    assert fields["Turrets"][0]["TurretTurnRate"]["value"] == 90
    assert fields["Turrets"][0]["ControlledWeaponSlots"]["value"] == [
        "PRIMARY",
        "SECONDARY",
    ]
    assert fields["Turrets"][0]["sourceIni"].endswith("fixture.ini")
    assert row["runtimeStatus"] == "executable"


@pytest.mark.parametrize(
    ("field", "value"),
    [
        ("AutoAcquireEnemiesWhenIdle", "Maybe"),
        ("MoodAttackCheckRate", "soon"),
        ("InventedAIField", "Yes"),
    ],
)
def test_ai_update_interface_fails_closed(field: str, value: str) -> None:
    lineage = _lineage(
        f"""
Object FixtureObject
  Behavior = AIUpdateInterface ModuleTag_AI
    {field} = {value}
  End
End
"""
    )
    with pytest.raises(ModuleContractError):
        compile_ai_update_interfaces(lineage, "FixtureObject")


def test_stances_behavior_extracts_template_with_provenance() -> None:
    lineage = _lineage(
        """
Object FixtureObject
  Behavior = StancesBehavior ModuleTag_Stances
    StanceTemplate = FighterHorde
  End
End
"""
    )
    row = compile_stances_behaviors(lineage, "FixtureObject")[0]
    assert row["fields"]["StanceTemplate"] == {
        "authored": "FighterHorde",
        "value": "FighterHorde",
        "sourceIni": "data/ini/object/fixture.ini",
        "line": 4,
    }
    assert row["runtimeStatus"] == "executable"
    assert row["extraction"] == "typed"


@pytest.mark.parametrize(
    "body",
    [
        "StanceTemplate = FighterHorde Extra",
        "InventedStanceField = FighterHorde",
        "StanceTemplate = FighterHorde\n    StanceTemplate = Hero",
    ],
)
def test_stances_behavior_fails_closed(body: str) -> None:
    lineage = _lineage(
        f"""
Object FixtureObject
  Behavior = StancesBehavior ModuleTag_Stances
    {body}
  End
End
"""
    )
    with pytest.raises(ModuleContractError):
        compile_stances_behaviors(lineage, "FixtureObject")


def test_horde_contain_extracts_repeated_payload_rank_and_formation_fields() -> None:
    lineage = _lineage(
        """
Object FixtureObject
  Behavior = HordeContain ModuleTag_Horde
    ObjectStatusOfContained =
    InitialPayload = GondorFighter GOOD_MEN_HORDE_SIZE
    InitialPayload = GondorBanner 1
    Slots = 10
    PassengerFilter = NONE +INFANTRY
    ShowPips = No
    ThisFormationIsTheMainFormation = Yes
    RandomOffset = X:4 Y:5
    RandomOffset = X:-2 Y:8
    RankInfo = RankNumber:1 UnitType:GondorFighter Position:X:0 Y:20 Position:X:0 Y:-20
    RankInfo = RankNumber:2 UnitType:GondorFighter Position:X:20 Y:0
    RanksToReleaseWhenAttacking = 1 2
    FrontAngle = 270
    FlankedDelay = 2000
    MeleeBehavior = Amoeba
      FacingBonus = 30
      AngleLimitCos = -0.25
      InnerRange = 1
      OuterRange = 100
      OuterRangeBuildings = 140
    End
  End
End
"""
    )
    row = compile_horde_contains(lineage, "FixtureObject")[0]
    fields = row["fields"]
    assert fields["ObjectStatusOfContained"]["value"] == []
    assert [item["objectId"] for item in fields["InitialPayload"]] == [
        "GondorFighter",
        "GondorBanner",
    ]
    assert fields["InitialPayload"][0]["countExpression"] == "GOOD_MEN_HORDE_SIZE"
    assert fields["InitialPayload"][0]["count"] == {
        "kind": "define",
        "name": "GOOD_MEN_HORDE_SIZE",
    }
    assert fields["InitialPayload"][1]["count"] == {"kind": "literal", "value": 1}
    assert fields["Slots"]["value"] == 10
    assert [item["value"] for item in fields["RandomOffset"]] == [
        {"x": 4.0, "y": 5.0},
        {"x": -2.0, "y": 8.0},
    ]
    assert fields["RandomOffset"][1]["line"] > fields["RandomOffset"][0]["line"]
    assert len(fields["RankInfo"]) == 2
    assert fields["RankInfo"][0]["clauses"][0] == {
        "key": "RankNumber",
        "value": "1",
    }
    assert fields["FlankedDelay"]["milliseconds"] == 2000
    assert fields["MeleeBehavior"]["value"] == "Amoeba"
    assert fields["FacingBonus"]["value"] == 30.0
    assert fields["OuterRangeBuildings"]["value"] == 140.0
    assert row["runtimeStatus"] == "executable"


def test_horde_contain_rejects_unknown_field_and_malformed_rank() -> None:
    unknown = _lineage(
        """
Object FixtureObject
  Behavior = HordeContain ModuleTag_Horde
    InventedHordeField = Yes
  End
End
"""
    )
    with pytest.raises(ModuleContractError, match="unsupported fields"):
        compile_horde_contains(unknown, "FixtureObject")
    malformed = _lineage(
        """
Object FixtureObject
  Behavior = HordeContain ModuleTag_Horde
    RankInfo = not-a-clause
  End
End
"""
    )
    with pytest.raises(ModuleContractError, match="RankInfo"):
        compile_horde_contains(malformed, "FixtureObject")

    unknown_clause = _lineage(
        """
Object FixtureObject
  Behavior = HordeContain ModuleTag_Horde
    RankInfo = RankNumber:1 UnitType:GondorFighter Position:X:0 Y:0 Invented:1
  End
End
"""
    )
    with pytest.raises(ModuleContractError, match="unsupported clauses"):
        compile_horde_contains(unknown_clause, "FixtureObject")

    unknown_nested = _lineage(
        """
Object FixtureObject
  Behavior = HordeContain ModuleTag_Horde
    MeleeBehavior = Amoeba
      InventedMeleeField = 1
    End
  End
End
"""
    )
    with pytest.raises(ModuleContractError, match="nested fields malformed"):
        compile_horde_contains(unknown_nested, "FixtureObject")


def test_horde_ai_update_preserves_repeated_lua_rows_and_typed_policy() -> None:
    lineage = _lineage(
        """
Object FixtureObject
  Behavior = HordeAIUpdate ModuleTag_AI
    AutoAcquireEnemiesWhenIdle = Yes ATTACK_BUILDINGS STEALTHED
    MoodAttackCheckRate = 500
    AILuaEventsList = InfantryFunctions
    AILuaEventsList = WildInfantryFunctions
    MinCowerTime = 3000
    MaxCowerTime = 5000
    AttackPriority = AttackPriority_Infantry
    CanAttackWhileContained = Yes
  End
End
"""
    )
    row = compile_horde_ai_updates(lineage, "FixtureObject")[0]
    fields = row["fields"]
    assert fields["AutoAcquireEnemiesWhenIdle"]["enabled"] is True
    assert fields["AutoAcquireEnemiesWhenIdle"]["flags"] == [
        "ATTACK_BUILDINGS", "STEALTHED"
    ]
    assert [item["value"] for item in fields["AILuaEventsList"]] == [
        "InfantryFunctions", "WildInfantryFunctions"
    ]
    assert fields["MoodAttackCheckRate"]["milliseconds"] == 500
    assert fields["CanAttackWhileContained"]["value"] is True
    assert all("sourceIni" in item and "line" in item for item in fields["AILuaEventsList"])
    assert row["runtimeStatus"] == "executable"


@pytest.mark.parametrize(
    "body",
    [
        "InventedHordeAIField = Yes",
        "MoodAttackCheckRate = 500\n    MoodAttackCheckRate = 600",
        "AutoAcquireEnemiesWhenIdle = No ATTACK_BUILDINGS",
        "AILuaEventsList = two tokens",
    ],
)
def test_horde_ai_update_fails_closed(body: str) -> None:
    lineage = _lineage(
        f"""
Object FixtureObject
  Behavior = HordeAIUpdate ModuleTag_AI
    {body}
  End
End
"""
    )
    with pytest.raises(ModuleContractError):
        compile_horde_ai_updates(lineage, "FixtureObject")


def test_pickup_stuff_update_types_scan_policy_and_provenance() -> None:
    lineage = _lineage(
        """
Object FixtureObject
  Behavior = PickupStuffUpdate ModuleTag_Pickup
    SkirmishAIOnly = Yes
    StuffToPickUp = NONE +CRATE
    ScanRange = 200
    ScanIntervalSeconds = 0.5
  End
End
"""
    )
    row = compile_pickup_stuff_updates(lineage, "FixtureObject")[0]
    fields = row["fields"]
    assert fields["SkirmishAIOnly"]["value"] is True
    assert fields["StuffToPickUp"]["value"] == ["NONE", "+CRATE"]
    assert fields["ScanRange"]["value"] == 200
    assert fields["ScanIntervalSeconds"]["seconds"] == 0.5
    assert fields["ScanIntervalSeconds"]["milliseconds"] == 500
    assert fields["ScanIntervalSeconds"]["sourceIni"].endswith("fixture.ini")
    assert row["runtimeStatus"] == "executable"


@pytest.mark.parametrize(
    "body",
    [
        "InventedPickupField = Yes",
        "SkirmishAIOnly = Maybe",
        "ScanRange = -1",
        "ScanIntervalSeconds = -0.5",
        "StuffToPickUp =",
        "ScanRange = 200\n    ScanRange = 300",
    ],
)
def test_pickup_stuff_update_fails_closed(body: str) -> None:
    lineage = _lineage(
        f"""
Object FixtureObject
  Behavior = PickupStuffUpdate ModuleTag_Pickup
    SkirmishAIOnly = Yes
    StuffToPickUp = NONE +CRATE
    ScanRange = 200
    ScanIntervalSeconds = 0.5
    {body}
  End
End
"""
    )
    with pytest.raises(ModuleContractError):
        compile_pickup_stuff_updates(lineage, "FixtureObject")


def test_auto_ability_behavior_types_ranges_and_repeated_queries() -> None:
    lineage = _lineage(
        """
Object FixtureObject
  Behavior = AutoAbilityBehavior ModuleTag_Auto
    SpecialAbility = SpecialAbilityFixture
    StartsActive = Yes
    AllowSelf = No
    MaxScanRange = #SUBTRACT( FIXTURE_RANGE 25 )
    MinScanRange = 50
    IdleTimeSeconds = 2
    Query = 1 ALL ENEMIES -STRUCTURE
    Query = 2 ANY ALLIES +HERO
    ForbiddenStatus = INSIDE_GARRISON
  End
End
"""
    )
    row = compile_auto_ability_behaviors(lineage, "FixtureObject")[0]
    fields = row["fields"]
    assert fields["MaxScanRange"] == {
        "authored": "#SUBTRACT( FIXTURE_RANGE 25 )",
        "sourceIni": "data/ini/object/fixture.ini", "line": 7,
        "expression": "#SUBTRACT( FIXTURE_RANGE 25 )", "kind": "subtract",
        "name": "FIXTURE_RANGE", "amount": 25,
    }
    assert fields["MinScanRange"]["value"] == 50
    assert fields["IdleTimeSeconds"]["milliseconds"] == 2000
    assert fields["Query"][0]["minimumMatches"] == 1
    assert fields["Query"][0]["filterTokens"] == ["ALL", "ENEMIES", "-STRUCTURE"]
    assert len(fields["Query"]) == 2
    assert fields["ForbiddenStatus"]["value"] == ["INSIDE_GARRISON"]
    assert row["runtimeStatus"] == "executable"


@pytest.mark.parametrize(
    "body",
    [
        "InventedAutoField = Yes",
        "AllowSelf = Maybe",
        "Query = zero ALL ENEMIES",
        "Query = 1 BAD-TOKEN",
        "MaxScanRange = #ADD( RANGE 25 )",
        "SpecialAbility = two tokens",
    ],
)
def test_auto_ability_behavior_fails_closed(body: str) -> None:
    lineage = _lineage(
        f"""
Object FixtureObject
  Behavior = AutoAbilityBehavior ModuleTag_Auto
    {body}
  End
End
"""
    )
    with pytest.raises(ModuleContractError):
        compile_auto_ability_behaviors(lineage, "FixtureObject")


def test_respawn_update_types_rules_entries_timers_and_presentation_refs() -> None:
    lineage = _lineage(
        """
Object FixtureObject
  Behavior = RespawnUpdate ModuleTag_Respawn
    DeathAnim = DYING
    DeathFX = FX_FixtureDeath
    DeathAnimationTime = 4100
    InitialSpawnFX = FX_FixtureInitial
    RespawnAnim = LEVELED
    RespawnFX = FX_FixtureRespawn
    RespawnAnimationTime = 2000
    AutoRespawnAtObjectFilter = NONE +CASTLE_KEEP
    ButtonImage = HIFixture_res
    RespawnRules = AutoSpawn:No Cost:550 Time:60000 Health:100%
    RespawnEntry = Level:2 Cost:550 Time:60000
    RespawnEntry = Level:3 Cost:715 Time:120000
    RespawnAsTemplate = FixtureRespawnedHero
  End
End
"""
    )
    row = compile_respawn_updates(lineage, "FixtureObject")[0]
    fields = row["fields"]
    assert fields["DeathAnimationTime"]["milliseconds"] == 4100
    assert fields["AutoRespawnAtObjectFilter"]["value"] == ["NONE", "+CASTLE_KEEP"]
    assert fields["RespawnRules"]["autoSpawn"] is False
    assert fields["RespawnRules"]["cost"] == 550
    assert fields["RespawnRules"]["timeMilliseconds"] == 60000
    assert fields["RespawnRules"]["healthPercent"] == 100.0
    assert [entry["level"] for entry in fields["RespawnEntry"]] == [2, 3]
    assert fields["RespawnEntry"][1]["timeMilliseconds"] == 120000
    assert fields["RespawnAsTemplate"]["value"] == "FixtureRespawnedHero"
    assert all("sourceIni" in entry and "line" in entry for entry in fields["RespawnEntry"])
    assert row["runtimeStatus"] == "executable"


def test_rotwk_eowyn_respawn_rules_resolve_nested_authored_macros() -> None:
    lineage = _lineage(
        """
Object RohanEowyn
  Behavior = RespawnUpdate ModuleTag_RespawnUpdate
    DeathAnim = DYING
    AutoRespawnAtObjectFilter = NONE +CASTLE_KEEP
    ButtonImage = HIEowyn_res
    RespawnRules = AutoSpawn:No Cost:#DIVIDE( #MULTIPLY( EOWYN_BUILDCOST HERO_RESPAWN_COST_SCALAR_LEVEL_1 ) REVIVE_DIV_FACTOR ) Time:#MULTIPLY( HERO_RESPAWNTIME_TIER_1 HERO_RESPAWN_TIME_SCALAR_LEVEL_1 ) Health:100%
  End
End
""",
        "RohanEowyn",
    )
    values = {
        "eowyn_buildcost": 1000,
        "hero_respawn_cost_scalar_level_1": 60,
        "revive_div_factor": 100,
        "hero_respawntime_tier_1": 40,
        "hero_respawn_time_scalar_level_1": 1000,
    }
    provenance = {
        key: {
            "defineId": key.upper(),
            "sourceIni": "data/ini/gamedata.ini",
            "line": 100 + ordinal,
            "authoredValue": str(value),
            "value": value,
        }
        for ordinal, (key, value) in enumerate(values.items())
    }

    row = compile_respawn_updates(
        lineage,
        "RohanEowyn",
        numeric_defines=values,
        numeric_define_provenance=provenance,
    )[0]
    rules = row["fields"]["RespawnRules"]
    assert rules["cost"] == 600
    assert rules["timeMilliseconds"] == 40000
    assert rules["costExpression"].startswith("#DIVIDE(")
    assert rules["timeExpression"].startswith("#MULTIPLY(")
    assert [row["defineId"] for row in rules["costDefineProvenance"]] == [
        "EOWYN_BUILDCOST",
        "HERO_RESPAWN_COST_SCALAR_LEVEL_1",
        "REVIVE_DIV_FACTOR",
    ]

    missing = dict(provenance)
    missing.pop("revive_div_factor")
    with pytest.raises(ModuleContractError, match="define provenance unresolved"):
        compile_respawn_updates(
            lineage,
            "RohanEowyn",
            numeric_defines=values,
            numeric_define_provenance=missing,
        )


@pytest.mark.parametrize(
    "body",
    [
        "InventedRespawnField = Yes",
        "RespawnRules = AutoSpawn:Maybe Cost:550 Time:60000 Health:100%",
        "RespawnRules = AutoSpawn:No Cost:550 Time:60000 Health:100% Extra:1",
        "RespawnEntry = Level:two Cost:550 Time:60000",
        "RespawnEntry = Level:2 Cost:550 Time:60000\n    RespawnEntry = Level:2 Cost:600 Time:70000",
        "DeathAnim = two tokens",
    ],
)
def test_respawn_update_fails_closed(body: str) -> None:
    lineage = _lineage(
        f"""
Object FixtureObject
  Behavior = RespawnUpdate ModuleTag_Respawn
    DeathAnim = DYING
    AutoRespawnAtObjectFilter = NONE +CASTLE_KEEP
    ButtonImage = HIFixture
    RespawnRules = AutoSpawn:No Cost:550 Time:60000 Health:100%
    {body}
  End
End
"""
    )
    with pytest.raises(ModuleContractError):
        compile_respawn_updates(lineage, "FixtureObject")


def test_dual_weapon_and_castle_member_typed_contracts() -> None:
    lineage = _lineage(
        """
Object FixtureObject
  Behavior = DualWeaponBehavior ModuleTag_Dual
    SwitchWeaponOnCloseRangeDistance = FIXTURE_SWITCH_RANGE
  End
  Behavior = CastleMemberBehavior ModuleTag_Castle
    CountsForEvaCastleBreached = Yes
    StoreUpgradePrice = No
    BeingBuiltSound = BuildingConstructionLoop
    CampDestroyedOwnerEvaEvent = EconPlotDestroyed
    CampDestroyedAllyEvaEvent = AllyEconPlotDestroyed
    CampDestroyedAttackerEvaEvent = EnemyEconPlotDestroyed
  End
End
"""
    )
    dual = compile_dual_weapon_behaviors(lineage, "FixtureObject")[0]
    threshold = dual["fields"]["SwitchWeaponOnCloseRangeDistance"]
    assert threshold["expression"] == "FIXTURE_SWITCH_RANGE"
    assert "value" not in threshold
    castle = compile_castle_member_behaviors(lineage, "FixtureObject")[0]
    assert castle["fields"]["CountsForEvaCastleBreached"]["value"] is True
    assert castle["fields"]["StoreUpgradePrice"]["value"] is False
    assert castle["fields"]["BeingBuiltSound"]["value"] == "BuildingConstructionLoop"
    assert castle["fields"]["CampDestroyedOwnerEvaEvent"]["sourceIni"].endswith("fixture.ini")
    assert dual["runtimeStatus"] == castle["runtimeStatus"] == "deferred"


@pytest.mark.parametrize(
    "module, body, compiler",
    [
        ("DualWeaponBehavior", "Invented = 1", compile_dual_weapon_behaviors),
        ("DualWeaponBehavior", "SwitchWeaponOnCloseRangeDistance = -1", compile_dual_weapon_behaviors),
        ("CastleMemberBehavior", "CountsForEvaCastleBreached = Maybe", compile_castle_member_behaviors),
        ("CastleMemberBehavior", "Invented = Yes", compile_castle_member_behaviors),
        ("CastleMemberBehavior", "BeingBuiltSound = two tokens", compile_castle_member_behaviors),
    ],
)
def test_dual_weapon_and_castle_member_fail_closed(module, body, compiler) -> None:
    lineage = _lineage(
        f"""
Object FixtureObject
  Behavior = {module} ModuleTag_Test
    {body}
  End
End
"""
    )
    with pytest.raises(ModuleContractError):
        compiler(lineage, "FixtureObject")


def test_emotion_tracker_preserves_plain_and_override_emotions() -> None:
    lineage = _lineage(
        """
Object FixtureObject
  Behavior = EmotionTrackerUpdate ModuleTag_Emotion
    AfraidOf = NONE +MONSTER
    FearScanDistance = INFANTRY_FEAR_SCAN_RADIUS
    AddEmotion = Alert_Base
    AddEmotion = OVERRIDE Taunt_Base
      Duration = 7000
    End
    TauntAndPointUpdateDelay = 1000
    QuarrelProbability = 0.0002%
    ImmuneToFearLevel = 2
    IgnoreVeterancy = Yes
  End
End
"""
    )
    row = compile_emotion_tracker_updates(lineage, "FixtureObject")[0]
    fields = row["fields"]
    assert fields["AfraidOf"]["value"] == ["NONE", "+MONSTER"]
    assert fields["FearScanDistance"]["expression"] == "INFANTRY_FEAR_SCAN_RADIUS"
    assert [(item["name"], item["override"]) for item in fields["AddEmotion"]] == [
        ("Alert_Base", False), ("Taunt_Base", True)
    ]
    assert fields["AddEmotion"][1]["Duration"]["milliseconds"] == 7000
    assert fields["QuarrelProbability"]["fraction"] == pytest.approx(0.000002)
    assert fields["ImmuneToFearLevel"]["value"] == 2
    assert row["runtimeStatus"] == "deferred"


@pytest.mark.parametrize(
    "body",
    [
        "InventedEmotionField = Yes",
        "IgnoreVeterancy = Maybe",
        "QuarrelProbability = 101%",
        "AddEmotion = two tokens",
        "FearScanDistance = -1",
    ],
)
def test_emotion_tracker_fails_closed(body: str) -> None:
    lineage = _lineage(
        f"""
Object FixtureObject
  Behavior = EmotionTrackerUpdate ModuleTag_Emotion
    {body}
  End
End
"""
    )
    with pytest.raises(ModuleContractError):
        compile_emotion_tracker_updates(lineage, "FixtureObject")


def test_fire_deletion_docking_and_refund_contracts() -> None:
    lineage = _lineage(
        """
Object FixtureObject
  Behavior = FireWeaponUpdate ModuleTag_Fire
    AliveOnly = Yes
    FireWeaponNugget
      WeaponName = FixtureWeapon
      FireDelay = 250
      OneShot = No
      Offset = X:-25 Y:0 Z:0
    End
  End
  Behavior = DeletionUpdate ModuleTag_Delete
    MinLifetime = -1
    MaxLifetime = -1
  End
  Behavior = SiegeDockingBehavior ModuleTag_Dock
  End
  Behavior = RefundDie ModuleTag_Refund
    UpgradeRequired = Upgrade_Fixture
    BuildingRequired = ANY +GondorMarketPlace
    RefundPercent = 50%
  End
End
"""
    )
    fire = compile_fire_weapon_updates(lineage, "FixtureObject")[0]
    nugget = fire["fields"]["FireWeaponNugget"][0]
    assert nugget["WeaponName"]["value"] == "FixtureWeapon"
    assert nugget["FireDelay"]["milliseconds"] == 250
    assert nugget["Offset"]["value"] == {"x": -25.0, "y": 0.0, "z": 0.0}
    deletion = compile_deletion_updates(lineage, "FixtureObject")[0]
    assert deletion["fields"]["MinLifetime"]["indefinite"] is True
    assert compile_siege_docking_behaviors(lineage, "FixtureObject")[0]["fields"] == {}
    refund = compile_refund_die_behaviors(lineage, "FixtureObject")[0]
    assert refund["fields"]["RefundPercent"]["fraction"] == 0.5
    assert refund["fields"]["BuildingRequired"]["value"] == ["ANY", "+GondorMarketPlace"]
    assert refund["runtimeStatus"] == "executable"
    assert refund["effectGraph"] == {
        "kind": "refund-on-death",
        "executionEligibility": {
            "runtimeStatus": "executable",
            "blockers": [],
        },
        "deathDispatch": "once-per-object-death-edge",
        "ownerResolution": "current-controlling-player-at-death",
        "costBasis": "object-cached-build-cost",
        "rounding": "ceil",
        "deathGuards": ["UNDER_CONSTRUCTION", "SOLD"],
        "requirementCandidateRejects": [
            "EFFECTIVELY_DEAD", "DESTROYED", "KINDOF_INERT",
        ],
    }


@pytest.mark.parametrize(
    "module, body, compiler",
    [
        ("FireWeaponUpdate", "Invented = 1", compile_fire_weapon_updates),
        ("DeletionUpdate", "MinLifetime = 10", compile_deletion_updates),
        ("SiegeDockingBehavior", "Invented = 1", compile_siege_docking_behaviors),
        ("RefundDie", "RefundPercent = 101%", compile_refund_die_behaviors),
        ("RefundDie", "RefundPercent = nope", compile_refund_die_behaviors),
    ],
)
def test_fire_deletion_docking_and_refund_fail_closed(module, body, compiler) -> None:
    lineage = _lineage(f"""
Object FixtureObject
  Behavior = {module} ModuleTag_Test
    {body}
  End
End
""")
    with pytest.raises(ModuleContractError):
        compiler(lineage, "FixtureObject")


def test_refund_die_arbitrary_mod_object_filter_stays_row_deferred() -> None:
    lineage = _lineage("""
Object ModObject
  Behavior = RefundDie ModuleTag_ModFilter
    RefundPercent = 37.5%
    BuildingRequired = ALL +STRUCTURE -IMMOBILE
  End
End
""", "ModObject")
    row = compile_refund_die_behaviors(lineage, "ModObject")[0]
    assert row["extraction"] == "typed"
    assert row["runtimeStatus"] == "deferred"
    assert row["effectGraph"]["executionEligibility"] == {
        "runtimeStatus": "deferred",
        "blockers": ["typed-row-shape"],
    }
    validate_module_contracts([row], label="mod RefundDie")


def test_spread_invisibility_attach_and_clearance_contracts() -> None:
    lineage = _lineage("""
Object FixtureObject
  Behavior = FireSpreadUpdate ModuleTag_Spread
    MinSpreadDelay = 2000
    MaxSpreadDelay = 4000
    SpreadTryRange = 50
  End
  Behavior = InvisibilityUpdate ModuleTag_Invisible
    StartsActive = Yes
    UpdatePeriod = 2000
    InvisibilityNugget
      InvisibilityType = CAMOUFLAGE
      ForbiddenConditions = MOVING FIRING_ANY
      DetectionRange = CAMOUFLAGE_RADIUS
    End
  End
  Behavior = AttachUpdate ModuleTag_Attach
    ObjectFilter = ANY +HERO
    ScanRange = 100
    AlwaysTeleport = No
  End
End
""")
    spread = compile_fire_spread_updates(lineage, "FixtureObject")[0]
    assert spread["fields"]["MaxSpreadDelay"]["milliseconds"] == 4000
    invis = compile_invisibility_updates(lineage, "FixtureObject")[0]
    assert invis["fields"]["InvisibilityNugget"][0]["InvisibilityType"]["value"] == "CAMOUFLAGE"
    assert invis["fields"]["InvisibilityNugget"][0]["DetectionRange"]["expression"] == "CAMOUFLAGE_RADIUS"
    attach = compile_attach_updates(lineage, "FixtureObject")[0]
    assert attach["fields"]["ObjectFilter"]["value"] == ["ANY", "+HERO"]


@pytest.mark.parametrize("module, body, compiler", [
    ("FireSpreadUpdate", "MinSpreadDelay = 4000\n    MaxSpreadDelay = 2000\n    SpreadTryRange = 50", compile_fire_spread_updates),
    ("InvisibilityUpdate", "StartsActive = Yes\n    UpdatePeriod = 2000", compile_invisibility_updates),
    ("AttachUpdate", "Invented = Yes", compile_attach_updates),
    ("ClearanceTestingSlowDeathBehavior", "Invented = Yes", compile_clearance_testing_slow_death_behaviors),
])
def test_spread_invisibility_attach_and_clearance_fail_closed(module, body, compiler) -> None:
    lineage = _lineage(f"Object FixtureObject\n  Behavior = {module} ModuleTag_Test\n    {body}\n  End\nEnd")
    with pytest.raises(ModuleContractError): compiler(lineage, "FixtureObject")


def test_production_squish_and_getting_built_contracts() -> None:
    lineage = _lineage("""
Object FixtureObject
  Behavior = ProductionUpdate ModuleTag_Production
    GiveNoXP = Yes
    DoorOpeningTime = 3000
    ProductionModifier
      RequiredUpgrade = Upgrade_Fixture
      CostMultiplier = 0.8
      ModifierFilter = NONE +HERO
    End
  End
  Behavior = SquishCollide ModuleTag_Squish
  End
  Behavior = GettingBuiltBehavior ModuleTag_Built
    WorkerName = GondorWorkerNoSelect
    SpawnTimer = -1.0
    RebuildTimeSeconds = CASTLE_WALL_REBUILD_TIME
  End
End
""")
    production = compile_production_updates(lineage, "FixtureObject")[0]
    assert production["fields"]["ProductionModifier"][0]["CostMultiplier"]["value"] == 0.8
    assert compile_squish_collides(lineage, "FixtureObject")[0]["fields"] == {}
    built = compile_getting_built_behaviors(lineage, "FixtureObject")[0]
    assert built["fields"]["SpawnTimer"]["disabled"] is True
    assert built["fields"]["RebuildTimeSeconds"]["define"] == "CASTLE_WALL_REBUILD_TIME"


@pytest.mark.parametrize("module, body, compiler", [
    ("ProductionUpdate", "Invented = Yes", compile_production_updates),
    ("SquishCollide", "Invented = Yes", compile_squish_collides),
    ("GettingBuiltBehavior", "SpawnTimer = -2", compile_getting_built_behaviors),
])
def test_production_squish_and_getting_built_fail_closed(module, body, compiler) -> None:
    lineage = _lineage(f"Object FixtureObject\n  Behavior = {module} ModuleTag_Test\n    {body}\n  End\nEnd")
    with pytest.raises(ModuleContractError): compiler(lineage, "FixtureObject")


def test_ai_special_power_update_types_routing_range_and_policy() -> None:
    lineage = _lineage("""
Object FixtureObject
  Behavior = AISpecialPowerUpdate ModuleTag_AIAbility
    CommandButtonName = Command_FixturePower
    SpecialPowerAIType = AI_SPECIAL_POWER_RANGED_AOE_ATTACK
    SpecialPowerRadius = FIXTURE_RADIUS
    SpecialPowerRange = 200
    SpellMakesAStructure = No
    RandomizeTargetLocation = Yes
  End
End
""")
    row = compile_ai_special_power_updates(lineage, "FixtureObject")[0]
    fields = row["fields"]
    assert fields["CommandButtonName"]["value"] == "Command_FixturePower"
    assert fields["SpecialPowerAIType"]["value"] == "AI_SPECIAL_POWER_RANGED_AOE_ATTACK"
    assert fields["SpecialPowerRadius"]["expression"] == "FIXTURE_RADIUS"
    assert fields["SpecialPowerRange"]["value"] == 200
    assert fields["RandomizeTargetLocation"]["value"] is True
    assert row["runtimeStatus"] == "executable"
    assert row["effectGraph"]["specialPowerAIType"] == (
        "AI_SPECIAL_POWER_RANGED_AOE_ATTACK"
    )


def test_ai_special_power_update_duplicate_scalar_still_fails_closed() -> None:
    lineage = _lineage("""
Object FixtureObject
  Behavior = AISpecialPowerUpdate ModuleTag_Test
    CommandButtonName = Command_First
    CommandButtonName = Command_Second
    SpecialPowerAIType = AI_TEST
  End
End
""")
    with pytest.raises(ModuleContractError, match="duplicate scalar fields"):
        compile_ai_special_power_updates(lineage, "FixtureObject")


@pytest.mark.parametrize("body", [
    "CommandButtonName = Command_Test",
    "SpecialPowerAIType = AI_TEST",
    "CommandButtonName = two tokens\n    SpecialPowerAIType = AI_TEST",
    "CommandButtonName = Command_Test\n    SpecialPowerAIType = AI_TEST\n    SpecialPowerRange = -1",
    "CommandButtonName = Command_Test\n    SpecialPowerAIType = AI_TEST\n    Invented = Yes",
])
def test_ai_special_power_update_fails_closed(body: str) -> None:
    lineage = _lineage(f"Object FixtureObject\n  Behavior = AISpecialPowerUpdate ModuleTag_Test\n    {body}\n  End\nEnd")
    with pytest.raises(ModuleContractError):
        compile_ai_special_power_updates(lineage, "FixtureObject")


def test_building_behavior_preserves_window_tokens_and_repeated_fires() -> None:
    lineage = _lineage("""
Object FixtureObject
  Behavior = BuildingBehavior ModuleTag_Building
    NightWindowName = N_WINDOW N_GLOW
    FireName = FIRE01
    FireName = FIRE02
  End
End
""")
    fields = compile_building_behaviors(lineage, "FixtureObject")[0]["fields"]
    assert fields["NightWindowName"]["value"] == ["N_WINDOW", "N_GLOW"]
    assert [item["value"] for item in fields["FireName"]] == ["FIRE01", "FIRE02"]
    assert all("sourceIni" in item and "line" in item for item in fields["FireName"])


@pytest.mark.parametrize("body", ["Invented = Yes", "FireName = two tokens"])
def test_building_behavior_fails_closed(body: str) -> None:
    lineage = _lineage(f"Object FixtureObject\n  Behavior = BuildingBehavior ModuleTag_Test\n    {body}\n  End\nEnd")
    with pytest.raises(ModuleContractError): compile_building_behaviors(lineage, "FixtureObject")


def test_queue_exit_horde_collide_and_banner_contracts() -> None:
    lineage = _lineage("""
Object FixtureObject
  Behavior = QueueProductionExitUpdate ModuleTag_Exit
    UnitCreatePoint = X:0 Y:0 Z:0
    NaturalRallyPoint = X:50 Y:0 Z:0
    ExitDelay = STANDARD_HORDE_EXIT_DELAY
    NoExitPath = Yes
    CanRallyToSlaughter = No
    UseReturnToFormation = Yes
  End
  Behavior = HordeMemberCollide ModuleTag_Collide
  End
  Behavior = BannerCarrierUpdate ModuleTag_Banner
    IdleSpawnRate = 10000
    MorphCondition = UnitType:GondorCavalry Locomotor:SET_MOUNTED ModelState:"USER_3 MOUNTED"
  End
End
""")
    provenance = {
        "standard_horde_exit_delay": {
            "defineId": "STANDARD_HORDE_EXIT_DELAY",
            "sourceIni": "data/ini/gamedata.ini",
            "line": 64,
            "authoredValue": "10",
            "value": 10,
        }
    }
    queue = compile_queue_production_exit_updates(
        lineage,
        "FixtureObject",
        numeric_defines={"standard_horde_exit_delay": 10},
        numeric_define_provenance=provenance,
    )[0]
    assert queue["fields"]["UnitCreatePoint"][0]["value"] == {"x": 0.0, "y": 0.0, "z": 0.0}
    assert queue["fields"]["ExitDelay"][0]["expression"] == "STANDARD_HORDE_EXIT_DELAY"
    assert queue["fields"]["ExitDelay"][0]["milliseconds"] == 10
    assert queue["fields"]["ExitDelay"][0]["defineProvenance"] == provenance["standard_horde_exit_delay"]
    assert queue["fields"]["NoExitPath"]["value"] is True
    assert queue["fields"]["CanRallyToSlaughter"]["value"] is False
    assert queue["fields"]["UseReturnToFormation"]["value"] is True
    assert queue["runtimeStatus"] == "deferred"
    assert compile_horde_member_collides(lineage, "FixtureObject")[0]["fields"] == {}
    morph = compile_banner_carrier_updates(lineage, "FixtureObject")[0]["fields"]["MorphCondition"][0]
    assert morph["locomotor"] == "SET_MOUNTED"
    assert morph["modelStates"] == ["USER_3", "MOUNTED"]


@pytest.mark.parametrize("module, body, compiler", [
    ("QueueProductionExitUpdate", "UnitCreatePoint = nope", compile_queue_production_exit_updates),
    ("HordeMemberCollide", "Invented = Yes", compile_horde_member_collides),
    ("BannerCarrierUpdate", "IdleSpawnRate = 10000\n    MorphCondition = malformed", compile_banner_carrier_updates),
])
def test_queue_exit_horde_collide_and_banner_fail_closed(module, body, compiler) -> None:
    lineage = _lineage(f"Object FixtureObject\n  Behavior = {module} ModuleTag_Test\n    {body}\n  End\nEnd")
    with pytest.raises(ModuleContractError): compiler(lineage, "FixtureObject")


def test_queue_exit_define_and_malformed_coordinate_are_fail_closed() -> None:
    unresolved = _lineage("""
Object FixtureObject
  Behavior = QueueProductionExitUpdate ModuleTag_Exit
    UnitCreatePoint = X:0 Y:0 Z:0
    ExitDelay = STANDARD_HORDE_EXIT_DELAY
  End
End
""")
    with pytest.raises(ModuleContractError, match="define is unresolved"):
        compile_queue_production_exit_updates(unresolved, "FixtureObject")
    with pytest.raises(ModuleContractError, match="provenance is invalid"):
        compile_queue_production_exit_updates(
            unresolved,
            "FixtureObject",
            numeric_defines={"standard_horde_exit_delay": 10},
            numeric_define_provenance={
                "standard_horde_exit_delay": {
                    "defineId": "OTHER_DELAY",
                    "sourceIni": "data/ini/gamedata.ini",
                    "line": 64,
                    "authoredValue": "10",
                    "value": 10,
                }
            },
        )

    malformed = _lineage("""
Object AngmarKennelExpansion
  Behavior = QueueProductionExitUpdate ModuleTag_Exit
    UnitCreatePoint = X:70.0.0 Y:0.0 Z:0.0
  End
End
""", "AngmarKennelExpansion")
    row = compile_queue_production_exit_updates(malformed, "AngmarKennelExpansion")[0]
    assert row["runtimeStatus"] == "deferred"
    assert row["fields"]["UnitCreatePoint"][0]["validNumeric"] is False
    assert row["fields"]["UnitCreatePoint"][0]["value"] is None

    supported = _lineage("""
Object FixtureObject
  Behavior = QueueProductionExitUpdate ModuleTag_Exit
    UnitCreatePoint = X:0 Y:0 Z:0
    NaturalRallyPoint = X:10 Y:0 Z:0
    ExitDelay = 10
    PlacementViewAngle = 90
    NoExitPath = Yes
  End
End
""")
    supported_row = compile_queue_production_exit_updates(
        supported, "FixtureObject"
    )[0]
    assert supported_row["runtimeStatus"] == "executable"
    validate_module_contracts([supported_row], label="fixture")

    deferred = _lineage("""
Object FixtureObject
  Behavior = QueueProductionExitUpdate ModuleTag_Exit
    UnitCreatePoint = X:0 Y:0 Z:0
    AllowAirborneCreation = Yes
  End
End
""")
    assert compile_queue_production_exit_updates(
        deferred, "FixtureObject"
    )[0]["runtimeStatus"] == "deferred"


def test_rebuild_hole_and_salvage_crate_contracts_are_typed_with_graphs() -> None:
    lineage = _lineage("""
Object FixtureObject
  Behavior = RebuildHoleExposeDie ModuleTag_Expose
    ExemptStatus = SOLD UNDER_CONSTRUCTION
    HoleName = FixtureHole
    HoleMaxHealth = 1000
    FadeInTimeSeconds = 2.5
    TransferAttackers = No
  End
  Behavior = RebuildHoleBehavior ModuleTag_Rebuild
    WorkerObjectName = FixtureWorker
    WorkerRespawnDelay = 120000
    HoleHealthRegen%PerSecond = 0.5%
  End
  Behavior = SalvageCrateCollide ModuleTag_Salvage
    ForbiddenKindOf = PROJECTILE ENVIRONMENT
    PorterChance = 0%
    BannerChance = 0%
    LevelUpChance = 100%
    LevelUpRadius = 100.0
    ResourceChance = 0%
    MinResource = 0
    MaxResource = 0
    AllowAIPickup = No
    Upgrade = Upgrade_FixtureReward
  End
End
""")
    expose = compile_rebuild_hole_expose_dies(lineage, "FixtureObject")[0]
    rebuild = compile_rebuild_hole_behaviors(lineage, "FixtureObject")[0]
    salvage = compile_salvage_crate_collides(lineage, "FixtureObject")[0]
    assert expose["fields"]["HoleName"]["value"] == "FixtureHole"
    assert expose["lifecycleGraph"]["holeObjectId"] == "FixtureHole"
    assert rebuild["fields"]["HoleHealthRegen%PerSecond"]["ratio"] == 0.005
    assert rebuild["lifecycleGraph"]["workerObjectId"] == "FixtureWorker"
    assert salvage["fields"]["LevelUpChance"]["ratio"] == 1.0
    assert salvage["rewardGraph"]["levelUpChanceRatio"] == 1.0
    assert salvage["rewardGraph"]["upgradeId"] == "Upgrade_FixtureReward"
    assert all(row["runtimeStatus"] == "executable" for row in (expose, rebuild, salvage))
    all_rows = compile_all_module_contracts(lineage, "FixtureObject")
    assert {row["module"] for row in all_rows} == {
        "RebuildHoleExposeDie", "RebuildHoleBehavior", "SalvageCrateCollide"
    }

    transfer_true = _lineage("""
Object FixtureObject
  Behavior = RebuildHoleExposeDie ModuleTag_Expose
    ExemptStatus = SOLD
    HoleName = FixtureHole
    HoleMaxHealth = 500
    FadeInTimeSeconds = 2
    TransferAttackers = Yes
  End
End
""")
    assert compile_rebuild_hole_expose_dies(
        transfer_true, "FixtureObject"
    )[0]["runtimeStatus"] == "deferred"


@pytest.mark.parametrize("module, body, compiler", [
    ("RebuildHoleExposeDie", "ExemptStatus = SOLD\n    HoleName = Bad-Id\n    HoleMaxHealth = 1\n    FadeInTimeSeconds = 1", compile_rebuild_hole_expose_dies),
    ("RebuildHoleBehavior", "WorkerRespawnDelay = 120000\n    HoleHealthRegen%PerSecond = 2", compile_rebuild_hole_behaviors),
    ("SalvageCrateCollide", "ForbiddenKindOf = PROJECTILE", compile_salvage_crate_collides),
])
def test_rebuild_hole_and_salvage_crate_contracts_fail_closed(module, body, compiler) -> None:
    lineage = _lineage(f"Object FixtureObject\n  Behavior = {module} ModuleTag_Test\n    {body}\n  End\nEnd")
    with pytest.raises(ModuleContractError):
        compiler(lineage, "FixtureObject")


def test_rebuild_hole_and_salvage_exact_canonical_corpora() -> None:
    from openbfme_importer.catalog import InstallCatalog
    from openbfme_importer.module_census import census_catalog_paths, read_catalog_documents

    expected = {
        "bfme2-retail": {
            "expose": {4: 5, 5: 6},
            "rebuild": {2: 1, 3: 1},
            "salvage": {9: 12},
        },
        "rotwk-retail": {
            "expose": {4: 5, 5: 9},
            "rebuild": {2: 1, 3: 1},
            "salvage": {5: 1, 9: 12},
        },
    }
    actual: dict[str, dict[str, object]] = {}
    for label, path in census_catalog_paths().items():
        expose_rows: list[dict[str, object]] = []
        rebuild_rows: list[dict[str, object]] = []
        salvage_rows: list[dict[str, object]] = []
        for virtual_path, source in read_catalog_documents(InstallCatalog.load(path)):
            folded = source.lower()
            if not any(needle in folded for needle in (
                b"rebuildholeexposedie", b"rebuildholebehavior", b"salvagecratecollide"
            )):
                continue
            if not (
                virtual_path.casefold().startswith("data/ini/object/")
                or virtual_path.casefold() == "data/ini/crate.ini"
            ):
                continue
            for obj in parse_sage_document(source, virtual_path=virtual_path).objects:
                lineage = [obj]
                expose_rows.extend(compile_rebuild_hole_expose_dies(lineage, obj.name))
                rebuild_rows.extend(compile_rebuild_hole_behaviors(lineage, obj.name))
                salvage_rows.extend(compile_salvage_crate_collides(lineage, obj.name))
        rows = expose_rows + rebuild_rows + salvage_rows
        assert all(row["runtimeStatus"] == "executable" for row in rows)
        assert all(row["extraction"] == "typed" for row in rows)
        assert all(row["sourceIni"] and row["line"] > 0 for row in rows)
        actual[label] = {
            "expose": dict(sorted(Counter(len(row["fields"]) for row in expose_rows).items())),
            "rebuild": dict(sorted(Counter(len(row["fields"]) for row in rebuild_rows).items())),
            "salvage": dict(sorted(Counter(len(row["fields"]) for row in salvage_rows).items())),
        }
    assert actual == expected


def test_respawn_body_crush_foundation_and_monitor_contracts() -> None:
    lineage = _lineage("""
Object FixtureObject
  Body = RespawnBody ModuleTag_Body
    MaxHealth = FIXTURE_HEALTH
    DodgePercent = 80%
    CanRespawn = No
  End
  Behavior = NotifyTargetsOfImminentProbableCrushingUpdate ModuleTag_Crush
  End
  Behavior = FoundationAIUpdate ModuleTag_Foundation
    BuildVariation = 2
  End
  Behavior = MonitorConditionUpdate ModuleTag_Monitor
    WeaponSetFlags = WEAPONSET_TOGGLE_1
    WeaponToggleCommandSet = FixtureToggleCommandSet
  End
End
""")
    body = compile_respawn_bodies(lineage, "FixtureObject")[0]
    assert body["fields"]["MaxHealth"]["define"] == "FIXTURE_HEALTH"
    assert body["fields"]["DodgePercent"]["percent"] == 80
    assert body["fields"]["CanRespawn"]["value"] is False
    assert compile_notify_crushing_updates(lineage, "FixtureObject")[0]["fields"] == {}
    assert compile_foundation_ai_updates(lineage, "FixtureObject")[0]["fields"]["BuildVariation"]["value"] == 2
    monitor = compile_monitor_condition_updates(lineage, "FixtureObject")[0]
    assert monitor["fields"]["WeaponSetRoute"]["flags"]["value"] == ["WEAPONSET_TOGGLE_1"]


@pytest.mark.parametrize("module, body, compiler", [
    ("RespawnBody", "DodgePercent = 80%", compile_respawn_bodies),
    ("NotifyTargetsOfImminentProbableCrushingUpdate", "Invented = Yes", compile_notify_crushing_updates),
    ("FoundationAIUpdate", "BuildVariation = 0", compile_foundation_ai_updates),
    ("MonitorConditionUpdate", "WeaponSetFlags = MOUNTED", compile_monitor_condition_updates),
    ("MonitorConditionUpdate", "WeaponSetFlags = MOUNTED\n    ModelConditionCommandSet = EmptyCommandSet", compile_monitor_condition_updates),
    ("MonitorConditionUpdate", "ModelConditionFlags = USER_69\n    ModelConditionFlags = USER_70\n    WeaponToggleCommandSet = EmptyCommandSet", compile_monitor_condition_updates),
])
def test_respawn_body_crush_foundation_and_monitor_fail_closed(module, body, compiler) -> None:
    lineage = _lineage(f"Object FixtureObject\n  Behavior = {module} ModuleTag_Test\n    {body}\n  End\nEnd")
    with pytest.raises(ModuleContractError): compiler(lineage, "FixtureObject")


def test_upgrade_gate_portal_and_detector_contracts() -> None:
    lineage=_lineage("""
Object FixtureObject
  Behavior = AIGateUpdate ModuleTag_AI
    TriggerWidthX = 300
    TriggerWidthY = 150
  End
  Behavior = FakePathfindPortalBehaviour ModuleTag_Portal
    AllowEnemies = No
    AllowNonSkirmishAIUnits = No
  End
  Behavior = StealthDetectorUpdate ModuleTag_Detect
    DetectionRate = DETECT_RATE
    DetectionRange = 450
  End
End
""")
    assert compile_ai_gate_updates(lineage,"FixtureObject")[0]["fields"]["TriggerWidthX"]["value"]==300
    assert compile_fake_pathfind_portals(lineage,"FixtureObject")[0]["fields"]["AllowEnemies"]["value"] is False
    assert compile_stealth_detector_updates(lineage,"FixtureObject")[0]["fields"]["DetectionRate"]["expression"]=="DETECT_RATE"


def test_slaved_and_castle_upgrade_contracts_preserve_typed_semantics() -> None:
    lineage = _lineage("""
Object FixtureObject
  Behavior = SlavedUpdate ModuleTag_Slaved
    LeashRange = 250
    GuardMaxRange = 420.0
    GuardWanderRange = 15
    AttackRange = 100
    UseSlaverAsControlForEvaObjectSightedEvents = Yes
    DieOnMastersDeath = No
    MarkUnselectable = No
    GuardPositionOffset = X:-15 Y:0 Z:0
    FadeOutRange = 20
    FadeTime = 1000
  End
  Behavior = CastleUpgrade ModuleTag_Castle
    TriggeredBy = Upgrade_FixtureTrigger
    Upgrade = Upgrade_Fixture
    WallUpgradeRadius = FIXTURE_WALL_RADIUS
  End
End
""")
    slaved = compile_slaved_updates(lineage, "FixtureObject")[0]
    assert slaved["extraction"] == "typed"
    assert slaved["runtimeStatus"] == "executable"
    assert slaved["fields"]["LeashRange"]["value"] == 250
    assert slaved["fields"]["GuardPositionOffset"]["value"] == {
        "x": -15.0, "y": 0.0, "z": 0.0,
    }
    assert slaved["fields"]["FadeTime"]["milliseconds"] == 1000
    assert slaved["fields"]["DieOnMastersDeath"]["value"] is False
    castle = compile_castle_upgrades(lineage, "FixtureObject")[0]
    assert castle["fields"]["TriggeredBy"]["value"] == "Upgrade_FixtureTrigger"
    assert castle["fields"]["Upgrade"]["value"] == "Upgrade_Fixture"
    assert castle["fields"]["WallUpgradeRadius"]["expression"] == "FIXTURE_WALL_RADIUS"
    assert castle["fields"]["TriggeredBy"]["sourceIni"].endswith("fixture.ini")


@pytest.mark.parametrize(
    "module, body, compiler",
    [
        ("SlavedUpdate", "FadeTime = -1", compile_slaved_updates),
        ("SlavedUpdate", "GuardPositionOffset = X:1 Y:2", compile_slaved_updates),
        ("SlavedUpdate", "Invented = Yes", compile_slaved_updates),
        ("CastleUpgrade", "Upgrade = Upgrade_Fixture", compile_castle_upgrades),
        (
            "CastleUpgrade",
            "TriggeredBy = Upgrade_Trigger\n    Upgrade = Upgrade_Fixture\n    WallUpgradeRadius = -1",
            compile_castle_upgrades,
        ),
        (
            "CastleUpgrade",
            "TriggeredBy = Upgrade_Trigger\n    Upgrade = Upgrade_Fixture\n    Invented = Yes",
            compile_castle_upgrades,
        ),
    ],
)
def test_slaved_and_castle_upgrade_contracts_fail_closed(module, body, compiler) -> None:
    lineage = _lineage(
        f"Object FixtureObject\n  Behavior = {module} ModuleTag_Test\n"
        f"    {body}\n  End\nEnd"
    )
    with pytest.raises(ModuleContractError):
        compiler(lineage, "FixtureObject")


def test_delayed_death_body_contract_preserves_health_timers_and_policy() -> None:
    lineage = _lineage("""
Object FixtureObject
  Body = DelayedDeathBody ModuleTag_Delayed
    MaxHealth = FIXTURE_HEALTH
    MaxHealthDamaged = 250
    MaxHealthReallyDamaged = 100
    DelayedDeathTime = 5000
    RecoveryTime = 1700
    CanRespawn = No
    DoHealthCheck = No
    CheerRadius = EMOTION_CHEER_RADIUS
    ImmortalUntilDeathTime = Yes
    BurningDeathBehavior = Yes
    BurningDeathFX = FX_FixtureBurning
    DodgePercent = 15
  End
End
""")
    row = compile_delayed_death_bodies(lineage, "FixtureObject")[0]
    assert row["carrier"].casefold() == "body"
    assert row["runtimeStatus"] == "deferred"
    assert row["fields"]["MaxHealth"]["expression"] == "FIXTURE_HEALTH"
    assert row["fields"]["DelayedDeathTime"]["milliseconds"] == 5000
    assert row["fields"]["RecoveryTime"]["milliseconds"] == 1700
    assert row["fields"]["ImmortalUntilDeathTime"]["value"] is True
    assert row["fields"]["DodgePercent"]["ratio"] == 0.15
    assert row["fields"]["BurningDeathFX"]["sourceIni"].endswith("fixture.ini")


@pytest.mark.parametrize("body", [
    "DelayedDeathTime = 5000\n    CanRespawn = No",
    "MaxHealth = 100\n    CanRespawn = No",
    "MaxHealth = 100\n    DelayedDeathTime = -1\n    CanRespawn = No",
    "MaxHealth = 100\n    DelayedDeathTime = 5000\n    CanRespawn = Maybe",
    "MaxHealth = 100\n    DelayedDeathTime = 5000\n    CanRespawn = No\n    DodgePercent = 101",
    "MaxHealth = 100\n    DelayedDeathTime = 5000\n    CanRespawn = No\n    Invented = Yes",
])
def test_delayed_death_body_contract_fails_closed(body: str) -> None:
    lineage = _lineage(
        "Object FixtureObject\n  Body = DelayedDeathBody ModuleTag_Test\n"
        f"    {body}\n  End\nEnd"
    )
    with pytest.raises(ModuleContractError):
        compile_delayed_death_bodies(lineage, "FixtureObject")


def test_dynamic_portal_contract_preserves_graph_and_activation() -> None:
    lineage = _lineage("""
Object FixtureObject
  Behavior = DynamicPortalBehaviour ModuleTag_Portal
    ActivationDelaySeconds = 7.0
    GenerateNow = Yes
    ObjectFilter = ANY +INFANTRY -MONSTER
    BonePrefix = Post
    NumberOfBones = 4
    WayPoint = Index:0 Type:Walk
    WayPoint = Index:1 Type:Walk
    Link = From:0 Via:4 Via:5 To:1
    TriggeredBy = Upgrade_PosternGate
    ConflictsWith = Upgrade_OpenGarrison Upgrade_WallBanner
    CustomAnimAndDuration = AnimState:UPGRADE_POSTERN_GATE AnimTime:0
    TopAttackPos = X:30 Y:0 Z:52
    TopAttackRadius = 30
  End
End
""")
    row = compile_dynamic_portal_behaviours(lineage, "FixtureObject")[0]
    assert row["runtimeStatus"] == "deferred"
    assert row["fields"]["ActivationDelaySeconds"]["milliseconds"] == 7000
    assert row["fields"]["WayPoint"][1]["index"] == 1
    assert row["fields"]["Link"][0]["via"] == [4, 5]
    assert row["fields"]["ObjectFilter"]["value"] == ["ANY", "+INFANTRY", "-MONSTER"]
    assert row["fields"]["CustomAnimAndDuration"]["animTimeMilliseconds"] == 0


@pytest.mark.parametrize("body", [
    "ObjectFilter = ANY\n    BonePrefix = Post\n    NumberOfBones = 4\n    WayPoint = Index:0 Type:Walk",
    "ObjectFilter = ANY\n    BonePrefix = Post\n    NumberOfBones = 0\n    WayPoint = Index:0 Type:Walk\n    Link = From:0 To:1",
    "ObjectFilter = ANY\n    BonePrefix = Post\n    NumberOfBones = 4\n    WayPoint = Bad\n    Link = From:0 To:1",
    "ObjectFilter = ANY\n    BonePrefix = Post\n    NumberOfBones = 4\n    WayPoint = Index:0 Type:Walk\n    Link = Via:0 To:1",
    "ObjectFilter = ANY\n    BonePrefix = Post\n    NumberOfBones = 4\n    WayPoint = Index:0 Type:Walk\n    Link = From:0 To:1\n    Invented = Yes",
])
def test_dynamic_portal_contract_fails_closed(body: str) -> None:
    lineage = _lineage(
        "Object FixtureObject\n  Behavior = DynamicPortalBehaviour ModuleTag_Test\n"
        f"    {body}\n  End\nEnd"
    )
    with pytest.raises(ModuleContractError):
        compile_dynamic_portal_behaviours(lineage, "FixtureObject")


def test_flammable_contract_preserves_damage_timers_formula_and_fx() -> None:
    lineage = _lineage("""
Object FixtureObject
  Behavior = FlammableUpdate ModuleTag_Fire
    AflameDuration = 7000
    AflameDamageAmount = FIXTURE_FIRE_DAMAGE
    AflameDamageDelay = 500
    FlameDamageLimit = #MULTIPLY( FIXTURE_HEALTH FIXTURE_THRESHOLD )
    FlameDamageExpiration = 1000
    BurnedDelay = 2500
    BurnContained = Yes
    SetBurnedStatus = No
    DamageType = FORCE
    FireFXList = FX:FX_FireStartWoosh
    FireFXList = FX:FX_ForgeSmoke BONE:FireSmall01
    BurningSoundName = GenericFireMediumLoop
  End
End
""")
    row = compile_flammable_updates(lineage, "FixtureObject")[0]
    assert row["runtimeStatus"] == "deferred"
    assert row["fields"]["AflameDuration"]["milliseconds"] == 7000
    assert row["fields"]["AflameDamageAmount"]["define"] == "FIXTURE_FIRE_DAMAGE"
    assert row["fields"]["FlameDamageLimit"]["operands"] == [
        "FIXTURE_HEALTH", "FIXTURE_THRESHOLD",
    ]
    assert row["fields"]["FireFXList"][1]["bone"] == "FireSmall01"
    assert row["fields"]["SetBurnedStatus"]["value"] is False


@pytest.mark.parametrize("body", [
    "AflameDuration = -1",
    "FlameDamageLimit = #ADD( HEALTH THRESHOLD )",
    "BurnContained = Maybe",
    "FireFXList = FX_Fixture",
    "DamageType = FORCE FIRE",
    "Invented = Yes",
])
def test_flammable_contract_fails_closed(body: str) -> None:
    lineage = _lineage(
        "Object FixtureObject\n  Behavior = FlammableUpdate ModuleTag_Test\n"
        f"    {body}\n  End\nEnd"
    )
    with pytest.raises(ModuleContractError):
        compile_flammable_updates(lineage, "FixtureObject")


def test_spawn_and_stealth_contracts_preserve_lifecycle_and_reveal_policy() -> None:
    lineage = _lineage("""
Object FixtureObject
  Behavior = SpawnBehavior ModuleTag_Spawn
    SpawnNumber = 4
    InitialBurst = 3
    SpawnReplaceDelay = 3000
    SpawnTemplateName = FixtureA_Slaved FixtureB_Slaved
    CanReclaimOrphans = Yes
    SpawnedRequireSpawner = Yes
    ShareUpgrades = Yes
    FadeInTime = 1000
    TriggeredBy = Upgrade_Fixture
  End
  Behavior = StealthUpdate ModuleTag_Stealth
    StealthDelay = 500
    FriendlyOpacityMin = 10.0%
    FriendlyOpacityMax = STEALTH_FRIENDLY_OPACITY_MAX
    PulseFrequency = 750
    InnateStealth = Yes
    OrderIdleEnemiesToAttackMeUponReveal = Yes
    StealthForbiddenConditions = ATTACKING MOVING TAKING_DAMAGE
    RevealWeaponSets = CLOSE_RANGE CONTESTING_BUILDING
    DetectedByAnyoneRange = 120
    DisguiseTransitionTime = 2000
    DisguiseRevealTransitionTime = 1000
    RevealDistanceFromTarget = 100.0f
  End
End
""")
    spawn = compile_spawn_behaviors(lineage, "FixtureObject")[0]
    assert spawn["fields"]["SpawnNumber"]["value"] == 4
    assert spawn["fields"]["SpawnReplaceDelay"]["milliseconds"] == 3000
    assert spawn["fields"]["SpawnTemplateName"]["value"] == [
        "FixtureA_Slaved", "FixtureB_Slaved",
    ]
    assert spawn["fields"]["ShareUpgrades"]["value"] is True
    assert spawn["fields"]["CanReclaimOrphans"] == {
        "authored": "Yes",
        "value": True,
        "sourceIni": "data/ini/object/fixture.ini",
        "line": 8,
        "runtimeStatus": "executable",
    }
    assert spawn["runtimeStatus"] == "deferred"
    stealth = compile_stealth_updates(lineage, "FixtureObject")[0]
    assert stealth["fields"]["FriendlyOpacityMin"]["ratio"] == 0.1
    assert stealth["fields"]["FriendlyOpacityMax"]["define"] == "STEALTH_FRIENDLY_OPACITY_MAX"
    assert stealth["fields"]["StealthForbiddenConditions"]["value"] == [
        "ATTACKING", "MOVING", "TAKING_DAMAGE",
    ]
    assert stealth["fields"]["RevealDistanceFromTarget"]["value"] == 100.0
    assert stealth["fields"]["DisguiseTransitionTime"]["sourceIni"].endswith("fixture.ini")


def test_spawn_reclaim_canonical_shape_is_row_executable() -> None:
    lineage = _lineage("""
Object FixtureObject
  Behavior = SpawnBehavior ModuleTag_Spawn
    SpawnNumber = 8
    InitialBurst = 8
    SpawnReplaceDelay = 60000
    SpawnTemplateName = FixtureSword_Slaved FixtureArcher_Slaved
    CanReclaimOrphans = Yes
  End
End
""")
    row = compile_spawn_behaviors(lineage, "FixtureObject")[0]
    assert row["fields"]["CanReclaimOrphans"]["runtimeStatus"] == "executable"
    assert row["runtimeStatus"] == "executable"
    validate_module_contracts([row], label="canonical reclaim fixture")


@pytest.mark.parametrize("extra", [
    "FadeInTime = 1000",
    "KillSpawnsBasedOnModelConditionState = Yes",
    "SpawnInsideBuilding = Yes",
    "RespectCommandLimit = Yes",
    "SpawnedRequireSpawner = Yes",
    "ShareUpgrades = Yes",
    "OneShot = Yes",
    "TriggeredBy = Upgrade_Fixture",
])
def test_spawn_reclaim_noncanonical_optional_fields_remain_row_deferred(extra: str) -> None:
    lineage = _lineage(
        "Object FixtureObject\n  Behavior = SpawnBehavior ModuleTag_Spawn\n"
        "    SpawnNumber = 1\n    InitialBurst = 1\n"
        "    SpawnReplaceDelay = 1000\n    SpawnTemplateName = Fixture_Slaved\n"
        f"    CanReclaimOrphans = Yes\n    {extra}\n  End\nEnd"
    )
    row = compile_spawn_behaviors(lineage, "FixtureObject")[0]
    assert row["runtimeStatus"] == "deferred"


def test_spawn_reclaim_executable_row_tamper_fails_closed() -> None:
    lineage = _lineage("""
Object FixtureObject
  Behavior = SpawnBehavior ModuleTag_Spawn
    SpawnNumber = 1
    InitialBurst = 1
    SpawnReplaceDelay = 1000
    SpawnTemplateName = Fixture_Slaved
    CanReclaimOrphans = Yes
  End
End
""")
    row = compile_spawn_behaviors(lineage, "FixtureObject")[0]
    row["runtimeStatus"] = "executable"
    row["fields"]["FadeInTime"] = {
        "authored": "1000", "milliseconds": 1000,
        "sourceIni": "data/ini/object/fixture.ini", "line": 9,
    }
    with pytest.raises(ModuleContractError, match="lacks closed runtime evidence"):
        validate_module_contracts([row], label="tampered reclaim fixture")


@pytest.mark.parametrize("module, body, compiler", [
    ("SpawnBehavior", "SpawnNumber = 1\n    SpawnReplaceDelay = 1000", compile_spawn_behaviors),
    ("SpawnBehavior", "SpawnNumber = 1\n    SpawnReplaceDelay = 1000\n    SpawnTemplateName = Unit\n    InitialBurst = 2", compile_spawn_behaviors),
    ("SpawnBehavior", "SpawnNumber = 1\n    SpawnReplaceDelay = -1\n    SpawnTemplateName = Unit", compile_spawn_behaviors),
    ("StealthUpdate", "FriendlyOpacityMin = 10%", compile_stealth_updates),
    ("StealthUpdate", "FriendlyOpacityMin = 101%\n    FriendlyOpacityMax = 60%", compile_stealth_updates),
    ("StealthUpdate", "DisguiseTransitionTime = 1000", compile_stealth_updates),
    ("StealthUpdate", "RevealDistanceFromTarget = far", compile_stealth_updates),
    ("StealthUpdate", "Invented = Yes", compile_stealth_updates),
])
def test_spawn_and_stealth_contracts_fail_closed(module, body, compiler) -> None:
    lineage = _lineage(
        f"Object FixtureObject\n  Behavior = {module} ModuleTag_Test\n"
        f"    {body}\n  End\nEnd"
    )
    with pytest.raises(ModuleContractError):
        compiler(lineage, "FixtureObject")


def test_object_creation_and_ocl_contracts_preserve_effects_and_timers() -> None:
    lineage = _lineage("""
Object FixtureObject
  Behavior = ObjectCreationUpgrade ModuleTag_Create
    TriggeredBy = Upgrade_First Upgrade_Second
    RequiresAllTriggers = Yes
    Delay = FIXTURE_CREATE_DELAY
    ThingToSpawn = FixtureSpawnedObject
    Offset = X:12 Y:-0.5 Z:48.2
    FadeInTime = 600
    GrantUpgrade = Upgrade_Ready
    RemoveUpgrade = Upgrade_ButtonEnable
    ConflictsWith = Upgrade_OpenGarrison Upgrade_PosternGate
    DestroyWhenSold = Yes
    DeathAnimAndDuration = AnimState:DEATH_2 AnimTime:999999
  End
  Behavior = OCLUpdate ModuleTag_OCL
    OCL = OCL_Fixture
    MinDelay = 1500
    MaxDelay = 1500
    Amount = 1
  End
End
""")
    creation = compile_object_creation_upgrades(lineage, "FixtureObject")[0]
    assert creation["fields"]["TriggeredBy"]["value"] == [
        "Upgrade_First", "Upgrade_Second",
    ]
    assert creation["fields"]["Delay"]["define"] == "FIXTURE_CREATE_DELAY"
    assert creation["fields"]["Offset"]["value"] == {
        "x": 12.0, "y": -0.5, "z": 48.2,
    }
    assert creation["fields"]["DeathAnimAndDuration"]["animTimeMilliseconds"] == 999999
    assert creation["fields"]["RequiresAllTriggers"]["value"] is True
    ocl = compile_ocl_updates(lineage, "FixtureObject")[0]
    assert ocl["fields"]["OCL"]["value"] == "OCL_Fixture"
    assert ocl["fields"]["MinDelay"]["milliseconds"] == 1500
    assert ocl["fields"]["Amount"]["value"] == 1
    assert ocl["fields"]["OCL"]["sourceIni"].endswith("fixture.ini")


@pytest.mark.parametrize("module, body, compiler", [
    ("ObjectCreationUpgrade", "ThingToSpawn = Fixture", compile_object_creation_upgrades),
    ("ObjectCreationUpgrade", "TriggeredBy = Upgrade_X\n    Delay = -1\n    ThingToSpawn = Fixture", compile_object_creation_upgrades),
    ("ObjectCreationUpgrade", "TriggeredBy = Upgrade_X", compile_object_creation_upgrades),
    ("ObjectCreationUpgrade", "TriggeredBy = Upgrade_X\n    ThingToSpawn = Fixture\n    Offset = X:0 Y:0", compile_object_creation_upgrades),
    ("OCLUpdate", "OCL = OCL_X\n    MinDelay = 2000\n    MaxDelay = 1000\n    Amount = 1", compile_ocl_updates),
    ("OCLUpdate", "OCL = OCL_X\n    MinDelay = 1000\n    MaxDelay = 1000\n    Amount = 0", compile_ocl_updates),
    ("OCLUpdate", "OCL = OCL_X\n    MinDelay = 1000\n    MaxDelay = 1000\n    Amount = 1\n    Invented = Yes", compile_ocl_updates),
])
def test_object_creation_and_ocl_contracts_fail_closed(module, body, compiler) -> None:
    lineage = _lineage(
        f"Object FixtureObject\n  Behavior = {module} ModuleTag_Test\n"
        f"    {body}\n  End\nEnd"
    )
    with pytest.raises(ModuleContractError):
        compiler(lineage, "FixtureObject")


def test_container_family_contracts_preserve_capacity_admission_and_routes() -> None:
    lineage = _lineage("""
Object FixtureObject
  Behavior = TransportContain ModuleTag_Transport
    ObjectStatusOfContained = UNSELECTABLE CAN_ATTACK
    PassengerFilter = ANY +INFANTRY -HERO
    Slots = 2
    ShowPips = No
    AllowEnemiesInside = No
    AllowNeutralInside = No
    AllowAlliesInside = Yes
    DamagePercentToUnits = 100%
    PassengerBonePrefix = PassengerBone:PASSENGER KindOf:INFANTRY
    BoneSpecificConditionState = 1 PASSENGER_VARIATION_1
    UpgradeCreationTrigger = Upgrade_Riders FixtureRider 2
    ExitDelay = 250
  End
  Behavior = TunnelContain ModuleTag_Tunnel
    ObjectStatusOfContained = UNSELECTABLE ENCLOSED
    ContainMax = 5
    DamagePercentToUnits = 0%
    PassengerFilter = ANY +INFANTRY
    AllowEnemiesInside = No
    AllowNeutralInside = No
    NumberOfExitPaths = 1
    PassengerBonePrefix = PassengerBone:ARROW_ KindOf:INFANTRY
    EntryPosition = X:0 Y:0 Z:0
    EntryOffset = X:50 Y:0 Z:0
    ExitOffset = X:100 Y:0 Z:0
    EnterSound = RuinedTowerEnterSound
    KillPassengersOnDeath = No
    ShowPips = No
  End
  Behavior = GarrisonContain ModuleTag_Garrison
    ObjectStatusOfContained = UNSELECTABLE CAN_ATTACK
    ContainMax = 10
    PassengerFilter = ANY +INFANTRY +HORDE
    AllowAlliesInside = Yes
    AllowEnemiesInside = No
  End
  Behavior = HordeGarrisonContain ModuleTag_HordeGarrison
    ObjectStatusOfContained = UNSELECTABLE CAN_ATTACK ENCLOSED
    ContainMax = 3
    DamagePercentToUnits = 0%
    PassengerFilter = GENERIC_FACTION_GARRISONABLE
    AllowEnemiesInside = No
    EntryPosition = X:0 Y:0 Z:0
    EntryOffset = X:50 Y:0 Z:0
    ExitOffset = X:50 Y:0 Z:0
  End
End
""")
    transport = compile_transport_contains(lineage, "FixtureObject")[0]
    assert transport["fields"]["Slots"]["value"] == 2
    assert transport["fields"]["DamagePercentToUnits"]["ratio"] == 1.0
    assert transport["fields"]["PassengerBonePrefix"][0]["passengerBone"] == "PASSENGER"
    assert transport["fields"]["UpgradeCreationTrigger"][0]["count"] == 2
    tunnel = compile_tunnel_contains(lineage, "FixtureObject")[0]
    assert tunnel["fields"]["ContainMax"]["value"] == 5
    assert tunnel["fields"]["ExitOffset"]["value"]["x"] == 100.0
    assert compile_garrison_contains(lineage, "FixtureObject")[0]["fields"]["ContainMax"]["value"] == 10
    horde = compile_horde_garrison_contains(lineage, "FixtureObject")[0]
    assert horde["fields"]["PassengerFilter"]["value"] == ["GENERIC_FACTION_GARRISONABLE"]
    assert horde["fields"]["EntryPosition"]["sourceIni"].endswith("fixture.ini")


@pytest.mark.parametrize("module, body, compiler", [
    ("TransportContain", "Slots = 1", compile_transport_contains),
    ("TransportContain", "ObjectStatusOfContained = UNSELECTABLE\n    PassengerFilter = ANY\n    Slots = 0\n    ShowPips = No\n    AllowEnemiesInside = No\n    AllowNeutralInside = No\n    AllowAlliesInside = Yes", compile_transport_contains),
    ("TunnelContain", "ObjectStatusOfContained = UNSELECTABLE\n    ContainMax = 5", compile_tunnel_contains),
    ("GarrisonContain", "ObjectStatusOfContained = UNSELECTABLE\n    ContainMax = 10\n    PassengerFilter = ANY\n    AllowAlliesInside = True\n    AllowEnemiesInside = No", compile_garrison_contains),
    ("HordeGarrisonContain", "ObjectStatusOfContained = UNSELECTABLE\n    ContainMax = 1\n    DamagePercentToUnits = 0%\n    PassengerFilter = ANY\n    AllowEnemiesInside = No\n    EntryPosition = X:0 Y:0 Z:0\n    EntryOffset = X:0 Y:0 Z:0\n    ExitOffset = X:0 Y:0", compile_horde_garrison_contains),
])
def test_container_family_contracts_fail_closed(module, body, compiler) -> None:
    lineage = _lineage(
        f"Object FixtureObject\n  Behavior = {module} ModuleTag_Test\n"
        f"    {body}\n  End\nEnd"
    )
    with pytest.raises(ModuleContractError):
        compiler(lineage, "FixtureObject")


def test_bonus_production_queue_and_siege_contracts_preserve_typed_rows() -> None:
    lineage = _lineage("""
Object FixtureObject
  Behavior = LargeGroupBonusUpdate ModuleTag_LargeGroup
    UpdateRate = 1000
    HordeMemberFilter = NONE +MordorFighter +MordorArcher
    Count = 100
    Radius = 160.0
    RubOffRadius = 160.0
    AlliesOnly = Yes
    AttributeModifier = MordorLargeGroupBonus
  End
  Behavior = ProductionQueueHordeContain ModuleTag_Queue
    ObjectStatusOfContained = UNSELECTABLE ENCLOSED
    ContainMax = 5
    DamagePercentToUnits = 0%
    PassengerFilter = ANY +INFANTRY +BANNER
    AllowEnemiesInside = No
    AllowNeutralInside = No
    AllowAlliesInside = Yes
    NumberOfExitPaths = 1
    EntryPosition = X:0 Y:0 Z:0
    EntryOffset = X:0 Y:45 Z:0
    ExitOffset = X:0 Y:-45 Z:0
    EnterSound = RuinedTowerEnterSound
  End
  Behavior = SiegeEngineContain ModuleTag_Siege
    ObjectStatusOfCrew = UNSELECTABLE UNATTACKABLE
    Slots = 1
    DamagePercentToUnits = 100%
    PassengerFilter = NONE +CAN_RIDE_BATTERING_RAM
    KillPassengersOnDeath = Yes
    AllowAlliesInside = Yes
    AllowEnemiesInside = No
    AllowNeutralInside = No
    CrewFilter = NONE +INFANTRY -CAN_RIDE_BATTERING_RAM
    CrewMax = 6
    InitialCrew = IsengardRamCrew 6
    ExitDelay = 500
    NumberOfExitPaths = 0
    GoAggressiveOnExit = Yes
    PassengerBonePrefix = PassengerBone:CREWBONE KindOf:INFANTRY
    BoneSpecificConditionState = 1 PASSENGER_VARIATION_1
    BoneSpecificConditionState = 2 PASSENGER_VARIATION_2
  End
End
""")
    bonus = compile_large_group_bonus_updates(lineage, "FixtureObject")[0]
    assert bonus["fields"]["UpdateRate"]["milliseconds"] == 1000
    assert bonus["fields"]["HordeMemberFilter"]["value"] == [
        "NONE", "+MordorFighter", "+MordorArcher",
    ]
    queue = compile_production_queue_horde_contains(lineage, "FixtureObject")[0]
    assert queue["fields"]["ContainMax"]["value"] == 5
    siege = compile_siege_engine_contains(lineage, "FixtureObject")[0]
    assert siege["fields"]["InitialCrew"]["count"] == 6
    assert len(siege["fields"]["BoneSpecificConditionState"]) == 2
    assert siege["fields"]["PassengerBonePrefix"][0]["sourceIni"].endswith("fixture.ini")


@pytest.mark.parametrize("module, body, compiler", [
    ("LargeGroupBonusUpdate", "UpdateRate = 1000", compile_large_group_bonus_updates),
    ("LargeGroupBonusUpdate", "UpdateRate = 1000\n    HordeMemberFilter = NONE +INFANTRY\n    Count = 0\n    Radius = 10\n    RubOffRadius = 10\n    AlliesOnly = Yes\n    AttributeModifier = Bonus", compile_large_group_bonus_updates),
    ("ProductionQueueHordeContain", "ContainMax = 5", compile_production_queue_horde_contains),
    ("SiegeEngineContain", "Slots = 1", compile_siege_engine_contains),
    ("SiegeEngineContain", "ObjectStatusOfCrew = UNSELECTABLE\n    Slots = 1\n    DamagePercentToUnits = 100%\n    PassengerFilter = NONE\n    AllowAlliesInside = Yes\n    AllowEnemiesInside = No\n    AllowNeutralInside = No\n    ExitDelay = 0\n    NumberOfExitPaths = 0\n    GoAggressiveOnExit = Yes", compile_siege_engine_contains),
])
def test_bonus_production_queue_and_siege_contracts_fail_closed(module, body, compiler) -> None:
    lineage = _lineage(
        f"Object FixtureObject\n  Behavior = {module} ModuleTag_Test\n"
        f"    {body}\n  End\nEnd"
    )
    with pytest.raises(ModuleContractError):
        compiler(lineage, "FixtureObject")


def test_audio_hit_animal_and_threat_contracts_are_typed() -> None:
    lineage = _lineage(
        """
Object FixtureObject
  Behavior = LargeGroupAudioUpdate ModuleTag_Audio
    Key = Human Unit Infantry
    UnitWeight = 2
  End
  Behavior = HitReactionBehavior ModuleTag_Hit
    HitReactionLifeTimer1 = 1000
    HitReactionLifeTimer2 = 2000
    HitReactionLifeTimer3 = 3000
    HitReactionThreshold1 = 5.0
    HitReactionThreshold2 = 25.0
    HitReactionThreshold3 = 50.0
    FastHitsResetReaction = Yes
  End
  Behavior = AnimalAIUpdate ModuleTag_Animal
    FleeRange = 100
    FleeDistance = 800
    WanderPercentage = 5
    MaxWanderDistance = 50
    MaxWanderRadius = 200
    UpdateTimer = 10000
  End
  Behavior = ThreatFinderUpdate ModuleTag_Threat
    DefaultRadius = 100.0f
  End
End
"""
    )
    audio = compile_large_group_audio_updates(lineage, "FixtureObject")[0]
    assert audio["fields"]["Key"]["value"] == ["Human", "Unit", "Infantry"]
    assert audio["fields"]["UnitWeight"]["value"] == 2
    hit = compile_hit_reaction_behaviors(lineage, "FixtureObject")[0]
    assert hit["fields"]["HitReactionThreshold3"]["value"] == 50.0
    assert hit["fields"]["FastHitsResetReaction"]["value"] is True
    animal = compile_animal_ai_updates(lineage, "FixtureObject")[0]
    assert animal["fields"]["UpdateTimer"]["milliseconds"] == 10000
    threat = compile_threat_finder_updates(lineage, "FixtureObject")[0]
    assert threat["fields"]["DefaultRadius"]["value"] == 100.0
    assert threat["fields"]["DefaultRadius"]["suffix"] == "f"
    modules = {row["module"] for row in compile_all_module_contracts(lineage, "FixtureObject")}
    assert modules == {
        "LargeGroupAudioUpdate", "HitReactionBehavior", "AnimalAIUpdate",
        "ThreatFinderUpdate",
    }


@pytest.mark.parametrize(
    ("kind", "body", "compiler"),
    (
        ("LargeGroupAudioUpdate", "Key = Human Unit\n    UnitWeight = 0", compile_large_group_audio_updates),
        ("HitReactionBehavior", "HitReactionLifeTimer1 = 1\n    HitReactionThreshold1 = 1\n    HitReactionLifeTimer2 = 2", compile_hit_reaction_behaviors),
        ("AnimalAIUpdate", "FleeRange = 1\n    WanderPercentage = 101\n    MaxWanderDistance = 1\n    MaxWanderRadius = 1", compile_animal_ai_updates),
        ("ThreatFinderUpdate", "DefaultRadius = 100parsecs", compile_threat_finder_updates),
    ),
)
def test_audio_hit_animal_and_threat_contracts_fail_closed(kind, body, compiler) -> None:
    lineage = _lineage(
        f"Object FixtureObject\n  Behavior = {kind} ModuleTag_Test\n"
        f"    {body}\n  End\nEnd"
    )
    with pytest.raises(ModuleContractError):
        compiler(lineage, "FixtureObject")


def test_sound_fear_poison_damage_and_spawn_contracts_are_typed() -> None:
    lineage = _lineage(
        """
Object FixtureObject
  ClientBehavior = ModelConditionSoundSelectorClientBehavior ModuleTag_ModelSound
    SoundState = MOUNTED
      VoiceMove = FixtureVoiceMove
      VoiceSelect = FixtureVoiceSelect
      VoicePriority = 10
    End
  End
  ClientBehavior = RandomSoundSelectorClientBehavior ModuleTag_RandomSound
    Chance = 5%
    RerollOnEveryFrame = No
    VoicePriority = 32
  End
  Behavior = RadiateFearUpdate ModuleTag_Fear
    InitiallyActive = No
    TriggeredBy = Upgrade_FixtureFear
    WhichSpecialPower = 1
    GenerateFear = Yes
    EmotionPulseRadius = FIXTURE_FEAR_RADIUS
    EmotionPulseInterval = 1000
    VictimFilter = ALL ENEMIES
  End
  Behavior = PoisonedBehavior ModuleTag_Poison
    PoisonDamageInterval = 1000
    PoisonDuration = 30000
  End
  Behavior = DamageFieldUpdate ModuleTag_Damage
    Radius = 100
    ObjectFilter = ALL ENEMIES
    RequiredUpgrade = Upgrade_FixtureDamage
    FireWeaponNugget
      WeaponName = FixtureDamageWeapon
      FireDelay = 0
      OneShot = No
    End
  End
  Behavior = SpawnUnitBehavior ModuleTag_SpawnUnit
    UnitName = FixtureSpawnedUnit
    UnitCommand = Command_ConstructFixture
    SpawnOnce = Yes
  End
End
"""
    )
    selector = compile_model_condition_sound_selectors(lineage, "FixtureObject")[0]
    assert selector["fields"]["SoundState"][0]["conditions"] == ["MOUNTED"]
    assert selector["fields"]["SoundState"][0]["sounds"]["VoicePriority"]["value"] == 10
    random = compile_random_sound_selectors(lineage, "FixtureObject")[0]
    assert random["fields"]["Chance"]["ratio"] == 0.05
    fear = compile_radiate_fear_updates(lineage, "FixtureObject")[0]
    assert fear["fields"]["EmotionPulseRadius"]["define"] == "FIXTURE_FEAR_RADIUS"
    assert fear["fields"]["VictimFilter"]["value"] == ["ALL", "ENEMIES"]
    poison = compile_poisoned_behaviors(lineage, "FixtureObject")[0]
    assert poison["fields"]["PoisonDuration"]["milliseconds"] == 30000
    damage = compile_damage_field_updates(lineage, "FixtureObject")[0]
    assert damage["fields"]["FireWeaponNugget"]["WeaponName"]["value"] == "FixtureDamageWeapon"
    spawned = compile_spawn_unit_behaviors(lineage, "FixtureObject")[0]
    assert spawned["fields"]["SpawnOnce"]["value"] is True
    assert {row["module"] for row in compile_all_module_contracts(lineage, "FixtureObject")} == {
        "ModelConditionSoundSelectorClientBehavior", "RandomSoundSelectorClientBehavior",
        "RadiateFearUpdate", "PoisonedBehavior", "DamageFieldUpdate", "SpawnUnitBehavior",
    }


@pytest.mark.parametrize("kind,body,compiler", [
    ("ModelConditionSoundSelectorClientBehavior", "VoiceMove = NotNested", compile_model_condition_sound_selectors),
    ("RandomSoundSelectorClientBehavior", "Chance = 101%\n    RerollOnEveryFrame = No\n    VoicePriority = 1", compile_random_sound_selectors),
    ("RadiateFearUpdate", "InitiallyActive = Yes\n    EmotionPulseRadius = 100\n    EmotionPulseInterval = 1000", compile_radiate_fear_updates),
    ("PoisonedBehavior", "PoisonDamageInterval = 1000", compile_poisoned_behaviors),
    ("DamageFieldUpdate", "Radius = 100\n    ObjectFilter = ALL ENEMIES\n    RequiredUpgrade = Upgrade_X", compile_damage_field_updates),
    ("SpawnUnitBehavior", "UnitName = Fixture\n    SpawnOnce = Maybe", compile_spawn_unit_behaviors),
])
def test_sound_fear_poison_damage_and_spawn_contracts_fail_closed(kind, body, compiler) -> None:
    carrier = "ClientBehavior" if "SoundSelectorClientBehavior" in kind else "Behavior"
    lineage = _lineage(
        f"Object FixtureObject\n  {carrier} = {kind} ModuleTag_Test\n"
        f"    {body}\n  End\nEnd"
    )
    with pytest.raises(ModuleContractError):
        compiler(lineage, "FixtureObject")


def test_replace_self_upgrade_compiles_replacement_upgrade_mux_and_additions() -> None:
    lineage = _lineage(
        """
Object FixtureObject
  Behavior = ReplaceSelfUpgrade ModuleTag_Replace
    ReplaceWith = FixtureWallHub
    AndThenAddA = FixtureWallHub
    AndThenAddA = FixtureWallSegment
    TriggeredBy = Upgrade_FixtureWallHub
    ConflictsWith = Upgrade_FixtureGate Upgrade_FixtureTower
  End
End
"""
    )
    row = compile_replace_self_upgrades(lineage, "FixtureObject")[0]
    assert row["fields"]["ReplaceWith"]["value"] == "FixtureWallHub"
    assert row["fields"]["TriggeredBy"]["value"] == "Upgrade_FixtureWallHub"
    assert row["fields"]["ConflictsWith"]["value"] == [
        "Upgrade_FixtureGate", "Upgrade_FixtureTower",
    ]
    assert [item["value"] for item in row["fields"]["AndThenAddA"]] == [
        "FixtureWallHub", "FixtureWallSegment",
    ]
    assert all(item["sourceIni"].endswith("fixture.ini") for item in row["fields"]["AndThenAddA"])
    assert {item["module"] for item in compile_all_module_contracts(lineage, "FixtureObject")} == {
        "ReplaceSelfUpgrade",
    }


@pytest.mark.parametrize("body", [
    "ReplaceWith = Fixture\n    TriggeredBy = Upgrade_X",
    "ReplaceWith = Fixture\n    TriggeredBy = Upgrade_X\n    ConflictsWith = Upgrade_Y\n    AndThenAddA = OnlyOne",
    "ReplaceWith = Fixture\n    TriggeredBy = Upgrade_X\n    ConflictsWith = Upgrade_Y\n    Invented = Nope",
])
def test_replace_self_upgrade_fails_closed(body: str) -> None:
    lineage = _lineage(
        "Object FixtureObject\n  Behavior = ReplaceSelfUpgrade ModuleTag_Test\n"
        f"    {body}\n  End\nEnd"
    )
    with pytest.raises(ModuleContractError):
        compile_replace_self_upgrades(lineage, "FixtureObject")


def test_citadel_slaughter_horde_contain_compiles_economy_and_ring_entry() -> None:
    lineage = _lineage(
        """
Object FixtureObject
  Behavior = CitadelSlaughterHordeContain ModuleTag_Slaughter
    PassengerFilter = GENERIC_FACTION_SLAUGHTERABLE
    ObjectStatusOfContained = UNSELECTABLE ENCLOSED
    CashBackPercent = 200%
    ContainMax = 99
    AllowEnemiesInside = No
    AllowAlliesInside = No
    AllowNeutralInside = No
    AllowOwnPlayerInsideOverride = Yes
    EnterSound = GUI_RingReturned
    EntryOffset = X:10 Y:20 Z:30
    EntryPosition = X:1 Y:2 Z:3
    ExitOffset = X:40 Y:50 Z:60
    StatusForRingEntry = HOLDING_THE_RING
    UpgradeForRingEntry = Upgrade_RingHero Upgrade_FortressRingHero
    ObjectToDestroyForRingEntry = NONE +TheDroppedRing
    FXForRingEntry = FX_OneRingFlare
  End
End
"""
    )
    row = compile_citadel_slaughter_horde_contains(lineage, "FixtureObject")[0]
    assert row["fields"]["CashBackPercent"]["ratio"] == 2.0
    assert row["fields"]["ContainMax"]["value"] == 99
    assert row["fields"]["EntryPosition"]["value"] == {"x": 1.0, "y": 2.0, "z": 3.0}
    assert row["fields"]["UpgradeForRingEntry"]["value"] == [
        "Upgrade_RingHero", "Upgrade_FortressRingHero",
    ]
    assert {item["module"] for item in compile_all_module_contracts(lineage, "FixtureObject")} == {
        "CitadelSlaughterHordeContain",
    }


@pytest.mark.parametrize("body", [
    "ContainMax = 99",
    "PassengerFilter = ANY\n    ObjectStatusOfContained = UNSELECTABLE\n    CashBackPercent = 20%\n    ContainMax = 99\n    AllowEnemiesInside = No\n    AllowNeutralInside = No\n    EnterSound = Sound\n    EntryOffset = X:0 Y:0 Z:0\n    EntryPosition = X:0 Y:0 Z:0\n    ExitOffset = X:0 Y:0 Z:0\n    StatusForRingEntry = HOLDING_THE_RING\n    UpgradeForRingEntry = Upgrade_X\n    ObjectToDestroyForRingEntry = NONE +Ring",
])
def test_citadel_slaughter_horde_contain_fails_closed(body: str) -> None:
    lineage = _lineage(
        "Object FixtureObject\n  Behavior = CitadelSlaughterHordeContain ModuleTag_Test\n"
        f"    {body}\n  End\nEnd"
    )
    with pytest.raises(ModuleContractError):
        compile_citadel_slaughter_horde_contains(lineage, "FixtureObject")


def test_wall_hub_behavior_compiles_repeated_segments_and_effective_distance() -> None:
    lineage = _lineage(
        """
Object FixtureObject
  Behavior = WallHubBehavior ModuleTag_Wall
    Options = OPTION_ONE
    MaxBuildoutDistance = FIXTURE_WALL_RADIUS
    MaxBuildoutDistance = 1500.0
    StaggeredBuildFactor = FIXTURE_STAGGER
    SegmentTemplateName = FixtureWallSegment
    SegmentTemplateName = FixtureWallHub
    BuilderRadius = 20
    HubCapTemplateName = FixtureWallHub
    DefaultSegmentTemplateName = FixtureWallSegment
    CliffCapTemplateName = FixtureWallCliffCap
  End
End
"""
    )
    row = compile_wall_hub_behaviors(lineage, "FixtureObject")[0]
    assert row["fields"]["Options"]["value"] == "OPTION_ONE"
    assert [item.get("define") or item.get("value") for item in row["fields"]["MaxBuildoutDistance"]] == [
        "FIXTURE_WALL_RADIUS", 1500.0,
    ]
    assert row["fields"]["EffectiveMaxBuildoutDistance"]["value"] == 1500.0
    assert [item["value"] for item in row["fields"]["SegmentTemplateName"]] == [
        "FixtureWallSegment", "FixtureWallHub",
    ]
    assert {item["module"] for item in compile_all_module_contracts(lineage, "FixtureObject")} == {
        "WallHubBehavior",
    }


@pytest.mark.parametrize("body", [
    "Options = OPTION_ONE",
    "Options = OPTION_FOUR\n    MaxBuildoutDistance = 100\n    SegmentTemplateName = Segment\n    HubCapTemplateName = Hub\n    DefaultSegmentTemplateName = Segment",
    "Options = OPTION_ONE\n    MaxBuildoutDistance = 100\n    SegmentTemplateName = Segment\n    SegmentTemplateName = Bad-Template\n    HubCapTemplateName = Hub\n    DefaultSegmentTemplateName = Segment",
])
def test_wall_hub_behavior_fails_closed(body: str) -> None:
    lineage = _lineage(
        "Object FixtureObject\n  Behavior = WallHubBehavior ModuleTag_Test\n"
        f"    {body}\n  End\nEnd"
    )
    with pytest.raises(ModuleContractError):
        compile_wall_hub_behaviors(lineage, "FixtureObject")


def test_activate_module_special_power_compiles_resolved_ordered_routes() -> None:
    lineage = _lineage(
        """
Object FixtureObject
  Behavior = SpecialPowerModule ModuleTag_First
    SpecialPowerTemplate = SpecialAbilityFirst
  End
  Behavior = PlayerHealSpecialPower ModuleTag_Second
    SpecialPowerTemplate = SpecialAbilitySecond
    HealAmount = 1
  End
  Behavior = ActivateModuleSpecialPower ModuleTag_Activate
    SpecialPowerTemplate = SpecialAbilityFixture
    StartAbilityRange = FIXTURE_RANGE
    EffectRange = 200
    MustFinishAbility = Yes
    UnpackTime = 800
    PackTime = 1200
    SpecialPowerDuration = CREATE_A_HERO_POWER_DURATION
    UnpackingVariation = 1
    TriggerSpecialPower = ModuleTag_First TARGETPOS
    TriggerSpecialPower = ModuleTag_Second OBJECTPOS
  End
End
"""
    )
    row = compile_activate_module_special_powers(lineage, "FixtureObject")[0]
    fields = row["fields"]
    assert fields["StartAbilityRange"]["define"] == "FIXTURE_RANGE"
    assert fields["SpecialPowerDuration"]["define"] == "CREATE_A_HERO_POWER_DURATION"
    assert fields["UnpackTime"]["milliseconds"] == 800
    assert [route["targetMode"] for route in fields["TriggerSpecialPower"]] == [
        "LOCATION", "CURRENT_TARGET",
    ]
    assert [route["targetModuleKind"] for route in fields["TriggerSpecialPower"]] == [
        "SpecialPowerModule", "PlayerHealSpecialPower",
    ]
    assert all(route["targetSourceIni"].endswith("fixture.ini") for route in fields["TriggerSpecialPower"])
    assert row["runtimeStatus"] == "executable"


@pytest.mark.parametrize("target,trigger", [
    ("", "ModuleTag_Missing TARGETPOS"),
    ("  Behavior = PhysicsBehavior ModuleTag_Bad\n  End\n", "ModuleTag_Bad TARGETPOS"),
    ("  Behavior = SpecialPowerModule ModuleTag_Good\n    SpecialPowerTemplate = SpecialAbilityX\n  End\n", "ModuleTag_Good BADMODE"),
])
def test_activate_module_special_power_fails_closed_on_target_routes(target: str, trigger: str) -> None:
    lineage = _lineage(
        "Object FixtureObject\n" + target
        + "  Behavior = ActivateModuleSpecialPower ModuleTag_Test\n"
        + "    SpecialPowerTemplate = SpecialAbilityFixture\n"
        + "    StartAbilityRange = 100\n"
        + f"    TriggerSpecialPower = {trigger}\n  End\nEnd"
    )
    with pytest.raises(ModuleContractError):
        compile_activate_module_special_powers(lineage, "FixtureObject")


def test_weapon_mode_special_power_update_compiles_typed_mode_and_modifier() -> None:
    row = compile_weapon_mode_special_power_updates(_lineage("""
Object FixtureObject
  Behavior = WeaponModeSpecialPowerUpdate ModuleTag_Mode
    SpecialPowerTemplate = SpecialAbilityFixtureMode
    Duration = FIXTURE_MODE_DURATION
    AttributeModifier = FixtureModeBonus
    WeaponSetFlags = WEAPONSET_TOGGLE_1 WEAPONSET_HERO_MODE
    StartsPaused = Yes
  End
End
"""), "FixtureObject")[0]
    fields = row["fields"]
    assert fields["Duration"]["define"] == "FIXTURE_MODE_DURATION"
    assert fields["WeaponSetFlags"]["value"] == ["WEAPONSET_TOGGLE_1", "WEAPONSET_HERO_MODE"]
    assert fields["StartsPaused"]["value"] is True
    assert fields["AttributeModifier"]["value"] == "FixtureModeBonus"
    assert row["runtimeStatus"] == "executable"


@pytest.mark.parametrize("body", [
    "Duration = 1000\n    StartsPaused = Yes",
    "SpecialPowerTemplate = SpecialAbilityMode\n    StartsPaused = Yes",
    "SpecialPowerTemplate = SpecialAbilityMode\n    Duration = -1\n    StartsPaused = Yes",
    "SpecialPowerTemplate = SpecialAbilityMode\n    Duration = 1\n    StartsPaused = Maybe",
    "SpecialPowerTemplate = SpecialAbilityMode\n    Duration = 1\n    StartsPaused = Yes\n    LockWeaponSlot = PRIMARY",
    "SpecialPowerTemplate = SpecialAbilityMode\n    Duration = 1\n    StartsPaused = Yes\n    WeaponSetFlags = BAD_FLAG",
    "SpecialPowerTemplate = SpecialAbilityMode\n    Duration = 1\n    StartsPaused = Yes\n    Unknown = value",
])
def test_weapon_mode_special_power_update_fails_closed(body: str) -> None:
    lineage = _lineage(f"Object FixtureObject\n  Behavior = WeaponModeSpecialPowerUpdate ModuleTag_Test\n    {body}\n  End\nEnd")
    with pytest.raises(ModuleContractError):
        compile_weapon_mode_special_power_updates(lineage, "FixtureObject")


def test_dominate_enemy_special_power_compiles_typed_filter_and_timing() -> None:
    row = compile_dominate_enemy_special_powers(_lineage("""
Object FixtureObject
  Behavior = DominateEnemySpecialPower ModuleTag_Dominate
    SpecialPowerTemplate = SpecialAbilityFixtureDominate
    StartAbilityRange = 200
    AttributeModifierAffects = ALL -HERO ENEMIES NEUTRAL
    DominateRadius = 60
    DominatedFX = FX_Dominated
    TriggerFX = FX_Trigger
    PermanentlyConvert = No
    UnpackTime = 2000
    PreparationTime = 1
    FreezeAfterTriggerDuration = 2500
  End
End
"""), "FixtureObject")[0]
    fields = row["fields"]
    assert fields["AttributeModifierAffects"]["value"] == ["ALL", "-HERO", "ENEMIES", "NEUTRAL"]
    assert fields["StartAbilityRange"]["value"] == 200
    assert fields["UnpackTime"]["milliseconds"] == 2000
    assert fields["PermanentlyConvert"]["value"] is False
    assert row["runtimeStatus"] == "executable"


@pytest.mark.parametrize("body", [
    "StartAbilityRange = 100\n    AttributeModifierAffects = ALL",
    "SpecialPowerTemplate = SpecialAbilityDominate\n    AttributeModifierAffects = ALL",
    "SpecialPowerTemplate = SpecialAbilityDominate\n    StartAbilityRange = -1\n    AttributeModifierAffects = ALL",
    "SpecialPowerTemplate = SpecialAbilityDominate\n    StartAbilityRange = 1\n    AttributeModifierAffects =",
    "SpecialPowerTemplate = SpecialAbilityDominate\n    StartAbilityRange = 1\n    AttributeModifierAffects = ALL\n    PermanentlyConvert = Maybe",
    "SpecialPowerTemplate = SpecialAbilityDominate\n    StartAbilityRange = 1\n    AttributeModifierAffects = ALL\n    Unknown = value",
])
def test_dominate_enemy_special_power_fails_closed(body: str) -> None:
    lineage = _lineage(f"Object FixtureObject\n  Behavior = DominateEnemySpecialPower ModuleTag_Test\n    {body}\n  End\nEnd")
    with pytest.raises(ModuleContractError):
        compile_dominate_enemy_special_powers(lineage, "FixtureObject")


def test_grab_passenger_special_power_compiles_typed_grab_route() -> None:
    row = compile_grab_passenger_special_powers(_lineage("""
Object FixtureObject
  Behavior = GrabPassengerSpecialPower ModuleTag_Grab
    SpecialPowerTemplate = SpecialAbilityGrabPassenger
    UpdateModuleStartsAttack = Yes
    AllowTree = Yes
    InitiateFX = FX_TrollGrabInitiate
  End
End
"""), "FixtureObject")[0]
    fields = row["fields"]
    assert fields["SpecialPowerTemplate"]["value"] == "SpecialAbilityGrabPassenger"
    assert fields["UpdateModuleStartsAttack"]["value"] is True
    assert fields["AllowTree"]["value"] is True
    assert fields["InitiateFX"]["value"] == "FX_TrollGrabInitiate"
    assert row["runtimeStatus"] == "deferred"


@pytest.mark.parametrize("body", [
    "UpdateModuleStartsAttack = Yes",
    "SpecialPowerTemplate = SpecialAbilityGrabPassenger",
    "SpecialPowerTemplate = SpecialAbilityGrabPassenger\n    UpdateModuleStartsAttack = Maybe",
    "SpecialPowerTemplate = SpecialAbilityGrabPassenger\n    UpdateModuleStartsAttack = Yes\n    AllowTree = Maybe",
    "SpecialPowerTemplate = bad-token!\n    UpdateModuleStartsAttack = Yes",
    "SpecialPowerTemplate = SpecialAbilityGrabPassenger\n    UpdateModuleStartsAttack = Yes\n    Unknown = value",
])
def test_grab_passenger_special_power_fails_closed(body: str) -> None:
    lineage = _lineage(f"Object FixtureObject\n  Behavior = GrabPassengerSpecialPower ModuleTag_Test\n    {body}\n  End\nEnd")
    with pytest.raises(ModuleContractError):
        compile_grab_passenger_special_powers(lineage, "FixtureObject")


def test_fling_passenger_special_ability_update_compiles_typed_payload() -> None:
    row = compile_fling_passenger_special_ability_updates(_lineage("""
Object FixtureObject
  Behavior = FlingPassengerSpecialAbilityUpdate ModuleTag_Fling
    SpecialPowerTemplate = SpecialAbilityFixtureFling
    UnpackTime = 1250
    PackTime = 1000
    FlingPassengerVelocity = X:1 Y:-2 Z:3.5
    FlingPassengerLandingWarhead = FixtureLandingWarhead
    CustomAnimAndDuration = AnimState:SPECIAL_WEAPON_TWO AnimTime:2967
    MustFinishAbility = Yes
  End
End
"""), "FixtureObject")[0]
    fields = row["fields"]
    assert fields["FlingPassengerVelocity"]["value"] == {"x": 1.0, "y": -2.0, "z": 3.5}
    assert fields["CustomAnimAndDuration"]["animState"] == "SPECIAL_WEAPON_TWO"
    assert fields["CustomAnimAndDuration"]["animTimeMilliseconds"] == 2967
    assert fields["MustFinishAbility"]["value"] is True
    assert row["runtimeStatus"] == "deferred"


@pytest.mark.parametrize("body", [
    "UnpackTime = 1",
    "SpecialPowerTemplate = SpecialAbilityFling",
    "SpecialPowerTemplate = SpecialAbilityFling\n    UnpackTime = -1",
    "SpecialPowerTemplate = SpecialAbilityFling\n    UnpackTime = 1\n    FlingPassengerVelocity = X:1 Y:2",
    "SpecialPowerTemplate = SpecialAbilityFling\n    UnpackTime = 1\n    CustomAnimAndDuration = bad",
    "SpecialPowerTemplate = SpecialAbilityFling\n    UnpackTime = 1\n    MustFinishAbility = Maybe",
    "SpecialPowerTemplate = SpecialAbilityFling\n    UnpackTime = 1\n    Unknown = value",
])
def test_fling_passenger_special_ability_update_fails_closed(body: str) -> None:
    lineage = _lineage(f"Object FixtureObject\n  Behavior = FlingPassengerSpecialAbilityUpdate ModuleTag_Test\n    {body}\n  End\nEnd")
    with pytest.raises(ModuleContractError):
        compile_fling_passenger_special_ability_updates(lineage, "FixtureObject")


def test_temporarily_defect_update_default_is_typed_and_fail_closed() -> None:
    row = compile_temporarily_defect_update_default(b"""
InheritableModule
  Behavior = TemporarilyDefectUpdate ModuleTag_TemporarilyDefectUpdate
    DefectDuration = 30000
  End
End
""")
    assert row["fields"]["DefectDuration"]["milliseconds"] == 30000
    assert row["runtimeStatus"] == "deferred"
    with pytest.raises(ModuleContractError):
        compile_temporarily_defect_update_default(b"""
InheritableModule
  Behavior = TemporarilyDefectUpdate ModuleTag_Test
    DefectDuration = 30000
    Unknown = 1
  End
End
""")


def test_repair_special_power_compiles_exact_template() -> None:
    row = compile_repair_special_powers(_lineage("""
Object FixtureObject
  Behavior = RepairSpecialPower ModuleTag_Repair
    SpecialPowerTemplate = SpecialRepairStructure
  End
End
"""), "FixtureObject")[0]
    assert row["fields"]["SpecialPowerTemplate"]["value"] == "SpecialRepairStructure"
    assert row["runtimeStatus"] == "deferred"
    with pytest.raises(ModuleContractError):
        compile_repair_special_powers(_lineage("""
Object FixtureObject
  Behavior = RepairSpecialPower ModuleTag_Repair
    SpecialPowerTemplate = SpecialRepairStructure
    Unknown = 1
  End
End
"""), "FixtureObject")


def test_horde_dispatch_special_power_compiles_dispatch_state() -> None:
    row = compile_horde_dispatch_special_powers(_lineage("""
Object FixtureHorde
  Behavior = HordeDispatchSpecialPower ModuleTag_Dispatch
    SpecialPowerTemplate = SpecialAbilityFixtureDispatch
    UpdateModuleStartsAttack = Yes
    StartsPaused = No
  End
End
""", "FixtureHorde"), "FixtureHorde")[0]
    assert row["fields"]["UpdateModuleStartsAttack"]["value"] is True
    assert row["fields"]["StartsPaused"]["value"] is False
    assert row["runtimeStatus"] == "deferred"
    with pytest.raises(ModuleContractError):
        compile_horde_dispatch_special_powers(_lineage("""
Object FixtureHorde
  Behavior = HordeDispatchSpecialPower ModuleTag_Dispatch
    SpecialPowerTemplate = SpecialAbilityFixtureDispatch
    StartsPaused = Maybe
  End
End
""", "FixtureHorde"), "FixtureHorde")


def test_stop_and_unleash_special_powers_compile_exact_state() -> None:
    stop = compile_stop_special_powers(_lineage("""
Object FixtureObject
  Behavior = SiegeDeploySpecialPower ModuleTag_Deploy
    SpecialPowerTemplate = SpecialAbilitySiegeDeploy
  End
  Behavior = StopSpecialPower ModuleTag_Stop
    SpecialPowerTemplate = SpecialAbilityStop
    StopPowerTemplate = SpecialAbilitySiegeDeploy
  End
End
"""), "FixtureObject")[0]
    assert stop["fields"]["StopPowerTemplate"]["value"] == "SpecialAbilitySiegeDeploy"
    assert stop["effectGraph"]["linkedModule"]["kind"] == "SiegeDeploySpecialPower"
    assert stop["effectGraph"]["interruptsCurrentOrder"] is True
    assert stop["runtimeStatus"] == "executable"
    unleash = compile_unleash_special_powers(_lineage("""
Object FixtureObject
  Behavior = ObjectCreationUpgrade ModuleTag_CreateSlave
    TriggeredBy = Upgrade_HasFixtureSlave
    ThingToSpawn = FixtureSlave
  End
  Behavior = SlaveWatcherBehavior ModuleTag_Watcher
    RemoveUpgrade = Upgrade_HasFixtureSlave
    GrantUpgrade = Upgrade_FixtureSlaveAvailable
  End
  Behavior = UnleashSpecialPower ModuleTag_Unleash
    SpecialPowerTemplate = SpecialAbilityUnleash
    UnpackTime = 0
    AwardXPForTriggering = 0
    Instant = Yes
  End
End
"""), "FixtureObject")[0]
    assert unleash["fields"]["UnpackTime"]["milliseconds"] == 0
    assert unleash["fields"]["AwardXPForTriggering"]["value"] == 0
    assert unleash["fields"]["Instant"]["value"] is True
    assert unleash["effectGraph"]["spawnedObjectId"] == "FixtureSlave"
    assert unleash["effectGraph"]["targetMode"] == "SELF_OWNED_SLAVE"
    assert unleash["runtimeStatus"] == "executable"
    with pytest.raises(ModuleContractError):
        compile_unleash_special_powers(_lineage("""
Object FixtureObject
  Behavior = ObjectCreationUpgrade ModuleTag_CreateSlave
    TriggeredBy = Upgrade_HasFixtureSlave
    ThingToSpawn = FixtureSlave
  End
  Behavior = SlaveWatcherBehavior ModuleTag_Watcher
    RemoveUpgrade = Upgrade_HasFixtureSlave
    GrantUpgrade = Upgrade_FixtureSlaveAvailable
  End
  Behavior = UnleashSpecialPower ModuleTag_Unleash
    SpecialPowerTemplate = SpecialAbilityUnleash
    UnpackTime = Tomorrow
    AwardXPForTriggering = 0
    Instant = Yes
  End
End
"""), "FixtureObject")


def test_siege_deploy_special_power_compiles_complete_retail_grammar() -> None:
    row = compile_siege_deploy_special_powers(_lineage("""
Object FixtureSiege
  Behavior = SiegeDeploySpecialPower ModuleTag_Deploy
    SpecialPowerTemplate = SpecialAbilitySiegeDeploy
    LowerDelay = 1200
    RaiseDelay = 2000
    EvacuatePassengersOnDeploy = Yes
    SkipAdjustPosition = Yes
    InitiateSound = SiegeLadderVoiceAttackMS
    ExtraWallDistance = 15.0
  End
End
""", "FixtureSiege"), "FixtureSiege")[0]
    assert row["runtimeStatus"] == "deferred"
    assert row["fields"]["LowerDelay"]["milliseconds"] == 1200
    assert row["fields"]["RaiseDelay"]["milliseconds"] == 2000
    assert row["effectGraph"]["kind"] == "siege-deploy"
    assert row["effectGraph"]["targetMode"] == "TARGET_STRUCTURE"
    assert row["effectGraph"]["extraWallDistanceSource"] == 15.0
    assert row["effectGraph"]["modelReceipts"] == [
        "wall-contact-offset:ExtraWallDistance requires retail docking geometry"
    ]


def test_siege_deploy_special_power_refuses_unknown_or_incomplete_grammar() -> None:
    for tail in ["UnknownField = 1", ""]:
        body = f"""
Object FixtureSiege
  Behavior = SiegeDeploySpecialPower ModuleTag_Deploy
    SpecialPowerTemplate = SpecialAbilitySiegeDeploy
    LowerDelay = 1200
    RaiseDelay = 2000
    EvacuatePassengersOnDeploy = Yes
    SkipAdjustPosition = Yes
    {tail}
  End
End
"""
        with pytest.raises(ModuleContractError):
            compile_siege_deploy_special_powers(
                _lineage(body, "FixtureSiege"), "FixtureSiege"
            )


def test_toggle_deploy_binds_exact_retail_deploy_style_grammar() -> None:
    lineage = _lineage("""
Object FixtureDemolisher
  Behavior = DeployStyleAIUpdate ModuleTag_DeployStyle
    AutoAcquireEnemiesWhenIdle = Yes ATTACK_BUILDINGS
    MoodAttackCheckRate = 2500
    MustDeployToAttack = No
    UnpackTime = 2000
    PackTime = 2000
    DeployedAttributeModifier = DwarvenDemolisherDeployModifier
  End
  Behavior = ToggleDeploySpecialAbilityUpdate ModuleTag_ToggleDeploy
    SpecialPowerTemplate = SpecialAbilityDwarvenDemolisherDeploy
    IgnoreFacingCheck = Yes
    SoundDeploy = DwarfDemolisherDeployMS
    SoundUndeploy = DwarfDemolisherUndeployMS
  End
End
""", "FixtureDemolisher")
    style = compile_deploy_style_ai_updates(lineage, "FixtureDemolisher")[0]
    toggle = compile_toggle_deploy_special_ability_updates(
        lineage, "FixtureDemolisher"
    )[0]

    assert style["runtimeStatus"] == "executable"
    assert style["fields"] == {
        "AutoAcquireEnemiesWhenIdle": {
            "authored": "Yes ATTACK_BUILDINGS",
            "value": ["Yes", "ATTACK_BUILDINGS"],
            "sourceIni": "data/ini/object/fixture.ini",
            "line": 4,
        },
        "MoodAttackCheckRate": {
            "authored": "2500",
            "milliseconds": 2500,
            "sourceIni": "data/ini/object/fixture.ini",
            "line": 5,
        },
        "MustDeployToAttack": {
            "authored": "No",
            "value": False,
            "sourceIni": "data/ini/object/fixture.ini",
            "line": 6,
        },
        "UnpackTime": {
            "authored": "2000",
            "milliseconds": 2000,
            "sourceIni": "data/ini/object/fixture.ini",
            "line": 7,
        },
        "PackTime": {
            "authored": "2000",
            "milliseconds": 2000,
            "sourceIni": "data/ini/object/fixture.ini",
            "line": 8,
        },
        "DeployedAttributeModifier": {
            "authored": "DwarvenDemolisherDeployModifier",
            "value": "DwarvenDemolisherDeployModifier",
            "sourceIni": "data/ini/object/fixture.ini",
            "line": 9,
        },
    }
    assert toggle["runtimeStatus"] == "executable"
    assert toggle["effectGraph"] == {
        "kind": "toggle-deploy",
        "autoAcquireEnabled": True,
        "autoAcquireModes": ["ATTACK_BUILDINGS"],
        "moodAttackCheckRateMs": 2500,
        "mustDeployToAttack": False,
        "unpackTimeMs": 2000,
        "packTimeMs": 2000,
        "deployedAttributeModifierId": "DwarvenDemolisherDeployModifier",
        "specialPowerTemplateId": "SpecialAbilityDwarvenDemolisherDeploy",
        "targetMode": "SELF",
        "ignoreFacingCheck": True,
        "soundDeployId": "DwarfDemolisherDeployMS",
        "soundUndeployId": "DwarfDemolisherUndeployMS",
        "deployStyle": {
            "tag": "ModuleTag_DeployStyle",
            "sourceIni": "data/ini/object/fixture.ini",
            "line": 3,
        },
        "sourceIni": "data/ini/object/fixture.ini",
        "line": 11,
    }


def test_toggle_deploy_refuses_missing_style_or_extra_fields() -> None:
    without_style = _lineage("""
Object FixtureDemolisher
  Behavior = ToggleDeploySpecialAbilityUpdate ModuleTag_ToggleDeploy
    SpecialPowerTemplate = SpecialAbilityDwarvenDemolisherDeploy
    IgnoreFacingCheck = Yes
    SoundDeploy = DwarfDemolisherDeployMS
    SoundUndeploy = DwarfDemolisherUndeployMS
  End
End
""", "FixtureDemolisher")
    with pytest.raises(ModuleContractError, match="exactly one DeployStyleAIUpdate"):
        compile_toggle_deploy_special_ability_updates(
            without_style, "FixtureDemolisher"
        )

    extra_style_field = _lineage("""
Object FixtureDemolisher
  Behavior = DeployStyleAIUpdate ModuleTag_DeployStyle
    AutoAcquireEnemiesWhenIdle = Yes ATTACK_BUILDINGS
    MoodAttackCheckRate = 2500
    MustDeployToAttack = No
    UnpackTime = 2000
    PackTime = 2000
    DeployedAttributeModifier = DwarvenDemolisherDeployModifier
    ResetTurretBeforePacking = Yes
  End
End
""", "FixtureDemolisher")
    with pytest.raises(ModuleContractError, match="unsupported fields"):
        compile_deploy_style_ai_updates(extra_style_field, "FixtureDemolisher")


def test_special_disguise_exact_binary_closed_row_is_executable() -> None:
    lineage = _lineage("""
Object RohanEowyn
  Behavior = SpecialDisguiseUpdate ModuleTag_SpecialDisguiseUpdateUpdate
    SpecialPowerTemplate = SpecialAbilityDisguise
    UnpackTime = 1000
    PreparationTime = 1
    PersistentPrepTime = 250
    PackTime = 1000
    OpacityTarget = .3
    DisguiseAsTemplate = RohanEowynDisguised
    DisguisedAsTemplate_EnemyPerspective = RohanRohirrimHorde
    DisguiseFX = FX_DisguiseExit
    ForceMountedWhenDisguising = Yes
  End
End
""", "RohanEowyn")
    row = compile_special_disguise_updates(lineage, "RohanEowyn")[0]

    assert row["runtimeStatus"] == "executable"
    assert row["fields"]["OpacityTarget"]["value"] == pytest.approx(0.3)
    assert row["effectGraph"] == {
        "kind": "special-disguise",
        "specialPowerTemplateId": "SpecialAbilityDisguise",
        "targetMode": "SELF",
        "unpackTimeMs": 1000,
        "preparationTimeMs": 1,
        "persistentPrepTimeMs": 250,
        "packTimeMs": 1000,
        "opacityTarget": pytest.approx(0.3),
        "ownerObjectId": "RohanEowyn",
        "ownerDisguiseTemplateId": "RohanEowynDisguised",
        "hostileDisguiseTemplateId": "RohanRohirrimHorde",
        "disguiseFxId": "FX_DisguiseExit",
        "forceMountedWhenDisguising": True,
        "deferredBoundaries": [
            "critical-hit-ordering", "death-reset-ordering",
            "user1-stealth-ordering", "viewer-perspective",
        ],
        "sourceIni": "data/ini/object/fixture.ini",
        "line": 3,
    }


def test_special_disguise_rider2_parse_is_typed_but_runtime_deferred() -> None:
    lineage = _lineage("""
Object RohanEowyn
  Behavior = SpecialDisguiseUpdate ModuleTag_SpecialDisguiseUpdateUpdate
    TriggerAttributeModifier = Rider2Tracker
    AttributeModifierDuration = 2000
    SpecialPowerTemplate = SpecialAbilityDisguise
    UnpackTime = 1000
    PreparationTime = 1
    PersistentPrepTime = 250
    PackTime = 1000
    OpacityTarget = .9
    DisguiseAsTemplate = RohanEowynDisguised
    DisguisedAsTemplate_EnemyPerspective = RohanRohirrimHorde
    DisguiseFX = FX_DisguiseExit
    ForceMountedWhenDisguising = Yes
  End
End
""", "RohanEowyn")
    row = compile_special_disguise_updates(lineage, "RohanEowyn")[0]

    assert row["runtimeStatus"] == "deferred"
    assert row["fields"]["TriggerAttributeModifier"]["value"] == "Rider2Tracker"
    assert row["fields"]["AttributeModifierDuration"]["milliseconds"] == 2000
    assert row["effectGraph"]["executionEligibility"] == {
        "runtimeStatus": "deferred",
        "reason": "binary-unresolved:TriggerAttributeModifier-application",
    }


@pytest.mark.parametrize("tail", ["UnknownField = 1", "", "ForceMountedWhenDisguising = No"])
def test_special_disguise_refuses_unknown_incomplete_or_unproven_rows(tail: str) -> None:
    body = f"""
Object RohanEowyn
  Behavior = SpecialDisguiseUpdate ModuleTag_SpecialDisguiseUpdateUpdate
    SpecialPowerTemplate = SpecialAbilityDisguise
    UnpackTime = 1000
    PreparationTime = 1
    PersistentPrepTime = 250
    PackTime = 1000
    OpacityTarget = .3
    DisguiseAsTemplate = RohanEowynDisguised
    DisguisedAsTemplate_EnemyPerspective = RohanRohirrimHorde
    DisguiseFX = FX_DisguiseExit
    {tail}
  End
End
"""
    if tail == "ForceMountedWhenDisguising = No":
        row = compile_special_disguise_updates(_lineage(body, "RohanEowyn"), "RohanEowyn")[0]
        assert row["runtimeStatus"] == "deferred"
    else:
        with pytest.raises(ModuleContractError):
            compile_special_disguise_updates(_lineage(body, "RohanEowyn"), "RohanEowyn")


@pytest.mark.parametrize(
    ("module", "template", "compiler"),
    [
        (
            "DeflectSpecialPower",
            "SpecialAbilityDeflectProjectiles",
            compile_deflect_special_powers,
        ),
        (
            "SplitHordeSpecialPower",
            "SpecialAbilitySplitHorde",
            compile_split_horde_special_powers,
        ),
    ],
)
def test_nonshipping_special_power_mod_grammar_stays_typed_deferred(
    module: str, template: str, compiler
) -> None:
    lineage = _lineage(f"""
Object FixtureModObject
  Behavior = {module} ModuleTag_Legacy
    SpecialPowerTemplate = {template}
  End
End
""", "FixtureModObject")
    row = compiler(lineage, "FixtureModObject")[0]
    assert row["runtimeStatus"] == "deferred"
    assert row["extraction"] == "typed"
    assert row["fields"]["SpecialPowerTemplate"] == {
        "authored": template,
        "value": template,
        "sourceIni": "data/ini/object/fixture.ini",
        "line": 4,
    }
    assert row["effectGraph"] == {
        "kind": "non-shipping-special-power",
        "authoredModuleKind": module,
        "specialPowerTemplateId": template,
        "subclassFields": [],
        "executionEligibility": {
            "runtimeStatus": "deferred",
            "shippingAdmission": False,
            "retailOwnerMatch": False,
            "disposition": "unadmitted-owner",
        },
        "sourceIni": "data/ini/object/fixture.ini",
        "line": 3,
    }

    malformed = _lineage(f"""
Object FixtureModObject
  Behavior = {module} ModuleTag_Legacy
    SpecialPowerTemplate = {template}
    InventedGameplay = Yes
  End
End
""", "FixtureModObject")
    with pytest.raises(ModuleContractError, match="only SpecialPowerTemplate"):
        compiler(malformed, "FixtureModObject")


def test_special_enemy_sense_update_compiles_filter_and_scan_timer() -> None:
    row = compile_special_enemy_sense_updates(_lineage("""
Object FixtureObject
  Behavior = SpecialEnemySenseUpdate ModuleTag_Sense
    SpecialEnemyFilter = ANY +ORC +URUK
    ScanRange = 200
    ScanInterval = 2000
  End
End
"""), "FixtureObject")[0]
    assert row["fields"]["SpecialEnemyFilter"]["value"] == ["ANY", "+ORC", "+URUK"]
    assert row["fields"]["ScanRange"]["value"] == 200
    assert row["fields"]["ScanInterval"]["milliseconds"] == 2000
    assert row["effectGraph"]["targetMode"] == "PERIODIC_ENEMY_RADIUS_SCAN"
    assert row["runtimeStatus"] == "executable"
    with pytest.raises(ModuleContractError):
        compile_special_enemy_sense_updates(_lineage("""
Object FixtureObject
  Behavior = SpecialEnemySenseUpdate ModuleTag_Sense
    SpecialEnemyFilter = ANY +ORC
    ScanRange = -1
    ScanInterval = 2000
  End
End
"""), "FixtureObject")


def test_special_enemy_sense_resolves_scan_range_define_with_provenance() -> None:
    lineage = _lineage("""
Object RohanEowyn
  Behavior = SpecialEnemySenseUpdate ModuleTag_EnhancedEnemySense
    SpecialEnemyFilter = ANY +HERO
    ScanRange = VISION_HERO_STANDARD
    ScanInterval = 2000
  End
End
""", "RohanEowyn")
    provenance = {
        "defineId": "VISION_HERO_STANDARD",
        "sourceIni": "data/ini/gamedata.ini",
        "line": 122,
        "authoredValue": "175",
        "value": 175,
    }
    row = compile_special_enemy_sense_updates(
        lineage,
        "RohanEowyn",
        numeric_defines={"vision_hero_standard": 175},
        numeric_define_provenance={"vision_hero_standard": provenance},
    )[0]
    assert row["fields"]["ScanRange"] == {
        "authored": "VISION_HERO_STANDARD",
        "expression": "VISION_HERO_STANDARD",
        "value": 175,
        "defineProvenance": provenance,
        "sourceIni": "data/ini/object/fixture.ini",
        "line": 5,
    }
    with pytest.raises(ModuleContractError, match="define provenance is unresolved"):
        compile_special_enemy_sense_updates(
            lineage,
            "RohanEowyn",
            numeric_defines={"vision_hero_standard": 175},
            numeric_define_provenance={},
        )


def test_scavenger_special_power_compiles_exact_bounty_contract() -> None:
    row = compile_scavenger_special_powers(_lineage("""
Object FixtureSpellBook
  Behavior = ScavengerSpecialPower ModuleTag_Scavenger
    SpecialPowerTemplate = SpellBookScavenger
    BountyPercent = 1.0
    AvailableAtStart = No
    RequirementsFilterMPSkirmish = SPELL_BOOK_REQUIREMENTS_FILTER
    RequirementsFilterStrategic = SPELL_BOOK_REQUIREMENTS_FILTER_STRATEGIC
  End
End
""", "FixtureSpellBook"), "FixtureSpellBook")[0]
    assert row["fields"]["BountyPercent"]["value"] == 1.0
    assert row["fields"]["AvailableAtStart"]["value"] is False
    assert row["runtimeStatus"] == "deferred"
    with pytest.raises(ModuleContractError):
        compile_scavenger_special_powers(_lineage("""
Object FixtureSpellBook
  Behavior = ScavengerSpecialPower ModuleTag_Scavenger
    SpecialPowerTemplate = SpellBookScavenger
    BountyPercent = -1
    AvailableAtStart = No
    RequirementsFilterMPSkirmish = SPELL_BOOK_REQUIREMENTS_FILTER
    RequirementsFilterStrategic = SPELL_BOOK_REQUIREMENTS_FILTER_STRATEGIC
  End
End
""", "FixtureSpellBook"), "FixtureSpellBook")


def test_scavenger_special_power_exact_effective_retail_corpora() -> None:
    from openbfme_importer.catalog import InstallCatalog
    from openbfme_importer.module_census import census_catalog_paths, read_catalog_documents

    actual: dict[str, list[tuple[object, ...]]] = {}
    for label, path in census_catalog_paths().items():
        rows: list[dict[str, object]] = []
        for virtual_path, source in read_catalog_documents(InstallCatalog.load(path)):
            if b"scavengerspecialpower" not in source.lower():
                continue
            for obj in parse_sage_document(source, virtual_path=virtual_path).objects:
                rows.extend(compile_scavenger_special_powers([obj], obj.name))
        actual[label] = [(
            row["fields"]["SpecialPowerTemplate"]["value"],
            row["fields"]["BountyPercent"]["value"],
            row["fields"]["AvailableAtStart"]["value"],
            row["fields"]["RequirementsFilterMPSkirmish"]["value"],
            row["fields"]["RequirementsFilterStrategic"]["value"],
        ) for row in rows]
    expected = [(
        "SpellBookScavenger", 1.0, False,
        "SPELL_BOOK_REQUIREMENTS_FILTER",
        "SPELL_BOOK_REQUIREMENTS_FILTER_STRATEGIC",
    )]
    assert actual == {"bfme2-retail": expected, "rotwk-retail": expected}


def test_unknown_field_fails_closed() -> None:
    lineage = _lineage(
        """
Object FixtureObject
  Behavior = AttributeModifierUpgrade ModuleTag_Bonus
    TriggeredBy = Upgrade_X
    AttributeModifier = Bonus_X
    InventedField = Nope
  End
End
"""
    )
    with pytest.raises(ModuleContractError, match="unsupported fields"):
        compile_attribute_modifier_upgrades(lineage, "FixtureObject")


def test_batch_union_and_validator() -> None:
    lineage = _lineage(
        """
Object FixtureObject
  Body = InactiveBody ModuleTag_Body
  End
  Behavior = KeepObjectDie ModuleTag_Rubble
  End
  Behavior = AttributeModifierUpgrade ModuleTag_Bonus
    TriggeredBy = Upgrade_X
    AttributeModifier = Bonus_X
  End
  Behavior = HordeContain ModuleTag_Horde
    Slots = 10
    InitialPayload = GondorFighter 10
  End
  Behavior = HordeAIUpdate ModuleTag_HordeAI
    MoodAttackCheckRate = 500
  End
  Behavior = PickupStuffUpdate ModuleTag_Pickup
    SkirmishAIOnly = Yes
    StuffToPickUp = NONE +CRATE
    ScanRange = 200
    ScanIntervalSeconds = 0.5
  End
  Behavior = AutoAbilityBehavior ModuleTag_Auto
    Query = 1 ALL ENEMIES
  End
  Behavior = RespawnUpdate ModuleTag_Respawn
    DeathAnim = DYING
    AutoRespawnAtObjectFilter = NONE +CASTLE_KEEP
    ButtonImage = HIFixture
    RespawnRules = AutoSpawn:No Cost:550 Time:60000 Health:100%
  End
  Behavior = SlavedUpdate ModuleTag_Slaved
    DieOnMastersDeath = Yes
  End
  Behavior = CastleUpgrade ModuleTag_CastleUpgrade
    TriggeredBy = Upgrade_FixtureTrigger
    Upgrade = Upgrade_Fixture
  End
  Body = DelayedDeathBody ModuleTag_Delayed
    MaxHealth = 100
    DelayedDeathTime = 5000
    CanRespawn = No
  End
  Behavior = DynamicPortalBehaviour ModuleTag_Portal
    ObjectFilter = ANY +INFANTRY
    BonePrefix = Post
    NumberOfBones = 1
    WayPoint = Index:0 Type:Walk
    Link = From:0 To:0
  End
  Behavior = FlammableUpdate ModuleTag_Flammable
    AflameDuration = 7000
  End
  Behavior = SpawnBehavior ModuleTag_Spawn
    SpawnNumber = 1
    SpawnReplaceDelay = 1000
    SpawnTemplateName = Fixture_Slaved
  End
  Behavior = StealthUpdate ModuleTag_Stealth
  End
  Behavior = ObjectCreationUpgrade ModuleTag_Create
    TriggeredBy = Upgrade_Fixture
    ThingToSpawn = FixtureSpawned
  End
  Behavior = OCLUpdate ModuleTag_OCL
    OCL = OCL_Fixture
    MinDelay = 1000
    MaxDelay = 1000
    Amount = 1
  End
  Behavior = GarrisonContain ModuleTag_Garrison
    ObjectStatusOfContained = UNSELECTABLE CAN_ATTACK
    ContainMax = 1
    PassengerFilter = ANY +INFANTRY
    AllowAlliesInside = Yes
    AllowEnemiesInside = No
  End
  Behavior = LargeGroupBonusUpdate ModuleTag_LargeGroup
    UpdateRate = 1000
    HordeMemberFilter = NONE +INFANTRY
    Count = 100
    Radius = 160
    RubOffRadius = 160
    AlliesOnly = Yes
    AttributeModifier = FixtureBonus
  End
End
"""
    )
    rows = compile_all_module_contracts(lineage, "FixtureObject")
    modules = {row["module"] for row in rows}
    assert modules == {
        "InactiveBody", "KeepObjectDie", "AttributeModifierUpgrade", "HordeContain",
        "HordeAIUpdate",
        "PickupStuffUpdate",
        "AutoAbilityBehavior", "RespawnUpdate",
        "SlavedUpdate", "CastleUpgrade",
        "DelayedDeathBody",
        "DynamicPortalBehaviour",
        "FlammableUpdate",
        "SpawnBehavior", "StealthUpdate",
        "ObjectCreationUpgrade", "OCLUpdate",
        "GarrisonContain",
        "LargeGroupBonusUpdate",
    }
    validate_module_contracts(rows, label="fixture")


def test_module_name_literals_are_census_visible() -> None:
    # AST-scan consumption requires the exact module-type string in pipeline files.
    from pathlib import Path

    from openbfme_importer.module_contracts import (
        OPAQUE_DEFERRED_MODULE_KINDS,
        TYPED_MODULE_KINDS,
    )

    text = Path("importer/openbfme_importer/module_contracts.py").read_text(
        encoding="utf-8"
    )
    for name in sorted(TYPED_MODULE_KINDS | OPAQUE_DEFERRED_MODULE_KINDS):
        assert f'"{name}"' in text


def test_opaque_deferred_preserves_all_fields_and_never_executes() -> None:
    from openbfme_importer.module_contracts import compile_opaque_deferred_module

    lineage = _lineage(
        """
Object FixtureObject
  Behavior = SwayClientUpdate ModuleTag_Sway
    Mass = 1.0
    Friction = 0.1
    InventedButPreserved = Hello
  End
  Draw = W3DTreeDraw ModuleTag_Draw
    Model = TBTree
  End
End
"""
    )
    sway = compile_opaque_deferred_module(lineage, "SwayClientUpdate", "FixtureObject")
    assert len(sway) == 1
    assert sway[0]["extraction"] == "opaque-authored"
    assert sway[0]["runtimeStatus"] == "deferred"
    assert set(sway[0]["fields"]) == {"Mass", "Friction", "InventedButPreserved"}
    tree = compile_opaque_deferred_module(lineage, "W3DTreeDraw", "FixtureObject")
    assert tree[0]["carrier"].casefold() == "draw"
    assert tree[0]["fields"]["Model"]["authored"].strip() == "TBTree"


def test_opaque_rejects_typed_kind_and_executable_claim() -> None:
    from openbfme_importer.module_contracts import (
        compile_opaque_deferred_module,
        validate_module_contracts,
    )

    lineage = _lineage(
        """
Object FixtureObject
  Body = InactiveBody ModuleTag_Body
  End
End
"""
    )
    with pytest.raises(ModuleContractError, match="typed extractor"):
        compile_opaque_deferred_module(lineage, "InactiveBody", "FixtureObject")
    with pytest.raises(ModuleContractError, match="must be deferred"):
        validate_module_contracts(
            [
                {
                    "module": "PhysicsBehavior",
                    "fields": {},
                    "runtimeStatus": "executable",
                    "extraction": "opaque-authored",
                    "sourceIni": "data/ini/object/fixture.ini",
                    "line": 1,
                    "tag": "",
                    "carrier": "Behavior",
                }
            ],
            label="fixture",
        )
    with pytest.raises(ModuleContractError, match="lacks closed runtime evidence"):
        validate_module_contracts(
            [
                {
                    "module": "WallHubBehavior",
                    "fields": {},
                    "runtimeStatus": "executable",
                    "extraction": "typed",
                    "sourceIni": "data/ini/object/fixture.ini",
                    "line": 1,
                    "tag": "",
                    "carrier": "Behavior",
                }
            ],
            label="fixture",
        )


def test_opaque_and_typed_sets_disjoint() -> None:
    from openbfme_importer.module_contracts import (
        EXECUTABLE_TYPED_MODULE_EVIDENCE,
        EXECUTABLE_TYPED_MODULE_KINDS,
        ROW_EXECUTABLE_TYPED_MODULE_EVIDENCE,
        OPAQUE_DEFERRED_MODULE_KINDS,
        TYPED_MODULE_KINDS,
    )

    assert not (OPAQUE_DEFERRED_MODULE_KINDS & TYPED_MODULE_KINDS)
    assert len(OPAQUE_DEFERRED_MODULE_KINDS) == 86
    assert len(EXECUTABLE_TYPED_MODULE_KINDS) == 65
    assert {
        "DeployStyleAIUpdate",
        "ToggleDeploySpecialAbilityUpdate",
    } <= EXECUTABLE_TYPED_MODULE_KINDS
    assert EXECUTABLE_TYPED_MODULE_KINDS <= TYPED_MODULE_KINDS
    assert not (EXECUTABLE_TYPED_MODULE_KINDS & OPAQUE_DEFERRED_MODULE_KINDS)
    assert set(EXECUTABLE_TYPED_MODULE_EVIDENCE) == set(EXECUTABLE_TYPED_MODULE_KINDS)
    root = Path(__file__).resolve().parents[2]
    for module, (consumer_path, test_path) in EXECUTABLE_TYPED_MODULE_EVIDENCE.items():
        consumer = root / consumer_path
        focused_test = root / test_path
        assert consumer.is_file(), f"{module} missing consumer evidence {consumer_path}"
        assert focused_test.is_file(), f"{module} missing test evidence {test_path}"
        assert module in focused_test.read_text(encoding="utf-8"), (
            f"{module} is not named by focused runtime evidence {test_path}"
        )
    assert set(ROW_EXECUTABLE_TYPED_MODULE_EVIDENCE) == {
        "AutoHealBehavior", "BezierProjectileBehavior", "DamageCreationList", "FxTiming", "GeometryUpgrade",
        "QueueProductionExitUpdate", "SlowDeathBehavior", "SpawnBehavior", "SpecialDisguiseUpdate",
        "SubObjectsUpgrade", "AnimationSoundClientBehavior", "TransitionDamageFX",
        "ModelConditionUpgrade", "AnimationState", "ParticleSysBone",
        "EnteringStateFX", "ClipFrameClock", "FXEvent", "DrawableFxList", "AttackPose",
        "RefundDie", "UpgradeSoundSelectorClientBehavior",
    }
    for module, (consumer_path, test_path) in ROW_EXECUTABLE_TYPED_MODULE_EVIDENCE.items():
        assert (root / consumer_path).is_file()
        runner = root / test_path
        assert runner.is_file()
        assert module in runner.read_text(encoding="utf-8")


def test_upgrade_sound_selector_preserves_retail_guardian_attack_mux() -> None:
    lineage = _lineage(
        """
Object DwarvenGuardian
  ClientBehavior = UpgradeSoundSelectorClientBehavior ModuleTag_UpgradeSoundSelector
    SoundUpgrade = Upgrade_DwarvenSiegeHammer
      VoiceAttack = DwarfGuardianVoiceAttackHammer
      VoiceAttack = DwarfGuardianVoiceEnterStateAttackHammer
    End
  End
End
""",
        "DwarvenGuardian",
    )
    row = compile_upgrade_sound_selectors(lineage, "DwarvenGuardianHorde")[0]
    assert row["runtimeStatus"] == "executable"
    assert row["fields"]["SoundUpgrade"] == [{
        "requiredUpgrades": ["Upgrade_DwarvenSiegeHammer"],
        "excludedUpgrades": [],
        "sounds": {
            "VoiceAttack": [
                "DwarfGuardianVoiceAttackHammer",
                "DwarfGuardianVoiceEnterStateAttackHammer",
            ],
        },
        "sourceIni": "data/ini/object/fixture.ini",
        "line": 4,
    }]
    all_rows = compile_all_module_contracts(lineage, "DwarvenGuardianHorde")
    assert all_rows == [row]
    validate_module_contracts(all_rows, label="DwarvenGuardianHorde")


def test_upgrade_sound_selector_exact_retail_owners_and_wav_byte_receipt() -> None:
    from openbfme_importer.catalog import InstallCatalog
    from openbfme_importer.module_census import census_catalog_paths, read_catalog_documents

    catalogs = {
        label: InstallCatalog.load(path)
        for label, path in census_catalog_paths().items()
    }
    executable: dict[str, list[tuple[str, dict[str, object]]]] = {}
    for label, catalog in catalogs.items():
        executable[label] = []
        for virtual_path, source in read_catalog_documents(catalog):
            if (
                not virtual_path.casefold().startswith("data/ini/object/")
                or b"upgradesoundselectorclientbehavior" not in source.lower()
            ):
                continue
            for obj in parse_sage_document(source, virtual_path=virtual_path).objects:
                for row in compile_upgrade_sound_selectors([obj], obj.name):
                    if row["runtimeStatus"] == "executable":
                        executable[label].append((obj.name, row))
    assert {
        label: [(owner, row["sourceIni"], row["line"]) for owner, row in rows]
        for label, rows in executable.items()
    } == {
        "bfme2-retail": [(
            "DwarvenGuardian",
            "data/ini/object/goodfaction/units/dwarven/dwarvenguardian.ini",
            729,
        )],
        "rotwk-retail": [(
            "DwarvenGuardian",
            "data/ini/object/goodfaction/units/dwarven/dwarvenguardian.ini",
            720,
        )],
    }

    root = Path(__file__).resolve().parents[2]
    descriptor_path = (
        root / "workspace" / "retail-work" / "reports" / "faction-import"
        / "dwarves" / "objects" / "dwarvenguardianhorde" / "descriptor.json"
    )
    if not descriptor_path.is_file():
        pytest.skip("private DwarvenGuardianHorde descriptor is unavailable")
    descriptor = json.loads(descriptor_path.read_text(encoding="utf-8"))
    resolved = descriptor["presentation"]["resolvedAudio"]
    event_ids = [
        "DwarfGuardianVoiceAttackHammer",
        "DwarfGuardianVoiceEnterStateAttackHammer",
    ]
    assert resolved[event_ids[0]] == resolved[event_ids[1]]
    effective_assets = (
        root / "workspace" / "retail-work" / "editions" / "rotwk" / "cache"
        / "effective-assets"
    )
    receipts: dict[str, list[dict[str, str]]] = {}
    for event_id in event_ids:
        receipts[event_id] = []
        for virtual_path in resolved[event_id]:
            payload = (effective_assets / virtual_path).read_bytes()
            receipts[event_id].append({
                "virtualPath": virtual_path,
                "sha256": hashlib.sha256(payload).hexdigest(),
            })
    assert receipts[event_ids[0]] == receipts[event_ids[1]]
    assert [leaf["sha256"] for leaf in receipts[event_ids[0]]] == [
        "a80547e91ad15beba994e45fa4762b32eb7ff353734a21fd8b06b1283aa11ebb",
        "f9509bf1e939b7d90f1bd9276b326985ef2cce90c49edd9bdaa433ed5bfaa5f3",
        "9968e3e707f0fe443df157669bb4010a226d9feb2b265504ee8ad7f3b0648cff",
    ]
    for catalog in catalogs.values():
        catalog_hashes: list[str] = []
        for leaf in receipts[event_ids[0]]:
            entry = catalog.resolve_exact(leaf["virtualPath"])
            assert entry is not None
            payload = catalog.open_archive_for(entry).read_entry(
                catalog.as_entry(entry), max_bytes=10 * 1024 * 1024
            )
            catalog_hashes.append(hashlib.sha256(payload).hexdigest())
        assert catalog_hashes == [leaf["sha256"] for leaf in receipts[event_ids[0]]]


@pytest.mark.parametrize("body", [
    "SoundUpgrade = Upgrade_DwarvenSiegeHammer\n      UnknownSound = Bad\n    End",
    "SoundUpgrade =\n      VoiceAttack = DwarfGuardianVoiceAttackHammer\n    End",
    "SoundUpgrade = Upgrade_DwarvenSiegeHammer\n      VoiceAttack = ../bad\n    End",
    "SoundUpgrade = Upgrade_DwarvenSiegeHammer\n      ExcludedUpgrades = Upgrade_Bad Upgrade_Bad\n      VoiceAttack = DwarfGuardianVoiceAttackHammer\n    End",
])
def test_upgrade_sound_selector_fails_closed_on_unknown_or_malformed(body: str) -> None:
    lineage = _lineage(
        "Object FixtureObject\n"
        "  ClientBehavior = UpgradeSoundSelectorClientBehavior ModuleTag_Test\n"
        f"    {body}\n"
        "  End\nEnd"
    )
    with pytest.raises(ModuleContractError):
        compile_upgrade_sound_selectors(lineage, "FixtureObject")


def test_bezier_projectile_behavior_types_complete_grammar_and_partial_runtime() -> None:
    lineage = _lineage(
        """
Object ProjectileFixture
  Behavior = BezierProjectileBehavior ModuleTag_Trajectory
    FirstHeight = 8
    SecondHeight = 0
    FirstPercentIndent = 43%
    SecondPercentIndent = 86%
    TumbleRandomly = Yes
    CrushStyle = No
    DieOnImpact = Yes
    BounceCount = 2
    BounceDistance = 40
    BounceFirstHeight = 24
    BounceSecondHeight = 16
    BounceFirstPercentIndent = 20%
    BounceSecondPercentIndent = 80%
    GroundHitFX = FX_GroundHit
    GroundHitWeapon = GroundHitWeapon
    GroundBounceFX = FX_GroundBounce
    GroundBounceWeapon = GroundBounceWeapon
    DetonateCallsKill = Yes
    FlightPathAdjustDistPerSecond = 50
    CurveFlattenMinDist = 100.5
    InvisibleFrames = 2
    PreLandingStateTime = 1000
    PreLandingEmotion = DOOM
    PreLandingEmotionRadius = 20.0
    FadeInTime = 300
    IgnoreTerrainHeight = No
    FirstPercentHeight = 25%
    SecondPercentHeight = 75%
    FinalStuckTime = 1766
    OrientToFlightPath = Yes
    GarrisonHitKillRequiredKindOf = INFANTRY HERO
    GarrisonHitKillForbiddenKindOf = MONSTER
    GarrisonHitKillCount = 3
    GarrisonHitKillFX = FX_GarrisonHit
    PreLandingEmotionAffectsAllies = Yes
  End
End
""",
        "ProjectileFixture",
    )

    row = compile_bezier_projectile_behaviors(lineage, "ProjectileFixture")[0]
    fields = row["fields"]
    assert row["runtimeStatus"] == "deferred"
    assert row["extraction"] == "typed"
    assert fields["FirstHeight"]["value"] == 8
    assert fields["FirstPercentIndent"]["percent"] == 43.0
    assert fields["FirstPercentIndent"]["ratio"] == 0.43
    assert fields["TumbleRandomly"]["value"] is True
    assert fields["CurveFlattenMinDist"]["value"] == 100.5
    assert fields["GarrisonHitKillRequiredKindOf"]["value"] == [
        "INFANTRY", "HERO"
    ]
    graph = row["effectGraph"]
    assert graph["kind"] == "bezier-projectile"
    assert graph["trajectory"] == {
        "kind": "cubic-bezier-envelope",
        "runtimeStatus": "executable",
        "firstHeight": 8.0,
        "secondHeight": 0.0,
        "firstIndentRatio": 0.43,
        "secondIndentRatio": 0.86,
        "progressAuthority": "external-authored-projectile-flight",
    }
    assert graph["executionEligibility"]["runtimeStatus"] == "deferred"
    assert set(graph["executionEligibility"]["blockers"]) == {
        "bounce", "impact", "kill", "weapon", "fx", "prelanding",
        "terrain", "presentation", "garrison",
    }
    validate_module_contracts([row], label="fixture")


def test_bezier_projectile_behavior_rejects_unknown_duplicate_and_graph_tamper() -> None:
    unknown = _lineage(
        """
Object ProjectileFixture
  Behavior = BezierProjectileBehavior ModuleTag_Trajectory
    FirstHeight = 8
    SecondHeight = 0
    FirstPercentIndent = 43%
    SecondPercentIndent = 86%
    InventedArc = Yes
  End
End
""",
        "ProjectileFixture",
    )
    with pytest.raises(ModuleContractError, match="unsupported fields"):
        compile_bezier_projectile_behaviors(unknown, "ProjectileFixture")

    duplicate = _lineage(
        """
Object ProjectileFixture
  Behavior = BezierProjectileBehavior ModuleTag_Trajectory
    FirstHeight = 8
    FirstHeight = 9
    SecondHeight = 0
    FirstPercentIndent = 43%
    SecondPercentIndent = 86%
  End
End
""",
        "ProjectileFixture",
    )
    with pytest.raises(ModuleContractError, match="duplicate"):
        compile_bezier_projectile_behaviors(duplicate, "ProjectileFixture")

    row = compile_bezier_projectile_behaviors(
        _lineage(
            """
Object ProjectileFixture
  Behavior = BezierProjectileBehavior ModuleTag_Trajectory
    FirstHeight = 8
    SecondHeight = 0
    FirstPercentIndent = 43%
    SecondPercentIndent = 86%
  End
End
""",
            "ProjectileFixture",
        ),
        "ProjectileFixture",
    )[0]
    row["effectGraph"]["trajectory"]["firstHeight"] = 9.0
    with pytest.raises(ModuleContractError, match="trajectory graph drifted"):
        validate_module_contracts([row], label="fixture")


def test_bezier_projectile_behavior_preserves_partial_authored_shape_without_arc_defaults() -> None:
    rows = compile_bezier_projectile_behaviors(
        _lineage(
            """
Object ProjectileFixture
  Behavior = BezierProjectileBehavior ModuleTag_Trajectory
    CrushStyle = Yes
  End
End
""",
            "ProjectileFixture",
        ),
        "ProjectileFixture",
    )
    assert len(rows) == 1
    row = rows[0]
    assert row["fields"]["CrushStyle"]["value"] is True
    assert row["effectGraph"]["trajectory"] == {
        "kind": "cubic-bezier-envelope",
        "runtimeStatus": "deferred",
        "authoredControlFields": [],
        "deferredReason": "incomplete-authored-cubic-controls",
    }
    assert "trajectory" in row["effectGraph"]["executionEligibility"]["blockers"]
    validate_module_contracts(rows, label="partial fixture")


def test_bezier_common_landing_shape_is_executable_without_promoting_variants() -> None:
    row = compile_bezier_projectile_behaviors(
        _lineage(
            """
Object KnockbackFixture
  Behavior = BezierProjectileBehavior ModuleTag_Landing
    FirstHeight = 24
    SecondHeight = 24
    FirstPercentIndent = 30%
    SecondPercentIndent = 70%
    TumbleRandomly = No
    CrushStyle = Yes
    DieOnImpact = No
    BounceCount = 1
    BounceDistance = 40
    BounceFirstHeight = 24
    BounceSecondHeight = 24
    BounceFirstPercentIndent = 20%
    BounceSecondPercentIndent = 80%
    GroundHitFX = FX_ThrownRockGroundHit
    GroundBounceFX = FX_ThrownRockBounceHit
  End
End
""",
            "KnockbackFixture",
        ),
        "KnockbackFixture",
    )[0]
    assert row["runtimeStatus"] == "executable"
    assert row["effectGraph"]["executionEligibility"] == {
        "runtimeStatus": "executable",
        "blockers": [],
    }
    assert row["effectGraph"]["arrival"] == {
        "kind": "authored-ground-impact-bounce",
        "runtimeStatus": "executable",
        "crushStyle": True,
        "dieOnImpact": False,
        "tumbleRandomly": False,
        "bounceCount": 1,
        "bounceDistance": 40.0,
        "bounceFirstHeight": 24.0,
        "bounceSecondHeight": 24.0,
        "bounceFirstIndentRatio": 0.2,
        "bounceSecondIndentRatio": 0.8,
        "groundHitFxId": "FX_ThrownRockGroundHit",
        "groundBounceFxId": "FX_ThrownRockBounceHit",
        "terminalPolicy": "land-and-clear-projectile-state",
    }
    validate_module_contracts([row], label="common landing")

    variant = deepcopy(row)
    variant["fields"]["OrientToFlightPath"] = {
        "authored": "Yes", "value": True,
        "sourceIni": row["sourceIni"], "line": row["line"],
    }
    variant["runtimeStatus"] = "deferred"
    variant["effectGraph"] = module_contracts_subject._bezier_effect_graph(
        variant["fields"]
    )
    assert variant["effectGraph"]["executionEligibility"]["runtimeStatus"] == "deferred"
    assert "presentation" in variant["effectGraph"]["executionEligibility"]["blockers"]
    validate_module_contracts([variant], label="common landing variant")


def test_bezier_common_landing_exact_canonical_partition() -> None:
    from openbfme_importer.catalog import InstallCatalog
    from openbfme_importer.module_census import (
        census_catalog_paths,
        read_catalog_documents,
    )

    expected = {
        "bfme2-retail": (203, 109),
        "rotwk-retail": (240, 139),
    }
    actual: dict[str, tuple[int, int]] = {}
    for label, catalog_path in census_catalog_paths().items():
        if not catalog_path.is_file():
            pytest.skip("retail catalogs are not available")
        rows: list[dict[str, object]] = []
        for virtual_path, source in read_catalog_documents(
            InstallCatalog.load(catalog_path)
        ):
            if (
                b"bezierprojectilebehavior" not in source.lower()
                or not virtual_path.casefold().startswith("data/ini/object/")
            ):
                continue
            document = parse_sage_document(source, virtual_path=virtual_path)
            for obj in document.objects:
                rows.extend(compile_bezier_projectile_behaviors((obj,), obj.name))
        actual[label] = (
            len(rows),
            sum(row["runtimeStatus"] == "executable" for row in rows),
        )
    assert actual == expected


def test_wall_hub_behavior_exact_effective_retail_corpora() -> None:
    from openbfme_importer.catalog import InstallCatalog
    from openbfme_importer.module_census import census_catalog_paths, read_catalog_documents

    expected = {
        "bfme2-retail": {
            "count": 19, "segments": 103, "distanceRows": 19,
            "duplicateDistanceModules": 0, "options": {"OPTION_ONE": 15, "OPTION_THREE": 2, "OPTION_TWO": 2},
            "builderRows": 11, "cliffRows": 12, "defineDistances": 0,
        },
        "rotwk-retail": {
            "count": 23, "segments": 151, "distanceRows": 25,
            "duplicateDistanceModules": 2, "options": {"OPTION_ONE": 17, "OPTION_THREE": 3, "OPTION_TWO": 3},
            "builderRows": 11, "cliffRows": 12, "defineDistances": 23,
        },
    }
    actual: dict[str, dict[str, object]] = {}
    for label, path in census_catalog_paths().items():
        rows: list[dict[str, object]] = []
        for virtual_path, source in read_catalog_documents(InstallCatalog.load(path)):
            if not virtual_path.casefold().startswith("data/ini/object/"):
                continue
            if b"wallhubbehavior" not in source.lower():
                continue
            for obj in parse_sage_document(source, virtual_path=virtual_path).objects:
                rows.extend(compile_wall_hub_behaviors([obj], obj.name))
        assert all(row["runtimeStatus"] == "deferred" for row in rows)
        assert all(row["extraction"] == "typed" for row in rows)
        actual[label] = {
            "count": len(rows),
            "segments": sum(len(row["fields"]["SegmentTemplateName"]) for row in rows),
            "distanceRows": sum(len(row["fields"]["MaxBuildoutDistance"]) for row in rows),
            "duplicateDistanceModules": sum(len(row["fields"]["MaxBuildoutDistance"]) > 1 for row in rows),
            "options": dict(sorted(Counter(row["fields"]["Options"]["value"] for row in rows).items())),
            "builderRows": sum("BuilderRadius" in row["fields"] for row in rows),
            "cliffRows": sum("CliffCapTemplateName" in row["fields"] for row in rows),
            "defineDistances": sum("define" in item for row in rows for item in row["fields"]["MaxBuildoutDistance"]),
        }
    assert actual == expected


def test_activate_module_special_power_exact_effective_retail_corpora() -> None:
    from openbfme_importer.catalog import InstallCatalog
    from openbfme_importer.module_census import census_catalog_paths, read_catalog_documents

    expected = {
        "bfme2-retail": {
            "count": 6, "triggers": 7, "location": 5, "currentTarget": 2,
            "targetKinds": {"OCLSpecialPower": 2, "SpecialPowerModule": 5},
            "defineRanges": 0, "effectRanges": 0, "mustFinish": 2,
        },
        "rotwk-retail": {
            "count": 12, "triggers": 24, "location": 18, "currentTarget": 6,
            "targetKinds": {"OCLSpecialPower": 2, "PlayerHealSpecialPower": 3, "SpecialPowerModule": 19},
            "defineRanges": 6, "effectRanges": 1, "mustFinish": 2,
        },
    }
    actual: dict[str, dict[str, object]] = {}
    for label, path in census_catalog_paths().items():
        rows: list[dict[str, object]] = []
        for virtual_path, source in read_catalog_documents(InstallCatalog.load(path)):
            if not virtual_path.casefold().startswith("data/ini/object/"):
                continue
            if b"activatemodulespecialpower" not in source.lower():
                continue
            for obj in parse_sage_document(source, virtual_path=virtual_path).objects:
                rows.extend(compile_activate_module_special_powers([obj], obj.name))
        routes = [route for row in rows for route in row["fields"]["TriggerSpecialPower"]]
        assert all(row["runtimeStatus"] == "executable" for row in rows)
        actual[label] = {
            "count": len(rows), "triggers": len(routes),
            "location": sum(route["targetMode"] == "LOCATION" for route in routes),
            "currentTarget": sum(route["targetMode"] == "CURRENT_TARGET" for route in routes),
            "targetKinds": dict(sorted(Counter(route["targetModuleKind"] for route in routes).items())),
            "defineRanges": sum("define" in row["fields"]["StartAbilityRange"] for row in rows),
            "effectRanges": sum("EffectRange" in row["fields"] for row in rows),
            "mustFinish": sum(row["fields"].get("MustFinishAbility", {}).get("value") is True for row in rows),
        }
    assert actual == expected


def test_weapon_mode_special_power_update_exact_effective_retail_corpora() -> None:
    from openbfme_importer.catalog import InstallCatalog
    from openbfme_importer.module_census import census_catalog_paths, read_catalog_documents

    expected = {
        "bfme2-retail": {
            "count": 3, "durations": {20000: 1, 25000: 1, 30000: 1},
            "modifiers": 2, "weaponFlags": 2, "lockSlots": 1, "startsPaused": 2,
        },
        "rotwk-retail": {
            "count": 9, "durations": {20000: 3, 30000: 6},
            "modifiers": 8, "weaponFlags": 3, "lockSlots": 1, "startsPaused": 8,
        },
    }
    actual: dict[str, dict[str, object]] = {}
    for label, path in census_catalog_paths().items():
        rows: list[dict[str, object]] = []
        for virtual_path, source in read_catalog_documents(InstallCatalog.load(path)):
            if not virtual_path.casefold().startswith("data/ini/object/"):
                continue
            if b"weaponmodespecialpowerupdate" not in source.lower():
                continue
            for obj in parse_sage_document(source, virtual_path=virtual_path).objects:
                rows.extend(compile_weapon_mode_special_power_updates([obj], obj.name))
        assert all(row["runtimeStatus"] == "executable" for row in rows)
        actual[label] = {
            "count": len(rows),
            "durations": dict(sorted(Counter(row["fields"]["Duration"]["milliseconds"] for row in rows).items())),
            "modifiers": sum("AttributeModifier" in row["fields"] for row in rows),
            "weaponFlags": sum("WeaponSetFlags" in row["fields"] for row in rows),
            "lockSlots": sum("LockWeaponSlot" in row["fields"] for row in rows),
            "startsPaused": sum(row["fields"]["StartsPaused"]["value"] is True for row in rows),
        }
    assert actual == expected


def test_dominate_enemy_special_power_exact_effective_retail_corpora() -> None:
    from openbfme_importer.catalog import InstallCatalog
    from openbfme_importer.module_census import census_catalog_paths, read_catalog_documents

    expected = {
        "bfme2-retail": {"count": 4, "radius": 3, "permanent": 1, "sound": 0, "modelCondition": 0},
        "rotwk-retail": {"count": 5, "radius": 3, "permanent": 2, "sound": 1, "modelCondition": 1},
    }
    actual: dict[str, dict[str, int]] = {}
    for label, path in census_catalog_paths().items():
        rows: list[dict[str, object]] = []
        for virtual_path, source in read_catalog_documents(InstallCatalog.load(path)):
            if not virtual_path.casefold().startswith("data/ini/object/"):
                continue
            if b"dominateenemyspecialpower" not in source.lower():
                continue
            for obj in parse_sage_document(source, virtual_path=virtual_path).objects:
                rows.extend(compile_dominate_enemy_special_powers([obj], obj.name))
        assert all(row["runtimeStatus"] == "executable" for row in rows)
        actual[label] = {
            "count": len(rows),
            "radius": sum("DominateRadius" in row["fields"] for row in rows),
            "permanent": sum(row["fields"].get("PermanentlyConvert", {}).get("value") is True for row in rows),
            "sound": sum("TriggerSound" in row["fields"] for row in rows),
            "modelCondition": sum("TriggerModelCondition" in row["fields"] for row in rows),
        }
    assert actual == expected


def test_grab_passenger_special_power_exact_effective_retail_corpora() -> None:
    from openbfme_importer.catalog import InstallCatalog
    from openbfme_importer.module_census import census_catalog_paths, read_catalog_documents

    expected = {
        "bfme2-retail": {"count": 4, "allowTree": 3, "initiateFx": 3},
        "rotwk-retail": {"count": 4, "allowTree": 3, "initiateFx": 3},
    }
    actual: dict[str, dict[str, int]] = {}
    for label, path in census_catalog_paths().items():
        rows: list[dict[str, object]] = []
        for virtual_path, source in read_catalog_documents(InstallCatalog.load(path)):
            if not virtual_path.casefold().startswith("data/ini/object/"):
                continue
            if b"grabpassengerspecialpower" not in source.lower():
                continue
            for obj in parse_sage_document(source, virtual_path=virtual_path).objects:
                rows.extend(compile_grab_passenger_special_powers([obj], obj.name))
        assert all(row["runtimeStatus"] == "deferred" for row in rows)
        actual[label] = {
            "count": len(rows),
            "allowTree": sum(row["fields"].get("AllowTree", {}).get("value") is True for row in rows),
            "initiateFx": sum("InitiateFX" in row["fields"] for row in rows),
        }
    assert actual == expected


def test_fling_passenger_special_ability_update_exact_effective_retail_corpora() -> None:
    from openbfme_importer.catalog import InstallCatalog
    from openbfme_importer.module_census import census_catalog_paths, read_catalog_documents

    expected = {
        "bfme2-retail": {"count": 2, "velocity": 1, "warhead": 1, "customAnim": 1, "mustFinish": 0},
        "rotwk-retail": {"count": 3, "velocity": 2, "warhead": 2, "customAnim": 1, "mustFinish": 1},
    }
    actual: dict[str, dict[str, int]] = {}
    for label, path in census_catalog_paths().items():
        rows: list[dict[str, object]] = []
        for virtual_path, source in read_catalog_documents(InstallCatalog.load(path)):
            if not virtual_path.casefold().startswith("data/ini/object/"):
                continue
            if b"flingpassengerspecialabilityupdate" not in source.lower():
                continue
            for obj in parse_sage_document(source, virtual_path=virtual_path).objects:
                rows.extend(compile_fling_passenger_special_ability_updates([obj], obj.name))
        assert all(row["runtimeStatus"] == "deferred" for row in rows)
        actual[label] = {
            "count": len(rows),
            "velocity": sum("FlingPassengerVelocity" in row["fields"] for row in rows),
            "warhead": sum("FlingPassengerLandingWarhead" in row["fields"] for row in rows),
            "customAnim": sum("CustomAnimAndDuration" in row["fields"] for row in rows),
            "mustFinish": sum(row["fields"].get("MustFinishAbility", {}).get("value") is True for row in rows),
        }
    assert actual == expected


def test_temporarily_defect_update_default_exact_effective_retail_corpora() -> None:
    from openbfme_importer.catalog import InstallCatalog
    from openbfme_importer.module_census import census_catalog_paths, read_catalog_documents

    actual: dict[str, tuple[int, int]] = {}
    for label, path in census_catalog_paths().items():
        matches = [
            (virtual_path, source)
            for virtual_path, source in read_catalog_documents(InstallCatalog.load(path))
            if virtual_path.casefold() == "data/ini/default/object.ini"
        ]
        assert len(matches) == 1
        row = compile_temporarily_defect_update_default(
            matches[0][1], source_ini=matches[0][0]
        )
        actual[label] = (
            row["fields"]["DefectDuration"]["milliseconds"],
            row["fields"]["DefectDuration"]["line"],
        )
    assert actual == {
        "bfme2-retail": (30000, 206), "rotwk-retail": (30000, 206),
    }


def test_repair_special_power_exact_effective_retail_corpora() -> None:
    from openbfme_importer.catalog import InstallCatalog
    from openbfme_importer.module_census import census_catalog_paths, read_catalog_documents

    actual: dict[str, tuple[int, set[str]]] = {}
    for label, path in census_catalog_paths().items():
        rows: list[dict[str, object]] = []
        for virtual_path, source in read_catalog_documents(InstallCatalog.load(path)):
            if not virtual_path.casefold().startswith("data/ini/object/") or b"repairspecialpower" not in source.lower():
                continue
            for obj in parse_sage_document(source, virtual_path=virtual_path).objects:
                rows.extend(compile_repair_special_powers([obj], obj.name))
        actual[label] = (
            len(rows), {row["fields"]["SpecialPowerTemplate"]["value"] for row in rows},
        )
    assert actual == {
        "bfme2-retail": (2, {"SpecialRepairStructure"}),
        "rotwk-retail": (3, {"SpecialRepairStructure"}),
    }


def test_horde_dispatch_special_power_exact_effective_retail_corpora() -> None:
    from openbfme_importer.catalog import InstallCatalog
    from openbfme_importer.module_census import census_catalog_paths, read_catalog_documents

    expected = {
        "bfme2-retail": {"count": 2, "startsAttack": 1, "paused": 0},
        "rotwk-retail": {"count": 2, "startsAttack": 1, "paused": 0},
    }
    actual: dict[str, dict[str, int]] = {}
    for label, path in census_catalog_paths().items():
        rows: list[dict[str, object]] = []
        for virtual_path, source in read_catalog_documents(InstallCatalog.load(path)):
            if not virtual_path.casefold().startswith("data/ini/object/") or b"hordedispatchspecialpower" not in source.lower():
                continue
            for obj in parse_sage_document(source, virtual_path=virtual_path).objects:
                rows.extend(compile_horde_dispatch_special_powers([obj], obj.name))
        actual[label] = {
            "count": len(rows),
            "startsAttack": sum(row["fields"].get("UpdateModuleStartsAttack", {}).get("value") is True for row in rows),
            "paused": sum(row["fields"]["StartsPaused"]["value"] is True for row in rows),
        }
    assert actual == expected


def test_toggle_deploy_exact_effective_retail_corpora() -> None:
    from openbfme_importer.catalog import InstallCatalog
    from openbfme_importer.module_census import census_catalog_paths, read_catalog_documents

    actual: dict[str, list[tuple[object, ...]]] = {}
    for label, path in census_catalog_paths().items():
        rows: list[tuple[object, ...]] = []
        for virtual_path, source in read_catalog_documents(InstallCatalog.load(path)):
            if b"toggledeployspecialabilityupdate" not in source.lower():
                continue
            for obj in parse_sage_document(source, virtual_path=virtual_path).objects:
                for toggle in compile_toggle_deploy_special_ability_updates(
                    [obj], obj.name
                ):
                    graph = toggle["effectGraph"]
                    rows.append(
                        (
                            obj.name,
                            toggle["sourceIni"],
                            toggle["line"],
                            graph["targetMode"],
                            graph["ignoreFacingCheck"],
                            graph["unpackTimeMs"],
                            graph["packTimeMs"],
                            graph["mustDeployToAttack"],
                            tuple(graph["autoAcquireModes"]),
                            graph["deployedAttributeModifierId"],
                            graph["soundDeployId"],
                            graph["soundUndeployId"],
                            graph["deployStyle"]["line"],
                        )
                    )
        actual[label] = rows

    common = (
        "DwarvenDemolisher",
        "data/ini/object/goodfaction/units/dwarven/dwarvenram.ini",
    )
    assert actual == {
        "bfme2-retail": [
            common
            + (
                421, "SELF", True, 2000, 2000, False,
                ("ATTACK_BUILDINGS",), "DwarvenDemolisherDeployModifier",
                "DwarfDemolisherDeployMS", "DwarfDemolisherUndeployMS", 380,
            )
        ],
        "rotwk-retail": [
            common
            + (
                422, "SELF", True, 2000, 2000, False,
                ("ATTACK_BUILDINGS",), "DwarvenDemolisherDeployModifier",
                "DwarfDemolisherDeployMS", "DwarfDemolisherUndeployMS", 381,
            )
        ],
    }


def test_nonshipping_special_power_exact_effective_retail_owners() -> None:
    from openbfme_importer.catalog import InstallCatalog
    from openbfme_importer.module_census import census_catalog_paths, read_catalog_documents

    actual: dict[str, list[tuple[object, ...]]] = {}
    for label, path in census_catalog_paths().items():
        rows: list[tuple[object, ...]] = []
        for virtual_path, source in read_catalog_documents(InstallCatalog.load(path)):
            folded = source.lower()
            if (
                b"deflectspecialpower" not in folded
                and b"splithordespecialpower" not in folded
            ):
                continue
            for obj in parse_sage_document(source, virtual_path=virtual_path).objects:
                for compiler in (
                    compile_deflect_special_powers,
                    compile_split_horde_special_powers,
                ):
                    for row in compiler([obj], obj.name):
                        graph = row["effectGraph"]
                        eligibility = graph["executionEligibility"]
                        rows.append(
                            (
                                row["module"],
                                obj.name,
                                obj.kind,
                                row["sourceIni"],
                                row["line"],
                                graph["specialPowerTemplateId"],
                                tuple(graph["subclassFields"]),
                                eligibility["runtimeStatus"],
                                eligibility["shippingAdmission"],
                                eligibility["retailOwnerMatch"],
                                eligibility["disposition"],
                            )
                        )
        actual[label] = sorted(rows)

    deflect_line = {"bfme2-retail": 688, "rotwk-retail": 753}
    for label in ("bfme2-retail", "rotwk-retail"):
        assert actual[label] == [
            (
                "DeflectSpecialPower",
                "MordorHaradrimObsolete",
                "Object",
                "data/ini/object/obsolete/evilmenharadrim.ini",
                deflect_line[label],
                "SpecialAbilityDeflectProjectiles",
                (),
                "deferred",
                False,
                True,
                "obsolete-non-shipping",
            ),
            (
                "SplitHordeSpecialPower",
                "LAElvenWarriorDoubleHorde",
                "ChildObject",
                "data/ini/object/cinematic/lastallianceunits.ini",
                2944,
                "SpecialAbilitySplitHorde",
                (),
                "deferred",
                False,
                True,
                "cinematic-non-shipping",
            ),
        ]


def test_stop_unleash_and_enemy_sense_exact_effective_retail_corpora() -> None:
    from openbfme_importer.catalog import InstallCatalog
    from openbfme_importer.module_census import census_catalog_paths, read_catalog_documents

    actual: dict[str, dict[str, object]] = {}
    for label, path in census_catalog_paths().items():
        stops: list[dict[str, object]] = []
        deploys: list[dict[str, object]] = []
        unleashes: list[dict[str, object]] = []
        senses: list[dict[str, object]] = []
        for virtual_path, source in read_catalog_documents(InstallCatalog.load(path)):
            if not virtual_path.casefold().startswith("data/ini/object/"):
                continue
            folded = source.lower()
            if not any(name in folded for name in (
                b"stopspecialpower", b"siegedeployspecialpower", b"unleashspecialpower", b"specialenemysenseupdate",
            )):
                continue
            for obj in parse_sage_document(source, virtual_path=virtual_path).objects:
                stops.extend(compile_stop_special_powers([obj], obj.name))
                deploys.extend(compile_siege_deploy_special_powers([obj], obj.name))
                unleashes.extend(compile_unleash_special_powers([obj], obj.name))
                senses.extend(compile_special_enemy_sense_updates([obj], obj.name))
        assert all(row["sourceIni"] and int(row["line"]) > 0 for row in stops + deploys + unleashes + senses)
        actual[label] = {
            "stop": [
                (
                    row["fields"]["SpecialPowerTemplate"]["value"],
                    row["fields"]["StopPowerTemplate"]["value"],
                ) for row in stops
            ],
            "deploy": [
                (
                    row["fields"]["SpecialPowerTemplate"]["value"],
                    row["fields"]["LowerDelay"]["milliseconds"],
                    row["fields"]["RaiseDelay"]["milliseconds"],
                    row["fields"]["InitiateSound"]["value"],
                    row["fields"].get("ExtraWallDistance", {}).get("value"),
                ) for row in deploys
            ],
            "unleash": [
                (
                    row["fields"]["SpecialPowerTemplate"]["value"],
                    row["fields"]["UnpackTime"]["milliseconds"],
                    row["fields"]["AwardXPForTriggering"]["value"],
                    row["fields"]["Instant"]["value"],
                ) for row in unleashes
            ],
            "sense": sorted(
                (
                    tuple(row["fields"]["SpecialEnemyFilter"]["value"]),
                    row["fields"]["ScanRange"]["value"],
                    row["fields"]["ScanInterval"]["milliseconds"],
                ) for row in senses
            ),
        }
    stop_rows = [
        ("SpecialAbilityStop", "SpecialAbilitySiegeDeploy"),
        ("SpecialAbilityStop", "SpecialAbilitySiegeDeploy"),
    ]
    deploy_rows = [
        ("SpecialAbilitySiegeDeploy", 1200, 2000, "SiegeLadderVoiceAttackMS", 15.0),
        ("SpecialAbilitySiegeDeploy", 2000, 2000, "SiegeTowerVoiceAttackMS", None),
    ]
    sense_rows = sorted([
        (("ANY", "+ORC", "+URUK", "+MordorShelob"), 200, 2000),
        (("NONE", "+ORC", "+MordorShelob"), 200, 2000),
    ])
    assert actual == {
        "bfme2-retail": {
            "stop": stop_rows,
            "deploy": deploy_rows,
            "unleash": [("SpecialAbilityUnleash", 0, 0, True)],
            "sense": sense_rows,
        },
        "rotwk-retail": {"stop": stop_rows, "deploy": deploy_rows, "unleash": [], "sense": sense_rows},
    }


def test_citadel_slaughter_horde_contain_exact_effective_retail_corpora() -> None:
    from openbfme_importer.catalog import InstallCatalog
    from openbfme_importer.module_census import census_catalog_paths, read_catalog_documents

    expected = {
        "bfme2-retail": {
            "count": 19, "capacity": 1881, "cashback": {20.0: 2, 200.0: 17},
            "alliesRows": 17, "upgradeRows": 19,
            "destroyFilters": {("NONE", "+TheDroppedRing"): 19},
        },
        "rotwk-retail": {
            "count": 28, "capacity": 2772, "cashback": {20.0: 2, 200.0: 26},
            "alliesRows": 26, "upgradeRows": 26,
            "destroyFilters": {("NONE", "+PalantirShard"): 2, ("NONE", "+TheDroppedRing"): 26},
        },
    }
    actual: dict[str, dict[str, object]] = {}
    for label, path in census_catalog_paths().items():
        rows: list[dict[str, object]] = []
        for virtual_path, source in read_catalog_documents(InstallCatalog.load(path)):
            if not virtual_path.casefold().startswith("data/ini/object/"):
                continue
            if b"citadelslaughterhordecontain" not in source.lower():
                continue
            for obj in parse_sage_document(source, virtual_path=virtual_path).objects:
                rows.extend(compile_citadel_slaughter_horde_contains([obj], obj.name))
        assert all(row["runtimeStatus"] == "deferred" for row in rows)
        assert all(row["extraction"] == "typed" for row in rows)
        actual[label] = {
            "count": len(rows),
            "capacity": sum(row["fields"]["ContainMax"]["value"] for row in rows),
            "cashback": dict(sorted(Counter(row["fields"]["CashBackPercent"]["percent"] for row in rows).items())),
            "alliesRows": sum("AllowAlliesInside" in row["fields"] for row in rows),
            "upgradeRows": sum("UpgradeForRingEntry" in row["fields"] for row in rows),
            "destroyFilters": dict(sorted(Counter(tuple(row["fields"]["ObjectToDestroyForRingEntry"]["value"]) for row in rows).items())),
        }
    assert actual == expected


def test_replace_self_upgrade_exact_effective_retail_corpora() -> None:
    from openbfme_importer.catalog import InstallCatalog
    from openbfme_importer.module_census import census_catalog_paths, read_catalog_documents

    expected = {
        "bfme2-retail": {
            "count": 39, "addRows": 1, "additions": 2,
            "replacementFamilies": {"Dwarven": 10, "Elven": 2, "Gondor": 10, "Isengard": 2, "Men": 15},
        },
        "rotwk-retail": {
            "count": 69, "addRows": 2, "additions": 4,
            "replacementFamilies": {"Angmar": 5, "Arnor": 24, "Dwarven": 10, "Elven": 2, "Gondor": 11, "Isengard": 2, "Men": 15},
        },
    }
    actual: dict[str, dict[str, object]] = {}
    for label, path in census_catalog_paths().items():
        rows: list[dict[str, object]] = []
        for virtual_path, source in read_catalog_documents(InstallCatalog.load(path)):
            if not virtual_path.casefold().startswith("data/ini/object/"):
                continue
            if b"replaceselfupgrade" not in source.lower():
                continue
            for obj in parse_sage_document(source, virtual_path=virtual_path).objects:
                rows.extend(compile_replace_self_upgrades([obj], obj.name))
        assert all(row["runtimeStatus"] == "executable" for row in rows)
        assert all(row["extraction"] == "typed" for row in rows)
        families = Counter(
            next(prefix for prefix in ("Angmar", "Arnor", "Dwarven", "Elven", "Gondor", "Isengard", "Men") if row["fields"]["ReplaceWith"]["value"].startswith(prefix))
            for row in rows
        )
        actual[label] = {
            "count": len(rows),
            "addRows": sum("AndThenAddA" in row["fields"] for row in rows),
            "additions": sum(len(row["fields"].get("AndThenAddA", [])) for row in rows),
            "replacementFamilies": dict(sorted(families.items())),
        }
    assert actual == expected


def test_sound_fear_poison_damage_and_spawn_exact_effective_retail_corpora() -> None:
    from openbfme_importer.catalog import InstallCatalog
    from openbfme_importer.module_census import census_catalog_paths, read_catalog_documents

    expected = {
        "bfme2-retail": {
            "counts": (11, 2, 6, 2, 1, 3), "mountedStates": 10,
            "deployedStates": 1, "voiceMove": 11, "fearActive": 3,
            "unitSpecificStates": 1,
            "fearDefines": 2, "poisonDuration": 60000,
            "damageWeapons": ["RazorSpinesBasicWeapon"],
            "spawned": ["ElvenFortressEagle", "RohanOathbreakerHorde", "WildFortressFireDrake"],
        },
        "rotwk-retail": {
            "counts": (13, 2, 13, 2, 1, 3), "mountedStates": 12,
            "deployedStates": 1, "voiceMove": 13, "fearActive": 5,
            "unitSpecificStates": 1,
            "fearDefines": 3, "poisonDuration": 60000,
            "damageWeapons": ["RazorSpinesBasicWeapon"],
            "spawned": ["ElvenFortressEagle", "RohanOathbreakerHorde", "WildFortressFireDrake"],
        },
    }
    names = (
        b"modelconditionsoundselectorclientbehavior",
        b"randomsoundselectorclientbehavior", b"radiatefearupdate",
        b"poisonedbehavior", b"damagefieldupdate", b"spawnunitbehavior",
    )
    actual: dict[str, dict[str, object]] = {}
    for label, path in census_catalog_paths().items():
        buckets: list[list[dict[str, object]]] = [[] for _ in names]
        catalog = InstallCatalog.load(path)
        for virtual_path, source in read_catalog_documents(catalog):
            if not virtual_path.casefold().startswith("data/ini/object/"):
                continue
            if not any(name in source.lower() for name in names):
                continue
            for obj in parse_sage_document(source, virtual_path=virtual_path).objects:
                lineage = [obj]
                compilers = (
                    compile_model_condition_sound_selectors,
                    compile_random_sound_selectors, compile_radiate_fear_updates,
                    compile_poisoned_behaviors, compile_damage_field_updates,
                    compile_spawn_unit_behaviors,
                )
                for bucket, compiler in zip(buckets, compilers, strict=True):
                    bucket.extend(compiler(lineage, obj.name))
        selectors, randoms, fears, poisons, damages, spawns = buckets
        all_rows = [row for bucket in buckets for row in bucket]
        assert all(row["runtimeStatus"] == "executable" for row in all_rows)
        assert all(row["extraction"] == "typed" for row in all_rows)
        states = [state for row in selectors for state in row["fields"]["SoundState"]]
        actual[label] = {
            "counts": tuple(len(bucket) for bucket in buckets),
            "mountedStates": sum(state["conditions"] == ["MOUNTED"] for state in states),
            "deployedStates": sum(state["conditions"] == ["DEPLOYED"] for state in states),
            "voiceMove": sum("VoiceMove" in state["sounds"] for state in states),
            "unitSpecificStates": sum("unitSpecificSounds" in state for state in states),
            "fearActive": sum(row["fields"]["InitiallyActive"]["value"] for row in fears),
            "fearDefines": sum("define" in row["fields"]["EmotionPulseRadius"] for row in fears),
            "poisonDuration": sum(row["fields"]["PoisonDuration"]["milliseconds"] for row in poisons),
            "damageWeapons": sorted(row["fields"]["FireWeaponNugget"]["WeaponName"]["value"] for row in damages),
            "spawned": sorted(row["fields"]["UnitName"]["value"] for row in spawns),
        }
    assert actual == expected


def test_audio_hit_animal_and_threat_exact_effective_retail_corpora() -> None:
    from openbfme_importer.catalog import InstallCatalog
    from openbfme_importer.module_census import census_catalog_paths, read_catalog_documents

    expected = {
        "bfme2-retail": {
            "counts": (220, 86, 22, 2), "audioWeighted": 24,
            "audioWeightTotal": 59, "hitThreeTier": 67, "hitFastTrue": 14,
            "hitFastFalse": 5, "animalFleeDistance": 18,
            "animalUpdateTimer": 19, "threatRadius": 200.0,
        },
        "rotwk-retail": {
            "counts": (285, 112, 22, 2), "audioWeighted": 32,
            "audioWeightTotal": 75, "hitThreeTier": 86, "hitFastTrue": 18,
            "hitFastFalse": 6, "animalFleeDistance": 18,
            "animalUpdateTimer": 19, "threatRadius": 200.0,
        },
    }
    actual: dict[str, dict[str, object]] = {}
    needles = (
        b"largegroupaudioupdate", b"hitreactionbehavior",
        b"animalaiupdate", b"threatfinderupdate",
    )
    for label, path in census_catalog_paths().items():
        audio_rows: list[dict[str, object]] = []
        hit_rows: list[dict[str, object]] = []
        animal_rows: list[dict[str, object]] = []
        threat_rows: list[dict[str, object]] = []
        for virtual_path, source in read_catalog_documents(InstallCatalog.load(path)):
            if (
                not virtual_path.casefold().startswith("data/ini/object/")
                and virtual_path.casefold() != "data/ini/crate.ini"
            ):
                continue
            if not any(needle in source.lower() for needle in needles):
                continue
            for obj in parse_sage_document(source, virtual_path=virtual_path).objects:
                lineage = [obj]
                audio_rows.extend(compile_large_group_audio_updates(lineage, obj.name))
                hit_rows.extend(compile_hit_reaction_behaviors(lineage, obj.name))
                animal_rows.extend(compile_animal_ai_updates(lineage, obj.name))
                threat_rows.extend(compile_threat_finder_updates(lineage, obj.name))
        all_rows = audio_rows + hit_rows + animal_rows + threat_rows
        assert all(row["runtimeStatus"] == "executable" for row in all_rows)
        assert all(row["extraction"] == "typed" for row in all_rows)
        assert all(
            field["sourceIni"] and field["line"] > 0
            for row in all_rows for field in row["fields"].values()
            if isinstance(field, dict) and "authored" in field
        )
        actual[label] = {
            "counts": (len(audio_rows), len(hit_rows), len(animal_rows), len(threat_rows)),
            "audioWeighted": sum("UnitWeight" in row["fields"] for row in audio_rows),
            "audioWeightTotal": sum(row["fields"].get("UnitWeight", {"value": 1})["value"] for row in audio_rows if "UnitWeight" in row["fields"]),
            "hitThreeTier": sum("HitReactionLifeTimer3" in row["fields"] for row in hit_rows),
            "hitFastTrue": sum(row["fields"].get("FastHitsResetReaction", {}).get("value") is True for row in hit_rows),
            "hitFastFalse": sum(row["fields"].get("FastHitsResetReaction", {}).get("value") is False for row in hit_rows),
            "animalFleeDistance": sum("FleeDistance" in row["fields"] for row in animal_rows),
            "animalUpdateTimer": sum("UpdateTimer" in row["fields"] for row in animal_rows),
            "threatRadius": sum(row["fields"]["DefaultRadius"]["value"] for row in threat_rows),
        }
    assert actual == expected


def test_typed_class_c_contracts_accept_every_effective_retail_site() -> None:
    """The typed field schemas must cover both measured retail trees exactly."""

    from openbfme_importer.catalog import InstallCatalog
    from openbfme_importer.module_census import (
        census_catalog_paths,
        read_catalog_documents,
    )
    from openbfme_importer.playable_unit_compiler import (
        _ancestry,
        PlayableUnitCompilerError,
        prepare_playable_unit_compiler,
    )

    paths = census_catalog_paths()
    if not all(path.is_file() for path in paths.values()):
        pytest.skip("retail catalogs are not available")
    expected = {
        "bfme2-retail": {
            "PhysicsBehavior": 453,
            "FireWeaponWhenDeadBehavior": 63,
            "ShipSlowDeathBehavior": 10,
            "HordeTransportContain": 8,
            "AttributeModifierAuraUpdate": 198,
            "LifetimeUpdate": 163,
            "AIUpdateInterface": 429,
            "StancesBehavior": 113,
            "HordeContain": 75,
            "HordeAIUpdate": 54,
            "PickupStuffUpdate": 32,
            "AutoAbilityBehavior": 62,
            "RespawnUpdate": 38,
            "DualWeaponBehavior": 28,
            "CastleMemberBehavior": 145,
            "EmotionTrackerUpdate": 139,
            "FireWeaponUpdate": 29,
            "DeletionUpdate": 27,
            "SiegeDockingBehavior": 33,
            "RefundDie": 18,
            "FireSpreadUpdate": 10, "InvisibilityUpdate": 18,
            "AttachUpdate": 8, "ClearanceTestingSlowDeathBehavior": 12,
            "ProductionUpdate": 305, "SquishCollide": 301,
            "GettingBuiltBehavior": 269,
            "AISpecialPowerUpdate": 178,
            "BuildingBehavior": 127,
            "QueueProductionExitUpdate": 94, "HordeMemberCollide": 52,
            "BannerCarrierUpdate": 26,
            "RespawnBody": 37, "NotifyTargetsOfImminentProbableCrushingUpdate": 34,
            "FoundationAIUpdate": 32, "MonitorConditionUpdate": 23,
            "GiveUpgradeUpdate": 39, "GateOpenAndCloseBehavior": 26,
            "AIGateUpdate": 12, "FakePathfindPortalBehaviour": 12,
            "StealthDetectorUpdate": 20,
            "SlavedUpdate": 23, "CastleUpgrade": 15,
            "DelayedDeathBody": 15,
            "DynamicPortalBehaviour": 14,
            "FlammableUpdate": 20,
            "SpawnBehavior": 11, "StealthUpdate": 12,
            "ObjectCreationUpgrade": 48, "OCLUpdate": 2,
            "TransportContain": 11, "TunnelContain": 4,
            "GarrisonContain": 1, "HordeGarrisonContain": 15,
            "LargeGroupBonusUpdate": 4, "ProductionQueueHordeContain": 2,
            "SiegeEngineContain": 4,
        },
        "rotwk-retail": {
            "PhysicsBehavior": 560,
            "FireWeaponWhenDeadBehavior": 63,
            "ShipSlowDeathBehavior": 10,
            "HordeTransportContain": 8,
            "AttributeModifierAuraUpdate": 236,
            "LifetimeUpdate": 202,
            "AIUpdateInterface": 542,
            "StancesBehavior": 172,
            "HordeContain": 121,
            "HordeAIUpdate": 84,
            "PickupStuffUpdate": 59,
            "AutoAbilityBehavior": 87,
            "RespawnUpdate": 58,
            "DualWeaponBehavior": 40,
            "CastleMemberBehavior": 177,
            "EmotionTrackerUpdate": 197,
            "FireWeaponUpdate": 37,
            "DeletionUpdate": 34,
            "SiegeDockingBehavior": 33,
            "RefundDie": 30,
            "FireSpreadUpdate": 10, "InvisibilityUpdate": 20,
            "AttachUpdate": 16, "ClearanceTestingSlowDeathBehavior": 12,
            "ProductionUpdate": 396, "SquishCollide": 374,
            "GettingBuiltBehavior": 350,
            "AISpecialPowerUpdate": 732,
            "BuildingBehavior": 155,
            "QueueProductionExitUpdate": 121, "HordeMemberCollide": 91,
            "BannerCarrierUpdate": 41,
            "RespawnBody": 56, "NotifyTargetsOfImminentProbableCrushingUpdate": 40,
            "FoundationAIUpdate": 39, "MonitorConditionUpdate": 27,
            "GiveUpgradeUpdate": 39, "GateOpenAndCloseBehavior": 32,
            "AIGateUpdate": 18, "FakePathfindPortalBehaviour": 18,
            "StealthDetectorUpdate": 16,
            "SlavedUpdate": 30, "CastleUpgrade": 24,
            "DelayedDeathBody": 14,
            "DynamicPortalBehaviour": 17,
            "FlammableUpdate": 21,
            "SpawnBehavior": 17, "StealthUpdate": 16,
            "ObjectCreationUpgrade": 65, "OCLUpdate": 3,
            "TransportContain": 12, "TunnelContain": 4,
            "GarrisonContain": 2, "HordeGarrisonContain": 18,
            "LargeGroupBonusUpdate": 7, "ProductionQueueHordeContain": 2,
            "SiegeEngineContain": 4,
        },
    }
    expected_ship_transport = {
        "bfme2-retail": {
            "shipExcludedFaded": 2,
            "shipSoundRows": 8,
            "shipDestructionDelays": {10000},
            "shipSinkRates": {12.0},
            "transportSlotTotal": 22,
            "transportPassengerBones": 10,
            "transportPayloadRows": 4,
            "transportEnterFadeTimes": {3000},
        },
        "rotwk-retail": {
            "shipExcludedFaded": 2,
            "shipSoundRows": 8,
            "shipDestructionDelays": {10000},
            "shipSinkRates": {12.0},
            "transportSlotTotal": 22,
            "transportPassengerBones": 10,
            "transportPayloadRows": 4,
            "transportEnterFadeTimes": {6000},
        },
    }
    expected_aura_lifetime = {
        "bfme2-retail": {
            "auraStartsActive": {True: 168, False: 30},
            "auraRefreshDelay": {1000: 11, 2000: 184, 2500: 1, 3800: 1, 5000: 1},
            "auraTargetEnemy": {True: 15},
            "auraMaxRankRows": 9,
            "auraDefineRanges": 5,
            "lifetimeBoundRows": 161,
            "lifetimeWakeRows": 2,
            "lifetimeFadedRows": 64,
            "lifetimeDefineBounds": 114,
        },
        "rotwk-retail": {
            "auraStartsActive": {True: 185, False: 51},
            "auraRefreshDelay": {1000: 11, 2000: 222, 2500: 2, 5000: 1},
            "auraTargetEnemy": {True: 26, False: 1},
            "auraMaxRankRows": 9,
            "auraDefineRanges": 13,
            "lifetimeBoundRows": 199,
            "lifetimeWakeRows": 3,
            "lifetimeFadedRows": 87,
            "lifetimeDefineBounds": 166,
        },
    }
    expected_ai = {
        "bfme2-retail": {
            "emptyRows": 101,
            "autoAcquire": {True: 275, False: 30},
            "attackBuildings": 168,
            "stealthed": 16,
            "moodRates": {20: 1, 250: 77, 500: 126, 2500: 2, 5000: 1},
            "burningDefineRows": 62,
            "turretRows": 4,
            "turretSlots": 4,
        },
        "rotwk-retail": {
            "emptyRows": 104,
            "autoAcquire": {True: 375, False: 38},
            "attackBuildings": 233,
            "stealthed": 25,
            "moodRates": {20: 5, 250: 110, 500: 171, 2500: 2, 5000: 1},
            "burningDefineRows": 106,
            "turretRows": 5,
            "turretSlots": 5,
        },
    }
    expected_stances = {
        "bfme2-retail": {
            "ArcherHorde": 13,
            "Artillery": 9,
            "CavalryHorde": 5,
            "FighterHorde": 39,
            "Hero": 41,
            "PikeHorde": 6,
        },
        "rotwk-retail": {
            "ArcherHorde": 18,
            "Artillery": 11,
            "CavalryHorde": 16,
            "FighterHorde": 57,
            "Hero": 60,
            "PikeHorde": 10,
        },
    }
    expected_horde_contain = {
        "bfme2-retail": {
            "fieldCounts": {
                "AlternateFormation": 29, "AngleLimitCos": 2,
                "AttributeModifiers": 12, "BackUpMaxDelayTime": 26,
                "BackUpMaxDistance": 26, "BackUpMinDelayTime": 26,
                "BackUpMinDistance": 26, "BackupPercentage": 26,
                "BannerCarrierPosition": 54, "BannerCarriersAllowed": 54,
                "FacingBonus": 2, "FlankedDelay": 41, "FrontAngle": 41,
                "InitialPayload": 75, "InnerRange": 2,
                "IsPorcupineFormation": 7, "MeleeAttackLeashDistance": 52,
                "MeleeBehavior": 40, "MinimumHordeSize": 7,
                "NotComboFormation": 7, "ObjectStatusOfContained": 75,
                "OuterRange": 2, "OuterRangeBuildings": 2,
                "PassengerFilter": 75, "RandomOffset": 71, "RankInfo": 75,
                "RankSplit": 1, "RanksToJustFreeWhenAttacking": 6,
                "RanksToReleaseWhenAttacking": 74, "ShowPips": 75,
                "Slots": 75, "SplitHorde": 1, "SplitHordeNumber": 1,
                "ThisFormationIsTheMainFormation": 65,
                "UseSlowHordeMovement": 4, "VisionRearOverride": 7,
                "VisionSideOverride": 7,
            },
            "payloadRows": 84, "offsetRows": 71, "rankRows": 171,
            "splitRows": 2, "meleeRows": 40,
        },
        "rotwk-retail": {
            "fieldCounts": {
                "AlternateFormation": 48, "AngleLimitCos": 8,
                "AttributeModifiers": 18, "BackUpMaxDelayTime": 50,
                "BackUpMaxDistance": 50, "BackUpMinDelayTime": 50,
                "BackUpMinDistance": 50, "BackupPercentage": 50,
                "BannerCarrierDestroyHordeOnDeath": 11,
                "BannerCarrierHordeDeathType": 11,
                "BannerCarrierMinLevel": 11, "BannerCarrierPosition": 87,
                "BannerCarriersAllowed": 87, "FacingBonus": 8,
                "FlankedDelay": 78, "FrontAngle": 78, "InitialPayload": 121,
                "InnerRange": 8, "IsPorcupineFormation": 11,
                "LivingWorldOverloadTemplate": 8,
                "MeleeAttackLeashDistance": 89, "MeleeBehavior": 77,
                "MinimumHordeSize": 11, "NotComboFormation": 11,
                "ObjectStatusOfContained": 121, "OuterRange": 8,
                "OuterRangeBuildings": 8, "PassengerFilter": 121,
                "RandomOffset": 114, "RankInfo": 121, "RankSplit": 1,
                "RanksToJustFreeWhenAttacking": 10,
                "RanksToReleaseWhenAttacking": 120, "ShowPips": 121,
                "Slots": 121, "SplitHorde": 1, "SplitHordeNumber": 1,
                "ThisFormationIsTheMainFormation": 104,
                "UseSlowHordeMovement": 7, "VisionRearOverride": 11,
                "VisionSideOverride": 11,
            },
            "payloadRows": 130, "offsetRows": 117, "rankRows": 273,
            "splitRows": 2, "meleeRows": 77,
        },
    }
    expected_horde_ai = {
        "bfme2-retail": {
            "fieldCounts": {
                "AILuaEventsList": 49, "AttackPriority": 48,
                "AutoAcquireEnemiesWhenIdle": 54,
                "CanAttackWhileContained": 25, "MaxCowerTime": 54,
                "MinCowerTime": 54, "MoodAttackCheckRate": 54,
            },
            "luaRows": 53,
            "autoAcquire": {
                (True, ("ATTACK_BUILDINGS",)): 48,
                (True, ("ATTACK_BUILDINGS", "STEALTHED")): 5,
                (False, ()): 1,
            },
            "minCower": {3000: 52, 5000: 2},
            "maxCower": {5000: 52, 7500: 2},
        },
        "rotwk-retail": {
            "fieldCounts": {
                "AILuaEventsList": 75, "AttackPriority": 78,
                "AutoAcquireEnemiesWhenIdle": 84,
                "CanAttackWhileContained": 35, "MaxCowerTime": 84,
                "MinCowerTime": 84, "MoodAttackCheckRate": 84,
            },
            "luaRows": 80,
            "autoAcquire": {
                (True, ("ATTACK_BUILDINGS",)): 73,
                (True, ("ATTACK_BUILDINGS", "STEALTHED")): 10,
                (False, ()): 1,
            },
            "minCower": {3000: 82, 5000: 2},
            "maxCower": {5000: 82, 7500: 2},
        },
    }
    needles = (
        b"physicsbehavior",
        b"fireweaponwhendeadbehavior",
        b"shipslowdeathbehavior",
        b"hordetransportcontain",
        b"attributemodifierauraupdate",
        b"lifetimeupdate",
        b"aiupdateinterface",
        b"stancesbehavior",
        b"hordecontain",
        b"hordeaiupdate",
        b"pickupstuffupdate",
        b"autoabilitybehavior",
        b"respawnupdate",
        b"dualweaponbehavior",
        b"castlememberbehavior",
        b"emotiontrackerupdate",
        b"fireweaponupdate",
        b"deletionupdate",
        b"siegedockingbehavior",
        b"refunddie",
        b"firespreadupdate", b"invisibilityupdate", b"attachupdate",
        b"clearancetestingslowdeathbehavior",
        b"productionupdate", b"squishcollide", b"gettingbuiltbehavior",
        b"aispecialpowerupdate",
        b"buildingbehavior",
        b"queueproductionexitupdate", b"hordemembercollide", b"bannercarrierupdate",
        b"respawnbody", b"notifytargetsofimminentprobablecrushingupdate",
        b"foundationaiupdate", b"monitorconditionupdate",
        b"giveupgradeupdate", b"gateopenandclosebehavior", b"aigateupdate",
        b"fakepathfindportalbehaviour", b"stealthdetectorupdate",
        b"slavedupdate", b"castleupgrade",
        b"delayeddeathbody",
        b"dynamicportalbehaviour",
        b"flammableupdate",
        b"spawnbehavior", b"stealthupdate",
        b"objectcreationupgrade", b"oclupdate",
        b"transportcontain", b"tunnelcontain", b"garrisoncontain",
        b"hordegarrisoncontain",
        b"largegroupbonusupdate", b"productionqueuehordecontain",
        b"siegeenginecontain",
    )
    actual: dict[str, dict[str, int]] = {}
    actual_ship_transport: dict[str, dict[str, object]] = {}
    actual_aura_lifetime: dict[str, dict[str, object]] = {}
    actual_ai: dict[str, dict[str, object]] = {}
    actual_stances: dict[str, dict[str, int]] = {}
    actual_horde_contain: dict[str, dict[str, object]] = {}
    actual_horde_ai: dict[str, dict[str, object]] = {}
    actual_pickup: dict[str, dict[str, object]] = {}
    actual_auto_ability: dict[str, dict[str, object]] = {}
    actual_respawn: dict[str, dict[str, object]] = {}
    actual_dual_castle: dict[str, dict[str, object]] = {}
    actual_emotion: dict[str, dict[str, object]] = {}
    actual_fire_delete_refund: dict[str, dict[str, object]] = {}
    actual_stealth_clearance: dict[str, dict[str, object]] = {}
    actual_production_built: dict[str, dict[str, object]] = {}
    actual_ai_special: dict[str, dict[str, object]] = {}
    actual_building: dict[str, dict[str, object]] = {}
    actual_queue_banner: dict[str, dict[str, object]] = {}
    actual_respawn_monitor: dict[str, dict[str, object]] = {}
    actual_slaved_castle: dict[str, dict[str, object]] = {}
    actual_delayed_death: dict[str, dict[str, object]] = {}
    actual_dynamic_portal: dict[str, dict[str, object]] = {}
    actual_flammable: dict[str, dict[str, object]] = {}
    actual_spawn_stealth: dict[str, dict[str, object]] = {}
    actual_creation_ocl: dict[str, dict[str, object]] = {}
    actual_container_family: dict[str, dict[str, object]] = {}
    actual_bonus_siege: dict[str, dict[str, object]] = {}
    for label, path in paths.items():
        counts: Counter[str] = Counter()
        ship_rows: list[dict[str, object]] = []
        transport_rows: list[dict[str, object]] = []
        aura_rows: list[dict[str, object]] = []
        lifetime_rows: list[dict[str, object]] = []
        ai_rows: list[dict[str, object]] = []
        stance_rows: list[dict[str, object]] = []
        horde_rows: list[dict[str, object]] = []
        horde_ai_rows: list[dict[str, object]] = []
        pickup_rows: list[dict[str, object]] = []
        auto_ability_rows: list[dict[str, object]] = []
        respawn_rows: list[dict[str, object]] = []
        dual_rows: list[dict[str, object]] = []
        castle_rows: list[dict[str, object]] = []
        emotion_rows: list[dict[str, object]] = []
        fire_rows: list[dict[str, object]] = []
        deletion_rows: list[dict[str, object]] = []
        refund_rows: list[dict[str, object]] = []
        invis_rows: list[dict[str, object]] = []
        clearance_rows: list[dict[str, object]] = []
        production_rows: list[dict[str, object]] = []
        built_rows: list[dict[str, object]] = []
        ai_special_rows: list[dict[str, object]] = []
        building_rows: list[dict[str, object]] = []
        queue_rows: list[dict[str, object]] = []
        banner_rows: list[dict[str, object]] = []
        respawn_body_rows: list[dict[str, object]] = []
        monitor_rows: list[dict[str, object]] = []
        slaved_rows: list[dict[str, object]] = []
        castle_upgrade_rows: list[dict[str, object]] = []
        delayed_death_rows: list[dict[str, object]] = []
        dynamic_portal_rows: list[dict[str, object]] = []
        flammable_rows: list[dict[str, object]] = []
        spawn_rows: list[dict[str, object]] = []
        spawn_true_authored_owners: list[str] = []
        stealth_rows: list[dict[str, object]] = []
        creation_rows: list[dict[str, object]] = []
        ocl_rows: list[dict[str, object]] = []
        transport_rows2: list[dict[str, object]] = []
        tunnel_rows: list[dict[str, object]] = []
        garrison_rows: list[dict[str, object]] = []
        horde_garrison_rows: list[dict[str, object]] = []
        bonus_rows: list[dict[str, object]] = []
        production_queue_rows: list[dict[str, object]] = []
        siege_rows: list[dict[str, object]] = []
        catalog = InstallCatalog.load(path)
        documents = dict(read_catalog_documents(catalog))
        prepared = prepare_playable_unit_compiler(documents)
        for virtual_path, source in documents.items():
            folded = source.lower()
            if not any(needle in folded for needle in needles):
                continue
            # These typed module contracts are Object-carried; crate.ini also
            # authors Object blocks, while weapon.ini can mention their names
            # inside a different grammar that is intentionally parsed elsewhere.
            if (
                not virtual_path.casefold().startswith("data/ini/object/")
                and virtual_path.casefold() != "data/ini/crate.ini"
            ):
                continue
            document = parse_sage_document(source, virtual_path=virtual_path)
            for obj in document.objects:
                lineage = [obj]
                counts["PhysicsBehavior"] += len(
                    compile_physics_behaviors(lineage, obj.name)
                )
                counts["FireWeaponWhenDeadBehavior"] += len(
                    compile_fire_weapon_when_dead_behaviors(lineage, obj.name)
                )
                ships = compile_ship_slow_death_behaviors(lineage, obj.name)
                transports = compile_horde_transport_contains(lineage, obj.name)
                counts["ShipSlowDeathBehavior"] += len(ships)
                counts["HordeTransportContain"] += len(transports)
                ship_rows.extend(ships)
                transport_rows.extend(transports)
                auras = compile_attribute_modifier_aura_updates(lineage, obj.name)
                lifetimes = compile_lifetime_updates(lineage, obj.name)
                counts["AttributeModifierAuraUpdate"] += len(auras)
                counts["LifetimeUpdate"] += len(lifetimes)
                aura_rows.extend(auras)
                lifetime_rows.extend(lifetimes)
                ais = compile_ai_update_interfaces(lineage, obj.name)
                counts["AIUpdateInterface"] += len(ais)
                ai_rows.extend(ais)
                stances = compile_stances_behaviors(lineage, obj.name)
                counts["StancesBehavior"] += len(stances)
                stance_rows.extend(stances)
                hordes = compile_horde_contains(lineage, obj.name)
                counts["HordeContain"] += len(hordes)
                horde_rows.extend(hordes)
                horde_ais = compile_horde_ai_updates(lineage, obj.name)
                counts["HordeAIUpdate"] += len(horde_ais)
                horde_ai_rows.extend(horde_ais)
                pickups = compile_pickup_stuff_updates(lineage, obj.name)
                counts["PickupStuffUpdate"] += len(pickups)
                pickup_rows.extend(pickups)
                auto_abilities = compile_auto_ability_behaviors(lineage, obj.name)
                counts["AutoAbilityBehavior"] += len(auto_abilities)
                auto_ability_rows.extend(auto_abilities)
                respawns = compile_respawn_updates(
                    lineage,
                    obj.name,
                    numeric_defines=prepared.numeric_defines,
                    numeric_define_provenance=prepared.numeric_define_provenance,
                )
                counts["RespawnUpdate"] += len(respawns)
                respawn_rows.extend(respawns)
                duals = compile_dual_weapon_behaviors(lineage, obj.name)
                castles = compile_castle_member_behaviors(lineage, obj.name)
                counts["DualWeaponBehavior"] += len(duals)
                counts["CastleMemberBehavior"] += len(castles)
                dual_rows.extend(duals)
                castle_rows.extend(castles)
                emotions = compile_emotion_tracker_updates(lineage, obj.name)
                counts["EmotionTrackerUpdate"] += len(emotions)
                emotion_rows.extend(emotions)
                fires = compile_fire_weapon_updates(lineage, obj.name)
                deletions = compile_deletion_updates(lineage, obj.name)
                docks = compile_siege_docking_behaviors(lineage, obj.name)
                refunds = compile_refund_die_behaviors(lineage, obj.name)
                counts["FireWeaponUpdate"] += len(fires)
                counts["DeletionUpdate"] += len(deletions)
                counts["SiegeDockingBehavior"] += len(docks)
                counts["RefundDie"] += len(refunds)
                fire_rows.extend(fires)
                deletion_rows.extend(deletions)
                refund_rows.extend(refunds)
                spreads = compile_fire_spread_updates(lineage, obj.name)
                invis = compile_invisibility_updates(lineage, obj.name)
                attaches = compile_attach_updates(lineage, obj.name)
                clearance = compile_clearance_testing_slow_death_behaviors(lineage, obj.name)
                counts["FireSpreadUpdate"] += len(spreads)
                counts["InvisibilityUpdate"] += len(invis)
                counts["AttachUpdate"] += len(attaches)
                counts["ClearanceTestingSlowDeathBehavior"] += len(clearance)
                invis_rows.extend(invis); clearance_rows.extend(clearance)
                production = compile_production_updates(lineage, obj.name)
                squish = compile_squish_collides(lineage, obj.name)
                built = compile_getting_built_behaviors(lineage, obj.name)
                counts["ProductionUpdate"] += len(production)
                counts["SquishCollide"] += len(squish)
                counts["GettingBuiltBehavior"] += len(built)
                production_rows.extend(production); built_rows.extend(built)
                ai_special = compile_ai_special_power_updates(lineage, obj.name)
                counts["AISpecialPowerUpdate"] += len(ai_special)
                ai_special_rows.extend(ai_special)
                buildings = compile_building_behaviors(lineage, obj.name)
                counts["BuildingBehavior"] += len(buildings)
                building_rows.extend(buildings)
                queues = compile_queue_production_exit_updates(
                    lineage,
                    obj.name,
                    numeric_defines=prepared.numeric_defines,
                    numeric_define_provenance=prepared.numeric_define_provenance,
                )
                collides = compile_horde_member_collides(lineage, obj.name)
                banners = compile_banner_carrier_updates(lineage, obj.name)
                counts["QueueProductionExitUpdate"] += len(queues)
                counts["HordeMemberCollide"] += len(collides)
                counts["BannerCarrierUpdate"] += len(banners)
                queue_rows.extend(queues); banner_rows.extend(banners)
                respawn_bodies = compile_respawn_bodies(lineage, obj.name)
                notify = compile_notify_crushing_updates(lineage, obj.name)
                foundations = compile_foundation_ai_updates(lineage, obj.name)
                monitors = compile_monitor_condition_updates(lineage, obj.name)
                counts["RespawnBody"] += len(respawn_bodies)
                counts["NotifyTargetsOfImminentProbableCrushingUpdate"] += len(notify)
                counts["FoundationAIUpdate"] += len(foundations)
                counts["MonitorConditionUpdate"] += len(monitors)
                respawn_body_rows.extend(respawn_bodies); monitor_rows.extend(monitors)
                counts["GiveUpgradeUpdate"] += len(compile_give_upgrade_updates(lineage,obj.name))
                counts["GateOpenAndCloseBehavior"] += len(compile_gate_open_close_behaviors(lineage,obj.name))
                counts["AIGateUpdate"] += len(compile_ai_gate_updates(lineage,obj.name))
                counts["FakePathfindPortalBehaviour"] += len(compile_fake_pathfind_portals(lineage,obj.name))
                counts["StealthDetectorUpdate"] += len(compile_stealth_detector_updates(lineage,obj.name))
                slaved = compile_slaved_updates(lineage, obj.name)
                castle_upgrades = compile_castle_upgrades(lineage, obj.name)
                counts["SlavedUpdate"] += len(slaved)
                counts["CastleUpgrade"] += len(castle_upgrades)
                slaved_rows.extend(slaved)
                castle_upgrade_rows.extend(castle_upgrades)
                delayed_deaths = compile_delayed_death_bodies(lineage, obj.name)
                counts["DelayedDeathBody"] += len(delayed_deaths)
                delayed_death_rows.extend(delayed_deaths)
                portals = compile_dynamic_portal_behaviours(lineage, obj.name)
                counts["DynamicPortalBehaviour"] += len(portals)
                dynamic_portal_rows.extend(portals)
                flammables = compile_flammable_updates(lineage, obj.name)
                counts["FlammableUpdate"] += len(flammables)
                flammable_rows.extend(flammables)
                spawns = compile_spawn_behaviors(lineage, obj.name)
                stealths = compile_stealth_updates(lineage, obj.name)
                counts["SpawnBehavior"] += len(spawns)
                counts["StealthUpdate"] += len(stealths)
                spawn_rows.extend(spawns)
                if any(
                    row["fields"].get("CanReclaimOrphans", {}).get("value") is True
                    for row in spawns
                ):
                    spawn_true_authored_owners.append(obj.name)
                stealth_rows.extend(stealths)
                creations = compile_object_creation_upgrades(lineage, obj.name)
                ocls = compile_ocl_updates(lineage, obj.name)
                counts["ObjectCreationUpgrade"] += len(creations)
                counts["OCLUpdate"] += len(ocls)
                creation_rows.extend(creations)
                ocl_rows.extend(ocls)
                transports2 = compile_transport_contains(lineage, obj.name)
                tunnels = compile_tunnel_contains(lineage, obj.name)
                garrisons = compile_garrison_contains(lineage, obj.name)
                horde_garrisons = compile_horde_garrison_contains(lineage, obj.name)
                counts["TransportContain"] += len(transports2)
                counts["TunnelContain"] += len(tunnels)
                counts["GarrisonContain"] += len(garrisons)
                counts["HordeGarrisonContain"] += len(horde_garrisons)
                transport_rows2.extend(transports2); tunnel_rows.extend(tunnels)
                garrison_rows.extend(garrisons); horde_garrison_rows.extend(horde_garrisons)
                bonuses = compile_large_group_bonus_updates(lineage, obj.name)
                production_queues = compile_production_queue_horde_contains(lineage, obj.name)
                sieges = compile_siege_engine_contains(lineage, obj.name)
                counts["LargeGroupBonusUpdate"] += len(bonuses)
                counts["ProductionQueueHordeContain"] += len(production_queues)
                counts["SiegeEngineContain"] += len(sieges)
                bonus_rows.extend(bonuses); production_queue_rows.extend(production_queues)
                siege_rows.extend(sieges)
        actual[label] = dict(counts)
        actual_ship_transport[label] = {
            "shipExcludedFaded": sum(
                "FADED" in row["fields"]["excludedDeathTypes"]
                for row in ship_rows
            ),
            "shipSoundRows": sum("Sound" in row["fields"] for row in ship_rows),
            "shipDestructionDelays": {
                row["fields"]["DestructionDelay"]["milliseconds"]
                for row in ship_rows
            },
            "shipSinkRates": {
                row["fields"]["SinkRate"]["value"] for row in ship_rows
            },
            "transportSlotTotal": sum(
                row["fields"]["Slots"]["value"] for row in transport_rows
            ),
            "transportPassengerBones": sum(
                len(row["fields"]["PassengerBonePrefix"])
                for row in transport_rows
            ),
            "transportPayloadRows": sum(
                "InitialPayload" in row["fields"] for row in transport_rows
            ),
            "transportEnterFadeTimes": {
                row["fields"]["EnterFadeTime"]["milliseconds"]
                for row in transport_rows
                if "EnterFadeTime" in row["fields"]
            },
        }
        starts = Counter(
            row["fields"]["StartsActive"]["value"] for row in aura_rows
        )
        refresh = Counter(
            row["fields"]["RefreshDelay"]["milliseconds"] for row in aura_rows
        )
        target_enemy = Counter(
            row["fields"]["TargetEnemy"]["value"]
            for row in aura_rows
            if "TargetEnemy" in row["fields"]
        )
        actual_aura_lifetime[label] = {
            "auraStartsActive": dict(starts),
            "auraRefreshDelay": dict(refresh),
            "auraTargetEnemy": dict(target_enemy),
            "auraMaxRankRows": sum(
                "MaxActiveRank" in row["fields"] for row in aura_rows
            ),
            "auraDefineRanges": sum(
                "Range" in row["fields"]
                and "value" not in row["fields"]["Range"]
                for row in aura_rows
            ),
            "lifetimeBoundRows": sum(
                "MinLifetime" in row["fields"] for row in lifetime_rows
            ),
            "lifetimeWakeRows": sum(
                "WaitForWakeUp" in row["fields"] for row in lifetime_rows
            ),
            "lifetimeFadedRows": sum(
                row["fields"].get("DeathType", {}).get("value") == "FADED"
                for row in lifetime_rows
            ),
            "lifetimeDefineBounds": sum(
                sum(
                    field in row["fields"]
                    and "milliseconds" not in row["fields"][field]
                    for field in ("MinLifetime", "MaxLifetime")
                )
                for row in lifetime_rows
            ),
        }
        auto_acquire = [
            row["fields"]["AutoAcquireEnemiesWhenIdle"]
            for row in ai_rows
            if "AutoAcquireEnemiesWhenIdle" in row["fields"]
        ]
        mood_rates = Counter(
            row["fields"]["MoodAttackCheckRate"]["milliseconds"]
            for row in ai_rows
            if "MoodAttackCheckRate" in row["fields"]
        )
        actual_ai[label] = {
            "emptyRows": sum(not row["fields"] for row in ai_rows),
            "autoAcquire": dict(Counter(row["enabled"] for row in auto_acquire)),
            "attackBuildings": sum(
                "ATTACK_BUILDINGS" in row["flags"] for row in auto_acquire
            ),
            "stealthed": sum("STEALTHED" in row["flags"] for row in auto_acquire),
            "moodRates": dict(mood_rates),
            "burningDefineRows": sum(
                "BurningDeathTime" in row["fields"]
                and "milliseconds" not in row["fields"]["BurningDeathTime"]
                for row in ai_rows
            ),
            "turretRows": sum(len(row["fields"].get("Turrets", [])) for row in ai_rows),
            "turretSlots": sum(
                len(turret["ControlledWeaponSlots"]["value"])
                for row in ai_rows
                for turret in row["fields"].get("Turrets", [])
            ),
        }
        actual_stances[label] = dict(
            sorted(
                Counter(
                    row["fields"]["StanceTemplate"]["value"]
                    for row in stance_rows
                ).items(),
                key=lambda item: item[0].casefold(),
            )
        )
        actual_horde_contain[label] = {
            "fieldCounts": dict(sorted(Counter(
                field for row in horde_rows for field in row["fields"]
            ).items())),
            "payloadRows": sum(
                len(row["fields"].get("InitialPayload", [])) for row in horde_rows
            ),
            "offsetRows": sum(
                len(row["fields"].get("RandomOffset", [])) for row in horde_rows
            ),
            "rankRows": sum(
                len(row["fields"].get("RankInfo", [])) for row in horde_rows
            ),
            "splitRows": sum(
                len(row["fields"].get("SplitHorde", [])) for row in horde_rows
            ),
            "meleeRows": sum(
                "MeleeBehavior" in row["fields"] for row in horde_rows
            ),
        }
        actual_horde_ai[label] = {
            "fieldCounts": dict(sorted(Counter(
                field for row in horde_ai_rows for field in row["fields"]
            ).items())),
            "luaRows": sum(
                len(row["fields"].get("AILuaEventsList", []))
                for row in horde_ai_rows
            ),
            "autoAcquire": dict(Counter(
                (
                    row["fields"]["AutoAcquireEnemiesWhenIdle"]["enabled"],
                    tuple(row["fields"]["AutoAcquireEnemiesWhenIdle"]["flags"]),
                )
                for row in horde_ai_rows
            )),
            "minCower": dict(Counter(
                row["fields"]["MinCowerTime"]["milliseconds"]
                for row in horde_ai_rows
            )),
            "maxCower": dict(Counter(
                row["fields"]["MaxCowerTime"]["milliseconds"]
                for row in horde_ai_rows
            )),
        }
        actual_pickup[label] = {
            "skirmishOnly": sum(
                row["fields"]["SkirmishAIOnly"]["value"] for row in pickup_rows
            ),
            "filters": dict(Counter(
                tuple(row["fields"]["StuffToPickUp"]["value"])
                for row in pickup_rows
            )),
            "ranges": dict(Counter(
                row["fields"]["ScanRange"]["value"] for row in pickup_rows
            )),
            "intervalMs": dict(Counter(
                row["fields"]["ScanIntervalSeconds"]["milliseconds"]
                for row in pickup_rows
            )),
        }
        actual_auto_ability[label] = {
            "fieldCounts": dict(sorted(Counter(
                field for row in auto_ability_rows for field in row["fields"]
            ).items())),
            "queryRows": sum(
                len(row["fields"].get("Query", [])) for row in auto_ability_rows
            ),
            "maxRangeKinds": dict(Counter(
                row["fields"]["MaxScanRange"]["kind"]
                for row in auto_ability_rows if "MaxScanRange" in row["fields"]
            )),
        }
        actual_respawn[label] = {
            "fieldCounts": dict(sorted(Counter(
                field for row in respawn_rows for field in row["fields"]
            ).items())),
            "entryRows": sum(
                len(row["fields"].get("RespawnEntry", [])) for row in respawn_rows
            ),
            "entryLevels": dict(Counter(
                entry["level"] for row in respawn_rows
                for entry in row["fields"].get("RespawnEntry", [])
            )),
            "ruleAutoSpawn": dict(Counter(
                row["fields"]["RespawnRules"]["autoSpawn"] for row in respawn_rows
            )),
        }
        actual_dual_castle[label] = {
            "dualLiteral": sum(
                "value" in row["fields"]["SwitchWeaponOnCloseRangeDistance"]
                for row in dual_rows
            ),
            "dualDefine": sum(
                "value" not in row["fields"]["SwitchWeaponOnCloseRangeDistance"]
                for row in dual_rows
            ),
            "castleFieldCounts": dict(sorted(Counter(
                field for row in castle_rows for field in row["fields"]
            ).items())),
        }
        actual_emotion[label] = {
            "fieldCounts": dict(sorted(Counter(
                field for row in emotion_rows for field in row["fields"]
            ).items())),
            "emotionRows": sum(
                len(row["fields"].get("AddEmotion", [])) for row in emotion_rows
            ),
            "overrideRows": sum(
                item["override"] for row in emotion_rows
                for item in row["fields"].get("AddEmotion", [])
            ),
            "durationRows": sum(
                "Duration" in item for row in emotion_rows
                for item in row["fields"].get("AddEmotion", [])
            ),
        }
        actual_fire_delete_refund[label] = {
            "nuggetRows": sum(
                len(row["fields"]["FireWeaponNugget"]) for row in fire_rows
            ),
            "offsetRows": sum(
                "Offset" in nugget for row in fire_rows
                for nugget in row["fields"]["FireWeaponNugget"]
            ),
            "indefiniteDeletionRows": sum(
                row["fields"]["MinLifetime"].get("indefinite", False)
                for row in deletion_rows
            ),
            "defineDeletionRows": sum(
                not row["fields"]["MinLifetime"].get("indefinite", False)
                and "milliseconds" not in row["fields"]["MinLifetime"]
                for row in deletion_rows
            ),
            "conditionalRefundRows": sum(
                "UpgradeRequired" in row["fields"] for row in refund_rows
            ),
            "refundRuntimeStatus": dict(sorted(Counter(
                row["runtimeStatus"] for row in refund_rows
            ).items())),
        }
        actual_stealth_clearance[label] = {
            "invisibilityNuggets": sum(len(row["fields"]["InvisibilityNugget"]) for row in invis_rows),
            "probabilityRows": sum(len(row["fields"].get("ProbabilityModifier", [])) for row in clearance_rows),
            "deathFlagRows": sum(len(row["fields"].get("DeathFlags", [])) for row in clearance_rows),
            "minFractionRows": sum(len(row["fields"].get("ClearanceMinHeightFraction", [])) for row in clearance_rows),
        }
        actual_production_built[label] = {
            "modifierRows": sum(len(row["fields"].get("ProductionModifier", [])) for row in production_rows),
            "spawnDisabled": sum(row["fields"].get("SpawnTimer", {}).get("disabled", False) for row in built_rows),
            "spawnDefines": sum("define" in row["fields"].get("SpawnTimer", {}) for row in built_rows),
        }
        actual_ai_special[label] = {
            "radiusRows": sum("SpecialPowerRadius" in row["fields"] for row in ai_special_rows),
            "rangeRows": sum("SpecialPowerRange" in row["fields"] for row in ai_special_rows),
            "structureRows": sum("SpellMakesAStructure" in row["fields"] for row in ai_special_rows),
            "randomRows": sum("RandomizeTargetLocation" in row["fields"] for row in ai_special_rows),
            "radiusDefines": sum("SpecialPowerRadius" in row["fields"] and "value" not in row["fields"]["SpecialPowerRadius"] for row in ai_special_rows),
            "rangeDefines": sum("SpecialPowerRange" in row["fields"] and "value" not in row["fields"]["SpecialPowerRange"] for row in ai_special_rows),
        }
        actual_building[label] = {
            "nightRows": sum("NightWindowName" in row["fields"] for row in building_rows),
            "fireRows": sum(len(row["fields"].get("FireName", [])) for row in building_rows),
            "fireModules": sum("FireName" in row["fields"] for row in building_rows),
            "fireWindowRows": sum("FireWindowName" in row["fields"] for row in building_rows),
            "glowRows": sum("GlowWindowName" in row["fields"] for row in building_rows),
        }
        actual_queue_banner[label] = {
            "createPoints": sum(len(row["fields"]["UnitCreatePoint"]) for row in queue_rows),
            "rallyPoints": sum(len(row["fields"]["NaturalRallyPoint"]) for row in queue_rows),
            "invalidCoordinates": sum(not coord["validNumeric"] for row in queue_rows for key in ("UnitCreatePoint", "NaturalRallyPoint") for coord in row["fields"][key]),
            "runtimeStatus": dict(sorted(Counter(row["runtimeStatus"] for row in queue_rows).items())),
            "defineDelays": dict(sorted(Counter(
                (
                    delay["defineProvenance"]["sourceIni"],
                    delay["defineProvenance"]["line"],
                    delay["milliseconds"],
                )
                for row in queue_rows
                for delay in row["fields"].get("ExitDelay", [])
                if "defineProvenance" in delay
            ).items())),
            "morphRows": sum(len(row["fields"].get("MorphCondition", [])) for row in banner_rows),
            "locomotorMorphs": sum(morph["locomotor"] is not None for row in banner_rows for morph in row["fields"].get("MorphCondition", [])),
        }
        actual_respawn_monitor[label] = {
            "cheerRows": sum(len(row["fields"].get("CheerRadius", [])) for row in respawn_body_rows),
            "dodgeDefines": sum("define" in row["fields"].get("DodgePercent", {}) for row in respawn_body_rows),
            "weaponRoutes": sum("WeaponSetRoute" in row["fields"] for row in monitor_rows),
            "modelRoutes": sum("ModelConditionRoute" in row["fields"] for row in monitor_rows),
        }
        actual_slaved_castle[label] = {
            "slavedFieldCounts": dict(sorted(Counter(
                field for row in slaved_rows for field in row["fields"]
            ).items())),
            "guardOffsets": sum("GuardPositionOffset" in row["fields"] for row in slaved_rows),
            "fadeTimes": dict(Counter(
                row["fields"]["FadeTime"]["milliseconds"]
                for row in slaved_rows if "FadeTime" in row["fields"]
            )),
            "wallRadiusDefines": sum(
                "WallUpgradeRadius" in row["fields"]
                and "expression" in row["fields"]["WallUpgradeRadius"]
                for row in castle_upgrade_rows
            ),
            "castleFieldCounts": dict(sorted(Counter(
                field for row in castle_upgrade_rows for field in row["fields"]
            ).items())),
        }
        actual_delayed_death[label] = {
            "fieldCounts": dict(sorted(Counter(
                field for row in delayed_death_rows for field in row["fields"]
            ).items())),
            "deathTimes": dict(Counter(
                row["fields"]["DelayedDeathTime"]["milliseconds"]
                for row in delayed_death_rows
            )),
            "recoveryTimes": dict(Counter(
                row["fields"]["RecoveryTime"]["milliseconds"]
                for row in delayed_death_rows if "RecoveryTime" in row["fields"]
            )),
            "healthDefines": sum(
                "value" not in row["fields"]["MaxHealth"]
                for row in delayed_death_rows
            ),
        }
        actual_dynamic_portal[label] = {
            "wayPoints": sum(len(row["fields"]["WayPoint"]) for row in dynamic_portal_rows),
            "links": sum(len(row["fields"]["Link"]) for row in dynamic_portal_rows),
            "activationMs": dict(Counter(
                row["fields"]["ActivationDelaySeconds"]["milliseconds"]
                for row in dynamic_portal_rows if "ActivationDelaySeconds" in row["fields"]
            )),
            "upgradeRows": sum("TriggeredBy" in row["fields"] for row in dynamic_portal_rows),
            "topAttackRows": sum(
                "TopAttackPos" in row["fields"] and "TopAttackRadius" in row["fields"]
                for row in dynamic_portal_rows
            ),
        }
        actual_flammable[label] = {
            "fieldCounts": dict(sorted(Counter(
                field for row in flammable_rows for field in row["fields"]
            ).items())),
            "fireFxRows": sum(
                len(row["fields"].get("FireFXList", [])) for row in flammable_rows
            ),
            "multiplyLimits": sum(
                row["fields"].get("FlameDamageLimit", {}).get("operation") == "multiply"
                for row in flammable_rows
            ),
            "emptyRows": sum(not row["fields"] for row in flammable_rows),
        }
        spawn_true_effective_owners: list[str] = []
        for obj in prepared.objects.values():
            try:
                effective_spawn_rows = compile_spawn_behaviors(
                    _ancestry(prepared.objects, obj), obj.name
                )
            except PlayableUnitCompilerError:
                # A few unrelated decorative templates carry deliberately
                # parenthesized WB-only parent labels. They have no SpawnBehavior
                # and are outside this exact effective-owner audit.
                continue
            if any(
                row["runtimeStatus"] == "executable"
                and row["fields"].get("CanReclaimOrphans", {}).get("value") is True
                for row in effective_spawn_rows
            ):
                spawn_true_effective_owners.append(obj.name)
        spawn_true_effective_owners.sort()
        actual_spawn_stealth[label] = {
            "spawnFieldCounts": dict(sorted(Counter(
                field for row in spawn_rows for field in row["fields"]
            ).items())),
            "spawnTemplateTokens": sum(
                len(row["fields"]["SpawnTemplateName"]["value"]) for row in spawn_rows
            ),
            "reclaimFieldRuntimeStatus": dict(sorted(Counter(
                row["fields"]["CanReclaimOrphans"]["runtimeStatus"]
                for row in spawn_rows if "CanReclaimOrphans" in row["fields"]
            ).items())),
            "spawnRuntimeStatus": dict(sorted(Counter(
                row["runtimeStatus"] for row in spawn_rows
            ).items())),
            "reclaimTrueAuthoredOwners": sorted(spawn_true_authored_owners),
            "reclaimTrueEffectiveOwners": spawn_true_effective_owners,
            "stealthFieldCounts": dict(sorted(Counter(
                field for row in stealth_rows for field in row["fields"]
            ).items())),
            "emptyStealthRows": sum(not row["fields"] for row in stealth_rows),
        }
        actual_creation_ocl[label] = {
            "creationFieldCounts": dict(sorted(Counter(
                field for row in creation_rows for field in row["fields"]
            ).items())),
            "triggerTokens": sum(
                len(row["fields"]["TriggeredBy"]["value"]) for row in creation_rows
            ),
            "delayDefines": sum(
                "define" in row["fields"].get("Delay", {}) for row in creation_rows
            ),
            "oclBounds": dict(Counter(
                (row["fields"]["MinDelay"]["milliseconds"], row["fields"]["MaxDelay"]["milliseconds"])
                for row in ocl_rows
            )),
            "oclAmount": sum(row["fields"]["Amount"]["value"] for row in ocl_rows),
        }
        actual_container_family[label] = {
            "transportFields": dict(sorted(Counter(
                field for row in transport_rows2 for field in row["fields"]
            ).items())),
            "transportBones": sum(len(row["fields"].get("PassengerBonePrefix", [])) for row in transport_rows2),
            "transportSlots": sum(row["fields"]["Slots"]["value"] for row in transport_rows2),
            "tunnelCapacity": sum(row["fields"]["ContainMax"]["value"] for row in tunnel_rows),
            "tunnelExitDelays": dict(Counter(row["fields"]["ExitDelay"]["milliseconds"] for row in tunnel_rows if "ExitDelay" in row["fields"])),
            "garrisonCapacity": sum(row["fields"]["ContainMax"]["value"] for row in garrison_rows),
            "hordeCapacity": sum(row["fields"]["ContainMax"]["value"] for row in horde_garrison_rows),
            "hordeBones": sum(len(row["fields"].get("PassengerBonePrefix", [])) for row in horde_garrison_rows),
        }
        actual_bonus_siege[label] = {
            "bonusModifiers": dict(Counter(row["fields"]["AttributeModifier"]["value"] for row in bonus_rows)),
            "bonusCounts": dict(Counter(row["fields"]["Count"]["value"] for row in bonus_rows)),
            "queueCapacity": sum(row["fields"]["ContainMax"]["value"] for row in production_queue_rows),
            "siegeSlots": sum(row["fields"]["Slots"]["value"] for row in siege_rows),
            "siegeBoneStates": sum(len(row["fields"].get("BoneSpecificConditionState", [])) for row in siege_rows),
            "siegeCrewRows": sum("InitialCrew" in row["fields"] for row in siege_rows),
            "siegeSpeedRows": sum("SpeedPercentPerCrew" in row["fields"] for row in siege_rows),
        }
    assert actual == expected
    assert actual_ship_transport == expected_ship_transport
    assert actual_aura_lifetime == expected_aura_lifetime
    assert actual_ai == expected_ai
    assert actual_stances == expected_stances
    assert actual_horde_contain == expected_horde_contain
    assert actual_horde_ai == expected_horde_ai
    assert actual_pickup == {
        "bfme2-retail": {
            "skirmishOnly": 32, "filters": {("NONE", "+CRATE"): 32},
            "ranges": {200: 32}, "intervalMs": {500: 32},
        },
        "rotwk-retail": {
            "skirmishOnly": 59, "filters": {("NONE", "+CRATE"): 59},
            "ranges": {200: 59}, "intervalMs": {500: 59},
        },
    }
    assert actual_auto_ability == {
        "bfme2-retail": {
            "fieldCounts": {
                "AdjustAttackMeleePosition": 1, "AllowSelf": 2,
                "BaseMaxRangeFromStartPos": 1, "ForbiddenStatus": 1,
                "IdleTimeSeconds": 1, "MaxScanRange": 15, "MinScanRange": 1,
                "Query": 22, "SpecialAbility": 27, "StartsActive": 1,
            },
            "queryRows": 31,
            "maxRangeKinds": {"literal": 7, "define": 4, "subtract": 4},
        },
        "rotwk-retail": {
            "fieldCounts": {
                "AdjustAttackMeleePosition": 1, "AllowSelf": 8,
                "BaseMaxRangeFromStartPos": 1, "ForbiddenStatus": 7,
                "IdleTimeSeconds": 1, "MaxScanRange": 20, "MinScanRange": 1,
                "Query": 34, "SpecialAbility": 40, "StartsActive": 1,
            },
            "queryRows": 43,
            "maxRangeKinds": {"literal": 10, "define": 4, "subtract": 6},
        },
    }
    assert actual_respawn == {
        "bfme2-retail": {
            "fieldCounts": {
                "AutoRespawnAtObjectFilter": 38, "ButtonImage": 38,
                "DeathAnim": 38, "DeathAnimationTime": 34, "DeathFX": 36,
                "InitialSpawnFX": 32, "RespawnAnim": 34,
                "RespawnAnimationTime": 34, "RespawnAsTemplate": 2,
                "RespawnEntry": 38, "RespawnFX": 37, "RespawnRules": 38,
            },
            "entryRows": 342, "entryLevels": {level: 38 for level in range(2, 11)},
            "ruleAutoSpawn": {False: 38},
        },
        "rotwk-retail": {
            "fieldCounts": {
                "AutoRespawnAtObjectFilter": 58, "ButtonImage": 58,
                "DeathAnim": 58, "DeathAnimationTime": 52, "DeathFX": 56,
                "InitialSpawnFX": 51, "RespawnAnim": 52,
                "RespawnAnimationTime": 52, "RespawnAsTemplate": 3,
                "RespawnEntry": 12, "RespawnFX": 56, "RespawnRules": 58,
            },
            "entryRows": 108, "entryLevels": {level: 12 for level in range(2, 11)},
            "ruleAutoSpawn": {False: 58},
        },
    }
    assert actual_dual_castle == {
        "bfme2-retail": {
            "dualLiteral": 26, "dualDefine": 2,
            "castleFieldCounts": {
                "BeingBuiltSound": 21, "CampDestroyedAllyEvaEvent": 1,
                "CampDestroyedAttackerEvaEvent": 1,
                "CampDestroyedOwnerEvaEvent": 1,
                "CountsForEvaCastleBreached": 41, "StoreUpgradePrice": 2,
            },
        },
        "rotwk-retail": {
            "dualLiteral": 38, "dualDefine": 2,
            "castleFieldCounts": {
                "BeingBuiltSound": 28, "CampDestroyedAllyEvaEvent": 2,
                "CampDestroyedAttackerEvaEvent": 2,
                "CampDestroyedOwnerEvaEvent": 2,
                "CountsForEvaCastleBreached": 43, "StoreUpgradePrice": 2,
            },
        },
    }
    assert actual_emotion == {
        "bfme2-retail": {
            "fieldCounts": {
                "AddEmotion": 135, "AfraidOf": 78, "AlwaysAfraidOf": 76,
                "FearScanDistance": 78, "HeroScanDistance": 68,
                "IgnoreVeterancy": 3, "ImmuneToFearLevel": 1,
                "PointAt": 74, "QuarrelProbability": 1,
                "TauntAndPointDistance": 102,
                "TauntAndPointExcluded": 72,
                "TauntAndPointUpdateDelay": 103,
            },
            "emotionRows": 1121, "overrideRows": 24, "durationRows": 9,
        },
        "rotwk-retail": {
            "fieldCounts": {
                "AddEmotion": 193, "AfraidOf": 113, "AlwaysAfraidOf": 112,
                "FearScanDistance": 117, "HeroScanDistance": 105,
                "IgnoreVeterancy": 4, "ImmuneToFearLevel": 1,
                "PointAt": 111, "QuarrelProbability": 3,
                "TauntAndPointDistance": 154,
                "TauntAndPointExcluded": 109,
                "TauntAndPointUpdateDelay": 155,
            },
            "emotionRows": 1743, "overrideRows": 30, "durationRows": 9,
        },
    }
    assert actual_fire_delete_refund == {
        "bfme2-retail": {
            "nuggetRows": 40, "offsetRows": 3,
            "indefiniteDeletionRows": 5, "defineDeletionRows": 3,
            "conditionalRefundRows": 17,
            "refundRuntimeStatus": {"executable": 18},
        },
        "rotwk-retail": {
            "nuggetRows": 51, "offsetRows": 6,
            "indefiniteDeletionRows": 5, "defineDeletionRows": 3,
            "conditionalRefundRows": 29,
            "refundRuntimeStatus": {"executable": 30},
        },
    }
    assert actual_stealth_clearance == {
        "bfme2-retail": {"invisibilityNuggets": 18, "probabilityRows": 16, "deathFlagRows": 13, "minFractionRows": 14},
        "rotwk-retail": {"invisibilityNuggets": 20, "probabilityRows": 16, "deathFlagRows": 13, "minFractionRows": 14},
    }
    assert actual_production_built == {
        "bfme2-retail": {"modifierRows": 8, "spawnDisabled": 47, "spawnDefines": 121},
        "rotwk-retail": {"modifierRows": 16, "spawnDisabled": 56, "spawnDefines": 174},
    }
    assert actual_ai_special == {
        "bfme2-retail": {"radiusRows": 79, "rangeRows": 3, "structureRows": 4, "randomRows": 20, "radiusDefines": 0, "rangeDefines": 0},
        "rotwk-retail": {"radiusRows": 105, "rangeRows": 13, "structureRows": 4, "randomRows": 23, "radiusDefines": 2, "rangeDefines": 4},
    }
    assert actual_building == {
        "bfme2-retail": {"nightRows": 110, "fireRows": 4, "fireModules": 2, "fireWindowRows": 3, "glowRows": 6},
        "rotwk-retail": {"nightRows": 135, "fireRows": 4, "fireModules": 2, "fireWindowRows": 4, "glowRows": 8},
    }
    assert actual_queue_banner == {
        "bfme2-retail": {"createPoints": 94, "rallyPoints": 94, "invalidCoordinates": 0, "runtimeStatus": {"deferred": 22, "executable": 72}, "defineDelays": {("data/ini/gamedata.ini", 64, 10): 13}, "morphRows": 36, "locomotorMorphs": 13},
        "rotwk-retail": {"createPoints": 122, "rallyPoints": 122, "invalidCoordinates": 1, "runtimeStatus": {"deferred": 26, "executable": 95}, "defineDelays": {("data/ini/gamedata.ini", 66, 10): 22}, "morphRows": 42, "locomotorMorphs": 15},
    }
    assert actual_respawn_monitor == {
        "bfme2-retail": {"cheerRows": 31, "dodgeDefines": 23, "weaponRoutes": 10, "modelRoutes": 18},
        "rotwk-retail": {"cheerRows": 50, "dodgeDefines": 42, "weaponRoutes": 10, "modelRoutes": 22},
    }
    assert actual_slaved_castle == {
        "bfme2-retail": {
            "slavedFieldCounts": {
                "AttackRange": 2, "DieOnMastersDeath": 9, "FadeOutRange": 1,
                "FadeTime": 1, "GuardMaxRange": 14, "GuardPositionOffset": 1,
                "GuardWanderRange": 14, "LeashRange": 2, "MarkUnselectable": 6,
                "UseSlaverAsControlForEvaObjectSightedEvents": 15,
            },
            "guardOffsets": 1, "fadeTimes": {1000: 1}, "wallRadiusDefines": 4,
            "castleFieldCounts": {"TriggeredBy": 15, "Upgrade": 15, "WallUpgradeRadius": 4},
        },
        "rotwk-retail": {
            "slavedFieldCounts": {
                "AttackRange": 4, "DieOnMastersDeath": 14, "FadeOutRange": 2,
                "FadeTime": 2, "GuardMaxRange": 18, "GuardPositionOffset": 3,
                "GuardWanderRange": 18, "LeashRange": 4, "MarkUnselectable": 8,
                "UseSlaverAsControlForEvaObjectSightedEvents": 19,
            },
            "guardOffsets": 3, "fadeTimes": {1000: 2}, "wallRadiusDefines": 6,
            "castleFieldCounts": {"TriggeredBy": 24, "Upgrade": 24, "WallUpgradeRadius": 6},
        },
    }
    assert actual_delayed_death == {
        "bfme2-retail": {
            "fieldCounts": {
                "BurningDeathBehavior": 1, "BurningDeathFX": 1,
                "CanRespawn": 15, "CheerRadius": 8, "DelayedDeathTime": 15,
                "DoHealthCheck": 13, "DodgePercent": 1,
                "ImmortalUntilDeathTime": 6, "MaxHealth": 15,
                "MaxHealthDamaged": 7, "MaxHealthReallyDamaged": 5,
                "RecoveryTime": 1,
            },
            "deathTimes": {999999: 1, 1700: 1, 5000: 8, 15000: 1, 25000: 4},
            "recoveryTimes": {5000: 1}, "healthDefines": 10,
        },
        "rotwk-retail": {
            "fieldCounts": {
                "BurningDeathBehavior": 1, "BurningDeathFX": 1,
                "CanRespawn": 14, "CheerRadius": 8, "DelayedDeathTime": 14,
                "DoHealthCheck": 12, "ImmortalUntilDeathTime": 6,
                "MaxHealth": 14, "MaxHealthDamaged": 6,
                "MaxHealthReallyDamaged": 4, "RecoveryTime": 1,
            },
            "deathTimes": {999999: 1, 1700: 1, 5000: 7, 15000: 1, 25000: 4},
            "recoveryTimes": {5000: 1}, "healthDefines": 10,
        },
    }
    assert actual_dynamic_portal == {
        "bfme2-retail": {
            "wayPoints": 84, "links": 28,
            "activationMs": {0: 5, 7000: 2},
            "upgradeRows": 4, "topAttackRows": 2,
        },
        "rotwk-retail": {
            "wayPoints": 102, "links": 34,
            "activationMs": {0: 8, 7000: 2},
            "upgradeRows": 4, "topAttackRows": 2,
        },
    }
    assert actual_flammable == {
        "bfme2-retail": {
            "fieldCounts": {
                "AflameDamageAmount": 18, "AflameDamageDelay": 18,
                "AflameDuration": 18, "BurnContained": 3, "BurnedDelay": 10,
                "BurningSoundName": 10, "DamageType": 2, "FireFXList": 4,
                "FlameDamageExpiration": 15, "FlameDamageLimit": 18,
                "SetBurnedStatus": 4,
            },
            "fireFxRows": 11, "multiplyLimits": 2, "emptyRows": 1,
        },
        "rotwk-retail": {
            "fieldCounts": {
                "AflameDamageAmount": 18, "AflameDamageDelay": 18,
                "AflameDuration": 18, "BurnContained": 3, "BurnedDelay": 10,
                "BurningSoundName": 10, "DamageType": 2, "FireFXList": 5,
                "FlameDamageExpiration": 15, "FlameDamageLimit": 18,
                "SetBurnedStatus": 4,
            },
            "fireFxRows": 19, "multiplyLimits": 2, "emptyRows": 1,
        },
    }
    assert actual_spawn_stealth == {
        "bfme2-retail": {
            "spawnFieldCounts": {
                "CanReclaimOrphans": 10, "FadeInTime": 1, "InitialBurst": 10,
                "KillSpawnsBasedOnModelConditionState": 2, "OneShot": 1,
                "RespectCommandLimit": 1, "ShareUpgrades": 3,
                "SpawnInsideBuilding": 1, "SpawnNumber": 11,
                "SpawnReplaceDelay": 11, "SpawnTemplateName": 11,
                "SpawnedRequireSpawner": 3, "TriggeredBy": 1,
            },
            "spawnTemplateTokens": 12,
            "reclaimFieldRuntimeStatus": {"executable": 10},
            "spawnRuntimeStatus": {"deferred": 5, "executable": 6},
            "reclaimTrueAuthoredOwners": [
                "BarrowWightLair", "CaveTrollLair", "FireDrakeLair",
                "MoriarGoblinLair", "SpiderLair", "WargLair",
            ],
            "reclaimTrueEffectiveOwners": [
                "BarrowWightLair", "CaveTrollLair", "CaveTrollLairSnow",
                "FireDrakeLair", "MoriarGoblinLair", "MoriarGoblinLairSnow",
                "SpiderLair", "WargLair",
            ],
            "stealthFieldCounts": {
                "DetectedByAnyoneRange": 5, "DisguiseRevealTransitionTime": 1,
                "DisguiseTransitionTime": 1, "DisguisesAsTeam": 1,
                "FriendlyOpacityMax": 10, "FriendlyOpacityMin": 10,
                "HintDetectableConditions": 5, "InnateStealth": 7,
                "OrderIdleEnemiesToAttackMeUponReveal": 11, "PulseFrequency": 10,
                "RemoveTerrainRestrictionOnUpgrade": 1, "RequiredUpgradeNames": 1,
                "RevealDistanceFromTarget": 1, "RevealWeaponSets": 4,
                "StartsActive": 4, "StealthDelay": 11,
                "StealthForbiddenConditions": 10,
            },
            "emptyStealthRows": 1,
        },
        "rotwk-retail": {
            "spawnFieldCounts": {
                "CanReclaimOrphans": 14, "FadeInTime": 3, "InitialBurst": 16,
                "KillSpawnsBasedOnModelConditionState": 2, "OneShot": 1,
                "RespectCommandLimit": 1, "ShareUpgrades": 4,
                "SpawnInsideBuilding": 3, "SpawnNumber": 17,
                "SpawnReplaceDelay": 17, "SpawnTemplateName": 17,
                "SpawnedRequireSpawner": 4, "TriggeredBy": 2,
            },
            "spawnTemplateTokens": 18,
            "reclaimFieldRuntimeStatus": {"executable": 14},
            "spawnRuntimeStatus": {"deferred": 8, "executable": 9},
            "reclaimTrueAuthoredOwners": [
                "BarrowWightLair", "CaveTrollLair", "DireWolfLair",
                "FireDrakeLair", "HillTrollLair", "MoriarGoblinLair",
                "SnowTrollLair", "SpiderLair", "WargLair",
            ],
            "reclaimTrueEffectiveOwners": [
                "BarrowWightLair", "CaveTrollLair", "CaveTrollLairSnow",
                "DireWolfLair", "FireDrakeLair", "HillTrollLair",
                "HillTrollLairSnow", "MoriarGoblinLair",
                "MoriarGoblinLairSnow", "SnowTrollLair",
                "SnowTrollLairSnow", "SpiderLair", "WargLair",
            ],
            "stealthFieldCounts": {
                "DetectedByAnyoneRange": 9, "DisguiseRevealTransitionTime": 1,
                "DisguiseTransitionTime": 1, "DisguisesAsTeam": 1,
                "FriendlyOpacityMax": 14, "FriendlyOpacityMin": 14,
                "HintDetectableConditions": 5, "InnateStealth": 7,
                "OrderIdleEnemiesToAttackMeUponReveal": 15, "PulseFrequency": 14,
                "RemoveTerrainRestrictionOnUpgrade": 1, "RequiredUpgradeNames": 1,
                "RevealDistanceFromTarget": 1, "RevealWeaponSets": 8,
                "StartsActive": 4, "StealthDelay": 15,
                "StealthForbiddenConditions": 14,
            },
            "emptyStealthRows": 1,
        },
    }
    assert actual_creation_ocl == {
        "bfme2-retail": {
            "creationFieldCounts": {
                "ConflictsWith": 9, "DeathAnimAndDuration": 12, "Delay": 46,
                "DestroyWhenSold": 13, "FadeInTime": 32, "GrantUpgrade": 14,
                "Offset": 38, "RemoveUpgrade": 22, "RequiresAllTriggers": 20,
                "ThingToSpawn": 33, "TriggeredBy": 48, "UpgradeObject": 1,
                "UseBuildingProduction": 2,
            },
            "triggerTokens": 68, "delayDefines": 1,
            "oclBounds": {(1500, 1500): 2}, "oclAmount": 2,
        },
        "rotwk-retail": {
            "creationFieldCounts": {
                "ConflictsWith": 15, "DeathAnimAndDuration": 18, "Delay": 63,
                "DestroyWhenSold": 21, "FadeInTime": 41, "GrantUpgrade": 21,
                "Offset": 52, "RemoveUpgrade": 33, "RequiresAllTriggers": 24,
                "ThingToSpawn": 43, "TriggeredBy": 65, "UpgradeObject": 1,
                "UseBuildingProduction": 2,
            },
            "triggerTokens": 89, "delayDefines": 1,
            "oclBounds": {(1500, 1500): 3}, "oclAmount": 3,
        },
    }
    assert actual_container_family == {
        "bfme2-retail": {
            "transportFields": {
                "AllowAlliesInside": 11, "AllowEnemiesInside": 11,
                "AllowNeutralInside": 11, "AllowOwnPlayerInsideOverride": 1,
                "BoneSpecificConditionState": 1, "CanGrabStructure": 1,
                "CollidePickup": 2, "DamagePercentToUnits": 10,
                "DestroyRidersWhoAreNotFreeToExit": 1,
                "EjectPassengersOnDeath": 7, "ExitDelay": 1, "FadeFilter": 1,
                "FireGrabWeaponOnVictim": 3, "ForceOrientationContainer": 3,
                "GrabWeapon": 3, "KillPassengersOnDeath": 3,
                "ManualPickUpFilter": 3, "NumberOfExitPaths": 1,
                "ObjectStatusOfContained": 11, "PassengerBonePrefix": 10,
                "PassengerFilter": 11, "ReleaseSnappyness": 2, "ShowPips": 11,
                "Slots": 11, "TypeOneForWeaponSet": 4,
                "TypeOneForWeaponState": 4, "TypeTwoForWeaponSet": 1,
                "TypeTwoForWeaponState": 2, "UpgradeCreationTrigger": 1,
            },
            "transportBones": 16, "transportSlots": 12,
            "tunnelCapacity": 20, "tunnelExitDelays": {0: 2, 250: 1},
            "garrisonCapacity": 10, "hordeCapacity": 29, "hordeBones": 14,
        },
        "rotwk-retail": {
            "transportFields": {
                "AllowAlliesInside": 12, "AllowEnemiesInside": 12,
                "AllowNeutralInside": 12, "AllowOwnPlayerInsideOverride": 1,
                "BoneSpecificConditionState": 1, "CanGrabStructure": 1,
                "CollidePickup": 2, "DamagePercentToUnits": 10,
                "DestroyRidersWhoAreNotFreeToExit": 2,
                "EjectPassengersOnDeath": 7, "ExitDelay": 1, "FadeFilter": 1,
                "FireGrabWeaponOnVictim": 3, "ForceOrientationContainer": 4,
                "GrabWeapon": 3, "KillPassengersOnDeath": 3,
                "ManualPickUpFilter": 3, "NumberOfExitPaths": 1,
                "ObjectStatusOfContained": 12, "PassengerBonePrefix": 11,
                "PassengerFilter": 12, "ReleaseSnappyness": 2, "ShowPips": 12,
                "Slots": 12, "TypeOneForWeaponSet": 4,
                "TypeOneForWeaponState": 4, "TypeTwoForWeaponSet": 1,
                "TypeTwoForWeaponState": 2, "UpgradeCreationTrigger": 1,
            },
            "transportBones": 17, "transportSlots": 13,
            "tunnelCapacity": 20, "tunnelExitDelays": {0: 2, 250: 1},
            "garrisonCapacity": 20, "hordeCapacity": 37, "hordeBones": 17,
        },
    }
    assert actual_bonus_siege == {
        "bfme2-retail": {
            "bonusModifiers": {"MordorLargeGroupBonus": 4},
            "bonusCounts": {100: 4}, "queueCapacity": 10,
            "siegeSlots": 3, "siegeBoneStates": 18,
            "siegeCrewRows": 3, "siegeSpeedRows": 1,
        },
        "rotwk-retail": {
            "bonusModifiers": {"MordorLargeGroupBonus": 7},
            "bonusCounts": {100: 7}, "queueCapacity": 10,
            "siegeSlots": 3, "siegeBoneStates": 18,
            "siegeCrewRows": 3, "siegeSpeedRows": 1,
        },
    }


_UNCAPPED_PRODUCER = """
Object FixtureObject
  Behavior = ProductionUpdate ModuleTag_Production
    GiveNoXP = Yes
  End
End
"""

# The ThrallMaster / BattleWagon shape, verbatim comment included.
_CAPPED_PRODUCER = """
Object FixtureObject
  Behavior = ProductionUpdate ModuleTag_Production
    GiveNoXP = Yes
    MaxQueueEntries = 1 ; only allow one queued upgrade at a time
  End
End
"""


def test_production_update_max_queue_entries_is_authored_only() -> None:
    """Q40. Absent MaxQueueEntries must not be emitted at all.

    RETAIL ORACLE (rotwk 2.01 effective view, counted 2026-08-18):
    ``MaxQueueEntries`` is authored on exactly TWO of 423 ``ProductionUpdate``
    blocks --
    ``data/ini/object/evilfaction/units/angmar/angmarthrallmaster.ini:587``
    and
    ``data/ini/object/goodfaction/units/dwarven/dwarvenbattlewagon.ini:492``,
    both ``MaxQueueEntries = 1 ; only allow one queued upgrade at a time``.
    Every other producer authors nothing, and absent means uncapped -- so the
    compiled contract must leave the key out rather than carry a default the
    runtime would read back as a real cap.
    """

    fields = compile_production_updates(
        _lineage(_UNCAPPED_PRODUCER), "FixtureObject"
    )[0]["fields"]
    assert "MaxQueueEntries" not in fields

    capped_fields = compile_production_updates(
        _lineage(_CAPPED_PRODUCER), "FixtureObject"
    )[0]["fields"]
    assert capped_fields["MaxQueueEntries"]["value"] == 1
