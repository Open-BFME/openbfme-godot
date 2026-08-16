from __future__ import annotations

from copy import deepcopy
from collections import Counter
import json
from pathlib import Path

import pytest

from openbfme_importer.catalog import InstallCatalog
from openbfme_importer.module_census import read_catalog_documents
from openbfme_importer.neutral_mob_catalog import (
    NeutralMobCatalogError,
    _digest,
    _role,
    _runtime_domain,
    compile_neutral_mob_catalog,
    validate_neutral_mob_catalog,
)
from openbfme_importer.playable_unit_import import _source_documents
from openbfme_importer.playable_unit_compiler import (
    prepare_playable_unit_compiler,
)


def _documents() -> dict[str, bytes]:
    return {
        "data/ini/object/neutral/creeps.ini": b"""
#define NATUREUNITS_KINDOF PRELOAD IGNORED_IN_GUI INFANTRY INERT
Object NeutralTree
 Side = Neutral
 KindOf = IMMOBILE INERT
End
Object NeutralBarracks
 Side = Neutral
 CommandSet = NeutralBarracksCommandSet
 KindOf = STRUCTURE IMMOBILE
End
Object WargLair
 Side = Neutral
 KindOf = STRUCTURE IMMOBILE CREEP
 Body = ActiveBody ModuleTag_Body
  MaxHealth = 1000
 End
 Behavior = ProductionUpdate ModuleTag_Production
  GiveNoXP = Yes
 End
End
Object NeutralWolf
 Side = Neutral
 KindOf = NATUREUNITS_KINDOF CREEP MONSTER CAN_ATTACK
End
ChildObject NeutralWolf_Slaved NeutralWolf
End
Object RohanPeasant
 Side = Neutral
 KindOf = INFANTRY
 SelectPortrait = UPRohanPeasant
End
Object RohanPeasantHorde
 Side = Neutral
 KindOf = HORDE INFANTRY
 SelectPortrait = UPRohanPeasantHorde
 Behavior = HordeContain ModuleTag_HordeContain
  InitialPayload = RohanPeasant 4
 End
End
""",
        "data/ini/commandset.ini": b"""
CommandSet NeutralBarracksCommandSet
 1 = Command_BuildRohanPeasantHorde
End
""",
        "data/ini/commandbutton.ini": b"""
CommandButton Command_BuildRohanPeasantHorde
 Command = UNIT_BUILD
 Object = RohanPeasantHorde
 ButtonImage = BIRohanPeasantHorde
End
""",
    }


def _assert_unit_unsupported_capabilities_are_untyped(
    result: dict[str, object], *, expected_count: int
) -> None:
    unsupported_count = 0
    for catalog_row in result["neutralMobs"]:
        if catalog_row["runtimeDomain"] != "unit":
            continue
        descriptor = catalog_row["descriptor"]
        contracts = descriptor["gameplay"]["simulation"]["resolved"].get(
            "moduleContracts", []
        )
        typed = {
            (
                row["sourceIni"].casefold(),
                row["line"],
                row["tag"].casefold(),
                row["module"].casefold(),
            )
            for row in contracts
        }
        evidence_by_id = {
            "module:%s:%s:%s"
            % (row["ownerRole"], row["kind"], row["instanceTag"]): row
            for row in descriptor["runtimeModuleEvidence"]
        }
        unsupported = descriptor["unsupportedCapabilities"]
        unsupported_count += len(unsupported)
        for row in unsupported:
            evidence = evidence_by_id[row["id"]]
            assert (
                evidence["sourceIni"].casefold(),
                evidence["line"],
                evidence["instanceTag"].casefold(),
                evidence["kind"].casefold(),
            ) not in typed
        assert descriptor["specialCapabilities"] == sorted(
            {evidence_by_id[row["id"]]["kind"] for row in unsupported},
            key=str.casefold,
        )
    assert unsupported_count == expected_count


