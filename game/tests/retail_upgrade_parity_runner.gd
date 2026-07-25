extends SceneTree
## Upgrade-parity gate: marketplace research surface, building level-swap
## node resolution across all six factions, per-battalion upgrade eligibility,
## purchase effect isolation, and research gating.
##
## Fixture sim rules mirror the converter's emitted shapes (pinned by
## importer/tests/test_upgrade_parity_compiler.py against synthetic INIs and
## by converter runs over the retail 1.06 effective-assets view):
##  - structure research: marketplace GrandHarvest/Defiance/IronOre, forge
##    ForgedBlades/HeavyArmor/IronOre technologies, barracks BasicTraining,
##    archery-range FireArrows (NeededUpgrade rows included);
##  - structure upgradeEffects: IronOre -25% discount, Defiance 50% refund,
##    GrandHarvest 110% farm income;
##  - unit upgradeCommands + levelUpgrades: the horde command-set
##    OBJECT_UPGRADE buttons and Basic Training LevelUpUpgrade.

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")
const UnitAdapterScript = preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")

var passed := 0
var failed := 0
var StructureScript: GDScript


const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
# Turns a GDScript runtime error inside `_run` — which unwinds past every
# `quit()` and would otherwise leave this headless process idling forever —
# into a loud non-zero exit. See tests/runner_watchdog.gd.
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "RETAIL_UPGRADE_PARITY_RUNNER")
	call_deferred("_run")


func _run() -> void:
	StructureScript = load("res://src/retail_slice/retail_structure.gd")
	_test_marketplace_research_surface()
	_test_research_gating_flow()
	_test_economy_effects()
	_test_battalion_eligibility_matrix()
	_test_battalion_purchase_flow()
	_test_legacy_forge_fallback()
	_test_real_pack_level_swap_audit()
	_finish()


## --- Fixture data -----------------------------------------------------------

func _research_surface(upgrades: Array) -> Dictionary:
	return {"upgrades": upgrades, "sourceIni": ["data/ini/upgrade.ini"]}


func _research_row(
	upgrade_id: String,
	command_id: String,
	slot: int,
	cost: int,
	seconds: float,
	image_id: String,
	label_id: String,
	needed: Array = []
) -> Dictionary:
	var row := {
		"upgradeId": upgrade_id,
		"commandId": command_id,
		"commandSetId": "FixtureSet",
		"slot": slot,
		"cost": cost,
		"buildTimeSeconds": seconds,
		"cancelable": true,
		"neededUpgradeAny": false,
		"labelId": label_id,
		"tooltipId": "TOOLTIP:Fixture%s" % upgrade_id,
		"buttonImageId": image_id,
	}
	if not needed.is_empty():
		row["neededUpgradeIds"] = needed
	return row


func _fixture_manifest() -> Dictionary:
	return {
		"faction": "men",
		"structure_kinds": ["fortress", "marketplace", "farm", "forge", "barracks", "archery_range"],
		"seed_structure_kinds": ["fortress", "marketplace", "farm", "forge", "barracks", "archery_range"],
		"structure_object_ids": {},
		"structure_max_health": {
			"fortress": 7500, "marketplace": 2500, "farm": 100,
			"forge": 1500, "barracks": 3000, "archery_range": 3000,
		},
		"structure_build_rules": {
			"fortress": {"cost": 5000, "seconds": 120.0},
			"marketplace": {"cost": 600, "seconds": 40.0},
			"farm": {"cost": 300, "seconds": 25.0},
			"forge": {"cost": 900, "seconds": 45.0},
			"barracks": {"cost": 350, "seconds": 30.0},
			"archery_range": {"cost": 300, "seconds": 30.0},
		},
		"structure_armor": {},
		"structure_upgrade_chains": {
			"forge": {
				"levelCap": 3,
				"steps": [
					{
						"upgradeId": "Upgrade_GondorForgeLevel2", "toLevel": 2,
						"cost": 1000, "buildTimeSeconds": 30.0,
						"fromCommandSet": "GondorForgeCommandSet",
						"toCommandSet": "GondorForgeCommandSetLevel2",
						"levelsToGain": 1, "levelCap": 3, "cancelable": true,
						"commandId": "Command_PurchaseUpgradeGondorForgeLevel2",
						"slot": 4, "labelId": "CONTROLBAR:ConstructGondorForgeLevel2Upgrade",
						"tooltipId": "CONTROLBAR:ToolTipBuildGondorForgeLevel2Upgrade",
						"buttonImageId": "UCCommon_UpgradeStructureNew",
						"effects": [{"category": "LEVEL", "id": "GondorForgeHitPointModLvl2", "modifiers": [{"application": "additive", "kind": "HEALTH", "value": 1500.0}], "sourceIni": "data/ini/attributemodifier.ini"}],
					},
					{
						"upgradeId": "Upgrade_GondorForgeLevel3", "toLevel": 3,
						"cost": 500, "buildTimeSeconds": 60.0,
						"fromCommandSet": "GondorForgeCommandSetLevel2",
						"toCommandSet": "GondorForgeCommandSetLevel3",
						"levelsToGain": 1, "levelCap": 3, "cancelable": true,
						"commandId": "Command_PurchaseUpgradeGondorForgeLevel3",
						"requiresUpgradeId": "Upgrade_GondorForgeLevel2",
						"slot": 4, "labelId": "CONTROLBAR:ConstructGondorForgeLevel3Upgrade",
						"tooltipId": "CONTROLBAR:ToolTipBuildGondorForgeLevel3Upgrade",
						"buttonImageId": "UCCommon_UpgradeStructureNew",
						"effects": [{"category": "LEVEL", "id": "GondorForgeHitPointModLvl3", "modifiers": [{"application": "additive", "kind": "HEALTH", "value": 1500.0}], "sourceIni": "data/ini/attributemodifier.ini"}],
					},
				],
			},
			"archery_range": {
				"levelCap": 3,
				"steps": [
					{
						"upgradeId": "Upgrade_GondorArcheryRangeLevel2", "toLevel": 2,
						"cost": 500, "buildTimeSeconds": 30.0,
						"fromCommandSet": "GondorArcheryCommandSet",
						"toCommandSet": "GondorArcheryCommandSetLevel2",
						"levelsToGain": 1, "levelCap": 3, "cancelable": true,
						"commandId": "Command_PurchaseUpgradeGondorArcheryRangeLevel2",
						"slot": 4, "labelId": "CONTROLBAR:ConstructGondorArcheryRangeLevel2Upgrade",
						"tooltipId": "CONTROLBAR:ToolTipBuildGondorArcheryRangeLevel2Upgrade",
						"buttonImageId": "UCCommon_UpgradeStructureNew",
						"effects": [{"category": "LEVEL", "id": "GondorArcheryRangeHitPointModLvl2", "modifiers": [{"application": "additive", "kind": "HEALTH", "value": 1500.0}], "sourceIni": "data/ini/attributemodifier.ini"}],
					},
					{
						"upgradeId": "Upgrade_GondorArcheryRangeLevel3", "toLevel": 3,
						"cost": 650, "buildTimeSeconds": 30.0,
						"fromCommandSet": "GondorArcheryCommandSetLevel2",
						"toCommandSet": "GondorArcheryCommandSetLevel3",
						"levelsToGain": 1, "levelCap": 3, "cancelable": true,
						"commandId": "Command_PurchaseUpgradeGondorArcheryRangeLevel3",
						"requiresUpgradeId": "Upgrade_GondorArcheryRangeLevel2",
						"slot": 4, "labelId": "CONTROLBAR:ConstructGondorArcheryRangeLevel3Upgrade",
						"tooltipId": "CONTROLBAR:ToolTipBuildGondorArcheryRangeLevel3Upgrade",
						"buttonImageId": "UCCommon_UpgradeStructureNew",
						"effects": [{"category": "LEVEL", "id": "GondorArcheryRangeHitPointModLvl3", "modifiers": [{"application": "additive", "kind": "HEALTH", "value": 1500.0}], "sourceIni": "data/ini/attributemodifier.ini"}],
					},
				],
			},
		},
		"structure_research": {
			"marketplace": _research_surface([
				_research_row("Upgrade_MarketplaceUpgradeGrandHarvest", "Command_PurchaseUpgradeGrandHarvest", 1, 1000, 60.0, "BGMarketplace_GrandHarvest", "CONTROLBAR:ConstructGrandHarvestUpgrade"),
				_research_row("Upgrade_MarketplaceUpgradeDefiance", "Command_PurchaseUpgradeDefiance", 2, 500, 60.0, "BGMarketplace_Defiance", "CONTROLBAR:ConstructSiegeMaterialsUpgrade"),
				_research_row("Upgrade_MarketplaceUpgradeIronOre", "Command_PurchaseUpgradeIronOre", 3, 500, 60.0, "BGMarketplace_IronOre", "CONTROLBAR:ConstructIronOreUpgrade", ["Upgrade_TechnologyIronOre"]),
			]),
			"forge": _research_surface([
				_research_row("Upgrade_TechnologyGondorForgedBlades", "Command_PurchaseTechnologyGondorForgedBlades", 1, 1000, 30.0, "BRArmory_ForgedBlades", "CONTROLBAR:PurchaseTechnologyGondorForgedBlades"),
				_research_row("Upgrade_TechnologyGondorHeavyArmor", "Command_PurchaseTechnologyGondorHeavyArmor", 2, 1000, 30.0, "BGBlacksmith_HeavyArmor", "CONTROLBAR:PurchaseTechnologyGondorHeavyArmor", ["Upgrade_GondorForgeLevel2"]),
				_research_row("Upgrade_TechnologyIronOre", "Command_PurchaseTechnologyGondorIronOre", 3, 200, 30.0, "BGMarketplace_IronOre", "CONTROLBAR:PurchaseTechnologyGondorIronOre", ["Upgrade_GondorForgeLevel3"]),
			]),
			"barracks": _research_surface([
				_research_row("Upgrade_TechnologyGondorBasicTraining", "Command_PurchaseTechnologyGondorBasicTraining", 3, 400, 15.0, "BGBlacksmith_SilverTreeBanner", "CONTROLBAR:PurchaseTechnologyGondorBasicTraining"),
			]),
			"archery_range": _research_surface([
				_research_row("Upgrade_TechnologyGondorFireArrows", "Command_PurchaseTechnologyGondorFireArrows", 3, 1000, 30.0, "BGArcheryRange_FireArrows", "CONTROLBAR:PurchaseTechnologyGondorFireArrows", ["Upgrade_GondorArcheryRangeLevel3"]),
			]),
		},
		"structure_upgrade_effects": {
			"marketplace": {
				"effects": [
					{"upgradeId": "Upgrade_MarketplaceUpgradeDefiance", "kind": "refund-on-death", "refundPercent": 50.0, "buildingRequired": "ANY +GondorMarketPlace", "sourceIni": "fixture", "line": 1},
					{"upgradeId": "Upgrade_MarketplaceUpgradeIronOre", "kind": "upgrade-discount", "applyToUpgradeIds": ["Upgrade_GondorForgedBlades", "Upgrade_GondorHeavyArmor", "Upgrade_RohanForgedBladesForRohirrim", "Upgrade_RohanHeavyArmorForRohirrim"], "percent": -25.0, "upgradeDiscount": true, "labelId": "GUI:UPGRADE_DISCOUNT", "sourceIni": "fixture", "line": 2},
				],
				"unsupportedEffects": [],
			},
			"farm": {
				"effects": [
					{"upgradeId": "Upgrade_MarketplaceUpgradeDefiance", "kind": "refund-on-death", "refundPercent": 50.0, "buildingRequired": "ANY +GondorMarketPlace", "sourceIni": "fixture", "line": 3},
					{"upgradeId": "Upgrade_MarketplaceUpgradeGrandHarvest", "kind": "income-bonus", "bonusPercent": 110.0, "upgradeMustBePresent": "ANY +GondorMarketPlace", "sourceIni": "fixture", "line": 4},
				],
				"unsupportedEffects": [],
			},
		},
		"producer_kind_registry": {
			"MenFortress": "fortress",
			"GondorMarketPlace": "marketplace",
			"GondorFarm": "farm",
			"GondorForge": "forge",
			"GondorBarracks": "barracks",
			"GondorArcherRange": "archery_range",
		},
		"unit_production_rules": {},
		"unit_damage_types": {},
		"ai_production_plan": [],
		"spawn_roster": [],
		"excluded_units": [],
		"builder_unit_ids": [],
		"faction_pack_roots": [],
	}


