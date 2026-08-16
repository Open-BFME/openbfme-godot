extends SceneTree

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const EXPECTED := 13
var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var sim := _sim()
	var producer := sim.structures[50] as Dictionary
	producer["facing"] = Vector2.UP
	sim._structure_module_contracts["barracks"] = [_exit_contract()]
	sim._attach_structure_module_contracts(producer)
	var policy := producer.get("queue_production_exit_update", {}) as Dictionary
	_check("use_return_remains_deferred", (policy.get("unsupported_semantics", []) as Array).has("presentation_or_movement_binding:UseReturnToFormation"))
	_check("allow_airborne_is_preserved_deferred", (policy.get("unsupported_semantics", []) as Array).has("presentation_or_movement_binding:AllowAirborneCreation"))
	_check("initial_burst_value_preserved", int(policy.get("initial_burst", -1)) == 2)
	_check("initial_burst_is_explicitly_deferred", (policy.get("unsupported_semantics", []) as Array).has("unsupported_exit_semantic:InitialBurst"))
	_check("queue_accepts", bool(sim.queue_unit(0, 50, Sim.SOLDIER_HORDE_ID).get("ok", false)))
	sim.advance(2)
	var produced_id := int(sim.entity_ids()[0])
	var produced := sim.entities[produced_id] as Dictionary
	_check("authored_exit_delay_preserved", int(produced.get("production_exit_duration_ticks", -1)) == 3)
	_check("no_exit_path_recorded", bool(produced.get("production_exit_no_path", false)))
	_check("no_exit_path_skips_door_travel", Vector2(produced.get("position", Vector2.ZERO)).is_equal_approx(Vector2(12.0, 23.0)))
	_check("no_exit_path_origin_is_create_point", Vector2(produced.get("production_exit_origin", Vector2.ZERO)).is_equal_approx(Vector2(12.0, 23.0)))
	_check("placement_angle_is_producer_relative", Vector2(produced.get("facing", Vector2.ZERO)).is_equal_approx(Vector2.UP.rotated(deg_to_rad(90.0))))
	sim.advance(1)
	produced = sim.entities[produced_id] as Dictionary
	_check("delay_still_counts_without_path", int(produced.get("production_exit_start_tick", -1)) >= 0 and float(produced.get("production_exit_progress", 0.0)) > 0.0)
	var snapshot := sim.snapshot()
	var hash := sim.state_hash()
	var restored := _sim()
	_check("snapshot_restore", restored.restore(snapshot))
	_check("hash_restore", restored.state_hash() == hash)
	print("QUEUE_PRODUCTION_EXIT_EXACT_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 and passed == EXPECTED else 1)


func _exit_contract() -> Dictionary:
	return {
		"module": "QueueProductionExitUpdate",
		"extraction": "typed",
		"fields": {
			"UnitCreatePoint": [{"validNumeric": true, "value": {"x": 20.0, "y": 30.0, "z": 0.0}}],
			"NaturalRallyPoint": [{"validNumeric": true, "value": {"x": 50.0, "y": 0.0, "z": 0.0}}],
			"PlacementViewAngle": [{"value": 90.0}],
			"ExitDelay": [{"milliseconds": 300}],
			"NoExitPath": {"value": true},
			"AllowAirborneCreation": {"value": true},
			"InitialBurst": {"value": 2},
			"UseReturnToFormation": {"value": true},
		},
	}


func _sim() -> RetailSliceSim:
	var unit_rules := {}
	for object_id in [Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, Sim.ARCHER_OBJECT_ID, Sim.TOWER_GUARD_OBJECT_ID, Sim.KNIGHT_OBJECT_ID]:
		unit_rules[object_id] = _rule()
	var sim: RetailSliceSim = Sim.new()
	sim.setup({}, {"unit_rules": unit_rules, "source_unit_scale": 0.1, "FIXTURE_COST": 0, "FIXTURE_BUILD": 1, "FIXTURE_CP": 0})
	sim.ai_enabled = false
	sim.base_loop_enabled = true
	sim.entities.clear()
	sim.structures.clear()
	sim.team_resources[0] = 100
	sim._unit_production_rules[Sim.SOLDIER_HORDE_ID] = {
		"object_id": Sim.SOLDIER_OBJECT_ID,
		"category": "infantry",
		"default_cost": 0,
		"default_build_ticks": 1,
		"default_command_points": 0,
		"cost_rule": "FIXTURE_COST",
		"build_ticks_rule": "FIXTURE_BUILD",
		"command_points_rule": "FIXTURE_CP",
		"producer_routes": [{"producer_kind": "barracks"}],
	}
	sim.structures[50] = {"id": 50, "team": 0, "health": 100, "position": Vector2(10, 20), "construction_progress": 1.0, "structure_kind": "barracks", "production": [Sim.SOLDIER_HORDE_ID], "queue": [], "completed_upgrades": []}
	return sim


func _rule() -> Dictionary:
	return {"horde_id": Sim.SOLDIER_HORDE_ID, "category": "infantry", "speed": 1.0, "speed_source": 10.0, "acceleration": 1.0, "acceleration_source": 10.0, "turn_rate_degrees_per_second": 180.0, "braking": 1.0, "braking_source": 10.0, "attack_range": 1.0, "attack_range_source": 1.0, "minimum_attack_range": 0.0, "minimum_attack_range_source": 0.0, "vision_range": 10.0, "vision_range_source": 10.0, "delay_between_shots_ms": 1000.0, "pre_attack_delay_ms": 0.0, "firing_duration_ms": 0.0, "attack_period_ticks": 10, "pre_attack_ticks": 0, "firing_duration_ticks": 0, "member_damage": 1, "member_health": 100, "member_count": 1, "formation_positions": [Vector3.ZERO], "provenance": {}}


func _check(label: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		push_error("QUEUE_PRODUCTION_EXIT_EXACT_RUNTIME_FAIL " + label)