_BEZIER_NEUTRAL_OWNERS = {
    "bfme2": {
        "FireDrake_Slaved",
        "HobbitCivilian",
        "HobbitCivilianHorde",
        "MinorSpider",
        "MinorSpider_Slaved",
        "MinorSpider_Summoned",
        "MordorBalrog",
        "MordorGoblinArcher_Slaved",
        "MordorGoblinSwordsman_Slaved",
        "RohanPeasant1",
        "RockBigTroll",
    },
    "rotwk": {
        "AngmarOrcWarriors_Placed",
        "AngmarOrcWarriors_Summoned",
        "FireDrake_Slaved",
        "HillTroll_Slaved",
        "HobbitCivilian",
        "HobbitCivilianHorde",
        "MinorSpider",
        "MinorSpider_Slaved",
        "MinorSpider_Summoned",
        "MordorBalrog",
        "MordorGoblinArcher_Slaved",
        "MordorGoblinSwordsman_Slaved",
        "RohanPeasant1",
        "SnowTroll_Slaved",
        "RockBigTroll",
    },
}

_BEZIER_EXECUTABLE_OWNERS = {
    "bfme2": {
        "FireDrake_Slaved", "MinorSpider", "MinorSpider_Slaved",
        "MinorSpider_Summoned", "MordorGoblinArcher_Slaved",
        "MordorGoblinSwordsman_Slaved", "RohanPeasant1",
    },
    "rotwk": {
        "AngmarOrcWarriors_Placed", "AngmarOrcWarriors_Summoned",
        "FireDrake_Slaved", "HillTroll_Slaved", "MinorSpider",
        "MinorSpider_Slaved", "MinorSpider_Summoned",
        "MordorGoblinArcher_Slaved", "MordorGoblinSwordsman_Slaved",
        "RohanPeasant1", "SnowTroll_Slaved",
    },
}


def _assert_exact_neutral_bezier_contracts(
    result: dict[str, object], *, game: str
) -> None:
    owners: set[str] = set()
    for catalog_row in result["neutralMobs"]:
        descriptor = catalog_row["descriptor"]
        if catalog_row["runtimeDomain"] == "unit":
            contracts = descriptor["gameplay"]["simulation"]["resolved"].get(
                "moduleContracts", []
            )
        else:
            contracts = descriptor.get("moduleContracts", [])
        bezier = [
            row for row in contracts
            if row["module"] == "BezierProjectileBehavior"
        ]
        if not bezier:
            continue
        owners.add(catalog_row["objectId"])
        assert len(bezier) == 1
        row = bezier[0]
        expected_status = (
            "executable"
            if catalog_row["objectId"] in _BEZIER_EXECUTABLE_OWNERS[game]
            else "deferred"
        )
        assert row["runtimeStatus"] == expected_status
        assert row["extraction"] == "typed"
        assert row["effectGraph"]["trajectory"]["runtimeStatus"] == "executable"
        assert row["effectGraph"]["trajectory"]["progressAuthority"] == (
            "external-authored-projectile-flight"
        )
        assert row["effectGraph"]["executionEligibility"]["runtimeStatus"] == expected_status
        assert bool(row["effectGraph"]["executionEligibility"]["blockers"]) == (
            expected_status == "deferred"
        )
        if catalog_row["objectId"] == "RockBigTroll":
            assert catalog_row["runtimeDomain"] == "prop"
            evidence = next(
                item for item in descriptor["runtimeModuleEvidence"]
                if item["kind"] == "BezierProjectileBehavior"
            )
            assert descriptor["runtimeCapabilities"] == [
                {
                    "kind": "projectile-capable",
                    "activation": "authored-projectile-launch",
                    "runtimeStatus": "deferred",
                    "moduleEvidence": evidence,
                }
            ]
            assert evidence["consumed"] is False
    assert owners == _BEZIER_NEUTRAL_OWNERS[game]


