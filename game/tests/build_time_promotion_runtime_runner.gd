extends SceneTree

## object.BuildTime / upgrade.BuildTime: authored seconds reach the sim's clocks
## with no unit change and no invented scaling.
##
## Retail authors BuildTime in SECONDS; every sim clock counts 100ms ticks, so
## the only legal transform is seconds / TICK_SECONDS. This runner pins that
## transform at the three consumers that exist today — unit production queues,
## structure construction, and structure upgrade/research purchases — and pins
## the two authored scalings that ARE retail (a ProductionUpdate upgrade
## modifier, and a producer's PRODUCTION level multiplier) so neither can be
## mistaken for drift.
##
## livingworldbuilding.BuildTime is a different unit and a different consumer:
## retail authors it in TURNS inside ArmyToSpawn/HeroToSpawn, and the only thing
## that reads it today is the WOTR building catalogue, which carries it verbatim
## onto each recruit row. No recruitment clock spends it yet — that gap is
## already named `army_recruitment_and_cp_costs` in wotr_strategic_gaps.gd — so
## this runner pins the projection fidelity and claims nothing about timing.
const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const PlayableUnitAdapter = preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")
const WotrBuildings = preload("res://src/wotr/wotr_buildings.gd")
const WotrMacros = preload("res://src/wotr/wotr_macros.gd")
var passed := 0
var failed := 0
var _finished := false
var _frames := 0


func _initialize() -> void:
	call_deferred("_run")


func _process(_delta: float) -> bool:
	_frames += 1
	if _finished:
		return true
	if _frames > 600:
		push_error("BUILD_TIME_PROMOTION_FAIL runner_aborted_before_reporting")
		print("BUILD_TIME_PROMOTION_RESULT passed=%d failed=%d" % [passed, failed + 1])
		quit(1)
		return true
	return false


## Every check this runner is supposed to make. A GDScript runtime error inside
## one section aborts only that section and lets the rest report green with a
## quietly smaller total, so the count is asserted rather than trusted.
const EXPECTED_CHECKS := 31


