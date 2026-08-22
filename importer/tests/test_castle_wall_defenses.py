from __future__ import annotations

import json

from pathlib import Path

import pytest

from openbfme_importer.playable_structure_compiler import (
    _structure_combat_contract,
    compile_playable_structure_descriptor,
    validate_playable_structure_descriptor,
)
from openbfme_importer.playable_unit_compiler import (
    _ancestry,
    prepare_playable_unit_compiler,
)
from importer.tests.test_playable_structure_compiler import _structure_documents


REPO_ROOT = Path(__file__).resolve().parents[2]
PRIVATE_ROOT = REPO_ROOT / "workspace" / "retail-work"
if not (PRIVATE_ROOT / "editions/rotwk/cache/effective-assets").is_dir() and REPO_ROOT.parent.name == "worktrees":
    PRIVATE_ROOT = REPO_ROOT.parents[2] / "workspace" / "retail-work"
EFFECTIVE_ASSETS = PRIVATE_ROOT / "editions/rotwk/cache/effective-assets"


@pytest.fixture(scope="module")
def retail_compiler_inputs():
    if not EFFECTIVE_ASSETS.is_dir():
        pytest.skip("pure RotWK effective-assets oracle is not present")
    documents = {
        path.relative_to(EFFECTIVE_ASSETS).as_posix(): path.read_bytes()
        for path in EFFECTIVE_ASSETS.rglob("*")
        if path.is_file() and path.suffix.casefold() in {".ini", ".inc"}
    }
    return documents, prepare_playable_unit_compiler(documents)


def _retail_combat(object_id: str, retail_compiler_inputs) -> dict[str, object]:
    documents, prepared = retail_compiler_inputs
    target = prepared.objects[object_id.casefold()]
    combat = _structure_combat_contract(
        _ancestry(prepared.objects, target), documents, prepared
    )
    assert combat is not None
    return combat


def _direct_weapon_documents(
    weapon_id: str = "CastleWallUpgradeBow",
    object_id: str = "TestKeep",
) -> dict[str, bytes]:
    documents = _structure_documents()
    object_path = "data/ini/object/units/test_units.ini"
    if object_id == "TestKeep":
        documents[object_path] = documents[object_path].replace(
            b"  BuildCost = KEEP_BUILDCOST",
            (
                "  WeaponSet\n"
                "    Conditions = None\n"
                f"    Weapon = PRIMARY {weapon_id}\n"
                "  End\n"
                "  Behavior = AIUpdateInterface ModuleTag_AutoAcquire\n"
                "    AutoAcquireEnemiesWhenIdle = Yes\n"
                "    MoodAttackCheckRate = 250\n"
                "  End\n"
                "  BuildCost = KEEP_BUILDCOST"
            ).encode("ascii"),
            1,
        )
    else:
        documents[object_path] += (
            f"""
Object {object_id}
  KindOf = STRUCTURE WALL_HUB CAN_ATTACK
  Body = StructureBody ModuleTag_Body
    MaxHealth = 1000
  End
  WeaponSet
    Conditions = None
    Weapon = PRIMARY {weapon_id}
  End
  Behavior = AIUpdateInterface ModuleTag_AutoAcquire
    AutoAcquireEnemiesWhenIdle = Yes
    MoodAttackCheckRate = 250
  End
End
"""
        ).encode("ascii")
    documents["data/ini/gamedata.ini"] += b"""
#define TEST_DELAY_MIN 5
#define TEST_DELAY_MAX 10
#define TEST_BASE_DAMAGE 75
#define TEST_UPGRADE_DAMAGE 125
"""
    documents["data/ini/weapon.ini"] = f"""
Weapon {weapon_id}
  AttackRange = 250
  WeaponSpeed = 321
  DelayBetweenShots = Min:TEST_DELAY_MIN Max:TEST_DELAY_MAX
  PreAttackDelay = 1000
  FiringDuration = TEST_DELAY_MAX
  ProjectileNugget
    ProjectileTemplateName = GoodFactionArrow
    WarheadTemplateName = TestBaseWarhead
    ForbiddenUpgradeNames = Upgrade_TestFireArrows
  End
  ProjectileNugget
    ProjectileTemplateName = GoodFactionFireArrow
    WarheadTemplateName = TestUpgradeWarhead
    RequiredUpgradeNames = Upgrade_TestFireArrows
  End
End
Weapon TestBaseWarhead
  DamageNugget
    Damage = TEST_BASE_DAMAGE
    DamageType = PIERCE
  End
End
Weapon TestUpgradeWarhead
  DamageNugget
    Damage = TEST_UPGRADE_DAMAGE
    DamageType = FLAME
  End
End
""".encode("ascii")
    return documents


def _combat(
    weapon_id: str = "CastleWallUpgradeBow",
    object_id: str = "TestKeep",
) -> dict[str, object]:
    descriptor = compile_playable_structure_descriptor(
        object_id,
        _direct_weapon_documents(weapon_id, object_id),
        wall_template_roots=(object_id,) if object_id != "TestKeep" else (),
    )
    validate_playable_structure_descriptor(descriptor)
    return descriptor["gameplay"]["combat"]


def test_delay_between_shots_min_max_compiles_as_authored_uniform_interval() -> None:
    delay = _combat()["delayBetweenShotsMs"]

    assert delay["minimumValue"] == 5
    assert delay["maximumValue"] == 10
    assert delay["distribution"] == "uniform-inclusive-integer"
    assert delay["expression"] == "Min:TEST_DELAY_MIN Max:TEST_DELAY_MAX"