def test_catalog_accounts_for_creeps_and_lairs_without_neutral_props() -> None:
    catalog = compile_neutral_mob_catalog(_documents())
    validate_neutral_mob_catalog(catalog)
    rows = {row["objectId"]: row for row in catalog["neutralMobs"]}

    assert set(rows) == {
        "WargLair",
        "NeutralWolf",
        "NeutralWolf_Slaved",
        "RohanPeasant",
        "RohanPeasantHorde",
    }
    assert rows["WargLair"]["role"] == "lair"
    assert rows["NeutralWolf"]["role"] == "creature"
    assert rows["WargLair"]["runtimeDomain"] == "structure"
    assert rows["WargLair"]["runtimeStatus"] == "descriptor-ready"
    assert rows["NeutralWolf"]["runtimeDomain"] == "unit"
    assert rows["NeutralWolf"]["runtimeStatus"] == "descriptor-ready"
    hostile_simulation = rows["NeutralWolf"]["descriptor"]["gameplay"]["simulation"]
    assert hostile_simulation["status"] == "unresolved"
    assert "combat.weapon" in hostile_simulation["missing"]
    assert "combat" not in hostile_simulation["resolved"]
    assert rows["NeutralWolf_Slaved"]["runtimeStatus"] == "descriptor-ready"
    assert rows["RohanPeasant"]["runtimeStatus"] == "descriptor-ready"
    assert rows["RohanPeasant"]["descriptor"]["objectId"] == "RohanPeasant"
    assert rows["RohanPeasant"]["descriptor"]["production"] == []
    assert rows["RohanPeasant"]["descriptor"]["scenarioAdmission"]["surfaces"] == [
        "map-placement",
        "script-spawn",
        "object-creation-list",
        "lair-spawn",
    ]
    assert "INFANTRY" in rows["NeutralWolf"]["descriptor"]["kindOf"]["container"]
    assert rows["NeutralWolf"]["kindOfDefineProvenance"] == [
        {
            "defineId": "NATUREUNITS_KINDOF",
            "sourceIni": "data/ini/object/neutral/creeps.ini",
            "line": 2,
            "authoredValue": "PRELOAD IGNORED_IN_GUI INFANTRY INERT",
            "tokens": ["PRELOAD", "IGNORED_IN_GUI", "INFANTRY", "INERT"],
        }
    ]
    assert catalog["summary"] == {
        "neutralMobCount": 5,
        "descriptorReadyCount": 5,
        "runtimeDeferredCount": 0,
        "lairCount": 1,
        "hordeCount": 1,
        "unitDomainCount": 4,
        "structureDomainCount": 1,
        "propDomainCount": 0,
        "mapPlacementRootCount": 0,
        "mapPlacementAddedCount": 0,
    }


def test_exact_map_roots_admit_only_active_retail_structures() -> None:
    documents = _documents()
    documents["data/ini/object/neutral/creeps.ini"] = documents[
        "data/ini/object/neutral/creeps.ini"
    ].replace(
        b"KindOf = STRUCTURE IMMOBILE\nEnd\nObject WargLair",
        b"KindOf = STRUCTURE IMMOBILE SELECTABLE\n"
        b" Body = ActiveBody ModuleTag_Body\n"
        b"  MaxHealth = 500\n"
        b" End\n"
        b"End\n"
        b"Object PassiveMapRock\n"
        b" Side = Neutral\n"
        b" KindOf = STRUCTURE IMMOBILE INERT\n"
        b"End\n"
        b"Object WargLair",
    )

    catalog = compile_neutral_mob_catalog(
        documents,
        map_placement_object_ids=(
            "NeutralTree",
            "NeutralBarracks",
            "PassiveMapRock",
            "*Waypoints/Waypoint",
        ),
    )
    validate_neutral_mob_catalog(catalog)
    rows = {row["objectId"]: row for row in catalog["neutralMobs"]}

    # Exact map identity is only a root. Retail KindOf evidence decides whether
    # it is active gameplay; passive scenery must not acquire a descriptor.
    assert "NeutralBarracks" in rows
    assert "NeutralTree" not in rows
    assert "PassiveMapRock" not in rows
    assert rows["NeutralBarracks"]["mapPlacementRoot"] is True
    assert rows["NeutralBarracks"]["runtimeDomain"] == "structure"
    assert rows["NeutralBarracks"]["descriptor"]["scenarioAdmission"]["surfaces"] == [
        "map-placement",
        "script-spawn",
        "object-creation-list",
    ]
    assert catalog["summary"]["mapPlacementRootCount"] == 1
    assert catalog["summary"]["mapPlacementAddedCount"] == 1