func _upgrade_command_row(
	upgrade_id: String,
	command_id: String,
	slot: int,
	cost: int,
	needed: Array,
	image_id: String
) -> Dictionary:
	return {
		"upgradeId": upgrade_id,
		"commandId": command_id,
		"commandSetId": "FixtureHordeCommandSet",
		"slot": slot,
		"cancelable": true,
		"multiSelect": true,
		"neededUpgradeAny": false,
		"neededUpgradeIds": needed,
		"labelId": "CONTROLBAR:Fixture%s" % upgrade_id,
		"tooltipId": "TOOLTIP:Fixture%s" % upgrade_id,
		"buttonImageId": image_id,
		"cost": cost,
		"buildTimeSeconds": 10,
	}


func _fixture_unit_doc(
	object_id: String,
	member_id: String,
	category: String,
	producer_object_id: String,
	producer_kind_slot: int,
	upgrade_rows: Array,
	combat_upgrades: Array,
	armor_upgrades: Array,
	level_ups: Array
) -> Dictionary:
	var gameplay := {}
	if not upgrade_rows.is_empty():
		gameplay["upgradeCommands"] = upgrade_rows
	if not level_ups.is_empty():
		gameplay["levelUpgrades"] = level_ups
	var armor := {
		"setId": "FixtureArmor",
		"table": {"default": {"percent": 100.0}, "scalars": {}, "damageScalar": {"percent": 100.0}},
		"upgrades": armor_upgrades,
		"excludedUpgradeSets": [],
	}
	return {
		"schema": "openbfme.playable-unit-runtime",
		"schemaVersion": 0,
		"objectId": object_id,
		"category": category,
		"registration": {
			"production": [{
				"producerObjectId": producer_object_id,
				"commandSetId": "%sCommandSet" % producer_object_id,
				"commandId": "Command_Construct%s" % object_id,
				"surface": "command-socket",
				"slot": producer_kind_slot,
				"prerequisites": [],
				"commandSetTransition": [],
			}],
			"composition": {
				"containerObjectId": object_id,
				"primaryMemberObjectId": member_id,
				"members": [{"objectId": member_id, "count": 10}],
			},
			"gameplay": gameplay,
			"simulation": {
				"displayName": object_id,
				"buildCost": 200,
				"buildTimeSeconds": 20.0,
				"commandPoints": 60,
				"memberCount": 10,
				"memberHealth": 100,
				"speed": 50.0,
				"visionRange": 360.0,
				"combat": {
					"attackRange": 40.0, "minimumAttackRange": 0.0,
					"delayBetweenShotsMs": 600.0, "preAttackDelayMs": 200.0,
					"firingDurationMs": 200.0, "damage": 25, "damageType": "SLASH",
				},
				"movement": {"acceleration": 100.0, "braking": 100.0, "turnRateDegreesPerSecond": 360.0},
				"formation": {
					"memberCount": 10,
					"positions": [
						{"x": 0.0, "y": 0.0}, {"x": 10.0, "y": 0.0}, {"x": 20.0, "y": 0.0},
						{"x": 0.0, "y": 10.0}, {"x": 10.0, "y": 10.0}, {"x": 20.0, "y": 10.0},
						{"x": 0.0, "y": 20.0}, {"x": 10.0, "y": 20.0}, {"x": 20.0, "y": 20.0},
						{"x": 30.0, "y": 10.0},
					],
				},
				"resolved": {
					"armor": armor,
					"combat": {
						"damageType": "SLASH",
						"upgrades": combat_upgrades,
					},
				},
			},
		},
	}


