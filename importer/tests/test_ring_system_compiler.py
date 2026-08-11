from __future__ import annotations

import json

import pytest

from openbfme_importer.cli import main as cli_main
from openbfme_importer.ring_system_compiler import (
    RingSystemCompilerError,
    build_ring_system_runtime,
    compile_ring_system,
    compile_ring_system_descriptor,
    validate_ring_system,
    validate_ring_system_runtime,
)


def _structure(index: int, *, erebor: bool = False) -> str:
    name = "EreborThrone" if erebor else f"FixtureFortress{index:02d}"
    ring_drop = "" if erebor else """
  Behavior = CreateObjectDie ModuleTag_DropOneRing
    TriggeredBy = Upgrade_RingHero Upgrade_FortressRingHero
    CreationList = OCL_TheOneRing
  End
"""
    return f"""
Object {name}
  Side = Dwarves
  Behavior = CitadelSlaughterHordeContain ModuleTag_RingEntry
    StatusForRingEntry = HOLDING_THE_RING
    UpgradeForRingEntry = Upgrade_RingHero Upgrade_FortressRingHero
    ObjectToDestroyForRingEntry = TheDroppedRing
    FXForRingEntry = FX_OneRingFlare
    EnterSound = RingPlacedOnFortress
  End
  Behavior = FXListDie ModuleTag_AnnounceRingLoss
    DeathFX = FX_FortressOneRingDeath
  End
{ring_drop}
  Behavior = ModelConditionUpgrade ModuleTag_ShowOneRing
    TriggeredBy = Upgrade_FortressRingHero
    AddConditionFlags = ONE_RING
  End
  Draw = W3DScriptedModelDraw ModuleTag_Draw
    ModelConditionState = ONE_RING
      Model = EXOneRing
      Model = EXOneRing_CR
    End
  End
End
"""


