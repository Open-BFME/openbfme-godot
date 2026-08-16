extends SceneTree

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const EXPECTED := 23
var passed := 0
var failed := 0

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var sim := _sim()
	var level_id := sim.spawn_scenario_pickup("LevelChest", Vector2(2, 0), "object-creation-list")
	_check("level_pickup_spawned", level_id > 0)
	var before_draws := sim.logic_random_draws
	(sim.entities[10] as Dictionary)["computer_controlled"] = true
	var refused := sim.collect_salvage_crate(level_id, 10)
	_check("ai_refused", String(refused.get("reason", "")) == "ai-pickup-disabled")
	_check("refusal_does_not_consume", sim.pickup_objects.has(level_id) and sim.logic_random_draws == before_draws)
	(sim.entities[10] as Dictionary)["computer_controlled"] = false
	(sim.entities[10] as Dictionary)["position"] = Vector2(500, 0)
	var level := sim.collect_salvage_crate(level_id, 10)
	_check("level_up_radius_is_dead_branch", bool(level.get("ok", false)))
	(sim.entities[10] as Dictionary)["position"] = Vector2.ZERO
	_check("level_reward", bool(level.get("ok", false)) and String(level.get("reward", "")) == "level")
	_check("level_is_exactly_one_rank", int((sim.entities[10] as Dictionary).get("level", 0)) == 2)
	_check("hundred_percent_still_draws", sim.logic_random_draws == before_draws + 1)
	_check("success_consumes", not sim.pickup_objects.has(level_id))
	_check("execute_fx_after_success", String(level.get("execute_fx", "")) == "FX_GoldChestPickup")

	var money_id := sim.spawn_scenario_pickup("MoneyChest", Vector2(3, 0), "object-creation-list")
	var resources_before := sim.resources_for_team(0)
	var money_draws := sim.logic_random_draws
	var money := sim.collect_salvage_crate(money_id, 10)
	_check("resource_chance_is_runtime_ignored", String(money.get("reward", "")) == "resource")
	_check("porter_chance_is_runtime_ignored", String(money.get("reward", "")) == "resource")
	_check("banner_chance_is_runtime_ignored", String(money.get("reward", "")) == "resource")
	_check("inclusive_money_bounds", int(money.get("amount", 0)) >= 160 and int(money.get("amount", 0)) <= 200)
	_check("money_deposited", sim.resources_for_team(0) == resources_before + int(money.get("amount", 0)))
	_check("money_path_draw_count", sim.logic_random_draws == money_draws + 2)

	var upgrade_id := sim.spawn_scenario_pickup("UpgradeChest", Vector2(4, 0), "object-creation-list")
	var upgrade := sim.collect_salvage_crate(upgrade_id, 10)
	_check("upgrade_reward", String(upgrade.get("reward", "")) == "upgrade")
	_check("upgrade_applied", ((sim.entities[10] as Dictionary).get("completed_upgrades", []) as Array).has("Upgrade_Test"))
	var collision_id:=sim.spawn_scenario_pickup("MoneyChest",Vector2.ZERO,"object-creation-list");var collision_before:=sim.resources_for_team(0);sim._step_active_pickup_collisions();_check("geometry_collision_collects",not sim.pickup_objects.has(collision_id));_check("geometry_collision_rewards",sim.resources_for_team(0)>collision_before)

	var forbidden_id := sim.spawn_scenario_pickup("MoneyChest", Vector2(5, 0), "object-creation-list")
	(sim.entities[10] as Dictionary)["kind_of"] = ["INFANTRY", "PROJECTILE"]
	var forbidden := sim.collect_salvage_crate(forbidden_id, 10)
	_check("forbidden_kind_refused", String(forbidden.get("reason", "")).begins_with("picker-forbidden-kind-of:"))
	_check("forbidden_remains", sim.pickup_objects.has(forbidden_id))
	(sim.entities[10] as Dictionary)["kind_of"] = ["INFANTRY"]
	var snapshot := sim.snapshot()
	var hash := sim.state_hash()
	var restored := _sim()
	_check("snapshot_restore", restored.restore(snapshot))
	_check("hash_restore", restored.state_hash() == hash)

	print("SALVAGE_CRATE_BINARY_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 and passed == EXPECTED else 1)

func _sim() -> RetailSliceSim:
	var sim: RetailSliceSim = Sim.new()
	var unit_rules := {}
	for object_id in [Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, Sim.ARCHER_OBJECT_ID, Sim.TOWER_GUARD_OBJECT_ID, Sim.KNIGHT_OBJECT_ID]:
		unit_rules[object_id] = _rule()
	sim.setup({}, {
		"game": "bfme2",
		"logic_random_seed": 12345,
		"unit_rules": unit_rules,
		"scenario_pickup_runtimes": {
			"LevelChest": _pickup("LevelChest", 1.0, 0, 0, "", false),
			"MoneyChest": _pickup("MoneyChest", 0.0, 160, 200, "", true),
			"UpgradeChest": _pickup("UpgradeChest", 0.0, 0, 0, "Upgrade_Test", true),
		},
	})
	sim.ai_enabled = false
	sim.base_loop_enabled = false
	sim.entities.clear()
	sim.structures.clear()
	sim.scenario_props.clear();sim.pickup_objects.clear()
	sim.entities[10] = {
		"id": 10, "team": 0, "health": 100, "maximum_health": 100,
		"member_maximum_health": 100, "member_health": [100],
		"unit_type": "TestUnit", "kind_of": ["INFANTRY"], "level": 1,
		"experience_xp": 0, "experience_max_level": 3,
		"completed_upgrades": [], "position": Vector2.ZERO,
	}
	sim._unit_experience_rules["TestUnit"] = {
		"initial_rank": 1, "max_level": 3,
		"levels": [
			{"rank": 1, "required_experience": 0},
			{"rank": 2, "required_experience": 100},
			{"rank": 3, "required_experience": 300},
		],
	}
	return sim

func _rule() -> Dictionary:
	return {"horde_id": Sim.SOLDIER_HORDE_ID, "category": "infantry", "speed": 1.0, "speed_source": 10.0, "acceleration": 1.0, "acceleration_source": 10.0, "turn_rate_degrees_per_second": 180.0, "braking": 1.0, "braking_source": 10.0, "attack_range": 1.0, "attack_range_source": 1.0, "minimum_attack_range": 0.0, "minimum_attack_range_source": 0.0, "vision_range": 10.0, "vision_range_source": 10.0, "delay_between_shots_ms": 1000.0, "pre_attack_delay_ms": 0.0, "firing_duration_ms": 0.0, "attack_period_ticks": 10, "pre_attack_ticks": 0, "firing_duration_ticks": 0, "member_damage": 1, "member_health": 100, "member_count": 1, "formation_positions": [Vector3.ZERO], "provenance": {}}

func _pickup(id: String, level_ratio: float, minimum: int, maximum: int, upgrade: String, allow_ai: bool) -> Dictionary:
	var fields := {
		"ForbiddenKindOf": {"value": ["PROJECTILE", "ENVIRONMENT"]},
		"LevelUpChance": {"ratio": level_ratio, "percent": level_ratio * 100.0},
		"LevelUpRadius": {"value": 100.0},
		"PorterChance": {"ratio": 1.0, "percent": 100.0},
		"BannerChance": {"ratio": 1.0, "percent": 100.0},
		"ResourceChance": {"ratio": 0.0, "percent": 0.0},
		"MinResource": {"value": minimum}, "MaxResource": {"value": maximum},
		"AllowAIPickup": {"value": allow_ai}, "ExecuteFX": {"value": "FX_GoldChestPickup"},
	}
	if upgrade != "": fields["Upgrade"] = {"value": upgrade}
	return {
		"schema": "openbfme.neutral-pickup-runtime", "schemaVersion": 0,
		"objectId": id, "runtimeDomain": "active-pickup", "runtimeStatus": "executable",
		"production": [],
		"scenarioAdmission": {"kind": "authored-ocl-pickup-leaf", "surfaces": ["object-creation-list"], "buildCommandExposed": false},
		"pickupContract": {"module": "SalvageCrateCollide", "extraction": "typed", "runtimeStatus": "executable", "fields": fields},
		"binaryOracleReceipt": {"domain":"active-collision-pickup","activeWhenAuthored":["AllowAIPickup","LevelUpChance","MaxResource","MinResource","Upgrade"],"deadBranchWhenAuthored":["LevelUpRadius"],"parsedIgnoredWhenAuthored":["BannerChance","PorterChance","ResourceChance"]}, "kindOf": {"effective": ["CRATE"]}, "geometry":{"footprint":{"radius":8.0}}, "presentation": {},
	}

func _check(label: String, condition: bool) -> void:
	if condition: passed += 1
	else: failed += 1; push_error("SALVAGE_CRATE_BINARY_RUNTIME_FAIL " + label)
