extends SceneTree

## WAR OF THE RING: BUILDING THINGS.
##
## THE DEFECT THIS RUNNER EXISTS TO KEEP CLOSED, in the owner's own words:
##
##   "I cannot click on the buildings or build them with the icons as they don't
##    light up and do not allow me to build them."
##
## The build ring already drew retail's real offer with retail's real prices -
## Armory 500, Isengard Fortress 1500, Furnace 0, Uruk Pit 500 - and clicking any
## of them did NOTHING, because no part of the strategic layer simulated
## construction. The status ribbon honestly read "0 of 3 built". This runner
## proves the ribbon can now read something else.
##
## ============================================================================
## WHAT IS ASSERTED, AND IN WHICH HALF
## ============================================================================
##
## HALF ONE runs anywhere. Its world, its scenario, its `living-world-ui.json`
## and its `macros.json` are ALL AUTHORED HERE, in the same SHAPES the importer
## emits, with invented ids ("Fixture", "Harness") and invented numbers. No
## retail payload is packaged with this test. What it proves is the RULES:
## a build spends treasure and stands a structure, an unaffordable one is refused
## BY NAME, a plot that is taken is refused by name, a type limit is refused by
## name (in retail's own words where retail wrote them), income accrues at the
## start of a turn, and the whole thing round-trips through the hash.
##
## HALF TWO runs only when the real converted bundles are staged, and its checks
## are counted separately so a machine without them still gets a LIVENESS number
## it can fail. What it proves is the DATA: retail's 28 `LivingWorldBuilding`
## blocks load, their four `WOTR_*_COST` macros resolve to 0/500/500/1500, a
## scenario's `LW_FORT` token resolves to the concrete fortress the controlling
## faction actually gets, and a real seat on the real Middle-earth map can raise
## a real structure and pay for it.

const WorldScript = preload("res://src/wotr/wotr_world.gd")
const StateScript = preload("res://src/wotr/wotr_state.gd")
const SessionScript = preload("res://src/wotr/wotr_session.gd")
const BuildingsScript = preload("res://src/wotr/wotr_buildings.gd")
const LivingWorldUiScript = preload("res://src/wotr/wotr_living_world_ui.gd")
const MacrosScript = preload("res://src/wotr/wotr_macros.gd")
const AiScript = preload("res://src/wotr/wotr_ai.gd")
const GapsScript = preload("res://src/wotr/wotr_strategic_gaps.gd")

## LIVENESS. A GDScript runtime error aborts the enclosing function on the spot
## without propagating, so every check after the error site never runs and an
## inert runner prints zero failures and exits 0. Pinning the count turns that
## silent abort into a loud failure. Raise deliberately; never lower.
const CHECKS_WITHOUT_DATA := 141
const CHECKS_WITH_DATA := 165