def test_lair_text_does_not_turn_an_inherited_creature_into_a_structure() -> None:
    documents = _documents()
    documents["data/ini/object/neutral/creeps.ini"] += b"""
ChildObject NeutralWolf_FromWargLair NeutralWolf
End
"""

    catalog = compile_neutral_mob_catalog(documents)
    validate_neutral_mob_catalog(catalog)
    rows = {row["objectId"]: row for row in catalog["neutralMobs"]}

    spawned = rows["NeutralWolf_FromWargLair"]
    assert spawned["role"] == "creature"
    assert spawned["runtimeDomain"] == "unit"
    admission = spawned["descriptor"]["scenarioAdmission"]
    assert admission["role"] == "creature"
    assert admission["surfaces"] == [
        "map-placement",
        "script-spawn",
        "object-creation-list",
        "lair-spawn",
    ]
    assert rows["WargLair"]["role"] == "lair"
    assert rows["WargLair"]["runtimeDomain"] == "structure"


def test_catalog_is_deterministic_and_digest_protected() -> None:
    first = compile_neutral_mob_catalog(_documents(), game="rotwk")
    second = compile_neutral_mob_catalog(dict(reversed(list(_documents().items()))), game="rotwk")
    assert first == second

    broken = deepcopy(first)
    broken["neutralMobs"][0]["descriptor"]["objectId"] = "ChangedIdentity"
    with pytest.raises(NeutralMobCatalogError, match="descriptor identity"):
        validate_neutral_mob_catalog(broken)


def test_catalog_prepared_inputs_are_equivalent_and_do_not_leak() -> None:
    documents = _documents()
    original = deepcopy(documents)
    expected = compile_neutral_mob_catalog(documents, game="rotwk")
    prepared = prepare_playable_unit_compiler(documents)

    first = compile_neutral_mob_catalog(
        documents, game="rotwk", prepared=prepared
    )
    second = compile_neutral_mob_catalog(
        documents, game="rotwk", prepared=prepared
    )

    assert first == second == expected
    assert documents == original


def test_catalog_rejects_prepared_inputs_for_other_documents() -> None:
    documents = _documents()
    prepared = prepare_playable_unit_compiler(documents)
    with pytest.raises(NeutralMobCatalogError, match="different document mapping"):
        compile_neutral_mob_catalog(dict(documents), prepared=prepared)


def test_catalog_rejects_unknown_game() -> None:
    with pytest.raises(NeutralMobCatalogError, match="unsupported game"):
        compile_neutral_mob_catalog(_documents(), game="invalid")