func _archer_doc() -> Dictionary:
	return _fixture_unit_doc(
		"GondorArcherHorde", "GondorArcher", "ranged-infantry", "GondorArcherRange", 1,
		[
			_upgrade_command_row("Upgrade_GondorArcherFireArrows", "Command_PurchaseUpgradeGondorArcherFireArrows", 3, 200, ["Upgrade_TechnologyGondorFireArrows"], "BGArcheryRange_FireArrows"),
			_upgrade_command_row("Upgrade_GondorHeavyArmor", "Command_PurchaseUpgradeGondorHeavyArmor", 4, 300, ["Upgrade_TechnologyGondorHeavyArmor"], "BGBlacksmith_HeavyArmor"),
			_upgrade_command_row("Upgrade_GondorBasicTraining", "Command_PurchaseUpgradeGondorBasicTraining", 5, 300, ["Upgrade_TechnologyGondorBasicTraining"], "BGBlacksmith_SilverTreeBanner"),
		],
		[{"upgradeId": "Upgrade_GondorArcherFireArrows", "kind": "warhead-upgrade", "warheadId": "GondorArcherBowFireWarhead", "nuggets": [{"damage": {"value": 32.0}, "damageType": "FLAME", "damageScalars": []}, {"damage": {"value": 25.0}, "damageType": "PIERCE", "damageScalars": []}], "damageScalars": []}],
		[{"upgradeId": "Upgrade_GondorHeavyArmor", "setId": "ArcherHeavyArmor", "table": {"default": {"percent": 100.0}, "scalars": {}, "damageScalar": {"percent": 120.0}}}],
		[{"upgradeId": "Upgrade_GondorBasicTraining", "levelsToGain": 1, "levelCap": 2, "sourceIni": "fixture", "line": 1}]
	)


func _ranger_doc() -> Dictionary:
	return _fixture_unit_doc(
		"GondorRangerHorde", "GondorRanger", "ranged-infantry", "GondorArcherRange", 2,
		[
			_upgrade_command_row("Upgrade_GondorFireArrows", "Command_PurchaseUpgradeGondorFireArrows", 3, 300, ["Upgrade_TechnologyGondorFireArrows"], "BGArcheryRange_FireArrows"),
			_upgrade_command_row("Upgrade_GondorBasicTraining", "Command_PurchaseUpgradeGondorBasicTraining", 5, 300, ["Upgrade_TechnologyGondorBasicTraining"], "BGBlacksmith_SilverTreeBanner"),
		],
		[{"upgradeId": "Upgrade_GondorFireArrows", "kind": "warhead-upgrade", "warheadId": "GondorRangerBowFireWarhead", "nuggets": [{"damage": {"value": 30.0}, "damageType": "FLAME", "damageScalars": []}], "damageScalars": []}],
		[],
		[{"upgradeId": "Upgrade_GondorBasicTraining", "levelsToGain": 1, "levelCap": 2, "sourceIni": "fixture", "line": 2}]
	)


func _fighter_doc() -> Dictionary:
	return _fixture_unit_doc(
		"GondorFighterHorde", "GondorFighter", "infantry", "GondorBarracks", 1,
		[
			_upgrade_command_row("Upgrade_GondorForgedBlades", "Command_PurchaseUpgradeGondorForgedBlades", 3, 300, ["Upgrade_TechnologyGondorForgedBlades"], "BRArmory_ForgedBlades"),
			_upgrade_command_row("Upgrade_GondorHeavyArmor", "Command_PurchaseUpgradeGondorHeavyArmor", 4, 300, ["Upgrade_TechnologyGondorHeavyArmor"], "BGBlacksmith_HeavyArmor"),
			_upgrade_command_row("Upgrade_GondorBasicTraining", "Command_PurchaseUpgradeGondorBasicTraining", 5, 300, ["Upgrade_TechnologyGondorBasicTraining"], "BGBlacksmith_SilverTreeBanner"),
		],
		[{"upgradeId": "Upgrade_GondorForgedBlades", "kind": "weapon-swap", "weaponId": "GondorSwordUpgraded", "damage": {"value": 90.0}, "damageType": "SLASH", "damageScalars": [{"filter": "ANY +INFANTRY -HERO", "percent": 200.0}]}],
		[{"upgradeId": "Upgrade_GondorHeavyArmor", "setId": "SoldierHeavyArmor", "table": {"default": {"percent": 100.0}, "scalars": {}, "damageScalar": {"percent": 120.0}}}],
		[{"upgradeId": "Upgrade_GondorBasicTraining", "levelsToGain": 1, "levelCap": 2, "sourceIni": "fixture", "line": 3}]
	)


func _fixture_unit_runtimes() -> Dictionary:
	return {
		"GondorArcherHorde": _archer_doc(),
		"GondorRangerHorde": _ranger_doc(),
		"GondorFighterHorde": _fighter_doc(),
	}


func _fixture_rules(with_units := true, with_research := true) -> Dictionary:
	var manifest := _fixture_manifest()
	if not with_research:
		manifest["structure_research"] = {}
		manifest["structure_upgrade_effects"] = {}
	var archer_type: String = UnitAdapterScript.runtime_unit_id(_archer_doc())
	var ranger_type: String = UnitAdapterScript.runtime_unit_id(_ranger_doc())
	var fighter_type: String = UnitAdapterScript.runtime_unit_id(_fighter_doc())
	manifest["unit_production_rules"] = {
		archer_type: {"object_id": "bfme2.object.gondor-archer", "display_name": "Archers", "category": "ranged-infantry", "producer_kind": "archery_range", "producer_kinds": ["archery_range"], "default_cost": 200, "default_build_ticks": 200, "default_command_points": 60},
		ranger_type: {"object_id": "bfme2.object.gondor-ranger", "display_name": "Rangers", "category": "ranged-infantry", "producer_kind": "archery_range", "producer_kinds": ["archery_range"], "default_cost": 600, "default_build_ticks": 300, "default_command_points": 70},
		fighter_type: {"object_id": "bfme2.object.gondor-fighter", "display_name": "Soldiers", "category": "infantry", "producer_kind": "barracks", "producer_kinds": ["barracks"], "default_cost": 200, "default_build_ticks": 200, "default_command_points": 60},
	}
	manifest["spawn_roster"] = [
		{"id": 1, "team": 0, "anchor": "player_spawn_primary", "name": "Archers", "object_id": "bfme2.object.gondor-archer", "unit_type": archer_type, "command_points": 60},
		{"id": 2, "team": 0, "anchor": "player_spawn_secondary", "name": "Archers", "object_id": "bfme2.object.gondor-archer", "unit_type": archer_type, "command_points": 60},
		{"id": 3, "team": 0, "anchor": "player_spawn_primary", "name": "Soldiers", "object_id": "bfme2.object.gondor-fighter", "unit_type": fighter_type, "command_points": 60},
		{"id": 101, "team": 1, "anchor": "enemy_spawn_primary", "name": "Rangers", "object_id": "bfme2.object.gondor-ranger", "unit_type": ranger_type, "command_points": 70},
	]
	var rules := {
		"enable_base_loop": true,
		"spawn_initial_battalions": true,
		"starting_resources": 50000,
		"command_point_cap": 10000,
		"farm_payout_ticks": 50,
		"farm_income": 25,
		"maximum_queue": 5,
		"source_map_transform_scale": 0.1,
		"faction_manifest": manifest,
		"producer_kind_by_source_object": {
			"GondorArcherRange": "archery_range",
			"GondorBarracks": "barracks",
		},
	}
	if with_units:
		rules["playable_unit_runtimes"] = _fixture_unit_runtimes()
	return rules