def _documents() -> dict[str, bytes]:
    structures = "".join(_structure(i) for i in range(25)) + _structure(25, erebor=True)
    return {
        "data/ini/object/neutral/neutralunits.ini": b"""
Object NeutralGollum
  Side = Neutral
  KindOf = PRELOAD SELECTABLE HERO NEUTRALGOLLUM
  ArmorSet
    Conditions = None
    Armor = GollumArmor
  End
  Body = ActiveBody ModuleTag_Body
    MaxHealth = 200
  End
  LocomotorSet
    Locomotor = HumanLocomotor
    Condition = SET_NORMAL
    Speed = 45
  End
  LocomotorSet
    Locomotor = HumanWanderLocomotor
    Condition = SET_WANDER
    Speed = 32
  End
  Behavior = InvisibilityUpdate ModuleTag_Camouflage
    InvisibilityNugget
      InvisibilityType = CAMOUFLAGE
      DetectionRange = 120
    End
  End
End
ChildObject NeutralGollum_RingHero NeutralGollum
  Behavior = CreateObjectDie ModuleTag_DropTheRing
    CreationList = OCL_TheOneRing
  End
  Behavior = AnimalAIUpdate ModuleTag_Wander
    FleeRange = 300
    FleeDistance = 800
    WanderPercentage = 80
    MaxWanderDistance = 50
  End
End
ChildObject NeutralGollum_RingStealer NeutralGollum
End
""",
        "data/ini/armor.ini": b"""
Armor GollumArmor
  Armor = MAGIC 500%
  Armor = HERO 500%
  Armor = HERO_RANGED 500%
End
""",
        "data/ini/crate.ini": b"""
Object TheDroppedRing
  Side = Civilian
  KindOf = SELECTABLE IMMOBILE REVEAL_TO_ALL CRATE ONE_RING
  ShroudClearingRange = 100
  Behavior = AttachUpdate ModuleTag_Attach
    ObjectFilter = ANY +INFANTRY +CAVALRY +HERO +MONSTER +MACHINE -NEUTRALGOLLUM NOT_FLYING_UNITS
    ScanRange = 10
    ParentStatus = HOLDING_THE_RING
  End
  Behavior = RemoveUpgradeUpgrade ModuleTag_RemoveRing
    TriggeredBy = Upgrade_AngmarFaction Upgrade_IsengardFaction Upgrade_MordorFaction Upgrade_WildFaction Upgrade_MenFaction Upgrade_ElfFaction Upgrade_DwarfFaction
    UpgradeToRemove = Upgrade_RingHero
  End
  Behavior = CreateObjectDie ModuleTag_DropTheRing
    CreationList = OCL_TheOneRing
  End
End
""",
        "data/ini/objectcreationlist.ini": b"""
ObjectCreationList OCL_TheOneRing
  CreateObject
    ObjectNames = TheDroppedRing
    Count = 1
  End
End
ObjectCreationList OCL_TheOneRingCD
  CreateObject
    ObjectNames = TheDroppedRing
    Count = 1
    Offset = X:126 Y:0 Z:0.0
  End
End
ObjectCreationList OCL_TheRingStealer
  CreateObject
    ObjectNames = NeutralGollum_RingStealer
    Count = 1
    DestinationPlayer = PlyrCreeps
    WaypointSpawnPoints = SpawnPoint_SkirmishGollum_
  End
End
ObjectCreationList OCL_TheRingCarrier
  CreateObject
    ObjectNames = NeutralGollum_RingHero
    Count = 1
    DestinationPlayer = PlyrCreeps
    WaypointSpawnPoints = SpawnPoint_SkirmishGollum_
  End
End
""",
        "data/ini/upgrade.ini": b"""
Upgrade Upgrade_RingHero
  Type = PLAYER
  LocalPlayerGainsUpgradeEvaEvent = LocalPlayerGainsRing
  AlliedPlayerGainsUpgradeEvaEvent = AlliedPlayerGainsRing
  EnemyPlayerGainsUpgradeEvaEvent = EnemyPlayerGainsRing
End
Upgrade Upgrade_FortressRingHero
  Type = OBJECT
End
""",
        "data/ini/commandbutton.ini": b"""
CommandButton Command_RingHeroReviveSlot
  Command = REVIVE
  NeededUpgrade = Upgrade_RingHero Upgrade_FortressRingHero
End
""",
        "data/ini/commandset.ini": b"",
        "data/ini/playertemplate.ini": b"""
PlayerTemplate FactionMen
  Side = Men
  BuildableRingHeroesMP = AnyModRingHero
End
PlayerTemplate FactionMordor
  Side = Mordor
  BuildableRingHeroesMP = MordorSauron_RingHero
End
PlayerTemplate FactionElves
  Side = Elves
  BuildableRingHeroesMP = ElvenGaladriel_RingHero
End
""",
        "data/ini/object/ringheroes.ini": b"""
Object MordorSauron
  BuildCost = SAURON_BUILDCOST
  BuildTime = SAURON_BUILDTIME
  BountyValue = SAURON_BOUNTY_VALUE
End
ChildObject MordorSauron_RingHero MordorSauron
  Behavior = RemoveUpgradeUpgrade ModuleTag_RemoveUpgrades
    TriggeredBy = Upgrade_MordorFaction
    UpgradeToRemove = Upgrade_RingHero Upgrade_FortressRingHero
    RemoveFromAllPlayerObjects = Yes
  End
  Behavior = CreateObjectDie ModuleTag_DropRing
    CreationList = OCL_TheOneRing
  End
  Behavior = ExperienceLevelCreate ModuleTag_Level10
    LevelToGrant = 10
  End
End
ChildObject AnyModRingHero MordorSauron
  Behavior = RemoveUpgradeUpgrade ModuleTag_RemoveUpgrades
    TriggeredBy = Upgrade_MenFaction
    UpgradeToRemove = Upgrade_RingHero Upgrade_FortressRingHero
    RemoveFromAllPlayerObjects = Yes
  End
  Behavior = CreateObjectDie ModuleTag_DropRing
    CreationList = OCL_TheOneRing
  End
  Behavior = ExperienceLevelCreate ModuleTag_Level10
    LevelToGrant = 10
  End
End
Object ElvenGaladriel
  BuildCost = GALADRIEL_BUILDCOST
  BuildTime = GALADRIEL_BUILDTIME
End
ChildObject ElvenGaladriel_RingHero ElvenGaladriel
  Behavior = RemoveUpgradeUpgrade ModuleTag_RemoveUpgrades
    TriggeredBy = Upgrade_ElfFaction
    UpgradeToRemove = Upgrade_RingHero Upgrade_FortressRingHero
    RemoveFromAllPlayerObjects = Yes
  End
  Behavior = CreateObjectDie ModuleTag_DropRing
    CreationList = OCL_TheOneRing
  End
  Behavior = ExperienceLevelCreate ModuleTag_Level10
    LevelToGrant = 10
  End
  Behavior = SpecialPowerModule ModuleTag_TerribleFury
    SpecialPowerTemplate = SpecialAbilityGaladrielTerribleFury
  End
  Behavior = SpecialAbilityUpdate ModuleTag_TerribleFuryUpdate
    SpecialPowerTemplate = SpecialAbilityGaladrielTerribleFury
  End
  Behavior = ModelConditionUpgrade ModuleTag_DarkSkin
    TriggeredBy = Upgrade_ObjectLevel1
    AddConditionFlags = USER_1
  End
  Draw = W3DScriptedModelDraw ModuleTag_Draw
    ModelConditionState = USER_1
      Model = EUGaldrl_SKN
    End
  End
End
""",
        "data/ini/gamedata.ini": b"""
#define SAURON_BUILDCOST 10000
#define GALADRIEL_BUILDCOST 10000
#define SAURON_BUILDTIME 300
#define GALADRIEL_BUILDTIME 300
#define SAURON_BOUNTY_VALUE 2500
""",
        "data/ini/object/structures.ini": structures.encode(),
    }