@pytest.mark.parametrize(
    (
        "catalog_name", "game", "expected_count", "expected_domains",
        "expected_unsupported",
    ),
    (
        ("bfme2.json", "bfme2", 69, (42, 15, 12), 37),
        ("rotwk-layered.json", "rotwk", 83, (48, 23, 12), 60),
    ),
)
def test_effective_retail_neutral_mob_family_is_completely_accounted_for(
    catalog_name: str,
    game: str,
    expected_count: int,
    expected_domains: tuple[int, int, int],
    expected_unsupported: int,
) -> None:
    repo = Path(__file__).resolve().parents[2]
    catalog_path = repo / ".private" / "retail-work" / "catalog" / catalog_name
    if not catalog_path.is_file():
        pytest.skip("operator retail catalog is not available")
    documents = dict(read_catalog_documents(InstallCatalog.load(catalog_path)))

    result = compile_neutral_mob_catalog(documents, game=game)

    assert result["summary"]["neutralMobCount"] == expected_count
    assert (
        result["summary"]["descriptorReadyCount"]
        + result["summary"]["runtimeDeferredCount"]
        == expected_count
    )
    assert len({row["objectId"].casefold() for row in result["neutralMobs"]}) == expected_count
    assert (
        result["summary"]["unitDomainCount"],
        result["summary"]["structureDomainCount"],
        result["summary"]["propDomainCount"],
    ) == expected_domains
    assert result["summary"]["descriptorReadyCount"] == expected_count
    assert result["summary"]["runtimeDeferredCount"] == 0
    assert not {
        row["objectId"] for row in result["neutralMobs"]
        if row["runtimeStatus"] == "deferred"
    }
    for row in result["neutralMobs"]:
        if row["runtimeDomain"] == "unit":
            assert row["runtimeStatus"] == "descriptor-ready"
            assert row["descriptor"]["objectId"] == row["objectId"]
            assert row["descriptor"]["production"] == [] or "scenarioAdmission" not in row["descriptor"]
        elif row["runtimeDomain"] == "structure":
            assert row["runtimeStatus"] == "descriptor-ready"
            assert row["descriptor"]["objectId"] == row["objectId"]
            assert row["descriptor"]["production"]["routes"] == []
            assert row["descriptor"]["production"]["evidence"] == "authored-neutral-map"
            assert row["descriptor"]["scenarioAdmission"]["buildCommandExposed"] is False
        else:
            assert row["runtimeStatus"] == "descriptor-ready"
            assert row["descriptor"]["objectId"] == row["objectId"]
            assert row["descriptor"]["production"] == []
            assert row["descriptor"]["scenarioAdmission"]["buildCommandExposed"] is False
    _assert_unit_unsupported_capabilities_are_untyped(
        result, expected_count=expected_unsupported
    )
    _assert_exact_neutral_bezier_contracts(result, game=game)
    if game == "bfme2":
        peasant = next(
            row for row in result["neutralMobs"]
            if row["objectId"] == "RohanPeasant1"
        )
        upgrades = peasant["descriptor"]["gameplay"]["simulation"]["resolved"]["combat"]["upgrades"]
        forged_blades = next(
            row for row in upgrades
            if row["upgradeId"] == "Upgrade_RohanForgedBladesForPeasants"
        )
        assert forged_blades["kind"] == "authored-scenario-noop"


def test_rotwk_canonical_effective_assets_preserve_exact_domains_and_prop_ids() -> None:
    repo = Path(__file__).resolve().parents[2]
    effective_root = (
        repo
        / ".private"
        / "retail-work"
        / "editions"
        / "rotwk"
        / "cache"
        / "effective-assets"
    )
    if not effective_root.is_dir():
        pytest.skip("operator canonical RotWK 2.01 effective assets are unavailable")
    documents = _source_documents(effective_root)
    prepared = prepare_playable_unit_compiler(documents)

    result = compile_neutral_mob_catalog(
        documents, game="rotwk", prepared=prepared
    )

    assert result["summary"] == {
        "neutralMobCount": 83,
        "descriptorReadyCount": 83,
        "runtimeDeferredCount": 0,
        "lairCount": 22,
        "hordeCount": 5,
        "unitDomainCount": 48,
        "structureDomainCount": 23,
        "propDomainCount": 12,
        "mapPlacementRootCount": 0,
        "mapPlacementAddedCount": 0,
    }
    _assert_unit_unsupported_capabilities_are_untyped(
        result, expected_count=60
    )
    _assert_exact_neutral_bezier_contracts(result, game="rotwk")
    rows = {row["objectId"]: row for row in result["neutralMobs"]}
    assert not {
        object_id for object_id, row in rows.items()
        if row["runtimeStatus"] == "deferred"
    }
    fangorn = rows["FangornTrollCave"]
    assert fangorn["role"] == "ambient-or-scenario"
    assert fangorn["runtimeDomain"] == "structure"
    assert "STRUCTURE" in fangorn["descriptor"]["kindOf"]
    assert {
        row["objectId"]
        for row in result["neutralMobs"]
        if row["runtimeDomain"] == "prop"
    } == {
        "RockBigTroll",
        *(f"SpiderWebs{index:02d}" for index in range(1, 12)),
    }