func _new_sim(rules: Dictionary) -> Object:
	var sim = SimScript.new()
	sim.setup({}, rules)
	sim.ai_enabled = false
	return sim


## --- Marketplace research surface (fixture) ---------------------------------

func _test_marketplace_research_surface() -> void:
	var sim := _new_sim(_fixture_rules())
	_check("fixture_sim_has_no_configuration_error", sim.configuration_error == "", sim.configuration_error)
	var marketplace: int = sim.producer_id(SimScript.PLAYER_TEAM, "marketplace")
	_check("marketplace_is_seeded", marketplace != 0)
	var commands: Array = sim.structure_upgrade_commands(marketplace)
	var research_rows: Array = []
	for command in commands:
		if bool(command.get("research", false)):
			research_rows.append(command)
	_check(
		"marketplace_research_surface_has_three_authored_rows",
		research_rows.size() == 3,
		str(commands)
	)
	if research_rows.size() == 3:
		var ids: Array = []
		for row in research_rows:
			ids.append(String(row.get("upgrade_id", "")))
		ids.sort()
		_check(
			"marketplace_research_ids_are_authored",
			ids == ["Upgrade_MarketplaceUpgradeDefiance", "Upgrade_MarketplaceUpgradeGrandHarvest", "Upgrade_MarketplaceUpgradeIronOre"],
			str(ids)
		)
		var by_id := {}
		for row in research_rows:
			by_id[String(row.get("upgrade_id", ""))] = row
		var harvest: Dictionary = by_id.get("Upgrade_MarketplaceUpgradeGrandHarvest", {})
		_check(
			"grand_harvest_row_carries_authored_slot_cost_image",
			int(harvest.get("slot", 0)) == 1
				and int(harvest.get("cost", 0)) == 1000
				and int(harvest.get("duration_ticks", 0)) == 600
				and String(harvest.get("image_id", "")) == "BGMarketplace_GrandHarvest"
				and String(harvest.get("label_id", "")) == "CONTROLBAR:ConstructGrandHarvestUpgrade"
				and bool(harvest.get("gate_satisfied", false)),
			str(harvest)
		)
		var iron_ore: Dictionary = by_id.get("Upgrade_MarketplaceUpgradeIronOre", {})
		_check(
			"iron_ore_row_records_its_technology_gate",
			not bool(iron_ore.get("gate_satisfied", true))
				and Array(iron_ore.get("needed_upgrade_ids", [])) == ["Upgrade_TechnologyIronOre"]
				and int(iron_ore.get("slot", 0)) == 3
				and String(iron_ore.get("image_id", "")) == "BGMarketplace_IronOre",
			str(iron_ore)
		)
	var denied: Dictionary = sim.queue_structure_upgrade(
		SimScript.PLAYER_TEAM, marketplace, "Upgrade_MarketplaceUpgradeIronOre"
	)
	_check(
		"iron_ore_research_is_gated_before_its_technology",
		not bool(denied.get("ok", true))
			and String(denied.get("reason", "")) == "missing-upgrade"
			and String(denied.get("required_upgrade", "")) == "Upgrade_TechnologyIronOre",
		str(denied)
	)
	var resources_before: int = sim.resources_for_team(SimScript.PLAYER_TEAM)
	var queued: Dictionary = sim.queue_structure_upgrade(
		SimScript.PLAYER_TEAM, marketplace, "Upgrade_MarketplaceUpgradeGrandHarvest"
	)
	_check(
		"grand_harvest_research_queues_with_authored_cost",
		bool(queued.get("ok", false))
			and int((queued.get("item", {}) as Dictionary).get("cost", -1)) == 1000
			and sim.resources_for_team(SimScript.PLAYER_TEAM) == resources_before - 1000,
		str(queued)
	)
	sim.advance(599)
	_check("grand_harvest_does_not_complete_early", not (sim.team_upgrades.get(SimScript.PLAYER_TEAM, {}) as Dictionary).has("Upgrade_MarketplaceUpgradeGrandHarvest"))
	sim.advance(1)
	_check(
		"grand_harvest_completes_as_team_technology",
		(sim.team_upgrades.get(SimScript.PLAYER_TEAM, {}) as Dictionary).has("Upgrade_MarketplaceUpgradeGrandHarvest"),
		str(sim.team_upgrades)
	)
	var after: Array = sim.structure_upgrade_commands(marketplace)
	var still_offered := false
	for command in after:
		if String(command.get("upgrade_id", "")) == "Upgrade_MarketplaceUpgradeGrandHarvest":
			still_offered = true
	_check("completed_research_leaves_the_surface", not still_offered, str(after))


func _test_research_gating_flow() -> void:
	var sim := _new_sim(_fixture_rules())
	_check("gating_sim_has_no_configuration_error", sim.configuration_error == "", sim.configuration_error)
	var forge: int = sim.producer_id(SimScript.PLAYER_TEAM, "forge")
	var denied: Dictionary = sim.queue_structure_upgrade(
		SimScript.PLAYER_TEAM, forge, "Upgrade_TechnologyGondorHeavyArmor"
	)
	_check(
		"heavy_armor_technology_needs_forge_level_two",
		not bool(denied.get("ok", true))
			and String(denied.get("reason", "")) == "missing-upgrade"
			and String(denied.get("required_upgrade", "")) == "Upgrade_GondorForgeLevel2",
		str(denied)
	)
	var blades: Dictionary = sim.queue_structure_upgrade(
		SimScript.PLAYER_TEAM, forge, "Upgrade_TechnologyGondorForgedBlades"
	)
	_check("forged_blades_technology_is_ungated", bool(blades.get("ok", false)), str(blades))
	sim.advance(300)
	var level_two: Dictionary = sim.queue_structure_upgrade(
		SimScript.PLAYER_TEAM, forge, "Upgrade_GondorForgeLevel2"
	)
	_check("forge_level_two_queues", bool(level_two.get("ok", false)), str(level_two))
	sim.advance(300)
	_check(
		"forge_level_two_completes",
		Array(sim.structure(forge).get("completed_upgrades", [])).has("Upgrade_GondorForgeLevel2"),
		str(sim.structure(forge))
	)
	var armory: Dictionary = sim.queue_structure_upgrade(
		SimScript.PLAYER_TEAM, forge, "Upgrade_TechnologyGondorHeavyArmor"
	)
	_check("heavy_armor_technology_queues_after_forge_level_two", bool(armory.get("ok", false)), str(armory))
	sim.advance(300)
	var iron_ore_tech_denied: Dictionary = sim.queue_structure_upgrade(
		SimScript.PLAYER_TEAM, forge, "Upgrade_TechnologyIronOre"
	)
	_check(
		"iron_ore_technology_needs_forge_level_three",
		not bool(iron_ore_tech_denied.get("ok", true))
			and String(iron_ore_tech_denied.get("required_upgrade", "")) == "Upgrade_GondorForgeLevel3",
		str(iron_ore_tech_denied)
	)
	var level_three: Dictionary = sim.queue_structure_upgrade(
		SimScript.PLAYER_TEAM, forge, "Upgrade_GondorForgeLevel3"
	)
	_check("forge_level_three_queues", bool(level_three.get("ok", false)), str(level_three))
	sim.advance(600)
	var iron_ore_tech: Dictionary = sim.queue_structure_upgrade(
		SimScript.PLAYER_TEAM, forge, "Upgrade_TechnologyIronOre"
	)
	_check("iron_ore_technology_queues_after_forge_level_three", bool(iron_ore_tech.get("ok", false)), str(iron_ore_tech))
	sim.advance(300)
	_check(
		"research_completions_grant_team_technologies",
		(sim.team_upgrades.get(SimScript.PLAYER_TEAM, {}) as Dictionary).has("Upgrade_TechnologyGondorForgedBlades")
			and (sim.team_upgrades.get(SimScript.PLAYER_TEAM, {}) as Dictionary).has("Upgrade_TechnologyGondorHeavyArmor")
			and (sim.team_upgrades.get(SimScript.PLAYER_TEAM, {}) as Dictionary).has("Upgrade_TechnologyIronOre"),
		str(sim.team_upgrades)
	)
	# The compiled research surface replaced the provisional forge contracts.
	_check(
		"compiled_research_replaces_provisional_forge_contracts",
		not sim._structure_upgrade_contracts.has("Upgrade_GondorForgedBlades")
			and sim._structure_upgrade_contracts.has("Upgrade_TechnologyGondorForgedBlades"),
		str(sim._structure_upgrade_contracts.keys())
	)