def test_upgrade_conditional_projectile_nuggets_select_base_damage_and_record_alternates() -> None:
    combat = _combat()

    assert combat["damage"]["value"] == 75
    assert combat["warheadId"] == "TestBaseWarhead"
    assert combat["projectileObjectId"] == "GoodFactionArrow"
    assert combat["projectileNuggets"] == [
        {
            "line": 8,
            "projectileObjectId": "GoodFactionArrow",
            "warheadId": "TestBaseWarhead",
            "forbiddenUpgradeIds": ["Upgrade_TestFireArrows"],
            "damage": combat["projectileNuggets"][0]["damage"],
        },
        {
            "line": 13,
            "projectileObjectId": "GoodFactionFireArrow",
            "warheadId": "TestUpgradeWarhead",
            "requiredUpgradeIds": ["Upgrade_TestFireArrows"],
            "damage": combat["projectileNuggets"][1]["damage"],
        },
    ]
    assert combat["projectileNuggets"][0]["damage"]["value"] == 75
    assert combat["projectileNuggets"][1]["damage"]["value"] == 125


def test_sentry_tower_bow_compiles_a_combat_contract(retail_compiler_inputs) -> None:
    combat = _retail_combat("GondorBattleTower", retail_compiler_inputs)

    assert combat["weaponId"] == "SentryTowerBow"
    assert combat["damage"]["value"] == 75
    assert combat["delayBetweenShotsMs"]["minimumValue"] == 20
    assert combat["delayBetweenShotsMs"]["maximumValue"] == 50


def test_castle_wall_upgrade_bow_compiles_a_combat_contract(retail_compiler_inputs) -> None:
    combat = _retail_combat("GondorCastleWallTower", retail_compiler_inputs)

    assert combat["weaponId"] == "CastleWallUpgradeBow"
    assert combat["damage"]["value"] == 75
    assert combat["warheadId"] == "CastleWallUpgradeBowWarhead"
    assert len(combat["projectileNuggets"]) == 7


def test_gondor_castle_wall_tower_carries_authored_castle_wall_upgrade_bow(retail_compiler_inputs) -> None:
    documents, prepared = retail_compiler_inputs
    descriptor = compile_playable_structure_descriptor(
        "GondorCastleWallTower",
        documents,
        prepared=prepared,
        wall_template_roots=("GondorCastleWallTower",),
    )
    validate_playable_structure_descriptor(descriptor)
    combat = descriptor["gameplay"]["combat"]

    assert combat["weaponId"] == "CastleWallUpgradeBow"
    assert combat["weaponSlot"] == "PRIMARY"
    assert combat["damage"]["value"] == 75


# ---------------------------------------------------------------------------
# Castle-map wall defenses (owner 2026-08-22): Minas Tirith, Carn Dum and Dol
# Guldur place wall-mounted defenses that no faction's command set reaches, so
# the faction cook never compiled them and the runtime seeded them as inert
# props (`CASTLE_WALL_DEFENSE_STALE ... missing-playable-structure-document`).
# They are declared as policy roots of the castle's faction and compile with
# their authored weapon.
# ---------------------------------------------------------------------------
CASTLE_MAP_WALL_DEFENSES = {
    "factionmen": ("MinisWallAUpgrade", "MinisWallAUpgradeNoGate"),
    "factionangmar": ("AngmarWallCatapultCarnDum", "AngmarWallTowerCarnDum"),
    "factionmordor": ("DoGoldurWallCatapultSmall", "DoGoldurWallTowerSmall"),
}


def test_castle_map_wall_defenses_are_policy_roots() -> None:
    from openbfme_importer.faction_policy import implicit_object_roots

    for template, objects in CASTLE_MAP_WALL_DEFENSES.items():
        roots = dict(implicit_object_roots(template, game="rotwk"))
        for object_id in objects:
            assert roots.get(object_id) == "castle-map-wall-defense", (template, object_id, roots)


@pytest.mark.parametrize(
    "object_id, shape",
    [
        # Minas slots: weapon arrives with the player's wall upgrade.
        ("MinisWallAUpgrade", "upgrade-slot"),
        ("MinisWallAUpgradeNoGate", "upgrade-slot"),
        # Bow towers carry their weapon outright.
        ("AngmarWallTowerCarnDum", "bow-tower"),
        ("DoGoldurWallTowerSmall", "bow-tower"),
        # Catapult mounts grant Upgrade_TrebuchetTurret and spawn a slaved
        # trebuchet on creation.
        ("AngmarWallCatapultCarnDum", "catapult-mount"),
        ("DoGoldurWallCatapultSmall", "catapult-mount"),
    ],
)
def test_castle_map_wall_defense_compiles_as_retail_authors_it(object_id, shape, retail_compiler_inputs) -> None:
    documents, prepared = retail_compiler_inputs
    descriptor = compile_playable_structure_descriptor(
        object_id,
        documents,
        prepared=prepared,
        engine_spawned_roots=(object_id,),
        engine_spawned_roles={object_id.casefold(): "castle-map-wall-defense"},
        game="rotwk",
    )
    validate_playable_structure_descriptor(descriptor)
    assert descriptor["compositeRole"] == "castle-map-wall-defense"
    gameplay = descriptor["gameplay"]
    if shape == "bow-tower":
        assert gameplay["combat"]["damage"]["value"] > 0
    elif shape == "upgrade-slot":
        assert "combat" not in gameplay, "a slot has no weapon until upgraded"
        assert gameplay.get("upgradeEffects"), "slot must carry its PLAYER_UPGRADE weapon effects"
        assert gameplay.get("trainedCommandSets"), "slot must offer its upgrade commands"
    else:
        assert "combat" not in gameplay
        grants = gameplay.get("createGrants") or []
        assert any("Upgrade_TrebuchetTurret".casefold() in json.dumps(g).casefold() for g in grants), grants