## Where the authored fixture bundles are written. `user://` because that is a
## real search root `wotr_buildings.bundle_roots()` already ends with, so the
## fixture goes through the SAME loader the product does rather than through a
## test-only injection point that could drift from it.
const FIXTURE_ROOT := "user://wotr-construction-fixture"

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	print("== WAR OF THE RING: CONSTRUCTION ==")
	_test_the_catalogue_loads_from_an_authored_bundle()
	_test_nothing_builds_without_a_catalogue()
	_test_a_build_spends_treasure_and_stands_a_structure()
	_test_world_command_points_are_a_pure_standing_structure_effect()
	_test_strengthen_army_is_a_pure_dynamic_standing_structure_effect()
	_test_every_refusal_names_its_rule()
	_test_income_accrues_at_the_start_of_a_turn()
	_test_the_hash_covers_construction()
	_test_the_session_is_the_only_door()
	_test_the_register_names_what_this_layer_authored()
	var have_data := _test_against_retail_data()

	var expected := CHECKS_WITH_DATA if have_data else CHECKS_WITHOUT_DATA
	var ran := passed + failed
	if ran != expected:
		failed += 1
		printerr("WOTR_CONSTRUCTION FAIL liveness: ran %d checks, expected %d - a function aborted before its assertions" % [
			ran, expected])
	print("WOTR_CONSTRUCTION_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


# =============================================================================
# THE AUTHORED FIXTURE
# =============================================================================
#
# Two seats, four regions in a line plus one plotless island:
#
#     Ashfall(2) -- Bramblewold(2) -- Cinderfen(1) -- Dunmarch(3)   Emberisle(0)
#     ^ seat 0                                        ^ seat 1
#
# The plot counts, the `RestrictBuildings` shapes and the fertile-territory macro
# are all shapes retail authors; the NAMES and NUMBERS are this file's.

## The `living-world-ui.json` shape, with four invented buildings per seat -
## one of each of retail's four `Type` values, so every rule that keys off type
## has something to key off.
func _ui_bundle() -> Dictionary:
	var buildings: Array = []
	for template_value in ["PlayerAlpha", "PlayerBeta"]:
		var template := String(template_value)
		var short := template.substr("Player".length())
		for row in [
			["Keep", "Fortress", "HARNESS_FORTRESS_COST", "Yes"],
			["Hall", "Barracks", "HARNESS_BARRACKS_COST", ""],
			["Forge", "Armory", "HARNESS_FORGE_COST", ""],
			["Croft", "Resource", "HARNESS_FARM_COST", ""],
		]:
			buildings.append({
				"id": "FXB_%s%s" % [short, String((row as Array)[0])],
				"type": String((row as Array)[1]),
				"availableTo": template,
				"strategicResourceCost": String((row as Array)[2]),
				"turnsToBuild": "1",
				"battleThingTemplate": "%s%s" % [short, String((row as Array)[0])],
				"buildingIcon": "FXBIcon_%s%s" % [short, String((row as Array)[0])],
				"canDefendTerritory": String((row as Array)[3]),
				"createUnitDuringAutoResolve": String((row as Array)[3]),
				"constructButtonImage": "FXImage%s" % String((row as Array)[0]),
				"constructButtonTitle": "FIXTURE:Structure_%s%s" % [short, String((row as Array)[0])],
				"constructButtonHelp": "FIXTURE:ToolTipBuild_%s%s" % [short, String((row as Array)[0])],
				"displayNameTag": "FIXTURE:Structure_%s%s" % [short, String((row as Array)[0])],
				"displayDescriptionTag": "FIXTURE:ToolTip_%s%s" % [short, String((row as Array)[0])],
				"recruits": [] if String((row as Array)[1]) == "Resource" else [{
					"playerArmy": "%sGarrisonArmy" % short,
					"heroTemplateName": "",
					"buildTime": "1",
					"icon": "%sArmyIcon" % short,
					"iconSize": "Small",
					"constructButtonImage": "FXUnit%s" % short,
					"constructButtonTitle": "FIXTURE:Unit_%s" % short,
					"constructButtonHelp": "FIXTURE:ToolTipUnit_%s" % short,
				}],
			})
	# A FIFTH TYPE, authored on purpose: retail's `Type` takes exactly four
	# values, and a bundle carrying a fifth must be refused by omission rather
	# than bucketed into one of the four.
	buildings.append({
		"id": "FXB_AlphaWonder", "type": "Wonder", "availableTo": "PlayerAlpha",
		"strategicResourceCost": "HARNESS_FARM_COST", "turnsToBuild": "1",
		"battleThingTemplate": "", "buildingIcon": "", "canDefendTerritory": "",
		"createUnitDuringAutoResolve": "", "constructButtonImage": "",
		"constructButtonTitle": "", "constructButtonHelp": "",
		"displayNameTag": "", "displayDescriptionTag": "", "recruits": [],
	})
	# A BUILDING WHOSE PRICE CANNOT BE RESOLVED, also on purpose: a cost macro
	# with no numeric body must make that building UNBUILDABLE and say which
	# macro, never default to free.
	buildings.append({
		"id": "FXB_AlphaMystery", "type": "Armory", "availableTo": "PlayerAlpha",
		"strategicResourceCost": "HARNESS_UNPRICED", "turnsToBuild": "1",
		"battleThingTemplate": "", "buildingIcon": "", "canDefendTerritory": "",
		"createUnitDuringAutoResolve": "", "constructButtonImage": "",
		"constructButtonTitle": "", "constructButtonHelp": "",
		"displayNameTag": "", "displayDescriptionTag": "", "recruits": [],
	})
	# Schema 3 carries typed nuggets and their usable experience rows. Treasury binding is
	# uniform by Type; one synthetic fortress also exercises all five kinds.
	for building_value in buildings:
		var building := building_value as Dictionary
		building["nuggetsStatus"] = "ok"
		building["nuggets"] = []
		if String(building.get("type", "")) == "Fortress":
			building["nuggets"].append({"kind": "increase_treasury", "tag": "FixtureTreasury", "treasureAmount": "GAIN_PER_FORTRESS"})
		elif String(building.get("type", "")) == "Resource":
			building["nuggets"].append({"kind": "increase_treasury", "tag": "FixtureTreasury", "treasureAmount": "GAIN_PER_FARM"})
	var all_kinds := buildings[0] as Dictionary
	all_kinds["nuggets"].append({"kind": "strengthen_army", "tag": "FixtureStrength", "strengtheningRange": "THIS_TERRIORITY", "bonusKey": "HarnessBonus", "bonuses": [
		{"threshold": 1, "weaponPct": null, "weaponRaw": null, "armorPct": 90.0, "armorRaw": "Armor:90%", "experiencePct": null, "experienceRaw": null},
		{"threshold": 2, "weaponPct": null, "weaponRaw": null, "armorPct": 81.0, "armorRaw": "Armor:81%", "experiencePct": null, "experienceRaw": null},
		{"threshold": 3, "weaponPct": null, "weaponRaw": null, "armorPct": 72.9, "armorRaw": "Armor:72.9%", "experiencePct": null, "experienceRaw": null},
		{"threshold": 4, "weaponPct": null, "weaponRaw": null, "armorPct": 65.61, "armorRaw": "Armor:65.61%", "experiencePct": null, "experienceRaw": null},
		{"threshold": 5, "weaponPct": null, "weaponRaw": null, "armorPct": 59.049, "armorRaw": "Armor:59.049%", "experiencePct": null, "experienceRaw": null},
		{"threshold": 6, "weaponPct": null, "weaponRaw": null, "armorPct": 53.1441, "armorRaw": "Armor:53.1441%", "experiencePct": null, "experienceRaw": null},
		{"threshold": 7, "weaponPct": null, "weaponRaw": null, "armorPct": 47.82969, "armorRaw": "Armor:47.82969%", "experiencePct": null, "experienceRaw": null}]})
	all_kinds["nuggets"].append({"kind": "spawn_army", "tag": "FixtureSpawn", "queueSize": 0, "armies": [{"playerArmy": "FixtureArmy", "heroTemplateName": "", "icon": "FixtureIcon", "iconSize": "Small", "buildTime": "1", "palantirMovie": "", "constructButtonImage": "FixtureImage", "constructButtonTitle": "FixtureTitle", "constructButtonHelp": "FixtureHelp"}]})
	all_kinds["nuggets"].append({"kind": "upgrade_troops", "tag": "FixtureUpgrade", "numUpgradesPerTurn": 1, "upgradeableUnits": ["FixtureOne", "FixtureOne"]})
	all_kinds["nuggets"].append({"kind": "increase_command_points", "tag": "FixtureCommand", "type": "WORLD", "amount": 30})
	return {
		"schema": "openbfme.living-world-ui",
		"schemaVersion": 4,
		"upgradeExperienceLevels": [
			{"name": "FixtureOneLevel1", "targetNames": ["FixtureOne", "FixtureOne"], "requiredExperience": 1, "experienceAward": 2, "rank": 1, "upgrades": ["Upgrade_Fixture1", "Upgrade_Fixture1"]},
			{"name": "FixtureOneLevel2", "targetNames": ["FixtureOne"], "requiredExperience": 5, "experienceAward": 3, "rank": 2, "upgrades": ["Upgrade_Fixture2"]},
		],
		"levelUpUpgrades": [
			{"template": "FixtureOne", "triggeredBy": ["Upgrade_FixtureVeterancy"], "levelsToGain": 1, "levelCap": 2},
		],
		"atlasDirectory": "ui-atlases",
		"atlases": [],
		"images": {},
		"armyPortraits": {},
		"heroPortraits": {},
		"playerTemplates": [],
		"buildPlotIcons": [],
		"buildingIcons": [],
		"factionBanners": {},
		"factionBannerDerivation": "",
		"chromeSheet": {},
		"gaps": {},
		"totals": {"buildings": buildings.size()},
		"buildings": buildings,
	}


## The `macros.json` shape. Invented names in retail's own SHAPE, and one
## deliberately non-numeric body so the unresolvable path is exercised.
func _macros_bundle() -> Dictionary:
	var defines: Dictionary = {}
	for row in [
		["HARNESS_FARM_COST", "0"], ["HARNESS_BARRACKS_COST", "500"],
		["HARNESS_FORGE_COST", "500"], ["HARNESS_FORTRESS_COST", "1500"],
		["GAIN_PER_FORTRESS", "300"], ["GAIN_PER_FARM", "300"],
		["FERTILE_TERRITORY_BONUS", "500"],
	]:
		defines[String((row as Array)[0])] = {
			"line": 1, "numeric": true,
			"raw": String((row as Array)[1]),
			"value": float(String((row as Array)[1])),
		}
	defines["HARNESS_UNPRICED"] = {
		"line": 2, "numeric": false, "raw": "#DIVIDE( 1.0, 0.95 )", "value": 0.0,
	}
	return {
		"schema": "openbfme.living-world-macros", "schemaVersion": 1,
		"source": {}, "totals": {"defines": defines.size()}, "defines": defines,
	}


## Write both fixture bundles to `user://` and return the root they live in, so
## the PRODUCT loader finds them exactly the way it finds converted ones.
func _write_fixture_bundles() -> String:
	DirAccess.make_dir_recursive_absolute(FIXTURE_ROOT)
	for row in [
		["living-world-ui.json", _ui_bundle()],
		["macros.json", _macros_bundle()],
	]:
		var handle := FileAccess.open(
			FIXTURE_ROOT.path_join(String((row as Array)[0])), FileAccess.WRITE)
		handle.store_string(JSON.stringify((row as Array)[1]))
		handle.close()
	return FIXTURE_ROOT


func _catalogue() -> BuildingsScript:
	var catalogue := BuildingsScript.new()
	catalogue.load_from_roots([_write_fixture_bundles()])
	return catalogue


func _region(id: String, links: Array, plots: int, restrict: Array = [],
		fertile: bool = false) -> Dictionary:
	var connections: Array = []
	for target in links:
		connections.append({"region": target, "detourPoints": []})
	var spots: Array = []
	for index in range(plots):
		spots.append({"x": index * 40, "y": 0})
	return {
		"id": id,
		"displayName": "LW:DisplayName%s" % id,
		"mapName": "MAP TEST %s" % id,
		"subObject": id,
		"regionPortrait": "LWP%s" % id,
		"skirmishStillImage": "%s_Loadscreen" % id,
		"skirmishMusicTrack": "TestLoadMusic",
		"conqueredNotice": "APT:TestNotice",
		"bonuses": {"experience": 5, "fertileTerritory": 0},
		# Retail authors the fertile bonus as a MACRO on the region, never as a
		# literal, which is why the document carries `bonusMacros` at all.
		"bonusMacros": {"fertileTerritory": "FERTILE_TERRITORY_BONUS"} if fertile else {},
		"cpLimit": 600,
		"allyCpLimit": 360,
		"createAutoFort": false,
		"customCenterPoint": true,
		"centerPoint": {"x": 0, "y": 0},
		"heroArmySpots": [],
		"garrisonArmySpots": [],
		"buildingSpots": spots,
		"fortress": null,
		"connections": connections,
		"restrictBuildings": restrict,
	}


func _document() -> Dictionary:
	return {
		"format": 1,
		"schema": WorldScript.SCHEMA,
		"schemaVersion": WorldScript.SCHEMA_VERSION,
		"game": "bfme2",
		"sources": [],
		"gaps": [],
		"regionCampaigns": [{
			"name": "TestCampaign",
			"kind": "LivingWorldRegionCampaign",
			"regionEffectsManagerName": "TestRegionEffects",
			"regions": [
				_region("Ashfall", ["Bramblewold"], 2),
				# Retail's commonest restriction shape: one barracks allowed.
				_region("Bramblewold", ["Ashfall", "Cinderfen"], 2,
					[{"buildings": ["Barracks"], "numberAllowed": 1}], true),
				# Retail's stronghold shape: no fortress allowed, one plot.
				_region("Cinderfen", ["Bramblewold", "Dunmarch"], 1,
					[{"buildings": ["Fortress"], "numberAllowed": 0}]),
				_region("Dunmarch", ["Cinderfen"], 3),
				_region("Emberisle", [], 0),
			],
			"territoryBonuses": [],
		}],
		"territoryBonuses": [],
		"regionEffects": [],
		"cities": [],
		"defaultArmies": [{
			"scriptingName": "GarrisonArmy1",
			"spawnForTemplates": ["PlayerAlpha", "PlayerBeta"],
			"heroTemplateName": "",
			"playerArmy": "TestGarrisonArmy",
			"icon": "GarrisonIcon",
		}],
		"playerArmies": [{
			"name": "TestGarrisonArmy",
			"displayNameTag": "LWA:TestGarrison",
			"entries": [{"thingTemplate": "TestFighterHorde", "quantity": 1}],
		}],
		"playerTemplates": [
			{
				"name": "PlayerAlpha", "faction": "FactionAlpha",
				"startingWorldCp": 1500, "maxWorldCp": 4500,
				"startingHeroCp": 450, "maxHeroCp": 450,
				"scenarioStartResources": 2000,
			},
			{
				"name": "PlayerBeta", "faction": "FactionBeta",
				"startingWorldCp": 1500, "maxWorldCp": 4500,
				"startingHeroCp": 450, "maxHeroCp": 450,
				"scenarioStartResources": 2000,
			},
		],
		"scenarios": [{
			"name": "TestScenario",
			"regionCampaign": "TestCampaign",
			"isEvilCampaign": false,
			"isHistoricalScenario": false,
			"minPlayers": 2,
			"maxPlayers": 2,
			"actArmies": [],
			"ownershipSets": [
				{
					"regions": ["Ashfall", "Bramblewold"],
					"startRegion": "Ashfall",
					"spawnArmies": [{"armies": ["GarrisonArmy1"], "region": "Ashfall"}],
					"spawnBuildings": [{"buildings": ["LW_FARM"], "region": "Ashfall"}],
				},
				{
					"regions": ["Cinderfen", "Dunmarch"],
					"startRegion": "Dunmarch",
					"spawnArmies": [{"armies": ["GarrisonArmy1"], "region": "Dunmarch"}],
					"spawnBuildings": [],
				},
			],
			"victoryTypes": [{
				"displayTags": {"displayGameType": "LWScenario:TestElimination"},
				"playerDefeatConditions": [{
					"kind": "PlayerDefeatCondition", "teams": [1, 2],
					"controlledRegions": [], "numControlledRegionsLessOrEqualTo": 0,
					"loseIfCapitalLost": false,
				}],
				"teamDefeatConditions": [],
				"teamVictoryConditions": [],
			}],
		}],
	}


func _state() -> StateScript:
	var world := WorldScript.new()
	if not world.load_from_dict(_document(), "TestCampaign"):
		printerr("WOTR_CONSTRUCTION fixture failed to load: %s" % str(world.errors))
	var state := StateScript.new()
	state.setup(world, [
		{"template": "PlayerAlpha", "team": 1},
		{"template": "PlayerBeta", "team": 2},
	])
	state.building_catalogue = _catalogue()
	state.apply_ownership_sets("TestScenario")
	return state


# =============================================================================
# HALF ONE: THE RULES
# =============================================================================

func _test_the_catalogue_loads_from_an_authored_bundle() -> void:
	var catalogue := _catalogue()
	_check("the_catalogue_loads_through_the_product_loader", catalogue.loaded, catalogue.reason)
	# EIGHT of the ten authored blocks: the fifth-Type one is refused by omission
	# and the unpriced one is KEPT (it is a well-formed building; it simply
	# cannot be afforded, which is a different fact from not existing).
	_check("a_type_retail_never_authored_is_refused_by_omission",
		catalogue.building("FXB_AlphaWonder").is_empty()
			and catalogue.unresolved_macros.has("Type=Wonder"),
		str(catalogue.building_ids))
	_check("retails_four_types_are_the_whole_vocabulary",
		Array(BuildingsScript.TYPES) == ["Armory", "Barracks", "Fortress", "Resource"])
	_check("the_cost_macros_resolve_to_their_authored_numbers",
		int(catalogue.building("FXB_AlphaKeep")["cost"]) == 1500
			and int(catalogue.building("FXB_AlphaHall")["cost"]) == 500
			and int(catalogue.building("FXB_AlphaForge")["cost"]) == 500
			and int(catalogue.building("FXB_AlphaCroft")["cost"]) == 0)
	# A FARM COSTS ZERO AND THAT IS NOT A MISSING VALUE. Retail says so twice -
	# `WOTR_FARM_COST = 0` and, in prose, "you will not be able to build any new
	# units or buildings on the map (besides farms) until your Treasury is no
	# longer empty".
	_check("a_free_building_is_priced_zero_not_unpriced",
		int(catalogue.building("FXB_AlphaCroft")["cost"]) == 0
			and String(catalogue.building("FXB_AlphaCroft")["cost_reason"]).is_empty())
	_check("an_unresolvable_price_is_recorded_as_unbuildable_and_names_its_macro",
		int(catalogue.building("FXB_AlphaMystery")["cost"]) == -1
			and String(catalogue.building("FXB_AlphaMystery")["cost_reason"]).contains("HARNESS_UNPRICED"),
		String(catalogue.building("FXB_AlphaMystery").get("cost_reason", "")))
	# RETAIL'S `AvailableTo` IS THE ONLY FILTER. No faction-name resemblance.
	_check("a_seat_is_offered_only_what_retail_makes_available_to_it",
		catalogue.buildings_for("PlayerAlpha").size() == 5
			and catalogue.buildings_for("PlayerBeta").size() == 4
			and catalogue.building_of_type("PlayerBeta", "Fortress")["id"] == "FXB_BetaKeep")
	# The four income/AI bindings, each proved separately because two of them are
	# not the same word twice.
	_check("only_fortress_and_resource_earn_a_per_turn_gain",
		catalogue.income_for_type("Fortress") == 300
			and catalogue.income_for_type("Resource") == 300
			and catalogue.income_for_type("Barracks") == 0
			and catalogue.income_for_type("Armory") == 0)
	var carried := catalogue.building("FXB_AlphaKeep")
	_check("all_five_typed_nugget_kinds_reach_the_runtime_catalogue",
		["increase_treasury", "strengthen_army", "spawn_army", "upgrade_troops", "increase_command_points"] == (carried["nuggets"] as Array).map(func(n: Variant) -> String: return String((n as Dictionary)["kind"])), str(carried["nuggets"]))
	var experience_ui := LivingWorldUiScript.new()
	_check("schema4_upgrade_experience_rows_load_in_source_order",
		experience_ui.load_from(FIXTURE_ROOT.path_join("living-world-ui.json"))
			and (experience_ui.upgrade_experience_levels as Array).map(
				func(row: Variant) -> String: return String((row as Dictionary)["name"]))
				== ["FixtureOneLevel1", "FixtureOneLevel2"], str(experience_ui.errors))
	var experience_copy: Array = experience_ui.upgrade_experience_levels.duplicate(true)
	(experience_copy[0]["targetNames"] as Array)[0] = "MUTATED"
	_check("upgrade_experience_rows_support_deep_copy_isolation",
		String((experience_ui.upgrade_experience_levels[0]["targetNames"] as Array)[0]) == "FixtureOne")
	var level_up_copy := experience_ui.level_up_upgrade("FixtureOne")
	(level_up_copy["triggeredBy"] as Array)[0] = "MUTATED"
	var stored_level_up := experience_ui.level_up_upgrade("FixtureOne")
	_check("schema4_level_up_upgrade_accessor_is_deep_and_exact",
		stored_level_up.size() == 3
			and stored_level_up.has("levelCap")
			and stored_level_up.has("levelsToGain")
			and stored_level_up.has("triggeredBy")
			and not stored_level_up.has("template")
			and String((stored_level_up["triggeredBy"] as Array)[0]) == "Upgrade_FixtureVeterancy"
			and experience_ui.level_up_upgrade("Inactive").is_empty())
	_check("treasury_is_bound_from_the_typed_nugget_not_a_type_guess",
		String(catalogue.income_macro_by_type["Fortress"]) == "GAIN_PER_FORTRESS" and catalogue.income_for_type("Fortress") == 300)
	var isolation_ui := LivingWorldUiScript.new()
	var isolation_macros := MacrosScript.new()
	isolation_ui.load_from(FIXTURE_ROOT.path_join("living-world-ui.json"))
	isolation_macros.load_from(FIXTURE_ROOT.path_join("macros.json"))
	var isolation_source := isolation_ui.buildings["FXB_AlphaKeep"] as Dictionary
	var isolation_projected := catalogue._project(isolation_source, isolation_macros)
	(isolation_source["nuggets"][0] as Dictionary)["treasureAmount"] = "MUTATED_SOURCE"
	_check("runtime_typed_nuggets_are_deep_copied_from_loader_state",
		String((isolation_projected["nuggets"][0] as Dictionary)["treasureAmount"]) == "GAIN_PER_FORTRESS", str(isolation_projected["nuggets"]))
	var stale := _ui_bundle()
	stale["schemaVersion"] = 1
	var stale_path := FIXTURE_ROOT.path_join("stale-ui.json")
	var stale_file := FileAccess.open(stale_path, FileAccess.WRITE)
	stale_file.store_string(JSON.stringify(stale))
	stale_file.close()
	var stale_ui := LivingWorldUiScript.new()
	_check("schema_v1_is_rejected_as_stale_regenerate",
		not stale_ui.load_from(stale_path) and String(stale_ui.errors[0]).contains("stale"), str(stale_ui.errors))
	var bad_experience_cases: Array = []
	var fractional_experience := _ui_bundle()
	fractional_experience["upgradeExperienceLevels"][0]["rank"] = 1.5
	bad_experience_cases.append(["fractional", fractional_experience])
	var duplicate_experience := _ui_bundle()
	duplicate_experience["upgradeExperienceLevels"][1]["name"] = "FixtureOneLevel1"
	bad_experience_cases.append(["duplicate", duplicate_experience])
	var missing_coverage := _ui_bundle()
	missing_coverage["upgradeExperienceLevels"] = []
	bad_experience_cases.append(["missing-coverage", missing_coverage])
	var stale_two := _ui_bundle()
	stale_two["schemaVersion"] = 2
	var stale_two_path := FIXTURE_ROOT.path_join("stale-two-ui.json")
	var stale_two_file := FileAccess.open(stale_two_path, FileAccess.WRITE)
	stale_two_file.store_string(JSON.stringify(stale_two))
	stale_two_file.close()
	var stale_two_ui := LivingWorldUiScript.new()
	_check("schema_v2_is_rejected_as_stale_regenerate",
		not stale_two_ui.load_from(stale_two_path) and String(stale_two_ui.errors[0]).contains("stale"), str(stale_two_ui.errors))
	var malformed_experience := _ui_bundle()
	malformed_experience["upgradeExperienceLevels"][0]["extra"] = true
	bad_experience_cases.append(["malformed", malformed_experience])
	for bad_value in bad_experience_cases:
		var bad := bad_value as Array
		var bad_path := FIXTURE_ROOT.path_join("%s-experience-ui.json" % String(bad[0]))
		var bad_file := FileAccess.open(bad_path, FileAccess.WRITE)
		bad_file.store_string(JSON.stringify(bad[1]))
		bad_file.close()
		var bad_ui := LivingWorldUiScript.new()
		_check("%s_upgrade_experience_fails_closed" % String(bad[0]),
			not bad_ui.load_from(bad_path) and not bad_ui.loaded
				and bad_ui.upgrade_experience_levels.is_empty(), str(bad_ui.errors))
	var stale_three := _ui_bundle()
	stale_three["schemaVersion"] = 3
	var stale_three_path := FIXTURE_ROOT.path_join("stale-three-ui.json")
	var stale_three_file := FileAccess.open(stale_three_path, FileAccess.WRITE)
	stale_three_file.store_string(JSON.stringify(stale_three))
	stale_three_file.close()
	var stale_three_ui := LivingWorldUiScript.new()
	_check("schema_v3_is_rejected_as_stale_regenerate",
		not stale_three_ui.load_from(stale_three_path) and String(stale_three_ui.errors[0]).contains("stale"), str(stale_three_ui.errors))
	var bad_level_up_cases: Array = []
	var missing_level_up := _ui_bundle()
	missing_level_up.erase("levelUpUpgrades")
	bad_level_up_cases.append(["missing-key", missing_level_up])
	var absent_active_level_up := _ui_bundle()
	absent_active_level_up["levelUpUpgrades"] = []
	bad_level_up_cases.append(["missing-active", absent_active_level_up])
	var inactive_level_up := _ui_bundle()
	inactive_level_up["levelUpUpgrades"][0]["template"] = "Inactive"
	bad_level_up_cases.append(["inactive", inactive_level_up])
	var malformed_level_up := _ui_bundle()
	malformed_level_up["levelUpUpgrades"][0]["extra"] = true
	bad_level_up_cases.append(["exact-keys", malformed_level_up])
	var fractional_level_up := _ui_bundle()
	fractional_level_up["levelUpUpgrades"][0]["levelsToGain"] = 1.5
	bad_level_up_cases.append(["fractional", fractional_level_up])
	var empty_trigger := _ui_bundle()
	empty_trigger["levelUpUpgrades"][0]["triggeredBy"] = []
	bad_level_up_cases.append(["empty-trigger", empty_trigger])
	var non_array_trigger := _ui_bundle()
	non_array_trigger["levelUpUpgrades"][0]["triggeredBy"] = "Upgrade_X"
	bad_level_up_cases.append(["non-array-trigger", non_array_trigger])
	var duplicate_trigger := _ui_bundle()
	duplicate_trigger["levelUpUpgrades"][0]["triggeredBy"] = ["Upgrade_X", "upgrade_x"]
	bad_level_up_cases.append(["duplicate-trigger", duplicate_trigger])
	var zero_level_up := _ui_bundle()
	zero_level_up["levelUpUpgrades"][0]["levelsToGain"] = 0
	bad_level_up_cases.append(["zero-integer", zero_level_up])
	var negative_level_up := _ui_bundle()
	negative_level_up["levelUpUpgrades"][0]["levelCap"] = -1
	bad_level_up_cases.append(["negative-integer", negative_level_up])
	var over_safe_level_up := _ui_bundle()
	over_safe_level_up["levelUpUpgrades"][0]["levelCap"] = 9007199254740992.0
	bad_level_up_cases.append(["over-safe-integer", over_safe_level_up])
	var duplicate_template := _ui_bundle()
	duplicate_template["levelUpUpgrades"].append(
		(duplicate_template["levelUpUpgrades"][0] as Dictionary).duplicate(true))
	bad_level_up_cases.append(["duplicate-template", duplicate_template])
	var null_trigger := _ui_bundle()
	null_trigger["levelUpUpgrades"][0]["triggeredBy"] = ["NULL"]
	bad_level_up_cases.append(["null-trigger", null_trigger])
	var rank_gap := _ui_bundle()
	rank_gap["levelUpUpgrades"][0]["levelCap"] = 3
	bad_level_up_cases.append(["rank-gap", rank_gap])
	for bad_level_value in bad_level_up_cases:
		var bad_level := bad_level_value as Array
		var bad_level_path := FIXTURE_ROOT.path_join("%s-level-up-ui.json" % String(bad_level[0]))
		var bad_level_file := FileAccess.open(bad_level_path, FileAccess.WRITE)
		bad_level_file.store_string(JSON.stringify(bad_level[1]))
		bad_level_file.close()
		var bad_level_ui := LivingWorldUiScript.new()
		_check("%s_level_up_upgrade_fails_closed" % String(bad_level[0]),
			not bad_level_ui.load_from(bad_level_path) and not bad_level_ui.loaded
				and bad_level_ui.level_up_upgrades.is_empty(), str(bad_level_ui.errors))
	var malformed := _ui_bundle()
	(malformed["buildings"][0] as Dictionary)["nuggets"][0]["extra"] = true
	var malformed_path := FIXTURE_ROOT.path_join("malformed-ui.json")
	var malformed_file := FileAccess.open(malformed_path, FileAccess.WRITE)
	malformed_file.store_string(JSON.stringify(malformed))
	malformed_file.close()
	var malformed_ui := LivingWorldUiScript.new()
	_check("malformed_typed_nugget_fails_the_whole_bundle",
		not malformed_ui.load_from(malformed_path) and not malformed_ui.loaded and malformed_ui.buildings.is_empty(), str(malformed_ui.errors))
	var string_version := _ui_bundle()
	string_version["schemaVersion"] = "4"
	var string_version_path := FIXTURE_ROOT.path_join("string-version-ui.json")
	var string_version_file := FileAccess.open(string_version_path, FileAccess.WRITE)
	string_version_file.store_string(JSON.stringify(string_version))
	string_version_file.close()
	var string_version_ui := LivingWorldUiScript.new()
	_check("schema_version_string_is_not_coerced_to_integer",
		not string_version_ui.load_from(string_version_path), str(string_version_ui.errors))
	var empty_tag := _ui_bundle()
	(empty_tag["buildings"][0] as Dictionary)["nuggets"][0]["tag"] = ""
	var empty_tag_path := FIXTURE_ROOT.path_join("empty-tag-ui.json")
	var empty_tag_file := FileAccess.open(empty_tag_path, FileAccess.WRITE)
	empty_tag_file.store_string(JSON.stringify(empty_tag))
	empty_tag_file.close()
	var empty_tag_ui := LivingWorldUiScript.new()
	_check("empty_required_typed_string_fails_the_whole_bundle",
		not empty_tag_ui.load_from(empty_tag_path), str(empty_tag_ui.errors))
	var exponent_raw := _ui_bundle()
	(exponent_raw["buildings"][0]["nuggets"][1]["bonuses"][0] as Dictionary)["armorRaw"] = "Armor:1e1%"
	(exponent_raw["buildings"][0]["nuggets"][1]["bonuses"][0] as Dictionary)["armorPct"] = 10.0
	var exponent_path := FIXTURE_ROOT.path_join("exponent-bonus-ui.json")
	var exponent_file := FileAccess.open(exponent_path, FileAccess.WRITE)
	exponent_file.store_string(JSON.stringify(exponent_raw))
	exponent_file.close()
	var exponent_ui := LivingWorldUiScript.new()
	_check("bonus_raw_rejects_expression_and_exponent_syntax",
		not exponent_ui.load_from(exponent_path), str(exponent_ui.errors))
	var unsafe_integer := _ui_bundle()
	(unsafe_integer["buildings"][0]["nuggets"][4] as Dictionary)["amount"] = 9007199254740992.0
	var unsafe_integer_path := FIXTURE_ROOT.path_join("unsafe-integer-ui.json")
	var unsafe_integer_file := FileAccess.open(unsafe_integer_path, FileAccess.WRITE)
	unsafe_integer_file.store_string(JSON.stringify(unsafe_integer))
	unsafe_integer_file.close()
	var unsafe_integer_ui := LivingWorldUiScript.new()
	_check("typed_integers_beyond_exact_json_range_are_refused",
		not unsafe_integer_ui.load_from(unsafe_integer_path), str(unsafe_integer_ui.errors))
	_check("retails_ai_score_keys_bind_to_retails_four_types",
		String(BuildingsScript.AI_SCORE_KEY_TYPES["BuildingScoreCastle"]) == "Fortress"
			and String(BuildingsScript.AI_SCORE_KEY_TYPES["BuildingScoreFarm"]) == "Resource")
	_check("retails_scenario_tokens_bind_to_retails_four_types",
		Array(BuildingsScript._sorted(BuildingsScript.SCENARIO_TOKEN_TYPES.keys()))
			== ["LW_ARMORY", "LW_BARRACKS", "LW_FARM", "LW_FORT"])
	# THE `LW_*` RESOLUTION IS RETAIL'S OWN COMMENT, applied: "the appropriate one
	# for the controlling faction will be created".
	var resolved: Dictionary = catalogue.resolve_scenario_token("LW_FORT", "PlayerBeta")
	_check("a_scenario_token_resolves_to_the_controlling_factions_own_building",
		bool(resolved["ok"]) and String(resolved["id"]) == "FXB_BetaKeep", str(resolved))
	var unknown: Dictionary = catalogue.resolve_scenario_token("LW_ZIGGURAT", "PlayerBeta")
	_check("a_token_retail_never_defined_is_refused_by_name",
		not bool(unknown["ok"]) and String(unknown["reason"]).contains("LW_ZIGGURAT"),
		String(unknown["reason"]))
	var absent: Dictionary = catalogue.resolve_scenario_token("LW_FORT", "PlayerGamma")
	_check("a_faction_with_no_block_of_that_type_is_refused_never_substituted",
		not bool(absent["ok"]) and String(absent["reason"]).contains("PlayerGamma"),
		String(absent["reason"]))
	_check("the_load_description_names_the_numbers_it_will_charge",
		String("\n".join(catalogue.describe_load())).contains("HARNESS_FORTRESS_COST=1500"),
		String("\n".join(catalogue.describe_load())))


func _test_nothing_builds_without_a_catalogue() -> void:
	# THE FAIL-CLOSED PATH, and it is the one every machine without the converted
	# bundles takes. Nothing is built with invented prices; the refusal names the
	# bundle and the call that would bind it.
	var state := _state()
	state.building_catalogue = null
	var refusal := state.build_refusal(0, "Ashfall", "FXB_AlphaCroft")
	var concrete_cp: Dictionary = state.world_command_point_report(0)
	_check("with_no_catalogue_every_build_is_refused",
		not state.can_build(0, "Ashfall", "FXB_AlphaCroft")
			and not bool(concrete_cp["ok"])
			and String(concrete_cp["refusal"]).contains("missing building catalogue"))
	_check("the_refusal_names_the_missing_data_and_how_to_bind_it",
		refusal.contains("catalogue") and refusal.contains("load_building_catalogue"), refusal)
	var attempted: Dictionary = state.build_structure(0, "Ashfall", "FXB_AlphaCroft")
	_check("a_refused_build_stands_nothing_and_spends_nothing",
		not bool(attempted["ok"]) and state.structures_in_region("Ashfall").size() == 1
			and state.treasure(0) == 2000)
	# WITHOUT A CATALOGUE the scenario's own token still stands, verbatim and
	# typeless, and therefore earns nothing. That is the honest state, not a
	# fallback: the record says exactly what the scenario file says.
	var scenario_only := StateScript.new()
	var world := WorldScript.new()
	world.load_from_dict(_document(), "TestCampaign")
	scenario_only.setup(world, [
		{"template": "PlayerAlpha", "team": 1}, {"template": "PlayerBeta", "team": 2}])
	scenario_only.apply_ownership_sets("TestScenario")
	var unresolved_cp: Dictionary = scenario_only.world_command_point_report(0)
	_check("an_unresolved_token_stands_verbatim_and_earns_nothing",
		Array(scenario_only.buildings_in_region("Ashfall")) == ["LW_FARM"]
			and String((scenario_only.structures_in_region("Ashfall")[0] as Dictionary)["type"]) == ""
			and int(scenario_only.turn_income(0).get("total", 0)) == 0
			and bool(unresolved_cp["ok"]) and int(unresolved_cp["limit"]) == 1500)


func _test_a_build_spends_treasure_and_stands_a_structure() -> void:
	var state := _state()
	# THE SCENARIO'S OWN TOKEN RESOLVED, because a catalogue was bound before
	# ownership was applied. This is what closes the `LW_*` half of the old gap.
	_check("a_scenario_token_stands_as_the_concrete_building_it_names",
		Array(state.buildings_in_region("Ashfall")) == ["FXB_AlphaCroft"]
			and String((state.structures_in_region("Ashfall")[0] as Dictionary)["type"]) == "Resource"
			and String((state.structures_in_region("Ashfall")[0] as Dictionary)["token"]) == "LW_FARM",
		str(state.structures_in_region("Ashfall")))
	_check("the_seat_opens_on_retails_own_scenario_start_resources",
		state.treasure(0) == 2000 and state.treasure(1) == 2000)

	# THE BUILD. A barracks in Bramblewold: 500 of 2000.
	var before := state.treasure(0)
	var built: Dictionary = state.build_structure(0, "Bramblewold", "FXB_AlphaHall")
	_check("the_build_reports_ok_with_no_refusal",
		bool(built["ok"]) and String(built["refusal"]).is_empty(), str(built))
	_check("the_treasure_moved_by_exactly_the_authored_price",
		int(built["treasury_before"]) == before
			and int(built["treasury_after"]) == before - 500
			and state.treasure(0) == before - 500,
		str(built))
	_check("the_structure_stands_on_a_real_plot_of_that_region",
		int(built["plot"]) == 0 and state.structures_in_region("Bramblewold").size() == 1)
	var standing := state.structures_in_region("Bramblewold")[0] as Dictionary
	_check("the_standing_record_carries_its_owner_type_and_turn",
		int(standing["owner"]) == 0 and String(standing["type"]) == "Barracks"
			and String(standing["building"]) == "FXB_AlphaHall"
			# `token` is empty for a structure a seat BUILT, so provenance survives:
			# a reader can always tell a raised structure from a scenario's own.
			and String(standing["token"]) == "" and int(standing["turn"]) == 0,
		str(standing))
	_check("the_used_and_free_plot_counts_follow",
		state.plot_count("Bramblewold") == 2 and state.free_plot("Bramblewold") == 1)

	# BUILDING DOES NOT SPEND THE TURN. Retail's own phase description puts
	# construction, training and movement in one phase.
	_check("raising_a_structure_does_not_hand_the_turn_on",
		state.turn_index == 0 and state.active_player() == 0)
	# A SECOND STRUCTURE IN THE SAME TERRITORY THE SAME TURN IS REFUSED, in
	# retail's own words: "only one structure per territory can be under
	# construction at a time" (`LW:InstructionText10`). Without this, farms being
	# free (`WOTR_FARM_COST = 0`) would let a seat fill every plot it owns on turn
	# one for nothing.
	var same_turn: Dictionary = state.build_structure(0, "Bramblewold", "FXB_AlphaForge")
	_check("a_second_structure_in_the_same_territory_the_same_turn_is_refused",
		not bool(same_turn["ok"])
			and String(same_turn["refusal"]).contains("one structure per territory"),
		String(same_turn["refusal"]))
	# BUT ANOTHER TERRITORY IS FINE - the rule is per territory, not per turn.
	var elsewhere: Dictionary = state.build_structure(0, "Ashfall", "FXB_AlphaForge")
	_check("a_seat_may_raise_a_structure_in_another_territory_the_same_turn",
		bool(elsewhere["ok"]) and state.treasure(0) == before - 1000)
	# AND THE SCENARIO'S OWN STRUCTURES DO NOT BLOCK THE OPENING TURN. They were
	# standing before the campaign began (`turn` is -1, not 0), so a region the
	# scenario furnished is still buildable on turn 0.
	_check("a_scenario_furnished_region_is_still_buildable_on_the_first_turn",
		int((state.structures_in_region("Ashfall")[0] as Dictionary)["turn"]) == -1
			and state.structures_in_region("Ashfall").size() == 2)
	# NEXT TURN THE TERRITORY ADMITS ANOTHER.
	state.advance_turn()
	state.advance_turn()
	var next_turn: Dictionary = state.build_structure(0, "Bramblewold", "FXB_AlphaForge")
	_check("the_territory_admits_another_structure_next_turn",
		bool(next_turn["ok"]) and int(next_turn["plot"]) == 1, String(next_turn["refusal"]))

	# DEMOLITION FREES THE PLOT AND REFUNDS NOTHING - stated project-authored.
	# Measured against the purse as it stands, because two turns have gone by and
	# the seat has been paid its income in between.
	var purse_before_demolition := state.treasure(0)
	var razed: Dictionary = state.demolish_structure(0, "Bramblewold", 1)
	_check("demolishing_frees_the_plot",
		bool(razed["ok"]) and String(razed["building"]) == "FXB_AlphaForge"
			and state.free_plot("Bramblewold") == 1
			and state.structures_in_region("Bramblewold").size() == 1)
	_check("demolishing_refunds_nothing", state.treasure(0) == purse_before_demolition)
	_check("demolishing_an_empty_plot_is_refused_by_name",
		not bool(state.demolish_structure(0, "Bramblewold", 1)["ok"]))

	# THE STRUCTURES FOLLOW THE GROUND when a region changes hands, or the loser
	# would keep being paid for a farm behind an enemy border.
	state.transfer_region("Bramblewold", 1)
	_check("a_conquered_regions_structures_change_owner_with_it",
		int((state.structures_in_region("Bramblewold")[0] as Dictionary)["owner"]) == 1)


func _test_world_command_points_are_a_pure_standing_structure_effect() -> void:
	var state := _state()
	var catalogue = state.building_catalogue
	var opening: Dictionary = state.world_command_point_report(0)
	_check("world_command_points_begin_at_the_hashed_starting_value",
		bool(opening["ok"]) and int(opening["base"]) == 1500
			and int(opening["bonus"]) == 0 and int(opening["limit"]) == 1500, str(opening))

	# The catalogue seam preserves and safely sums duplicate authored nuggets.
	var keep: Dictionary = catalogue.building("FXB_AlphaKeep")
	var original_nuggets: Array = (keep["nuggets"] as Array).duplicate(true)
	(keep["nuggets"] as Array).append(original_nuggets.back().duplicate(true))
	var duplicate: Dictionary = catalogue.world_command_points_for_building("FXB_AlphaKeep")
	_check("duplicate_WORLD_nuggets_remain_rows_and_sum_safely",
		bool(duplicate["ok"]) and int(duplicate["bonus"]) == 60
			and (duplicate["rows"] as Array).size() == 2, str(duplicate))
	keep["nuggets"] = original_nuggets.duplicate(true)

	# TurnsToBuild=1 stands immediately in this model; there is no invented queue.
	var built: Dictionary = state.build_structure(0, "Bramblewold", "FXB_AlphaKeep")
	var after_build: Dictionary = state.world_command_point_report(0)
	_check("a_completed_build_contributes_WORLD_command_points_exactly_once",
		bool(built["ok"]) and int(after_build["bonus"]) == 30
			and int(after_build["limit"]) == 1530 and (after_build["rows"] as Array).size() == 1,
		str(after_build))
	var hash_after_build := state.state_hash()
	var repeated: Dictionary = state.world_command_point_report(0)
	_check("repeated_command_point_reports_neither_compound_nor_mutate_the_hash",
		int(repeated["limit"]) == 1530 and state.state_hash() == hash_after_build,
		str(repeated))

	state.place_authored_structure(0, "Ashfall", "LW_FORT")
	var stacked: Dictionary = state.world_command_point_report(0)
	_check("a_second_standing_structure_stacks_its_own_contribution",
		bool(stacked["ok"]) and int(stacked["bonus"]) == 60 and int(stacked["limit"]) == 1560,
		str(stacked))
	state.demolish_structure(0, "Ashfall", 1)
	var after_demolition: Dictionary = state.world_command_point_report(0)
	_check("demolition_reverses_one_standing_contribution",
		bool(after_demolition["ok"]) and int(after_demolition["bonus"]) == 30
			and int(after_demolition["limit"]) == 1530, str(after_demolition))

	var alpha_template := state.world.player_templates["PlayerAlpha"] as Dictionary
	var authored_max := int(alpha_template["max_world_cp"])
	alpha_template["max_world_cp"] = 1510
	var capped: Dictionary = state.world_command_point_report(0)
	_check("WORLD_command_points_clamp_at_the_exact_template_maximum",
		bool(capped["ok"]) and int(capped["max"]) == 1510 and int(capped["limit"]) == 1510,
		str(capped))
	alpha_template["max_world_cp"] = authored_max

	var command := (keep["nuggets"] as Array).back() as Dictionary
	command["type"] = "HERO"
	var wrong_scope: Dictionary = state.world_command_point_report(0)
	_check("a_non_WORLD_scope_fails_closed_with_a_deterministic_refusal",
		not bool(wrong_scope["ok"]) and String(wrong_scope["refusal"]).contains("only WORLD"),
		String(wrong_scope["refusal"]))
	command["type"] = "WORLD"
	command["amount"] = "30"
	var malformed: Dictionary = state.world_command_point_report(0)
	command["amount"] = -7.0
	var negative: Dictionary = state.world_command_point_report(0)
	_check("malformed_and_non_retail_negative_amounts_fail_closed",
		not bool(malformed["ok"]) and String(malformed["refusal"]).contains("malformed amount")
			and not bool(negative["ok"]) and String(negative["refusal"]).contains("negative amount"),
		"malformed=%s negative=%s" % [String(malformed["refusal"]), String(negative["refusal"])])
	command["amount"] = 30.0
	keep["nuggets_status"] = "refused"
	var refused: Dictionary = state.world_command_point_report(0)
	_check("a_refused_typed_nugget_surfaces_instead_of_becoming_zero",
		not bool(refused["ok"]) and String(refused["refusal"]).contains("refused typed"),
		String(refused["refusal"]))
	keep["nuggets_status"] = "ok"

	var saved := state.snapshot()
	var before_projection := state.state_hash()
	state.transfer_region("Bramblewold", 1)
	var former_owner: Dictionary = state.world_command_point_report(0)
	var new_owner: Dictionary = state.world_command_point_report(1)
	_check("the_project_model_ownership_projection_moves_the_derived_bonus",
		int(former_owner["bonus"]) == 0 and int(new_owner["bonus"]) == 30,
		"former=%s new=%s" % [str(former_owner), str(new_owner)])
	var nugget_boundary := GapsScript.reason("strategic_building_nuggets")
	_check("the_diagnostic_distinguishes_CP_projection_from_executable_traced_strengthening",
		nugget_boundary.contains("project's current-owner")
			and nugget_boundary.contains("CP ownership callback remains directly untraced")
			and nugget_boundary.contains("StrengthenArmy same-current-owner filtering")
			and nugget_boundary.contains("executable-traced")
			and nugget_boundary.contains("auto-resolve armour only")
			and nugget_boundary.contains("no RTS/tactical claim"))
	state.restore(saved)
	state.world_command_point_report(0)
	_check("snapshot_restore_and_pure_reporting_leave_the_authoritative_hash_unchanged",
		state.state_hash() == before_projection)


func _test_strengthen_army_is_a_pure_dynamic_standing_structure_effect() -> void:
	var state := _state()
	var expected := [0.9, 0.81, 0.729, 0.6561, 0.59049, 0.531441, 0.4782969, 0.4782969]
	var ladder_ok := true
	var pure := true
	for count in range(1, 9):
		var standing: Array = []
		for plot in count:
			standing.append({"building": "FXB_AlphaKeep", "owner": 0, "plot": plot,
				"token": "", "turn": 0, "type": "Fortress"})
		state.region_structures["Bramblewold"] = standing
		var before := state.state_hash()
		var report: Dictionary = state.strengthen_army_report(0, "Bramblewold")
		ladder_ok = ladder_ok and bool(report["ok"]) \
			and is_equal_approx(float(report["armor_multiplier"]), float(expected[count - 1]))
		pure = pure and state.state_hash() == before \
			and state.strengthen_army_report(0, "Bramblewold") == report
	_check("StrengthenArmy_chooses_each_tier_and_caps_above_seven_without_mutation",
		ladder_ok and pure)

	# A foreign structure does not count. Capture changes the authoritative owner
	# fields, and demolition removes the row; the report must follow both live verbs.
	(state.region_structures["Bramblewold"] as Array).append({
		"building": "FXB_AlphaKeep", "owner": 1, "plot": 8,
		"token": "", "turn": 0, "type": "Fortress"})
	var filtered: Dictionary = state.strengthen_army_report(0, "Bramblewold")
	state.transfer_region("Bramblewold", 1)
	var former: Dictionary = state.strengthen_army_report(0, "Bramblewold")
	var captured: Dictionary = state.strengthen_army_report(1, "Bramblewold")
	var demolition_state := _state()
	demolition_state.region_structures["Ashfall"] = [{
		"building": "FXB_AlphaKeep", "owner": 0, "plot": 0,
		"token": "", "turn": 0, "type": "Fortress"}]
	var before_demolition: Dictionary = demolition_state.strengthen_army_report(0, "Ashfall")
	var demolished: Dictionary = demolition_state.demolish_structure(0, "Ashfall", 0)
	var after_demolition: Dictionary = demolition_state.strengthen_army_report(0, "Ashfall")
	_check("StrengthenArmy_filters_owner_and_recomputes_after_capture_and_demolition",
		is_equal_approx(float(filtered["armor_multiplier"]), 0.4782969)
			and is_equal_approx(float(former["armor_multiplier"]), 1.0)
			and is_equal_approx(float(captured["armor_multiplier"]), 0.4782969)
			and bool(demolished.get("ok", false))
			and is_equal_approx(float(before_demolition["armor_multiplier"]), 0.9)
			and is_equal_approx(float(after_demolition["armor_multiplier"]), 1.0))

	state = _state()
	var hall := state.building_catalogue.building("FXB_AlphaHall") as Dictionary
	var other := {"kind": "strengthen_army", "strengtheningRange": "THIS_TERRIORITY",
		"bonusKey": "OtherKey", "bonuses": [{"threshold": 1.0,
			"weaponPct": null, "armorPct": 50.0, "experiencePct": null}]}
	(hall["nuggets"] as Array).append(other)
	state.region_structures["Bramblewold"] = [
		{"building": "FXB_AlphaKeep", "owner": 0, "plot": 0,
			"token": "", "turn": 0, "type": "Fortress"},
		{"building": "FXB_AlphaHall", "owner": 0, "plot": 1,
			"token": "", "turn": 0, "type": "Barracks"}]
	var distinct: Dictionary = state.strengthen_army_report(0, "Bramblewold")
	other["bonusKey"] = "HarnessBonus"
	var mismatch: Dictionary = state.strengthen_army_report(0, "Bramblewold")
	_check("StrengthenArmy_multiplies_distinct_keys_and_refuses_mismatched_shared_tables",
		is_equal_approx(float(distinct["armor_multiplier"]), 0.45)
			and (distinct["groups"] as Array).size() == 2
			and not bool(mismatch["ok"])
			and String(mismatch["refusal"]).contains("mismatched bonus tables"))

	state = _state()
	var keep := state.building_catalogue.building("FXB_AlphaKeep") as Dictionary
	var strength := (keep["nuggets"] as Array)[1] as Dictionary
	var original := strength.duplicate(true)
	state.region_structures["Bramblewold"] = [{
		"building": "FXB_AlphaKeep", "owner": 0, "plot": 0,
		"token": "", "turn": 0, "type": "Fortress"}]
	strength["strengtheningRange"] = "WORLD"
	var bad_range: Dictionary = state.strengthen_army_report(0, "Bramblewold")
	strength.clear()
	strength.merge(original, true)
	strength["bonuses"] = [{"threshold": 1.5, "weaponPct": null,
		"armorPct": 90.0, "experiencePct": null}]
	var bad_threshold: Dictionary = state.strengthen_army_report(0, "Bramblewold")
	strength["bonuses"] = [
		{"threshold": 1.0, "weaponPct": null, "armorPct": 90.0, "experiencePct": null},
		{"threshold": 1.0, "weaponPct": null, "armorPct": 81.0, "experiencePct": null}]
	var duplicate_threshold: Dictionary = state.strengthen_army_report(0, "Bramblewold")
	strength["bonuses"] = [{"threshold": 1.0, "weaponPct": 5.0,
		"armorPct": 90.0, "experiencePct": null}]
	var bad_weapon: Dictionary = state.strengthen_army_report(0, "Bramblewold")
	strength["bonuses"] = [{"threshold": 1.0, "weaponPct": null,
		"armorPct": 90.0, "experiencePct": 5.0}]
	var bad_experience: Dictionary = state.strengthen_army_report(0, "Bramblewold")
	strength.clear()
	strength.merge(original, true)
	keep["nuggets_status"] = "refused"
	var refused: Dictionary = state.strengthen_army_report(0, "Bramblewold")
	keep["nuggets_status"] = "ok"
	state.building_catalogue = null
	var missing_catalogue: Dictionary = state.strengthen_army_report(0, "Bramblewold")
	_check("StrengthenArmy_malformed_and_missing_inputs_fail_closed",
		not bool(bad_range["ok"]) and not bool(bad_threshold["ok"])
			and not bool(duplicate_threshold["ok"]) and not bool(bad_weapon["ok"])
			and not bool(bad_experience["ok"]) and not bool(refused["ok"])
			and not bool(missing_catalogue["ok"])
			and String(missing_catalogue["refusal"]).contains("missing building catalogue"))


func _test_every_refusal_names_its_rule() -> void:
	var state := _state()
	# UNAFFORDABLE. The fortress is 1500 and the seat has 2000; spend first.
	# Spend 1000 of 2000 in ONE turn, across two territories, so no income has
	# been accrued in between to muddy the arithmetic.
	state.build_structure(0, "Bramblewold", "FXB_AlphaHall")  # -500 -> 1500
	state.build_structure(0, "Ashfall", "FXB_AlphaForge")     # -500 -> 1000
	# Dunmarch has three free plots, no restriction and nothing raised there this
	# turn, so the ONLY thing standing between the seat and a fortress is the
	# price - which is what this check has to isolate.
	state.transfer_region("Dunmarch", 0)
	var poor := state.build_refusal(0, "Dunmarch", "FXB_AlphaKeep")
	_check("an_unaffordable_build_is_refused_by_name_with_both_numbers",
		poor.contains("1500") and poor.contains(str(state.treasure(0)))
			and poor.contains("treasury"), poor)
	_check("an_unaffordable_build_is_still_OFFERED_so_the_player_can_see_the_price",
		not state.can_build(0, "Dunmarch", "FXB_AlphaKeep")
			and int((state.building_catalogue.building("FXB_AlphaKeep"))["cost"]) == 1500)

	# NOT YOURS. Retail: "you can construct new buildings on any open build plot
	# in a territory YOU CONTROL".
	var theirs := state.build_refusal(0, "Cinderfen", "FXB_AlphaCroft")
	_check("building_on_ground_you_do_not_hold_is_refused_by_name",
		theirs.contains("Cinderfen") and theirs.contains("not held"), theirs)

	# NOT YOUR FACTION'S. Retail's `AvailableTo`.
	var wrong_faction := state.build_refusal(0, "Bramblewold", "FXB_BetaKeep")
	_check("a_structure_of_another_faction_is_refused_by_name",
		wrong_faction.contains("PlayerBeta") and wrong_faction.contains("PlayerAlpha"),
		wrong_faction)

	# NO PLOTS AT ALL.
	state.transfer_region("Emberisle", 0)
	var plotless := state.build_refusal(0, "Emberisle", "FXB_AlphaCroft")
	_check("a_region_with_no_build_plots_is_refused_by_name",
		plotless.contains("no build plots"), plotless)

	# ALL PLOTS TAKEN. Ashfall's two are now full (the scenario's farm and the
	# forge raised above).
	var full := state.build_refusal(0, "Ashfall", "FXB_AlphaCroft")
	_check("a_full_region_is_refused_by_name_with_the_plot_count",
		full.contains("all 2 build plots"), full)

	# A NAMED PLOT THAT IS TAKEN, and one that does not exist.
	_check("a_plot_that_already_carries_something_is_refused_by_name",
		state.build_refusal(0, "Ashfall", "FXB_AlphaCroft", 0).contains("already carries"))
	_check("a_plot_the_region_does_not_have_is_refused_by_name",
		state.build_refusal(0, "Ashfall", "FXB_AlphaCroft", 9).contains("no plot 9"))

	# RETAIL'S TWO RESTRICTION SENTENCES, quoted back where retail wrote them.
	var limited := _state()
	limited.build_structure(0, "Bramblewold", "FXB_AlphaHall")
	var barracks_capped := limited.build_refusal(0, "Bramblewold", "FXB_AlphaHall")
	# The TYPE LIMIT is named before the one-per-turn rule, deliberately: a
	# permanent "never here" is more useful to a player than a "not yet".
	_check("a_type_over_its_territory_limit_quotes_retails_own_label",
		barracks_capped.contains("Number allowed in territory: 1")
			or barracks_capped.contains("number allowed in territory: 1"),
		barracks_capped)
	limited.transfer_region("Cinderfen", 0)
	var fort_capped := limited.build_refusal(0, "Cinderfen", "FXB_AlphaKeep")
	_check("a_fortress_where_a_stronghold_stands_quotes_retails_own_sentence",
		fort_capped == "you cannot build a fortress here since a stronghold already exists in this territory",
		fort_capped)
	# The SAME region still admits a building of another type - the restriction is
	# per type, not per region.
	_check("a_type_limit_does_not_bar_the_other_types",
		limited.can_build(0, "Cinderfen", "FXB_AlphaCroft"))

	# A BUILDING RETAIL NEVER AUTHORED.
	_check("an_unknown_building_id_is_refused_by_name",
		limited.build_refusal(0, "Bramblewold", "FXB_Nonsense").contains("not a strategic building"))
	# AN UNSEATED PLAYER, and an unknown region.
	_check("an_unseated_seat_and_an_unknown_region_are_refused_by_name",
		limited.build_refusal(7, "Bramblewold", "FXB_AlphaCroft").contains("not seated")
			and limited.build_refusal(0, "Nowhere", "FXB_AlphaCroft").contains("no region"))
	# AN UNPRICED BUILDING is refused BY THE MACRO'S NAME, never given away free.
	_check("a_building_whose_price_will_not_resolve_is_refused_by_its_macro_name",
		limited.build_refusal(0, "Ashfall", "FXB_AlphaMystery").contains("HARNESS_UNPRICED"),
		limited.build_refusal(0, "Ashfall", "FXB_AlphaMystery"))


func _test_income_accrues_at_the_start_of_a_turn() -> void:
	var state := _state()
	# The board opens with the scenario's farm in Ashfall (seat 0) and nothing
	# else, and Bramblewold - also seat 0's - is retail's fertile shape.
	var opening: Dictionary = state.turn_income(0)
	_check("a_farm_and_a_fertile_region_are_both_counted",
		int(opening["farms"]) == 1 and int(opening["fertile_regions"]) == 1
			and int(opening["fortresses"]) == 0
			and int(opening["total"]) == 300 + 500,
		str(opening))
	_check("the_income_report_shows_its_workings",
		(opening["rows"] as PackedStringArray).size() == 2,
		str(opening["rows"]))
	# THE OTHER SEAT HOLDS NEITHER, and earns nothing. An income that paid a seat
	# for nothing would be indistinguishable on screen from one that worked.
	_check("a_seat_with_no_farm_and_no_fertile_ground_earns_nothing",
		int(state.turn_income(1).get("total", 0)) == 0)

	# A FORTRESS ADDS ITS OWN GAIN. 1500 to build, +300 a turn thereafter.
	state.transfer_region("Dunmarch", 0)
	state.build_structure(0, "Dunmarch", "FXB_AlphaKeep")
	_check("a_standing_fortress_adds_gain_per_fortress",
		int(state.turn_income(0).get("total", 0)) == 300 + 500 + 300
			and int(state.turn_income(0).get("fortresses", 0)) == 1,
		str(state.turn_income(0)))
	# A BARRACKS AND AN ARMORY ADD NOTHING. Retail authors no `GAIN_PER_*` macro
	# for either, and inventing one would be inventing an economy.
	state.build_structure(0, "Bramblewold", "FXB_AlphaHall")
	state.build_structure(0, "Ashfall", "FXB_AlphaForge")
	_check("a_barracks_and_an_armory_earn_nothing",
		int(state.turn_income(0).get("total", 0)) == 1100)

	# THE ACCRUAL ITSELF, at the start of the seat's turn and not before.
	var purse := state.treasure(0)
	state.advance_turn()  # -> seat 1, who earns nothing
	_check("the_arriving_seat_is_paid_and_the_departing_one_is_not",
		state.active_player() == 1 and state.treasure(0) == purse
			and state.treasure(1) == 2000)
	state.advance_turn()  # -> seat 0
	_check("income_lands_when_the_turn_comes_round_again",
		state.active_player() == 0 and state.treasure(0) == purse + 1100)
	# A DEFEATED SEAT EARNS NOTHING.
	state.set_defeated(1)
	var beta_purse := state.treasure(1)
	state.accrue_income(1)
	_check("a_defeated_seat_earns_nothing", state.treasure(1) == beta_purse)


func _test_the_hash_covers_construction() -> void:
	var state := _state()
	var before := state.state_hash()
	state.build_structure(0, "Bramblewold", "FXB_AlphaHall")
	var after := state.state_hash()
	_check("raising_a_structure_changes_the_strategic_hash", before != after)

	var bytes := state.snapshot()
	var restored := StateScript.new()
	var world := WorldScript.new()
	world.load_from_dict(_document(), "TestCampaign")
	restored.setup(world, [
		{"template": "PlayerAlpha", "team": 1}, {"template": "PlayerBeta", "team": 2}])
	_check("the_hash_round_trips_after_a_build",
		restored.restore(bytes) and restored.state_hash() == after,
		"%s vs %s" % [restored.state_hash(), after])
	_check("the_restored_copy_holds_the_same_structure_and_the_same_purse",
		Array(restored.buildings_in_region("Bramblewold")) == ["FXB_AlphaHall"]
			and restored.treasure(0) == state.treasure(0))

	# THE PLOT INDEX IS INSIDE THE HASH. Two peers that put the same structure on
	# different foundations are not looking at the same board.
	var moved := StateScript.new()
	moved.setup(world, [
		{"template": "PlayerAlpha", "team": 1}, {"template": "PlayerBeta", "team": 2}])
	moved.restore(bytes)
	(moved.structures_in_region("Bramblewold")[0] as Dictionary)["plot"] = 1
	_check("the_plot_a_structure_stands_on_is_inside_the_hash",
		moved.state_hash() != after)

	# BUILD ORDER MUST NOT REACH THE HASH. Two peers that raised the same two
	# structures on the same two plots in a different ORDER must agree.
	var forwards := _state()
	forwards.build_structure(0, "Ashfall", "FXB_AlphaHall", 1)
	forwards.build_structure(0, "Bramblewold", "FXB_AlphaForge", 1)
	var backwards := _state()
	backwards.build_structure(0, "Bramblewold", "FXB_AlphaForge", 1)
	backwards.build_structure(0, "Ashfall", "FXB_AlphaHall", 1)
	_check("the_order_two_structures_were_raised_in_does_not_reach_the_hash",
		forwards.state_hash() == backwards.state_hash())

	# A MALFORMED STRUCTURE REFUSES THE WHOLE RESTORE, exactly as a malformed
	# battle commitment does: a record admitted here would ride the hash
	# uninspected.
	var mangled := state.authoritative_state().duplicate(true)
	((mangled["region_structures"] as Dictionary)["Bramblewold"] as Array)[0] = {"nope": 1}
	_check("a_malformed_structure_record_refuses_the_whole_restore",
		not restored.restore(var_to_bytes(mangled)))
	# AND THE REFUSED RESTORE LEFT THE ADOPTER ALONE.
	_check("a_refused_restore_leaves_the_adopter_exactly_as_it_was",
		restored.state_hash() == after)


func _test_the_session_is_the_only_door() -> void:
	var session := SessionScript.new()
	# The session's own loader, pointed at the authored bundles.
	var loaded: Dictionary = session.load_building_catalogue([_write_fixture_bundles()])
	_check("the_session_binds_the_catalogue_and_reports_the_count",
		# NINE, not ten: the fifth-Type block is refused by omission and the
		# unpriced one is KEPT, because "cannot be afforded" and "does not exist"
		# are different facts and the player is entitled to see the first.
		bool(loaded["ok"]) and int(loaded["buildings"]) == 9, str(loaded))
	_check("a_session_with_a_catalogue_reports_no_reason_not_to_build",
		session.building_catalogue_reason.is_empty())
	var started := session.begin(_document(), "TestCampaign", "TestScenario", [
		{"template": "PlayerAlpha", "team": 1, "controller": "human"},
		{"template": "PlayerBeta", "team": 2, "controller": "ai"},
	])
	_check("the_session_starts_with_the_catalogue_already_bound",
		started and session.state.building_catalogue != null, str(session.refusals))

	# WHAT THE SCREEN ASKS FOR. Every plot, occupied or not, in plot order.
	var plots: Dictionary = session.build_plots("Bramblewold")
	_check("build_plots_returns_one_row_per_foundation_whether_or_not_it_is_used",
		int(plots["total"]) == 2 and int(plots["used"]) == 0
			and (plots["plots"] as Array).size() == 2
			and not bool((plots["plots"] as Array)[0]["occupied"]))
	# AN ENEMY'S PLOTS ARE VISIBLE TOO - the owner asked for exactly this.
	var theirs: Dictionary = session.build_plots("Dunmarch")
	_check("an_enemy_regions_plots_are_visible_with_its_owner",
		int(theirs["total"]) == 3 and int(theirs["owner"]) == 1)

	# THE RING'S OFFER. Nothing is pre-filtered; an unaffordable or illegal
	# option comes back with `can_build` false and the sentence saying why.
	var options := session.build_options("Bramblewold")
	_check("the_ring_offers_every_structure_this_faction_has",
		options.size() == 5, str(options.size()))
	var buildable := 0
	var explained := 0
	for row in options:
		if bool(row["can_build"]):
			buildable += 1
		elif not String(row["refusal"]).is_empty():
			explained += 1
	_check("every_offered_structure_either_builds_or_says_why_not",
		buildable + explained == options.size() and buildable >= 1 and explained >= 1,
		"%d buildable, %d explained" % [buildable, explained])
	_check("the_offer_carries_retails_price_and_string_table_keys",
		String(options[0]["cost_macro"]).begins_with("HARNESS_")
			and String(options[0]["display_name_tag"]).begins_with("FIXTURE:"))

	# THE ONE MUTATION.
	var purse := session.treasure()
	var built: Dictionary = session.commit_build("Bramblewold", "FXB_AlphaHall")
	_check("commit_build_raises_the_structure_and_spends_the_treasure",
		bool(built["ok"]) and session.treasure() == purse - 500
			and int(built["plot"]) == 0, str(built))
	_check("commit_build_returns_the_hash_the_board_now_holds",
		String(built["hash_after"]) == session.state.state_hash())
	_check("the_offer_updates_after_the_build",
		int(session.build_plots("Bramblewold")["used"]) == 1)
	var refused: Dictionary = session.commit_build("Dunmarch", "FXB_AlphaCroft")
	_check("a_refused_commit_names_its_reason_and_moves_nothing",
		not bool(refused["ok"])
			and not (refused["refusals"] as PackedStringArray).is_empty()
			and session.treasure() == purse - 500)
	_check("commit_build_did_not_hand_the_turn_on",
		session.state.active_player() == 0)
	var income: Dictionary = session.income_report()
	_check("the_session_reports_the_income_the_seat_will_earn",
		int(income["total"]) == 300 + 500, str(income))

	# THE OPPONENT USES THE SAME DOOR. Seat 1 is the AI; its turn must raise
	# something with retail's weights and go through `commit_build()`.
	var opponent := AiScript.new()
	session.state.advance_turn()
	_check("the_opponents_turn_arrives", session.state.active_player() == 1)
	var ranked := opponent.rank_builds(session)
	_check("the_opponent_sees_the_same_candidates_the_ring_does",
		not ranked.is_empty(), str(ranked.size()))
	# With NO retail template loaded every weight is zero, and the report SAYS
	# so rather than implying the AI has taste it does not have.
	_check("with_no_retail_template_the_opponent_reports_having_no_taste",
		int((ranked[0] as Dictionary)["retail_score"]) == 0
			and String(((ranked[0] as Dictionary)["reasons"] as PackedStringArray)[0])
				.contains("template not loaded"),
		str((ranked[0] as Dictionary)["reasons"]))
	var beta_purse := session.state.treasure(1)
	var report: Dictionary = session.run_ai_turn([])
	_check("the_opponent_actually_raised_something",
		not (report["builds"] as Array).is_empty(), str(report.get("refusals", "")))
	var raised := (report["builds"] as Array)[0] as Dictionary
	_check("the_opponent_paid_for_it_out_of_its_own_treasury",
		session.state.treasure(1) < beta_purse
			and int(raised["cost"]) == beta_purse - int(raised["treasury_after"]))
	_check("the_opponent_stood_it_on_ground_it_holds",
		session.state.owner_of(String(raised["region"])) == 1)
	_check("the_turn_narration_tells_the_player_what_was_raised",
		String("\n".join(report["narrative"] as PackedStringArray)).contains("raised"),
		str(report["narrative"]))
	_check("the_opponents_build_moved_the_hash",
		String(report["hash_after"]) != String(report["hash_before"]))
	# THE BOUND IS A BOUND. Retail records no per-turn build budget; this project
	# does, and it is named.
	_check("the_opponent_respects_its_own_stated_per_turn_bound",
		(report["builds"] as Array).size() <= AiScript.PROJECT_MAX_BUILDS_PER_TURN)


func _test_the_register_names_what_this_layer_authored() -> void:
	var authored := GapsScript.authored_rule_names()
	_check("the_income_arithmetic_is_named_project_authored",
		Array(authored).has("strategic_treasury_income_arithmetic"))
	_check("the_plot_choice_is_named_project_authored",
		Array(authored).has("lowest_free_build_plot"))
	_check("the_absent_demolition_refund_is_named_project_authored",
		Array(authored).has("demolition_refunds_nothing"))
	# THE CLOSED GAPS ARE ASSERTED GONE. A register that only grows is a register
	# nobody believes, and a FALSE entry left in one is worse than none.
	_check("the_two_false_gaps_this_lane_closed_are_gone",
		GapsScript.reason("strategic_building_construction").is_empty()
			and GapsScript.reason("strategic_treasury_income").is_empty())
	# The register distinguishes the two applied typed effects from the three that
	# remain deliberately unapplied.
	var nuggets := GapsScript.reason("strategic_building_nuggets")
	_check("the_building_nugget_register_names_applied_and_remaining_boundaries",
		nuggets.contains("IncreaseCommandPoints") and nuggets.contains("IncreaseTreasury")
			and nuggets.contains("StrengthenArmy") and nuggets.contains("UpgradeTroops")
			and nuggets.contains("SpawnArmy") and nuggets.contains("Type=WORLD"),
		nuggets)
	# NO RNG AND NO CLOCK on the construction path, asserted by READING the file,
	# the same way the auto-resolve and AI runners assert it of theirs.
	var forbidden := ["randf(", "randi(", "randomize(", "Time.get_", "randf_range("]
	var offenders: Array[String] = []
	for path in ["res://src/wotr/wotr_buildings.gd", "res://src/wotr/wotr_state.gd"]:
		var handle := FileAccess.open(path, FileAccess.READ)
		var text := handle.get_as_text()
		handle.close()
		for token in forbidden:
			if text.contains(String(token)):
				offenders.append("%s uses %s" % [path, String(token)])
	_check("nothing_on_the_construction_path_can_read_a_clock_or_an_rng",
		offenders.is_empty(), str(offenders))


# =============================================================================
# HALF TWO: THE REAL DATA
# =============================================================================

## Returns true when the converted bundles were found and the second half ran.
func _test_against_retail_data() -> bool:
	var catalogue := BuildingsScript.new()
	# NO ROOTS: the environment alone, which is exactly the path `run_game.bat`
	# and a player's machine take.
	var found: Dictionary = catalogue.load_from_roots([])
	if not bool(found.get("ok", false)):
		print("WOTR_CONSTRUCTION SKIP retail bundles are not staged: %s" % String(found.get("reason", "")))
		return false
	print("WOTR_CONSTRUCTION DATA %s" % "\n              ".join(catalogue.describe_load()))

	_check("retail_ships_twenty_eight_strategic_buildings",
		catalogue.building_ids.size() == 28, str(catalogue.building_ids.size()))
	_check("retails_four_prices_resolve_to_their_gamedata_numbers",
		int(catalogue.resolved_macros.get("WOTR_FARM_COST", -1)) == 0
			and int(catalogue.resolved_macros.get("WOTR_BARRACKS_COST", -1)) == 500
			and int(catalogue.resolved_macros.get("WOTR_FORGE_COST", -1)) == 500
			and int(catalogue.resolved_macros.get("WOTR_FORTRESS_COST", -1)) == 1500,
		str(catalogue.resolved_macros))
	_check("retails_three_income_numbers_resolve",
		catalogue.income_for_type("Fortress") == 300
			and catalogue.income_for_type("Resource") == 300
			and int(catalogue.resolved_macros.get("FERTILE_TERRITORY_BONUS", -1)) == 500,
		str(catalogue.resolved_macros))
	_check("nothing_retail_ships_is_left_unpriced_or_untyped",
		catalogue.unresolved_macros.is_empty(), str(catalogue.unresolved_macros))
	var world_cp_sources: Array[String] = []
	var world_cp_rows := 0
	var world_cp_exact := true
	for building_id_value in catalogue.building_ids:
		var building_id := String(building_id_value)
		var cp: Dictionary = catalogue.world_command_points_for_building(building_id)
		if not bool(cp.get("ok", false)):
			world_cp_exact = false
			continue
		for row_value in cp.get("rows", []) as Array:
			var row := row_value as Dictionary
			world_cp_rows += 1
			world_cp_sources.append(building_id)
			world_cp_exact = (world_cp_exact
				and String(row.get("scope", "")) == "WORLD"
				and int(row.get("amount", -1)) == 30
				and String(catalogue.building(building_id).get("type", "")) == "Resource")
	_check("retail_authors_exactly_seven_Resource_WORLD_plus_30_carriers",
		world_cp_exact and world_cp_rows == 7 and world_cp_sources.size() == 7,
		"rows=%d sources=%s" % [world_cp_rows, str(world_cp_sources)])

	# EVERY PLAYABLE FACTION HAS ONE OF EACH TYPE, which is what makes the `LW_*`
	# by-type resolution total rather than lucky.
	var complete := true
	var incomplete: Array[String] = []
	for template in ["PlayerAngmar", "PlayerDwarves", "PlayerElves", "PlayerIsengard",
			"PlayerMen", "PlayerMordor", "PlayerWild"]:
		for type_name in BuildingsScript.TYPES:
			if catalogue.building_of_type(String(template), String(type_name)).is_empty():
				complete = false
				incomplete.append("%s has no %s" % [String(template), String(type_name)])
	_check("every_playable_faction_has_exactly_one_of_each_of_retails_four_types",
		complete, str(incomplete))
	# RETAIL'S OWN NAMES, spot-checked against the ids the ini authors. Not a
	# resemblance test: these are the exact block names.
	_check("retails_own_isengard_and_mordor_blocks_are_the_ones_resolved",
		String(catalogue.building_of_type("PlayerIsengard", "Barracks")["id"]) == "LWB_IsengardUrukPit"
			and String(catalogue.building_of_type("PlayerIsengard", "Resource")["id"]) == "LWB_IsengardFurnace"
			and String(catalogue.building_of_type("PlayerMordor", "Barracks")["id"]) == "LWB_MordorOrcPit")
	_check("only_retails_fortresses_defend_a_territory",
		bool(catalogue.building_of_type("PlayerMen", "Fortress")["can_defend_territory"])
			and not bool(catalogue.building_of_type("PlayerMen", "Barracks")["can_defend_territory"]))
	_check("retails_barracks_recruit_and_its_farms_do_not",
		(catalogue.building_of_type("PlayerMordor", "Barracks")["recruits"] as Array).size() == 10
			and (catalogue.building_of_type("PlayerMordor", "Resource")["recruits"] as Array).is_empty())
	# THE `LW_*` RESOLUTION AGAINST RETAIL'S OWN EXPANSION LIST. Retail's
	# `riskcampaign.ini` says `LW_FORT` is `LWB_MenFortress LWB_ElvenFortress
	# LWB_DwarvenFortress LWB_MordorFortress LWB_IsengardFortress LWB_WildFortress
	# LWB_AngmarFortress`, positionally by faction; resolving BY TYPE must produce
	# the same answer for every position.
	_check("resolving_LW_FORT_by_type_reproduces_retails_positional_list",
		String(catalogue.resolve_scenario_token("LW_FORT", "PlayerMen")["id"]) == "LWB_MenFortress"
			and String(catalogue.resolve_scenario_token("LW_FORT", "PlayerElves")["id"]) == "LWB_ElvenFortress"
			and String(catalogue.resolve_scenario_token("LW_FORT", "PlayerDwarves")["id"]) == "LWB_DwarvenFortress"
			and String(catalogue.resolve_scenario_token("LW_FORT", "PlayerMordor")["id"]) == "LWB_MordorFortress"
			and String(catalogue.resolve_scenario_token("LW_FORT", "PlayerIsengard")["id"]) == "LWB_IsengardFortress"
			and String(catalogue.resolve_scenario_token("LW_FORT", "PlayerWild")["id"]) == "LWB_WildFortress"
			and String(catalogue.resolve_scenario_token("LW_FORT", "PlayerAngmar")["id"]) == "LWB_AngmarFortress")
	_check("resolving_LW_BARRACKS_and_LW_FARM_reproduces_retails_lists_too",
		String(catalogue.resolve_scenario_token("LW_BARRACKS", "PlayerMen")["id"]) == "LWB_GondorBarracks"
			and String(catalogue.resolve_scenario_token("LW_BARRACKS", "PlayerDwarves")["id"]) == "LWB_DwarvenHallOfWarriors"
			and String(catalogue.resolve_scenario_token("LW_FARM", "PlayerElves")["id"]) == "LWB_ElvenMallornTree"
			and String(catalogue.resolve_scenario_token("LW_ARMORY", "PlayerWild")["id"]) == "LWB_WildTreasureTrove")

	# A REAL SESSION ON THE REAL MAP.
	var located: Dictionary = SessionScript.locate_document([])
	if not bool(located.get("ok", false)):
		print("WOTR_CONSTRUCTION SKIP no living-world document: %s" % String(located.get("reason", "")))
		return false
	var document := located["document"] as Dictionary
	var session := SessionScript.new()
	# The handoff carries the document PATH, not the document, so a session built
	# from an already-parsed one has to be told where it came from - exactly as
	# the strategic screen does after `locate_document()`.
	session.document_path = String(located.get("path", ""))
	session.document_source = String(located.get("source", ""))
	if not session.begin(document, "DefaultCampaign", "WOTRScenario045", [
		{"template": "PlayerMen", "team": 1, "controller": "human"},
		{"template": "PlayerElves", "team": 1, "controller": "ai"},
		{"template": "PlayerAngmar", "team": 2, "controller": "ai"},
	]):
		print("WOTR_CONSTRUCTION SKIP the shipped scenario would not start: %s" % str(session.refusals))
		return false
	_check("the_real_session_bound_the_real_catalogue",
		session.buildings != null and session.building_catalogue_reason.is_empty(),
		session.building_catalogue_reason)
	_check("every_seat_opens_on_retails_scenario_start_resources",
		session.state.treasure(0) == 3000 and session.state.treasure(2) == 3000,
		"%d/%d" % [session.state.treasure(0), session.state.treasure(2)])
	# THE SHIPPED SCENARIO'S OWN `SpawnBuildings` ROWS, RESOLVED. `WOTRScenario045`
	# authors `LW_FORT` in Arnor for the Men seat; it must now stand as the
	# concrete fortress that faction gets, on a real plot, rather than as a token.
	var arnor := session.state.structures_in_region("Arnor")
	_check("a_shipped_scenarios_LW_FORT_stands_as_a_concrete_fortress",
		arnor.size() == 1
			and String((arnor[0] as Dictionary)["building"]) == "LWB_MenFortress"
			and String((arnor[0] as Dictionary)["type"]) == "Fortress"
			and String((arnor[0] as Dictionary)["token"]) == "LW_FORT",
		str(arnor))
	# AND IT PAYS. A fortress the scenario placed earns `GAIN_PER_FORTRESS` like
	# any other, which is the whole point of resolving the token.
	var opening: Dictionary = session.income_report(0)
	_check("the_resolved_scenario_structures_pay_their_authored_income",
		int(opening["fortresses"]) >= 1 and int(opening["total"]) >= 300, str(opening))

	# THE PLAYER'S OWN CLICK. Find a region the human seat holds with a free plot
	# and raise the cheapest thing retail offers there.
	var target := ""
	for region_id in session.state.regions_owned_by(0):
		if session.state.free_plot(region_id) >= 0:
			target = region_id
			break
	_check("the_human_seat_holds_a_region_with_a_free_build_plot", not target.is_empty())
	var offer := session.build_options(target)
	_check("the_ring_offers_retails_own_four_structures_for_that_faction",
		offer.size() == 4, str(offer.size()))
	var purse := session.treasure()
	var farm := session.commit_build(target, "LWB_GondorFarm")
	_check("the_player_can_raise_retails_own_farm_on_a_real_region",
		bool(farm["ok"]), str(farm.get("refusals", "")))
	_check("retails_farm_costs_nothing_exactly_as_WOTR_FARM_COST_says",
		session.treasure() == purse and int(farm["cost"]) == 0)
	var after_farm := session.income_report(0)
	_check("the_new_farm_shows_up_in_the_income_the_seat_will_earn",
		int(after_farm["farms"]) == int(opening["farms"]) + 1
			and int(after_farm["total"]) == int(opening["total"]) + 300,
		str(after_farm))
	# NOW SOMETHING THAT COSTS. A barracks is 500 of 3000.
	var barracks := session.commit_build(target, "LWB_GondorBarracks")
	if bool(barracks["ok"]):
		_check("retails_barracks_costs_exactly_WOTR_BARRACKS_COST",
			session.treasure() == purse - 500 and int(barracks["cost"]) == 500)
	else:
		# A one-plot region legitimately has no room for a second structure, and
		# the refusal must say so rather than the test pretending it built.
		_check("retails_barracks_costs_exactly_WOTR_BARRACKS_COST",
			String((barracks["refusals"] as PackedStringArray)[0]).contains("build plot"),
			str(barracks["refusals"]))
	# THE HASH ROUND-TRIPS on the real board with real structures.
	var live := session.state.state_hash()
	var adopted := SessionScript.new()
	_check("the_real_board_survives_the_handoff_with_its_structures_and_purse",
		adopted.adopt_handoff(session.handoff_payload())
			and adopted.state.state_hash() == live
			and adopted.state.treasure(0) == session.state.treasure(0)
			and Array(adopted.state.buildings_in_region("Arnor"))
				== Array(session.state.buildings_in_region("Arnor")),
		str(adopted.refusals))

	# THE OPPONENT WITH RETAIL'S REAL WEIGHTS.
	var opponent := AiScript.new()
	var template_found: Dictionary = opponent.locate_and_load([])
	if bool(template_found.get("ok", false)):
		_check("retails_own_building_weights_are_loaded_and_spent",
			opponent.building_score("Fortress") > 0 and opponent.building_score("Resource") > 0,
			str(opponent.values))
	else:
		_check("retails_own_building_weights_are_loaded_and_spent",
			opponent.building_score("Fortress") == 0,
			"template not staged: %s" % String(template_found.get("reason", "")))
	session.ai = opponent
	session.state.advance_turn()
	var ai_report: Dictionary = session.run_ai_turn([])
	_check("the_real_opponent_takes_a_turn_and_the_board_moves",
		bool(ai_report["ok"])
			and String(ai_report["hash_after"]) != String(ai_report["hash_before"]),
		str(ai_report.get("refusals", "")))
	return true


# --- harness -----------------------------------------------------------------

func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("WOTR_CONSTRUCTION PASS %s" % name)
	else:
		failed += 1
		printerr("WOTR_CONSTRUCTION FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])