func _test_economy_effects() -> void:
	var sim := _new_sim(_fixture_rules())
	_check("economy_sim_has_no_configuration_error", sim.configuration_error == "", sim.configuration_error)
	var marketplace: int = sim.producer_id(SimScript.PLAYER_TEAM, "marketplace")
	var farm: int = sim.producer_id(SimScript.PLAYER_TEAM, "farm")
	# Grand Harvest: authored 110% farm income once the technology is owned and
	# a marketplace stands (the pinned pack's farm doc carries no effect rows
	# until republish; the fixture mirrors the compiled block).
	var base_payout := _payout_amount_for(sim, farm)
	_check("farm_payout_is_base_before_grand_harvest", base_payout == 25, str(base_payout))
	sim.queue_structure_upgrade(SimScript.PLAYER_TEAM, marketplace, "Upgrade_MarketplaceUpgradeGrandHarvest")
	sim.advance(600)
	var boosted_payout := _payout_amount_for(sim, farm)
	_check(
		"grand_harvest_applies_authored_income_bonus",
		boosted_payout == roundi(25.0 * 110.0 / 100.0),
		"base=%d boosted=%d" % [base_payout, boosted_payout]
	)
	# Siege Materials: authored 50% refund of the farm's build cost on death
	# while the technology is owned and a marketplace stands.
	var resources_before: int = sim.resources_for_team(SimScript.PLAYER_TEAM)
	sim.queue_structure_upgrade(SimScript.PLAYER_TEAM, marketplace, "Upgrade_MarketplaceUpgradeDefiance")
	sim.advance(600)
	resources_before = sim.resources_for_team(SimScript.PLAYER_TEAM)
	sim._apply_structure_death_refund(sim.structures[farm])
	var refund: int = sim.resources_for_team(SimScript.PLAYER_TEAM) - resources_before
	_check(
		"defiance_refunds_authored_percent_of_build_cost",
		refund == 150,
		"refund=%d" % refund
	)
	_check(
		"defiance_refund_event_is_recorded",
		_has_event(sim.events, "economy.refund", farm),
		str(sim.events.slice(maxi(0, sim.events.size() - 6)))
	)
	# No marketplace standing: the same refund must not apply.
	var sim_two := _new_sim(_fixture_rules())
	var farm_two: int = sim_two.producer_id(SimScript.PLAYER_TEAM, "farm")
	var marketplace_two: int = sim_two.producer_id(SimScript.PLAYER_TEAM, "marketplace")
	(sim_two.team_upgrades[SimScript.PLAYER_TEAM] as Dictionary)["Upgrade_MarketplaceUpgradeDefiance"] = true
	(sim_two.structures[marketplace_two] as Dictionary)["health"] = 0
	resources_before = sim_two.resources_for_team(SimScript.PLAYER_TEAM)
	sim_two._apply_structure_death_refund(sim_two.structures[farm_two])
	_check(
		"defiance_requires_a_standing_marketplace",
		sim_two.resources_for_team(SimScript.PLAYER_TEAM) == resources_before,
		"refund applied without a marketplace"
	)
	# Iron Ore: authored -25% on forged blades / heavy armor battalion
	# purchases while the technology is owned and a marketplace stands.
	var sim_three := _new_sim(_fixture_rules())
	var marketplace_three: int = sim_three.producer_id(SimScript.PLAYER_TEAM, "marketplace")
	(sim_three.team_upgrades[SimScript.PLAYER_TEAM] as Dictionary)["Upgrade_TechnologyGondorForgedBlades"] = true
	(sim_three.team_upgrades[SimScript.PLAYER_TEAM] as Dictionary)["Upgrade_MarketplaceUpgradeIronOre"] = true
	var fighter_commands: Array = sim_three.battalion_upgrade_commands(3)
	var blades_cost := -1
	var armor_cost := -1
	for command in fighter_commands:
		if String(command.get("upgrade_id", "")) == "Upgrade_GondorForgedBlades":
			blades_cost = int(command.get("cost", -1))
		if String(command.get("upgrade_id", "")) == "Upgrade_GondorHeavyArmor":
			armor_cost = int(command.get("cost", -1))
	_check(
		"iron_ore_discounts_blades_and_armor_purchases",
		blades_cost == 225 and armor_cost == 225,
		"blades=%d armor=%d" % [blades_cost, armor_cost]
	)
	(sim_three.structures[marketplace_three] as Dictionary)["health"] = 0
	fighter_commands = sim_three.battalion_upgrade_commands(3)
	blades_cost = -1
	for command in fighter_commands:
		if String(command.get("upgrade_id", "")) == "Upgrade_GondorForgedBlades":
			blades_cost = int(command.get("cost", -1))
	_check(
		"iron_ore_discount_needs_a_standing_marketplace",
		blades_cost == 300,
		"blades=%d" % blades_cost
	)


func _payout_amount_for(sim: Object, farm_id: int) -> int:
	var event_count: int = sim.events.size()
	sim.advance(50 - (sim.tick_index % 50))
	for index in range(event_count, sim.events.size()):
		var event: Dictionary = sim.events[index]
		if String(event.get("kind", "")) == "economy.payout" and int(event.get("entity_id", -1)) == farm_id:
			return int(event.get("amount", -1))
	return -1


## --- Per-battalion purchase surface (fixture) -------------------------------