func _run() -> void:
	_authored_seconds_become_ticks()
	_queue_honors_authored_unit_build_time()
	_structure_construction_honors_authored_seconds()
	_upgrade_purchase_honors_authored_seconds()
	_authored_scalings_are_the_only_scalings()
	_living_world_recruit_turns_are_carried_verbatim()
	_finished = true
	if passed + failed != EXPECTED_CHECKS:
		failed += 1
		push_error(
			"BUILD_TIME_PROMOTION_FAIL check_count_drifted expected=%d observed=%d"
			% [EXPECTED_CHECKS, passed + failed - 1]
		)
	print("BUILD_TIME_PROMOTION_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _authored_seconds_become_ticks() -> void:
	# Magnitudes taken from retail, one per faction family:
	#   RohanRoyalGuardHorde      BuildTime = 5.0   (rohanhordes.ini:508)
	#   RohanOathbreakerHordeSmall BuildTime = 20.0 (menhordes.ini:2101)
	#   GoblinArcherHorde         BuildTime = 45    (wildhordes.ini:569)
	#   BarrowWight               BuildTime = 90    (neutral/barrowwight.ini:128)
	for pair in [[5.0, 50], [20.0, 200], [45.0, 450], [90.0, 900]]:
		var seconds := float(pair[0])
		var expected := int(pair[1])
		var rule := PlayableUnitAdapter.simulation_rule(_document(seconds))
		_check(
			"seconds_%s_become_%d_ticks" % [String.num(seconds, 1), expected],
			int(rule.get("default_build_ticks", -1)) == expected
		)
		_check(
			"seconds_%s_are_not_read_as_milliseconds" % String.num(seconds, 1),
			int(rule.get("default_build_ticks", -1)) != roundi(seconds)
		)
	# A sub-tick authored time still costs at least one tick; it never becomes
	# free production. (RohanOathbreakerHorde authors BuildTime = 0 outright,
	# menhordes.ini:1925, so the clamp is retail's own case and not a fixture.)
	var tiny := PlayableUnitAdapter.simulation_rule(_document(0.04))
	_check("sub_tick_build_time_costs_one_tick", int(tiny.get("default_build_ticks", -1)) == 1)


func _queue_honors_authored_unit_build_time() -> void:
	var sim := _sim_with_producer(18.0)
	var queued: Dictionary = sim.queue_unit(Sim.PLAYER_TEAM, 900, "FixtureMonster")
	_check("queue_accepted", bool(queued.get("ok", false)))
	var item: Dictionary = queued.get("item", {}) as Dictionary
	_check("queue_duration_is_authored_seconds", int(item.get("duration_ticks", -1)) == 180)
	_check(
		"completion_is_start_plus_authored_duration",
		int(item.get("complete_tick", -1)) == int(item.get("start_tick", 0)) + 180
	)


func _structure_construction_honors_authored_seconds() -> void:
	# The faction manifest's structure_build_rules carry authored BuildTime
	# SECONDS (retail_faction_manifest.gd reads the structure's `BuildTime`
	# scalar); begin_structure is the only place allowed to turn them into ticks,
	# and _step_construction must then spend exactly that many.
	var sim: RetailSliceSim = Sim.new()
	sim.setup({}, {"unit_rules": _rules()})
	sim.ai_enabled = false
	sim.base_loop_enabled = true
	sim.entities.clear()
	sim.structures.clear()
	sim.team_resources = {Sim.PLAYER_TEAM: 100000, Sim.ENEMY_TEAM: 0}
	sim._structure_build_rules["fixture-yard"] = {"cost": 300, "seconds": 27.0}
	sim._structure_max_health["fixture-yard"] = 1500
	sim.entities[7] = {
		"id": 7, "team": Sim.PLAYER_TEAM, "kind": "battalion",
		"unit_type": Sim.SOLDIER_HORDE_ID, "object_id": Sim.SOLDIER_OBJECT_ID,
		"health": 100, "maximum_health": 100, "is_builder": true,
		"position": Vector2(10.0, 10.0), "state": "idle", "order_kind": "",
		"construction_id": 0, "members": [], "speed": 1.0,
	}
	var builders: Array[int] = [7]
	var started: Dictionary = sim._issue_construct_for_team(
		Sim.PLAYER_TEAM, builders, "fixture-yard", Vector2(12.0, 10.0)
	)
	_check("construction_started", bool(started.get("ok", false)))
	_check("construction_seconds_become_ticks", int(started.get("build_ticks", -1)) == 270)
	_check("construction_seconds_are_not_read_as_ticks", int(started.get("build_ticks", -1)) != 27)
	var structure_id := int(started.get("structure_id", 0))
	if not sim.structures.has(structure_id):
		_check("construction_site_exists", false)
		return
	var row := sim.structures[structure_id] as Dictionary
	# The site advances without re-walking the builder's route: the clock, not
	# the pathing, is what this runner is pinning.
	row["builder_free"] = true
	for _tick in range(0, 269):
		sim.tick_index += 1
		sim._step_construction()
	_check("construction_incomplete_one_tick_early", float(row.get("construction_progress", 1.0)) < 1.0)
	sim.tick_index += 1
	sim._step_construction()
	_check("construction_completes_on_authored_tick", float(row.get("construction_progress", 0.0)) >= 1.0)


func _upgrade_purchase_honors_authored_seconds() -> void:
	# _compile_structure_upgrade_chains is the upgrade.BuildTime consumer: the
	# manifest's buildTimeSeconds becomes the purchase duration in ticks.
	var sim: RetailSliceSim = Sim.new()
	sim.setup({}, {"unit_rules": _rules()})
	var contracts := {}
	sim._compile_structure_upgrade_chains(
		{
			"structure_upgrade_chains": {
				"fixture-yard": {
					"levelCap": 3,
					"steps": [{
						"upgradeId": "Upgrade_FixtureYardLevel2",
						"cost": 300,
						"buildTimeSeconds": 22.0,
						"toLevel": 2,
						"levelCap": 3,
						"levelsToGain": 1,
						"fromCommandSet": "FixtureYardCommandSet",
						"toCommandSet": "FixtureYardLevel2CommandSet",
					}],
				},
			},
		},
		contracts
	)
	_check("upgrade_chain_registered", not contracts.is_empty())
	var contract: Dictionary = {}
	for key in contracts.keys():
		contract = contracts[key] as Dictionary
		break
	_check("upgrade_seconds_become_ticks", int(contract.get("duration_ticks", -1)) == 220)
	_check("upgrade_seconds_are_not_read_as_ticks", int(contract.get("duration_ticks", -1)) != 22)
	_check("upgrade_cost_is_untouched", int(contract.get("cost", -1)) == 300)
	# The castle-upgrade surface is the second upgrade.BuildTime consumer, and it
	# is the one that meets retail's dominant authored value. Upgrade_ReinforcedGate
	# authors BuildTime = 30.0 (upgrade.ini:594) while Upgrade_WallBanner authors
	# 0 with the real 15.0 commented out (upgrade.ini:581) - zero must clamp to one
	# tick, never to a default and never to fifteen seconds.
	var castle := Sim.new()
	castle.setup({}, {"unit_rules": _rules()})
	var castle_contracts := {}
	castle._compile_structure_castle_upgrades(
		{
			"structure_castle_upgrades": {
				"fixture-fortress": {
					"upgrades": [
						{
							"upgradeId": "Upgrade_FixtureReinforcedGate",
							"grantsUpgradeId": "Upgrade_FixtureGateGrant",
							"cost": 500, "buildTimeSeconds": 30.0,
						},
						{
							"upgradeId": "Upgrade_FixtureWallBanner",
							"grantsUpgradeId": "",
							"cost": 200, "buildTimeSeconds": 0.0,
						},
					],
				},
			},
		},
		castle_contracts
	)
	_check("castle_upgrade_compile_clean", String(castle.configuration_error) == "")
	_check(
		"castle_upgrade_seconds_become_ticks",
		int((castle_contracts.get("Upgrade_FixtureReinforcedGate", {}) as Dictionary).get("duration_ticks", -1)) == 300
	)
	_check(
		"castle_upgrade_authored_zero_clamps_to_one_tick",
		int((castle_contracts.get("Upgrade_FixtureWallBanner", {}) as Dictionary).get("duration_ticks", -1)) == 1
	)


func _authored_scalings_are_the_only_scalings() -> void:
	# A ProductionUpdate time multiplier applies only behind its authored
	# upgrade, and a producer's PRODUCTION level factor divides the authored
	# time. Both are retail; nothing else may touch the number.
	var sim := _sim_with_producer(18.0)
	var producer := sim.structures[900] as Dictionary
	producer["production_update"] = {
		"maximum_queue_entries": 5,
		"modifiers": [{
			"required_upgrade": "Upgrade_FixtureYardSpeed",
			"time_multiplier": 0.5,
			"cost_multiplier": 1.0,
			"filter": [],
			"hero_purchase": false,
		}],
	}
	var without: Dictionary = sim.queue_unit(Sim.PLAYER_TEAM, 900, "FixtureMonster")
	_check(
		"unearned_modifier_does_not_shorten_build",
		int((without.get("item", {}) as Dictionary).get("duration_ticks", -1)) == 180
	)
	producer["completed_upgrades"] = ["Upgrade_FixtureYardSpeed"]
	producer["queue"] = []
	sim.team_resources[Sim.PLAYER_TEAM] = 100000
	var with_upgrade: Dictionary = sim.queue_unit(Sim.PLAYER_TEAM, 900, "FixtureMonster")
	_check(
		"authored_modifier_halves_authored_build",
		int((with_upgrade.get("item", {}) as Dictionary).get("duration_ticks", -1)) == 90
	)
	producer["completed_upgrades"] = []
	producer["queue"] = []
	producer["production_multiplier"] = 2.0
	var levelled: Dictionary = sim.queue_unit(Sim.PLAYER_TEAM, 900, "FixtureMonster")
	_check(
		"production_level_multiplier_divides_authored_build",
		int((levelled.get("item", {}) as Dictionary).get("duration_ticks", -1)) == 90
	)


func _living_world_recruit_turns_are_carried_verbatim() -> void:
	# LWB_GondorBarracks authors BuildTime 1 and 2 turns on its ArmyToSpawn rows
	# and LWB_MenFortress authors 3 on its heroes; the catalogue must carry those
	# turn counts unchanged - not seconds, not ticks, not clamped to one.
	var catalogue := WotrBuildings.new()
	var macros := WotrMacros.new()
	var record: Dictionary = catalogue._project(
		{
			"id": "LWB_GondorBarracks",
			"type": "Barracks",
			"availableTo": "PlayerMen",
			"strategicResourceCost": "WOTR_BARRACKS_COST",
			"turnsToBuild": "1",
			"recruits": [
				{"playerArmy": "GondorArcherArmy", "buildTime": "1"},
				{"playerArmy": "GondorRangerArmy", "buildTime": "2"},
				{"heroTemplateName": "GondorGandalf", "buildTime": "3"},
			],
		},
		macros
	)
	_check("living_world_building_projected", not record.is_empty())
	var recruits := Array(record.get("recruits", []))
	_check("living_world_recruit_rows_kept", recruits.size() == 3)
	if recruits.size() == 3:
		_check(
			"living_world_recruit_turns_are_authored",
			[
				int((recruits[0] as Dictionary).get("build_time", -1)),
				int((recruits[1] as Dictionary).get("build_time", -1)),
				int((recruits[2] as Dictionary).get("build_time", -1)),
			] == [1, 2, 3]
		)
	# The building's own TurnsToBuild is a separate authored field and must not be
	# confused with a recruit's BuildTime.
	_check("living_world_turns_to_build_is_separate", int(record.get("turns_to_build", -1)) == 1)


func _sim_with_producer(seconds: float) -> RetailSliceSim:
	var sim: RetailSliceSim = Sim.new()
	sim.setup({}, {"unit_rules": _rules()})
	sim.ai_enabled = false
	sim.base_loop_enabled = true
	sim.command_point_cap = 100000
	sim.entities.clear()
	sim.structures.clear()
	sim.team_resources = {Sim.PLAYER_TEAM: 100000, Sim.ENEMY_TEAM: 0}
	var rule := PlayableUnitAdapter.simulation_rule(_document(seconds))
	sim._unit_production_rules["FixtureMonster"] = {
		"producer_kind": "fixture-yard",
		"producer_kinds": ["fixture-yard"],
		"object_id": "FixtureMonster",
		"display_name": "Fixture Monster",
		"category": "monster",
		"default_cost": int(rule.get("default_cost", 0)),
		"default_build_ticks": int(rule.get("default_build_ticks", 0)),
		"default_command_points": int(rule.get("default_command_points", 0)),
	}
	if not sim._production_unit_order.has("FixtureMonster"):
		sim._production_unit_order.append("FixtureMonster")
	sim.structures[900] = {
		"id": 900, "team": Sim.PLAYER_TEAM, "structure_kind": "fixture-yard",
		"source_object_id": "FixtureYard",
		"health": 1000, "maximum_health": 1000, "construction_progress": 1.0,
		"level": 1, "completed_upgrades": [], "upgrade_queue": [],
		"production": ["FixtureMonster"], "queue": [],
		"structure_module_contracts_attached": true,
		"module_contracts": [],
	}
	return sim


func _document(seconds: float) -> Dictionary:
	return {
		"schema": "openbfme.playable-unit-runtime",
		"schemaVersion": 0,
		"objectId": "FixtureMonster",
		"category": "monster",
		"registration": {
			"production": [{
				"producerObjectId": "FixtureYard",
				"commandSetId": "FixtureYardCommandSet",
				"commandId": "Command_ConstructFixtureMonster",
				"surface": "command-socket",
				"slot": 1,
				"prerequisites": [],
				"commandSetTransition": [],
			}],
			"composition": {
				"containerObjectId": "FixtureMonster",
				"primaryMemberObjectId": "FixtureMonster",
				"members": [{"objectId": "FixtureMonster", "count": 1}],
			},
			"simulation": {
				"displayName": "Fixture Monster",
				"buildCost": 700,
				"buildTimeSeconds": seconds,
				"commandPoints": 35,
				"memberCount": 1,
				"memberHealth": 2500,
				"speed": 50.0,
				"visionRange": 400.0,
				"combat": {
					"attackRange": 30.0, "minimumAttackRange": 0.0,
					"delayBetweenShotsMs": 1000.0, "preAttackDelayMs": 250.0,
					"firingDurationMs": 250.0, "damage": 200,
				},
				"movement": {"acceleration": 100.0, "braking": 100.0, "turnRateDegreesPerSecond": 360.0},
				"formation": {"memberCount": 1, "positions": [{"x": 0.0, "y": 0.0}]},
			},
		},
	}


func _check(label: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		push_error("BUILD_TIME_PROMOTION_FAIL %s" % label)


func _rules() -> Dictionary:
	var rule := {
		"horde_id": Sim.SOLDIER_HORDE_ID, "category": "infantry", "speed": 1.0,
		"speed_source": 10.0, "acceleration": 1.0, "acceleration_source": 10.0,
		"turn_rate_degrees_per_second": 180.0, "braking": 1.0, "braking_source": 10.0,
		"attack_range": 1.0, "attack_range_source": 10.0, "minimum_attack_range": 0.0,
		"minimum_attack_range_source": 0.0, "vision_range": 10.0, "vision_range_source": 100.0,
		"delay_between_shots_ms": 100.0, "pre_attack_delay_ms": 0.0, "firing_duration_ms": 0.0,
		"attack_period_ticks": 1, "pre_attack_ticks": 0, "firing_duration_ticks": 0,
		"member_damage": 1, "member_health": 100, "member_count": 1,
		"formation_positions": [Vector3.ZERO], "provenance": {},
	}
	var output := {}
	for object_id in [Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, Sim.ARCHER_OBJECT_ID, Sim.TOWER_GUARD_OBJECT_ID, Sim.KNIGHT_OBJECT_ID]:
		output[object_id] = rule.duplicate(true)
	return output
