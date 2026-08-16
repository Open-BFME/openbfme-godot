extends SceneTree

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const EXPECTED := 16
var passed := 0
var failed := 0

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var sim := _sim()
	var rule := _ability_rule()
	sim._unit_ability_rules[Sim.SOLDIER_HORDE_ID] = sim._scaled_ability_rules([rule], 0.1)
	_spawn(sim, 1, 0, Vector2.ZERO, 3)
	_spawn(sim, 2, 0, Vector2.ZERO, 3)
	_spawn(sim, 3, 1, Vector2(5, 0), 1)
	var hero := sim.entities[1] as Dictionary
	var scaled := (sim._unit_ability_rules[Sim.SOLDIER_HORDE_ID] as Array)[0] as Dictionary
	var contract := scaled.get("special_power_contract", {}) as Dictionary
	_check("max_cast_range_scaled", is_equal_approx(float(contract.get("maxCastRangeScaled", 0.0)), 10.0))
	_check("public_timer_receipted", (contract.get("unsupported_semantics", []) as Array).has("hud_binding:PublicTimer"))
	hero["route"] = [Vector2.ONE]
	_check("moving_condition_blocks", String(sim.cast_ability(1, "Fixture", Vector2(5, 0), 0).get("reason", "")) == "activation-condition:MOVING")
	hero["route"] = []
	hero["current_speed"] = 0.0
	(sim.entities[3] as Dictionary)["kind_of"] = ["MACHINE"]
	var forbidden_result := sim.cast_ability(1, "Fixture", Vector2(5, 0), 0)
	_check("forbidden_object_blocks", String(forbidden_result.get("reason", "")) == "forbidden-object-nearby")
	(sim.entities[3] as Dictionary)["kind_of"] = ["HERO"]
	_check("max_cast_range_blocks", String(sim.cast_ability(1, "Fixture", Vector2(11, 0), 0).get("reason", "")) == "out-of-range")
	(sim.entities[3] as Dictionary)["kind_of"] = ["STRUCTURE"]
	_check("object_filter_blocks", String(sim.cast_ability(1, "Fixture", Vector2(5, 0), 0).get("reason", "")) == "object-filter-refused")
	(sim.entities[3] as Dictionary)["kind_of"] = ["HERO"]
	(sim.entities[2] as Dictionary)["member_health"] = [50, 100, 100]
	(sim.entities[2] as Dictionary)["health"] = 250
	var cast := sim.cast_ability(1, "Fixture", Vector2(5, 0), 0)
	_check("valid_cast_succeeds", bool(cast.get("ok", false)))
	_check("unit_cost_consumes_exact_members", (hero.get("member_health", []) as Array).count(0) == 2 and int(hero.get("health", 0)) == 100)
	_check("shared_timer_blocks_peer", String(sim.cast_ability(2, "Fixture", Vector2(5, 0), 0).get("reason", "")) == "cooldown-active")
	_check("death_type_receipt_emitted", _has_unit_cost_event(sim, ["NORMAL", "CRUSHED"]))
	var snapshot := sim.snapshot()
	var before := sim.state_hash()
	var restored := _sim()
	_check("snapshot_restores", restored.restore(snapshot))
	_check("hash_round_trips", restored.state_hash() == before)
	_check("shared_timer_round_trips", not (restored._shared_ability_cooldowns as Dictionary).is_empty())
	var pathable := _sim()
	var pathable_rule := _ability_rule()
	(pathable_rule["special_power_contract"] as Dictionary)["flags"] = ["PATHABLE_ONLY"]
	pathable_rule["targeting"] = "point"
	pathable._unit_ability_rules[Sim.SOLDIER_HORDE_ID] = pathable._scaled_ability_rules([pathable_rule], 0.1)
	var unknown_pathability := _sim()
	unknown_pathability._unit_ability_rules[Sim.SOLDIER_HORDE_ID] = unknown_pathability._scaled_ability_rules([pathable_rule], 0.1)
	_spawn(unknown_pathability, 11, 0, Vector2.ZERO, 3)
	_check("pathable_only_rejects_missing_navigation_authority", String(unknown_pathability.cast_ability(11, "Fixture", Vector2(1, 0), 0).get("reason", "")) == "target-unpathable")
	pathable.route_provider = PathableProbe.new()
	_spawn(pathable, 10, 0, Vector2.ZERO, 3)
	_check("pathable_only_rejects_unwalkable_target", String(pathable.cast_ability(10, "Fixture", Vector2(5, 0), 0).get("reason", "")) == "target-unpathable")
	(pathable.entities[10] as Dictionary)["member_health"] = [50, 100, 100]
	(pathable.entities[10] as Dictionary)["health"] = 250
	_check("pathable_only_accepts_walkable_target", bool(pathable.cast_ability(10, "Fixture", Vector2(1, 0), 0).get("ok", false)))
	var ran := passed + failed
	if ran != EXPECTED:
		failed += 1
		push_error("SPECIAL_POWER_CONTRACT_RUNTIME_FAIL liveness ran=%d expected=%d" % [ran, EXPECTED])
	print("SPECIAL_POWER_CONTRACT_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)

func _ability_rule() -> Dictionary:
	return {"ability_id":"Fixture","special_power_id":"SpecialFixture","targeting":"enemy-object","cooldown_ticks":10,"required_level":1,"level_gate_resolved":true,"castable":true,"effect":{"kind":"heal","radius":20.0,"amount":10.0,"amountKind":"flat"},"special_power_contract":{"publicTimer":true,"sharedSyncedTimer":true,"objectFilter":["NONE","+HERO","-STRUCTURE"],"forbiddenObjectFilter":["NONE","+MACHINE"],"forbiddenObjectRange":60,"maxCastRange":100,"unitCost":2,"unitCostDeathTypes":["NORMAL","CRUSHED"],"preventActivationConditions":["MOVING"]}}

func _sim() -> RetailSliceSim:
	var rules := {}
	for object_id in [Sim.SOLDIER_OBJECT_ID,Sim.SOLDIER_HORDE_ID,Sim.ARCHER_OBJECT_ID,Sim.TOWER_GUARD_OBJECT_ID,Sim.KNIGHT_OBJECT_ID]: rules[object_id] = _unit_rule(3)
	var sim: RetailSliceSim = Sim.new()
	sim.setup({}, {"unit_rules":rules,"source_map_transform_scale":0.1})
	sim.ai_enabled = false; sim.base_loop_enabled = false; sim.entities.clear(); sim.structures.clear()
	return sim

func _spawn(sim: RetailSliceSim, id: int, team: int, point: Vector2, count: int) -> void:
	var rule := _unit_rule(count)
	sim._add_battalion(id, team, point, "Fixture", Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, 0, rule)
	sim._attach_hero_ability_state(sim.entities[id] as Dictionary)

func _unit_rule(count: int) -> Dictionary:
	return {"horde_id":Sim.SOLDIER_HORDE_ID,"category":"hero","speed":1.0,"speed_source":10.0,"acceleration":1.0,"acceleration_source":10.0,"turn_rate_degrees_per_second":180.0,"braking":1.0,"braking_source":10.0,"attack_range":1.0,"attack_range_source":1.0,"minimum_attack_range":0.0,"minimum_attack_range_source":0.0,"vision_range":10.0,"vision_range_source":10.0,"delay_between_shots_ms":1000.0,"pre_attack_delay_ms":0.0,"firing_duration_ms":0.0,"attack_period_ticks":10,"pre_attack_ticks":0,"firing_duration_ticks":0,"member_damage":1,"member_health":100,"member_count":count,"formation_positions":[Vector3.ZERO,Vector3.ONE,Vector3(2,0,0)],"provenance":{}}

func _has_unit_cost_event(sim: RetailSliceSim, death_types: Array) -> bool:
	for event in sim.events:
		if String(event.get("kind", "")) == "ability.unit_cost" and (event.get("death_types", []) as Array) == death_types:
			return true
	return false

func _check(label: String, condition: bool) -> void:
	if condition: passed += 1
	else: failed += 1; push_error("SPECIAL_POWER_CONTRACT_RUNTIME_FAIL %s" % label)


class PathableProbe:
	extends RefCounted
	func is_local_inside_navigation(_position: Vector2) -> bool: return true
	func local_to_grid_cell(position: Vector2) -> Vector2i: return Vector2i(roundi(position.x), roundi(position.y))
	func is_navigation_walkable(cell: Vector2i) -> bool: return cell.x <= 1