func _test_battalion_eligibility_matrix() -> void:
	var sim := _new_sim(_fixture_rules())
	_check("matrix_sim_has_no_configuration_error", sim.configuration_error == "", sim.configuration_error)
	var archer_ids: Array = _surface_ids(sim.battalion_upgrade_commands(1))
	_check(
		"archer_surface_is_authored_fire_arrows_armor_training",
		archer_ids == ["Upgrade_GondorArcherFireArrows", "Upgrade_GondorHeavyArmor", "Upgrade_GondorBasicTraining"],
		str(archer_ids)
	)
	var ranger_ids: Array = _surface_ids(sim.battalion_upgrade_commands(101))
	_check(
		"ranger_surface_has_fire_arrows_but_no_heavy_armor",
		ranger_ids == ["Upgrade_GondorFireArrows", "Upgrade_GondorBasicTraining"],
		str(ranger_ids)
	)
	var fighter_ids: Array = _surface_ids(sim.battalion_upgrade_commands(3))
	_check(
		"swordsman_surface_is_authored_blades_armor_training",
		fighter_ids == ["Upgrade_GondorForgedBlades", "Upgrade_GondorHeavyArmor", "Upgrade_GondorBasicTraining"],
		str(fighter_ids)
	)
	_check(
		"no_melee_unit_surface_offers_fire_arrows",
		not fighter_ids.has("Upgrade_GondorFireArrows") and not fighter_ids.has("Upgrade_GondorArcherFireArrows"),
		str(fighter_ids)
	)
	var ranger_armor: Dictionary = sim.queue_battalion_upgrade(
		SimScript.ENEMY_TEAM, 101, "Upgrade_GondorHeavyArmor"
	)
	_check(
		"ranger_heavy_armor_is_rejected_as_unauthored",
		not bool(ranger_armor.get("ok", true)) and String(ranger_armor.get("reason", "")) == "unsupported-upgrade",
		str(ranger_armor)
	)
	var fighter_arrows: Dictionary = sim.queue_battalion_upgrade(
		SimScript.PLAYER_TEAM, 3, "Upgrade_GondorFireArrows"
	)
	_check(
		"swordsman_fire_arrows_is_rejected_as_unauthored",
		not bool(fighter_arrows.get("ok", true)) and String(fighter_arrows.get("reason", "")) == "unsupported-upgrade",
		str(fighter_arrows)
	)
	var gated: Dictionary = sim.queue_battalion_upgrade(
		SimScript.PLAYER_TEAM, 1, "Upgrade_GondorArcherFireArrows"
	)
	_check(
		"purchase_is_gated_until_the_technology_is_researched",
		not bool(gated.get("ok", true))
			and String(gated.get("reason", "")) == "missing-upgrade"
			and String(gated.get("required_upgrade", "")) == "Upgrade_TechnologyGondorFireArrows",
		str(gated)
	)
	# Eligibility is compiled evidence: a purchase row without a compiled
	# weapon/armor/level effect fails the whole setup closed.
	var broken_rules := _fixture_rules()
	var broken_doc := _fighter_doc()
	((broken_doc["registration"] as Dictionary)["simulation"] as Dictionary)["resolved"] = {
		"armor": {"setId": "FixtureArmor", "table": {"default": {"percent": 100.0}, "scalars": {}, "damageScalar": {"percent": 100.0}}, "upgrades": [], "excludedUpgradeSets": []},
		"combat": {"damageType": "SLASH", "upgrades": []},
	}
	((broken_doc["registration"] as Dictionary)["gameplay"] as Dictionary).erase("levelUpgrades")
	(broken_rules["playable_unit_runtimes"] as Dictionary)["GondorFighterHorde"] = broken_doc
	var broken_sim := _new_sim(broken_rules)
	_check(
		"purchase_without_compiled_effect_fails_closed",
		broken_sim.configuration_error.contains("Upgrade_GondorForgedBlades"),
		broken_sim.configuration_error
	)


func _surface_ids(commands: Array) -> Array:
	var ids: Array = []
	for command in commands:
		ids.append(String(command.get("upgrade_id", "")))
	return ids


func _test_battalion_purchase_flow() -> void:
	var sim := _new_sim(_fixture_rules())
	_check("purchase_sim_has_no_configuration_error", sim.configuration_error == "", sim.configuration_error)
	var archery: int = sim.producer_id(SimScript.PLAYER_TEAM, "archery_range")
	# Retail gate: fire arrows technology needs a level-three archery range.
	sim.queue_structure_upgrade(SimScript.PLAYER_TEAM, archery, "Upgrade_GondorArcheryRangeLevel2")
	sim.advance(300)
	sim.queue_structure_upgrade(SimScript.PLAYER_TEAM, archery, "Upgrade_GondorArcheryRangeLevel3")
	sim.advance(300)
	sim.queue_structure_upgrade(SimScript.PLAYER_TEAM, archery, "Upgrade_TechnologyGondorFireArrows")
	sim.advance(300)
	_check(
		"fire_arrows_technology_researched_at_level_three_range",
		(sim.team_upgrades.get(SimScript.PLAYER_TEAM, {}) as Dictionary).has("Upgrade_TechnologyGondorFireArrows"),
		str(sim.team_upgrades)
	)
	var resources_before: int = sim.resources_for_team(SimScript.PLAYER_TEAM)
	var purchased: Dictionary = sim.queue_battalion_upgrade(
		SimScript.PLAYER_TEAM, 1, "Upgrade_GondorArcherFireArrows"
	)
	_check(
		"archer_fire_arrows_purchase_charges_authored_cost",
		bool(purchased.get("ok", false))
			and int((purchased.get("item", {}) as Dictionary).get("cost", -1)) == 200
			and sim.resources_for_team(SimScript.PLAYER_TEAM) == resources_before - 200,
		str(purchased)
	)
	sim.advance(99)
	_check(
		"purchase_does_not_apply_early",
		not (sim.entity(1).get("applied_upgrades", {}) as Dictionary).has("Upgrade_GondorArcherFireArrows"),
		str(sim.entity(1).get("applied_upgrades", {}))
	)
	sim.advance(1)
	var first: Dictionary = sim.entity(1)
	var second: Dictionary = sim.entity(2)
	_check(
		"purchase_applies_to_one_battalion_only",
		(first.get("applied_upgrades", {}) as Dictionary).has("Upgrade_GondorArcherFireArrows")
			and not (second.get("applied_upgrades", {}) as Dictionary).has("Upgrade_GondorArcherFireArrows"),
		"first=%s second=%s" % [str(first.get("applied_upgrades", {})), str(second.get("applied_upgrades", {}))]
	)
	var effect: Dictionary = sim._applied_weapon_effect(first)
	_check(
		"applied_effect_is_the_compiled_warhead_upgrade",
		String(effect.get("kind", "")) == "warhead-upgrade",
		str(effect)
	)
	var repurchased: Dictionary = sim.queue_battalion_upgrade(
		SimScript.PLAYER_TEAM, 1, "Upgrade_GondorArcherFireArrows"
	)
	_check(
		"applied_upgrade_cannot_be_repurchased",
		not bool(repurchased.get("ok", true)) and String(repurchased.get("reason", "")) == "already-completed",
		str(repurchased)
	)
	# Basic Training: research at the barracks, purchase on the swordsman,
	# authored LevelUpUpgrade grants the level; the banner-carrier member
	# visual stays a recorded unsupported effect.
	var barracks: int = sim.producer_id(SimScript.PLAYER_TEAM, "barracks")
	sim.queue_structure_upgrade(SimScript.PLAYER_TEAM, barracks, "Upgrade_TechnologyGondorBasicTraining")
	sim.advance(150)
	var level_before := int(sim.entity(3).get("level", 1))
	var trained: Dictionary = sim.queue_battalion_upgrade(
		SimScript.PLAYER_TEAM, 3, "Upgrade_GondorBasicTraining"
	)
	_check("basic_training_purchase_queues", bool(trained.get("ok", false)), str(trained))
	sim.advance(100)
	var fighter: Dictionary = sim.entity(3)
	_check(
		"basic_training_applies_authored_level_up",
		int(fighter.get("level", 1)) == mini(2, level_before + 1)
			and (fighter.get("applied_upgrades", {}) as Dictionary).has("Upgrade_GondorBasicTraining"),
		str({"level": fighter.get("level", -1), "upgrades": fighter.get("applied_upgrades", {})})
	)
	var banner_event := {}
	for index in range(sim.events.size() - 1, -1, -1):
		var event: Dictionary = sim.events[index]
		if String(event.get("kind", "")) == "battalion.upgrade_applied" and int(event.get("target_id", -1)) == 3:
			banner_event = event
			break
	_check(
		"banner_carrier_member_visual_is_a_recorded_unsupported_effect",
		Array(banner_event.get("unsupported_effects", [])).has("banner-carrier-member-spawn"),
		str(banner_event)
	)