@pytest.mark.parametrize(
    ("game", "catalog_relative", "map_pack_prefix", "expected", "passive_ids"),
    (
        (
            "bfme2",
            "catalog/bfme2.json",
            "bfme2-skirmish-maps-private/",
            {
                "BarrowWightLair": 10,
                "CaveTrollLair": 2,
                "Crow": 24,
                "Dove_white_in_game": 34,
                "FireDrakeLair": 6,
                "MoriarGoblinLair": 20,
                "MoriarGoblinLairSnow": 4,
                "WargLair": 4,
                "Wolf": 9,
            },
            {"Crow", "Dove_white_in_game", "Wolf"},
        ),
        (
            "rotwk",
            "editions/rotwk/catalog/rotwk.json",
            "rotwk-playable-maps-private/",
            {
                "BarrowWightLair": 43,
                "CaveTrollLair": 53,
                "CaveTrollLairSnow": 14,
                "Cow": 24,
                "Crow": 90,
                "Dove_white_in_game": 69,
                "FireDrakeLair": 24,
                "MoriarGoblinLair": 111,
                "MoriarGoblinLairSnow": 26,
                "RockBigTroll": 3,
                "Sheep": 19,
                "SpiderLair": 28,
                "WargLair": 52,
                "Wolf": 42,
            },
            {"Cow", "Crow", "Dove_white_in_game", "Sheep", "Wolf"},
        ),
    ),
)
def test_retail_map_placed_passive_units_have_closed_scenario_simulation(
    game: str,
    catalog_relative: str,
    map_pack_prefix: str,
    expected: dict[str, int],
    passive_ids: set[str],
) -> None:
    repo = Path(__file__).resolve().parents[2]
    private = repo / ".private" / "retail-work"
    catalog_path = private / catalog_relative
    selection_path = repo / ".private" / "content-packs" / "selection.json"
    if not catalog_path.is_file() or not selection_path.is_file():
        pytest.skip("operator retail catalog/map selection is unavailable")
    selection = json.loads(selection_path.read_text(encoding="utf-8"))
    selected = [
        value
        for value in selection["supplementalPacks"]
        if value.startswith(map_pack_prefix)
    ]
    if len(selected) != 1:
        pytest.skip("canonical selected map pack is unavailable")
    documents = dict(read_catalog_documents(InstallCatalog.load(catalog_path)))
    result = compile_neutral_mob_catalog(documents, game=game)
    rows = {row["objectId"].casefold(): row for row in result["neutralMobs"]}
    counts: Counter[str] = Counter()
    map_root = repo / ".private" / "content-packs" / selected[0]
    for path in map_root.rglob("objects.json"):
        document = json.loads(path.read_text(encoding="utf-8"))
        for placement in document["objects"]:
            key = str(placement.get("typeName", "")).casefold()
            if int(placement.get("roadType", 0)) == 0 and key in rows:
                row = rows[key]
                counts[row["objectId"]] += 1
    assert dict(sorted(counts.items())) == expected
    for object_id in passive_ids:
        catalog_row = rows[object_id.casefold()]
        assert catalog_row["runtimeStatus"] == "descriptor-ready"
        simulation = catalog_row["descriptor"]["gameplay"]["simulation"]
        assert simulation["status"] == "ready"
        assert simulation["missing"] == []
        resolved = simulation["resolved"]
        assert resolved["scenarioOnly"]["disposition"] == "explicit-scenario-admission"
        assert resolved["combat"] == {
            "disposition": "noncombatant",
            "evidence": "no-effective-weapon-or-damage-route",
            "kindOfEvidence": resolved["combat"]["kindOfEvidence"],
        }
        assert resolved["armor"]["setId"] == "NoArmor"
        assert float(resolved["memberHealth"]["value"]) > 0.0
        assert float(resolved["speed"]["value"]) > 0.0
        assert resolved["movement"]["locomotorId"]
        assert float(resolved["visionRange"]["value"]) > 0.0
        assert not any(
            field in resolved for field in ("buildCost", "buildTimeSeconds", "commandPoints")
        )
        squish = next(
            row for row in resolved["moduleContracts"]
            if row["module"] == "SquishCollide"
        )
        assert squish["fields"] == {}
        assert squish["runtimeStatus"] == "executable"
        slow_deaths = [
            row for row in resolved["moduleContracts"]
            if row["module"] == "SlowDeathBehavior"
        ]
        assert slow_deaths
        assert all(row["runtimeStatus"] == "executable" for row in slow_deaths)
        assert all(
            row["effectGraph"]["executionEligibility"] == {
                "status": "evidence-closed-core",
                "blockers": [],
                "runtimeStatus": "executable",
            }
            for row in slow_deaths
        )

    faction_upgrades = {
        "Upgrade_DwarfFaction",
        "Upgrade_ElfFaction",
        "Upgrade_IsengardFaction",
        "Upgrade_MenFaction",
        "Upgrade_MordorFaction",
        "Upgrade_WildFaction",
        *({"Upgrade_AngmarFaction"} if game == "rotwk" else set()),
    }
    lair_rows = [
        rows[object_id.casefold()]
        for object_id in expected
        if rows[object_id.casefold()]["role"] == "lair"
    ]
    assert len(lair_rows) == (8 if game == "rotwk" else 6)
    all_command_set_effects = [
        effect
        for neutral_row in rows.values()
        if neutral_row["role"] == "lair"
        for effect in neutral_row["descriptor"]["gameplay"]
            .get("upgradeEffects", {}).get("effects", [])
        if effect.get("kind") == "command-set-transition"
    ]
    assert len(all_command_set_effects) == (91 if game == "rotwk" else 48)
    assert all(
        effect["game"] == game
        and effect["runtimeStatus"] == "executable"
        and effect["descriptorStatus"] == "resolved"
        and effect["commandSetProvenance"]["sourceIni"] == effect["sourceIni"]
        and effect["commandSetProvenance"]["line"] > effect["line"]
        and effect["triggerUpgradeIds"]
        and effect["upgradeId"] in effect["triggerUpgradeIds"]
        for effect in all_command_set_effects
    )
    command_set_effect_count = 0
    for lair_row in lair_rows:
        gameplay = lair_row["descriptor"]["gameplay"]
        command_set_effects = [
            row
            for row in gameplay["upgradeEffects"]["effects"]
            if row["kind"] == "command-set-transition"
        ]
        command_set_effect_count += len(command_set_effects)
        assert {row["upgradeId"] for row in command_set_effects} == faction_upgrades
        assert len({row["commandSetId"] for row in command_set_effects}) == 1
        assert all(
            row["descriptorStatus"] == "resolved"
            and row["runtimeStatus"] == "executable"
            and row["triggerSemantics"] == "any"
            and set(row["triggerUpgradeIds"]) == faction_upgrades
            and row["moduleTag"]
            and row["sourceIni"]
            and row["line"] > 0
            and row["customAnimation"]["animState"] == "USER_2"
            and row["customAnimation"]["animTimeMs"] == 0.0
            and row["customAnimation"]["runtimeStatus"] == "deferred"
            for row in command_set_effects
        )
        trained = [
            row
            for row in gameplay["trainedCommandSets"]
            if row["kind"] == "upgraded"
        ]
        assert len(trained) == 1
        assert trained[0]["id"] == command_set_effects[0]["commandSetId"]
        assert set(trained[0]["triggeredBy"]) == faction_upgrades
    assert command_set_effect_count == (56 if game == "rotwk" else 36)

    passive_id = sorted(passive_ids)[0]
    for mutation in ("simulation", "slow_death"):
        stale = deepcopy(result)
        stale_row = next(
            row for row in stale["neutralMobs"] if row["objectId"] == passive_id
        )
        stale_simulation = stale_row["descriptor"]["gameplay"]["simulation"]
        stale_row["runtimeStatus"] = "descriptor-ready"
        stale_row.pop("deferredReason", None)
        if mutation == "simulation":
            stale_simulation["status"] = "unresolved"
            stale_simulation["missing"] = ["combat.weapon"]
        else:
            stale_simulation["status"] = "ready"
            stale_simulation["missing"] = []
            stale_slow_death = next(
                row
                for row in stale_simulation["resolved"]["moduleContracts"]
                if row["module"] == "SlowDeathBehavior"
            )
            stale_slow_death["runtimeStatus"] = "deferred"
        descriptor = stale_row["descriptor"]
        descriptor["descriptorSha256"] = _digest(
            {key: value for key, value in descriptor.items() if key != "descriptorSha256"}
        )
        stale["catalogSha256"] = _digest(
            {key: value for key, value in stale.items() if key != "catalogSha256"}
        )
        with pytest.raises(NeutralMobCatalogError, match="runtime evidence is incomplete"):
            validate_neutral_mob_catalog(stale)