def test_compiles_complete_ring_contract_from_synthetic_effective_ini() -> None:
    result = compile_ring_system(_documents())
    validate_ring_system(result)

    assert set(result["objects"]) == {
        "NeutralGollum",
        "NeutralGollum_RingHero",
        "TheDroppedRing",
        "MordorSauron_RingHero",
        "ElvenGaladriel_RingHero",
        "AnyModRingHero",
    }
    assert "NeutralGollum_RingStealer" not in result["objects"]
    gollum = result["objects"]["NeutralGollum"]
    assert gollum["side"] == "Neutral"
    assert gollum["body"] == {"kind": "ActiveBody", "maxHealth": 200}
    assert gollum["camouflage"] == {"type": "CAMOUFLAGE", "detectionRange": 120}
    assert gollum["locomotors"] == {"normal": 45, "wander": 32}
    assert gollum["armor"]["damagePercent"] == {
        "MAGIC": 500,
        "HERO": 500,
        "HERO_RANGED": 500,
    }
    child = result["objects"]["NeutralGollum_RingHero"]
    assert child["animalAI"] == {
        "fleeRange": 300,
        "fleeDistance": 800,
        "wanderPercentage": 80,
        "maxWanderDistance": 50,
    }
    assert child["onDeath"] == {"createObjectList": "OCL_TheOneRing"}

    ring = result["objects"]["TheDroppedRing"]
    assert ring["ringMechanic"]["attach"]["filter"] == {
        "mode": "ANY",
        "include": ["INFANTRY", "CAVALRY", "HERO", "MONSTER", "MACHINE"],
        "exclude": ["NEUTRALGOLLUM"],
        "predicate": ["NOT_FLYING_UNITS"],
    }
    assert ring["ringMechanic"]["removeUpgrade"]["triggerFactionUpgrades"] == [
        "Upgrade_AngmarFaction", "Upgrade_IsengardFaction", "Upgrade_MordorFaction",
        "Upgrade_WildFaction", "Upgrade_MenFaction", "Upgrade_ElfFaction",
        "Upgrade_DwarfFaction",
    ]
    assert result["objectCreationLists"]["OCL_TheOneRingCD"]["offset"] == {
        "x": 126, "y": 0, "z": 0,
    }