func _test_legacy_forge_fallback() -> void:
	var sim := _new_sim(_fixture_rules(true, false))
	_check("legacy_sim_has_no_configuration_error", sim.configuration_error == "", sim.configuration_error)
	var forge: int = sim.producer_id(SimScript.PLAYER_TEAM, "forge")
	_check(
		"stale_pack_keeps_the_provisional_forge_contracts",
		sim._structure_upgrade_contracts.has("Upgrade_GondorForgedBlades")
			and sim._structure_upgrade_contracts.has("Upgrade_GondorFireArrows")
			and sim._structure_upgrade_contracts.has("Upgrade_GondorHeavyArmor"),
		str(sim._structure_upgrade_contracts.keys())
	)
	var commands: Array = sim.structure_upgrade_commands(forge)
	var research_rows := 0
	for command in commands:
		if bool(command.get("research", false)):
			research_rows += 1
	_check(
		"provisional_contracts_stay_off_the_doc_command_surface",
		research_rows == 0,
		str(commands)
	)
	var queued: Dictionary = sim.queue_structure_upgrade(
		SimScript.PLAYER_TEAM, forge, "Upgrade_GondorForgedBlades"
	)
	_check("provisional_research_queues", bool(queued.get("ok", false)), str(queued))
	sim.advance(300)
	_check(
		"provisional_completion_keeps_the_recorded_auto_equip",
		(sim.entity(3).get("applied_upgrades", {}) as Dictionary).has("Upgrade_GondorForgedBlades"),
		str(sim.entity(3).get("applied_upgrades", {}))
	)


## --- Real-pack all-faction level-swap audit ---------------------------------
##
## Every buildable structure in every mounted faction pack: the staged
## sub-object swap must resolve against the actual GLB node names, and no
## building may ever go invisible at any level (the vanish regression lock:
## authored per-level health additions must not trip the presenter contract).
## Buildings whose authored tokens cannot resolve ride this recorded gap
## table — a new gap fails the gate, a healed gap fails it too.

const LEVEL_SWAP_GAP_REASONS := {
	"GondorWorkshop": "bounded-workshop model-state evidence path binds no upgrade-chain levels",
}
const LEVEL_SWAP_UNMATCHED_TOKENS := {
	"GondorArcherRange": ["V1_PIECE*", "V2_PIECE*"],
	"GondorBarracks": ["V1FLAG", "V1_PIECE*", "V2A", "V2_PIECE*"],
	"GondorForge": ["V1_PIECE*", "V2FLAG", "V2_PIECE*"],
	"GondorStable": ["V1_PIECE*", "V2_PIECE*"],
	"IsengardArmory": ["DrawFloor_Bib", "DrawFloor_V1"],
	"IsengardUrukPit": ["DrawFloor_Bib", "DrawFloor_V1"],
	"IsengardWargPit": ["DrawFloor_Bib", "DrawFloor_V1"],
	"MordorHaradrimPalace": ["Banner_Harad02", "Banner_Harad03", "Banner_Harad04", "DrawFloorV1", "DrawFloorV2", "V1A", "V1_PIECE*", "V2B", "V2_PIECE*"],
	"MordorMumakilPen": ["BANNER01", "BANNER02", "BANNER03", "DrawFloorV1", "DrawFloorV2", "V1_PIECE*", "V2_PIECE*"],
	"MordorOrcPit": ["DrawFloorV1", "DrawFloorV2", "V1", "V1SPIKES", "V1_PIECE*", "V2_PIECE*", "bib"],
	"MordorSiegeWorks": ["Bib", "DrawFloorV1", "DrawFloorV2", "V1", "V2_Piece*"],
	"MordorTavern": ["V1_PIECE*", "V2", "V2FLAG", "V2_PIECE*"],
	"MordorTrollCage": ["Bib", "DrawFloorV1", "DrawFloorV2", "V1", "V1_PIECE*", "V2_PIECE*"],
}


func _test_real_pack_level_swap_audit() -> void:
	var content_db = root.get_node_or_null("ContentDB")
	if content_db == null:
		_fail("ContentDB autoload is missing")
		return
	var runtimes: Dictionary = content_db.get_playable_structure_runtimes()
	_check("audit_loads_all_faction_structure_runtimes", runtimes.size() >= 120, str(runtimes.size()))
	var roots: Array = content_db.pack_roots.duplicate()
	var ids: Array[String] = []
	for key in runtimes.keys():
		ids.append(String(key))
	ids.sort()
	var chained := 0
	var configure_errors: Array[String] = []
	var vanished: Array[String] = []
	var gap_mismatches: Array[String] = []
	var unmatched_mismatches: Array[String] = []
	for object_id in ids:
		var report := _audit_structure(content_db, object_id, roots)
		if String(report.get("skip", "")) != "":
			continue
		if String(report.get("contract_error", "")) != "":
			configure_errors.append("%s:%s" % [object_id, String(report.get("contract_error", ""))])
			continue
		if bool(report.get("chained", false)):
			chained += 1
		vanished.append_array(report.get("vanished", []))
		gap_mismatches.append_array(report.get("gap_mismatches", []))
		unmatched_mismatches.append_array(report.get("unmatched_mismatches", []))
	_check("every_structure_configures_without_contract_error", configure_errors.is_empty(), str(configure_errors))
	_check("audit_finds_compiled_chains", chained >= 17, str(chained))
	_check("no_structure_vanishes_at_any_level", vanished.is_empty(), str(vanished))
	_check("level_swap_gaps_match_the_recorded_table", gap_mismatches.is_empty(), str(gap_mismatches))
	_check("unmatched_tokens_match_the_recorded_table", unmatched_mismatches.is_empty(), str(unmatched_mismatches))
	_audit_farm_staged_presentation(content_db, roots)


func _audit_structure(content_db: Object, object_id: String, roots: Array) -> Dictionary:
	var report := {"vanished": [], "gap_mismatches": [], "unmatched_mismatches": [], "chained": false, "contract_error": "", "skip": ""}
	var document: Dictionary = content_db.get_playable_structure_runtime(object_id)
	var registration: Dictionary = document.get("registration", {}) as Dictionary
	var lifecycle: Dictionary = (registration.get("presentation", {}) as Dictionary).get("buildingLifecycle", {}) as Dictionary
	if lifecycle.is_empty():
		# Deferred/bounded composites (the workshop evidence path) present
		# through their own contract; they are out of the swap audit's scope.
		report["skip"] = "no-building-lifecycle"
		return report
	var maximum := int((lifecycle.get("simulationFacts", {}) as Dictionary).get("maximumHealth", 0))
	if maximum <= 0:
		maximum = int(lifecycle.get("maxHealth", 0))
	var gameplay: Dictionary = registration.get("gameplay", {}) as Dictionary
	var chain: Dictionary = gameplay.get("upgradeChain", {}) as Dictionary
	var presentation: Dictionary = gameplay.get("structureLevelPresentation", {}) as Dictionary
	var has_staged := not chain.is_empty() or not presentation.is_empty()
	report["chained"] = not chain.is_empty()
	var bundle_id: String = UnitAdapterScript._runtime_id(object_id)
	var node: Node3D = StructureScript.new()
	root.add_child(node)
	node.set_allowed_pack_roots(roots)
	var entity := {
		"id": 1, "team": 1, "structure_kind": String(document.get("slug", "structure")),
		"maximum_health": maximum, "health": maximum,
		"construction_progress": 1.0, "level": 1, "upgrade_queue": [],
	}
	node.configure(entity, bundle_id, 0.0)
	if node.contract_error != "":
		report["contract_error"] = node.contract_error
		node.queue_free()
		return report
	var health_adds := {}
	for step_value in Array(chain.get("steps", [])):
		var step := step_value as Dictionary
		var add := 0
		for leaf in Array(step.get("effects", [])):
			for modifier in Array((leaf as Dictionary).get("modifiers", [])):
				if String((modifier as Dictionary).get("kind", "")) == "HEALTH":
					add += roundi(float((modifier as Dictionary).get("value", 0.0)))
		if add != 0:
			health_adds[int(step.get("toLevel", 0))] = add
	var cumulative := 0
	var unmatched_union: Array[String] = []
	var min_applied := 1 << 30
	for lvl in [1, 2, 3]:
		cumulative += int(health_adds.get(lvl, 0))
		var sync_entity := entity.duplicate(true)
		sync_entity["level"] = lvl
		sync_entity["maximum_health"] = maximum + cumulative
		sync_entity["health"] = maximum + cumulative
		node.sync_state(sync_entity)
		var state: Dictionary = node.level_state()
		for token in Array(state.get("unmatchedTokens", [])):
			if not unmatched_union.has(String(token)):
				unmatched_union.append(String(token))
		if has_staged:
			min_applied = mini(min_applied, (state.get("appliedNodes", {}) as Dictionary).size())
		var visible_names: Array[String] = []
		_count_visible_meshes(node, visible_names)
		if visible_names.is_empty() or not node.get_node("StructureVisual").visible or node.contract_error != "":
			(report["vanished"] as Array).append("%s@L%d(err=%s)" % [object_id, lvl, node.contract_error])
	if has_staged:
		if LEVEL_SWAP_GAP_REASONS.has(object_id):
			if min_applied > 0:
				(report["gap_mismatches"] as Array).append("%s: recorded gap healed (applied=%d) — update the gate" % [object_id, min_applied])
		elif min_applied < 1:
			(report["gap_mismatches"] as Array).append("%s: staged swap applies no node at some level" % object_id)
	unmatched_union.sort()
	var expected_unmatched: Array = Array(LEVEL_SWAP_UNMATCHED_TOKENS.get(object_id, []))
	var expected_sorted: Array[String] = []
	for token in expected_unmatched:
		expected_sorted.append(String(token))
	expected_sorted.sort()
	if unmatched_union != expected_sorted:
		(report["unmatched_mismatches"] as Array).append("%s: unmatched=%s expected=%s" % [object_id, str(unmatched_union), str(expected_sorted)])
	# The vanish regression lock in both directions: an authored pool passes,
	# a non-authored pool still fails closed. The bounded-workshop evidence
	# path owns its health contract separately and is out of this check.
	if String(node.presentation_mode) != "bounded-workshop-model-state-evidence":
		var bogus := entity.duplicate(true)
		bogus["maximum_health"] = maximum + 999
		bogus["health"] = maximum
		bogus["level"] = 1
		var node_two: Node3D = StructureScript.new()
		root.add_child(node_two)
		node_two.set_allowed_pack_roots(roots)
		node_two.configure(bogus, bundle_id, 0.0)
		if node_two.contract_error == "":
			(report["vanished"] as Array).append("%s: non-authored health pool did not fail closed" % object_id)
		node_two.queue_free()
	node.queue_free()
	return report


