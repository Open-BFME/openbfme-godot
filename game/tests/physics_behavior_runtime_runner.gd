extends SceneTree

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const EXPECTED_CHECKS := 20

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var sim := _make_sim()
	var body_id: int = sim.spawn_physics_object(
		"FixtureThrownBody", Vector2.ZERO, 0.30, Vector2(10.0, 0.0), 0.0,
		_typed_contract(2.0, true, true, false, 0.10, 0.05, 200.0, 400.0, 300.0)
	)
	_check("typed_contract_spawns_body", body_id > 0 and sim.physics_objects.has(body_id))
	var body: Dictionary = sim.physics_objects[body_id]
	_check("typed_gravity_multiplier_is_bound", is_equal_approx(float(body.get("gravity_multiplier", 0.0)), 2.0))
	_check("bounce_and_orientation_flags_are_bound", bool(body.get("allow_bouncing", false)) and bool(body.get("orient_to_flight_path", false)))
	_check("first_and_second_height_are_bound", is_equal_approx(float(body.get("first_height_source", -1.0)), 0.10) and is_equal_approx(float(body.get("second_height_source", -1.0)), 0.05))
	sim.tick()
	body = sim.physics_objects[body_id]
	_check("gravity_multiplier_changes_vertical_velocity", is_equal_approx(float(body.get("vertical_velocity_source", 0.0)), -2.0))
	_check("horizontal_velocity_advances_world_position", Vector2(body.get("position", Vector2.ZERO)).is_equal_approx(Vector2(1.0, 0.0)))
	_check("orient_to_flight_path_updates_yaw_and_pitch", is_equal_approx(float(body.get("yaw_radians", 99.0)), 0.0) and float(body.get("pitch_radians", 0.0)) < 0.0)
	sim.tick()
	body = sim.physics_objects[body_id]
	_check("first_height_drives_first_rebound", int(body.get("bounce_count", 0)) == 1 and float(body.get("vertical_velocity_source", 0.0)) > 0.0)
	var saw_second := false
	for _index in range(20):
		sim.tick()
		if sim.physics_objects.has(body_id) and int((sim.physics_objects[body_id] as Dictionary).get("bounce_count", 0)) == 2:
			saw_second = true
			break
	_check("second_height_drives_second_rebound", saw_second)
	for _index in range(30):
		sim.tick()
		if String((sim.physics_objects[body_id] as Dictionary).get("phase", "")) == "shock_stunned":
			break
	body = sim.physics_objects[body_id]
	_check("body_rests_after_authored_rebounds", String(body.get("phase", "")) == "shock_stunned" and is_zero_approx(float(body.get("height_source", -1.0))))
	_check("shock_stunned_timer_uses_authored_inclusive_range", int(body.get("phase_ticks_remaining", 0)) >= 2 and int(body.get("phase_ticks_remaining", 0)) <= 4)
	var snapshot := sim.snapshot()
	var state_hash := sim.state_hash()
	var restored := _make_sim()
	_check("physics_state_restores", restored.restore(snapshot))
	_check("physics_state_hash_round_trips", restored.state_hash() == state_hash)
	for _index in range(10):
		sim.tick()
		restored.tick()
	_check("restored_physics_continues_deterministically", restored.state_hash() == sim.state_hash())
	_check("shock_state_reaches_recovered", String((sim.physics_objects[body_id] as Dictionary).get("phase", "")) == "recovered")

	var killed := _make_sim()
	var killed_id: int = killed.spawn_physics_object(
		"FixtureDebris", Vector2.ZERO, 0.01, Vector2.ZERO, 0.0,
		_typed_contract(1.0, false, false, true, 0.0, 0.0, 0.0, 0.0, 0.0)
	)
	killed.tick()
	_check("kill_when_resting_removes_body", killed_id > 0 and not killed.physics_objects.has(killed_id))

	var no_orient := _make_sim()
	var fixed_id: int = no_orient.spawn_physics_object(
		"FixtureNoOrient", Vector2.ZERO, 1.0, Vector2(0.0, 2.0), 0.0,
		_typed_contract(0.0, false, false, false, 0.0, 0.0, 0.0, 0.0, 0.0)
	)
	no_orient.tick()
	var fixed_body: Dictionary = no_orient.physics_objects[fixed_id]
	_check("zero_gravity_multiplier_preserves_vertical_velocity", is_zero_approx(float(fixed_body.get("vertical_velocity_source", 1.0))))
	_check("orientation_disabled_preserves_spawn_orientation", is_zero_approx(float(fixed_body.get("yaw_radians", 1.0))) and is_zero_approx(float(fixed_body.get("pitch_radians", 1.0))))

	var rejected := _typed_contract(1.0, false, false, false, 0.0, 0.0, 0.0, 0.0, 0.0)
	rejected["extraction"] = "opaque"
	_check("opaque_contract_fails_closed", sim.spawn_physics_object("Opaque", Vector2.ZERO, 1.0, Vector2.ZERO, 0.0, rejected) == -1)
	var wrong_module := _typed_contract(1.0, false, false, false, 0.0, 0.0, 0.0, 0.0, 0.0)
	wrong_module["module"] = "NotPhysicsBehavior"
	_check("wrong_module_fails_closed", sim.spawn_physics_object("Wrong", Vector2.ZERO, 1.0, Vector2.ZERO, 0.0, wrong_module) == -1)

	var ran := passed + failed
	if ran != EXPECTED_CHECKS:
		failed += 1
		printerr("PHYSICS_BEHAVIOR_RUNTIME_FAIL liveness ran=%d expected=%d" % [ran, EXPECTED_CHECKS])
	print("PHYSICS_BEHAVIOR_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _make_sim() -> RetailSliceSim:
	var sim: RetailSliceSim = Sim.new()
	var rules := {}
	for object_id in [Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, Sim.ARCHER_OBJECT_ID, Sim.TOWER_GUARD_OBJECT_ID, Sim.KNIGHT_OBJECT_ID]:
		rules[object_id] = _unit_rule().duplicate(true)
	sim.setup({}, {
		"physics_gravity_source_per_second_squared": 10.0,
		"logic_random_seed": 91,
		"unit_rules": rules,
	})
	sim.ai_enabled = false
	sim.base_loop_enabled = false
	sim.entities.clear()
	sim.structures.clear()
	return sim


func _unit_rule() -> Dictionary:
	return {
		"horde_id": Sim.SOLDIER_HORDE_ID, "category": "infantry",
		"speed": 1.0, "speed_source": 10.0,
		"acceleration": 1.0, "acceleration_source": 10.0,
		"turn_rate_degrees_per_second": 180.0,
		"braking": 1.0, "braking_source": 10.0,
		"attack_range": 0.1, "attack_range_source": 1.0,
		"minimum_attack_range": 0.0, "minimum_attack_range_source": 0.0,
		"vision_range": 1.0, "vision_range_source": 10.0,
		"delay_between_shots_ms": 1000.0, "pre_attack_delay_ms": 0.0,
		"firing_duration_ms": 0.0, "attack_period_ticks": 10,
		"pre_attack_ticks": 0, "firing_duration_ticks": 0,
		"member_damage": 1, "member_health": 100, "member_count": 1,
		"formation_positions": [Vector3.ZERO], "provenance": {},
	}


func _typed_contract(
	gravity: float, bouncing: bool, orient: bool, kill_on_ground: bool,
	first_height: float, second_height: float,
	stunned_low_ms: float, stunned_high_ms: float, standing_ms: float
) -> Dictionary:
	return {
		"module": "PhysicsBehavior",
		"fields": {
			"GravityMult": {"authored": str(gravity), "value": gravity},
			"AllowBouncing": {"authored": "Yes" if bouncing else "No", "value": bouncing},
			"OrientToFlightPath": {"authored": "Yes" if orient else "No", "value": orient},
			"KillWhenRestingOnGround": {"authored": "Yes" if kill_on_ground else "No", "value": kill_on_ground},
			"FirstHeight": {"authored": str(first_height), "value": first_height},
			"SecondHeight": {"authored": str(second_height), "value": second_height},
			"ShockStunnedTimeLow": {"authored": str(stunned_low_ms), "milliseconds": stunned_low_ms},
			"ShockStunnedTimeHigh": {"authored": str(stunned_high_ms), "milliseconds": stunned_high_ms},
			"ShockStandingTime": {"authored": str(standing_ms), "milliseconds": standing_ms},
		},
		"runtimeStatus": "deferred",
		"extraction": "typed",
		"carrier": "Behavior",
		"sourceIni": "data/ini/object/fixture.ini",
		"line": 14,
		"tag": "ModuleTag_Physics",
	}


func _check(label: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		push_error("PHYSICS_BEHAVIOR_RUNTIME_FAIL %s" % label)