def test_ring_filter_preserves_any_vs_none_mode() -> None:
    any_result = compile_ring_system(_documents())
    none_documents = _documents()
    none_documents["data/ini/crate.ini"] = none_documents[
        "data/ini/crate.ini"
    ].replace(b"ObjectFilter = ANY ", b"ObjectFilter = NONE ")

    none_result = compile_ring_system(none_documents)

    assert any_result["objects"]["TheDroppedRing"]["ringMechanic"]["attach"][
        "filter"
    ]["mode"] == "ANY"
    assert none_result["objects"]["TheDroppedRing"]["ringMechanic"]["attach"][
        "filter"
    ]["mode"] == "NONE"
    assert any_result["objects"]["TheDroppedRing"]["ringMechanic"]["attach"][
        "filter"
    ] != none_result["objects"]["TheDroppedRing"]["ringMechanic"]["attach"][
        "filter"
    ]


def test_delivery_upgrades_routes_heroes_and_system_are_source_backed() -> None:
    result = compile_ring_system(_documents())
    assert len(result["delivery"]["structures"]) == 26
    erebor = next(row for row in result["delivery"]["structures"] if row["objectId"] == "EreborThrone")
    assert erebor["note"] == "retail-bug-accepts-ring-never-returns-it"
    assert erebor["dropsRingOnDeath"] is False
    assert all(
        row["dropsRingOnDeath"] is True
        for row in result["delivery"]["structures"]
        if row["objectId"] != "EreborThrone"
    )
    assert set(result["delivery"]["fortressModules"]) == {
        "lossAnnouncement", "ringDrop", "modelCondition", "drawModels",
    }
    assert result["delivery"]["fortressModules"]["ringDrop"]["requiredUpgrades"] == [
        "Upgrade_RingHero", "Upgrade_FortressRingHero",
    ]
    assert result["upgrades"]["Upgrade_RingHero"]["type"] == "PLAYER"
    assert result["upgrades"]["Upgrade_FortressRingHero"] == {"type": "OBJECT"}
    assert result["routes"]["prerequisites"] == [
        "Upgrade_RingHero", "Upgrade_FortressRingHero",
    ]
    assert result["system"]["ringHeroesByFaction"]["Men"] == ["AnyModRingHero"]
    assert result["system"]["modeToken"] == "ringheroes"
    assert result["system"]["spawn"] == {
        "objectId": "NeutralGollum_RingHero",
        "waypointFamily": "SpawnPoint_SkirmishGollum_",
        "team": "PlyrCreeps",
    }
    assert len(result["system"]["evaEvents"]) == 8

    sauron = result["objects"]["MordorSauron_RingHero"]
    assert sauron["economy"] == {"buildCost": 10000, "buildTime": 300, "bountyValue": 2500}
    assert sauron["onCreate"]["removeFromAllPlayerObjects"] is True
    assert sauron["experienceLevelOnCreate"] == 10
    galadriel = result["objects"]["ElvenGaladriel_RingHero"]
    assert [row["kind"] for row in galadriel["terribleFuryModules"]] == [
        "SpecialPowerModule", "SpecialAbilityUpdate",
    ]
    assert galadriel["darkSkin"] == {
        "triggeredBy": "Upgrade_ObjectLevel1", "condition": "USER_1", "model": "EUGaldrl_SKN",
    }