func _count_visible_meshes(n: Node, out: Array[String]) -> void:
	var mesh := n as MeshInstance3D
	if mesh != null and mesh.mesh != null and mesh.is_visible_in_tree():
		if not String(mesh.name) in ["SelectionRing", "BuildBack", "BuildFill", "HealthBack", "HealthFill", "BIB"]:
			out.append(String(mesh.name))
	for child in n.get_children():
		_count_visible_meshes(child, out)


func _audit_farm_staged_presentation(content_db: Object, roots: Array) -> void:
	## The pinned men pack's farm doc predates the converter's
	## structureLevelPresentation block (the FarmInterface inheritance the
	## converter now compiles; importer tests pin the emitted shape). The live
	## doc must record the gap today; injecting exactly the compiled block must
	## stage the real farm GLB's V1/V1HIDE/V2/V2HIDE nodes — the republish
	## simulation, never an invisible or all-variants farm.
	var document: Dictionary = content_db.get_playable_structure_runtime("GondorFarm")
	var gameplay: Dictionary = (document.get("registration", {}) as Dictionary).get("gameplay", {}) as Dictionary
	_check(
		"pinned_farm_doc_records_no_staged_presentation_yet",
		not gameplay.has("upgradeChain") and not gameplay.has("structureLevelPresentation"),
		str(gameplay.keys())
	)
	var lifecycle: Dictionary = ((document.get("registration", {}) as Dictionary).get("presentation", {}) as Dictionary).get("buildingLifecycle", {}) as Dictionary
	var maximum := int((lifecycle.get("simulationFacts", {}) as Dictionary).get("maximumHealth", 0))
	var bundle_id: String = UnitAdapterScript._runtime_id("GondorFarm")
	var staged_doc := document.duplicate(true)
	((staged_doc["registration"] as Dictionary)["gameplay"] as Dictionary)["structureLevelPresentation"] = {
		"levels": {
			"1": {"visibleSubObjects": ["V1HIDE", "V2HIDE"], "hiddenSubObjects": ["V1", "V1_PIECE*", "V2", "V2_PIECE*"]},
			"2": {"visibleSubObjects": ["V1", "V1_PIECE*", "V2HIDE"], "hiddenSubObjects": ["V1HIDE", "V2", "V2_PIECE*"]},
			"3": {"visibleSubObjects": ["V1", "V2"], "hiddenSubObjects": ["V1_PIECE*", "V1HIDE", "V2_PIECE*", "V2HIDE"]},
		},
		"triggerUpgrades": ["Upgrade_StructureLevel1", "Upgrade_StructureLevel2", "Upgrade_StructureLevel3"],
		"sourceIni": ["data/ini/object/goodfaction/structures/farminterface.ini"],
	}
	var registry: Dictionary = content_db.playable_structure_runtimes
	registry["GondorFarm"] = staged_doc
	var entity := {
		"id": 1, "team": 1, "structure_kind": "farm",
		"maximum_health": maximum, "health": maximum,
		"construction_progress": 1.0, "level": 1, "upgrade_queue": [],
	}
	var node: Node3D = StructureScript.new()
	root.add_child(node)
	node.set_allowed_pack_roots(roots)
	node.configure(entity, bundle_id, 0.0)
	var ok: bool = node.contract_error == ""
	var level_expectations := {
		1: {"shown": ["V1HIDE", "V2HIDE"], "hidden": ["V1", "V2"]},
		2: {"shown": ["V1", "V2HIDE"], "hidden": ["V1HIDE", "V2"]},
		3: {"shown": ["V1", "V2"], "hidden": ["V1HIDE", "V2HIDE"]},
	}
	for lvl in [1, 2, 3]:
		node.set_level(lvl)
		var state: Dictionary = node.level_state()
		# The W3D V*_PIECE sub-objects survive conversion as empty pivots, the
		# same recorded-benign unmatched set the all-faction audit table holds.
		var unmatched: Array = Array(state.get("unmatchedTokens", []))
		unmatched.sort()
		if unmatched != ["V1_PIECE*", "V2_PIECE*"]:
			ok = false
			print("farm staged L%d unmatched: %s" % [lvl, str(state.get("unmatchedTokens", []))])
		var applied: Dictionary = state.get("appliedNodes", {})
		for token in level_expectations[lvl]["shown"]:
			if not bool(applied.get(token, false)):
				ok = false
				print("farm staged L%d missing shown %s (applied=%s)" % [lvl, token, str(applied)])
		for token in level_expectations[lvl]["hidden"]:
			if not applied.has(token) or bool(applied.get(token, true)):
				ok = false
				print("farm staged L%d missing hidden %s (applied=%s)" % [lvl, token, str(applied)])
		var visible_names: Array[String] = []
		_count_visible_meshes(node, visible_names)
		if visible_names.is_empty():
			ok = false
			print("farm staged L%d vanished" % lvl)
	node.queue_free()
	_check("compiled_farm_presentation_stages_real_glb_nodes", ok, "")


func _has_event(events: Array, kind: String, entity_id: int) -> bool:
	for value in events:
		if typeof(value) != TYPE_DICTIONARY:
			continue
		var event := value as Dictionary
		if String(event.get("kind", "")) == kind and int(event.get("entity_id", -1)) == entity_id:
			return true
	return false


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("RETAIL_UPGRADE_PARITY PASS %s" % name)
	else:
		_fail(name, detail)


func _fail(name: String, detail: String = "") -> void:
	failed += 1
	printerr("RETAIL_UPGRADE_PARITY FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])


func _finish() -> void:
	print("RETAIL_UPGRADE_PARITY_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)