def test_art_plan_is_exact_and_marks_authored_conditional_hero_model_required() -> None:
    plan = compile_ring_system(_documents())["artConversionPlan"]
    assert plan["layout"] == "standard-unit-model-pack"
    assert plan["gollum"]["sourceGame"] == "bfme2"
    assert plan["gollum"]["models"] == ["art/w3d/cu/cugollum_skn.w3d", "art/w3d/cu/cugollum_skl.w3d"]
    assert plan["gollum"]["animations"] == [
        f"art/w3d/cu/cugollum_{suffix}.w3d"
        for suffix in ("atkb", "chra", "diea", "dieb", "gtpa", "hita", "idla", "idlb", "idlc", "idld", "runa")
    ]
    assert plan["ring"]["models"] == ["art/w3d/th/thering.w3d"]
    assert plan["fortressRingModels"] == ["art/w3d/ex/exonering.w3d", "art/w3d/ex/exonering_cr.w3d"]
    assert plan["conditionalHeroModels"] == [{
        "objectId": "ElvenGaladriel_RingHero",
        "condition": "USER_1",
        "model": "EUGaldrl_SKN",
        "requiredIfUnshipped": True,
    }]


def test_descriptor_and_runtime_are_separate_sealed_schemas() -> None:
    descriptor = compile_ring_system_descriptor(_documents())
    runtime = build_ring_system_runtime(descriptor)
    validate_ring_system_runtime(runtime)

    assert descriptor["schema"] == "openbfme.ring-system-descriptor"
    assert "descriptorSha256" in descriptor
    assert "importerEvidence" in descriptor
    assert runtime["schema"] == "openbfme.ring-system-runtime"
    assert runtime["descriptorSha256"] == descriptor["descriptorSha256"]
    assert "runtimeSha256" in runtime
    assert "importerEvidence" not in runtime
    assert "artConversionPlan" not in runtime["registration"]


def test_modded_ring_hero_reference_fails_closed_when_object_is_missing() -> None:
    documents = _documents()
    source = documents["data/ini/object/ringheroes.ini"]
    start = source.index(b"ChildObject AnyModRingHero")
    end = source.index(b"Object ElvenGaladriel", start)
    documents["data/ini/object/ringheroes.ini"] = source[:start] + source[end:]

    with pytest.raises(RingSystemCompilerError, match="AnyModRingHero"):
        compile_ring_system_descriptor(documents)


def test_compile_ring_system_cli_writes_descriptor_and_runtime(
    tmp_path, capsys
) -> None:
    assets_root = tmp_path / "effective-assets"
    for virtual_path, payload in _documents().items():
        target = assets_root / virtual_path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(payload)
    state_root = tmp_path / "state"

    status = cli_main(
        [
            "--json",
            "--state-root",
            str(state_root),
            "compile-ring-system",
            "--game",
            "rotwk",
            "--assets-root",
            str(assets_root),
        ]
    )

    assert status == 0
    summary = json.loads(capsys.readouterr().out)
    descriptor = json.loads(
        (state_root / "reports" / "rotwk-ring-system-descriptor.json").read_text()
    )
    runtime = json.loads(
        (state_root / "reports" / "rotwk-ring-system-runtime.json").read_text()
    )
    assert summary["descriptor_sha256"] == descriptor["descriptorSha256"]
    assert summary["runtime_sha256"] == runtime["runtimeSha256"]


@pytest.mark.parametrize(
    ("path", "old", "new", "message"),
    [
        ("data/ini/armor.ini", b"HERO 500%", b"HERO 400%", "GollumArmor HERO"),
        ("data/ini/commandbutton.ini", b"Upgrade_RingHero Upgrade_FortressRingHero", b"Upgrade_RingHero", "NeededUpgrade"),
        ("data/ini/playertemplate.ini", b"BuildableRingHeroesMP", b"RenamedRingHeroes", "BuildableRingHeroesMP"),
    ],
)
def test_ring_compiler_fails_closed_on_semantic_drift(path: str, old: bytes, new: bytes, message: str) -> None:
    documents = _documents()
    documents[path] = documents[path].replace(old, new)
    with pytest.raises(RingSystemCompilerError, match=message):
        compile_ring_system(documents)
