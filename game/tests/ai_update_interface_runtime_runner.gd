extends SceneTree

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const EXPECTED_CHECKS := 14
var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var sim := _make_sim()
	sim._unit_module_contracts[Sim.SOLDIER_HORDE_ID] = [_contract(true, ["ATTACK_BUILDINGS"])]
	_spawn(sim, 1, Sim.PLAYER_TEAM, Vector2.ZERO)
	_spawn(sim, 90, Sim.ENEMY_TEAM, Vector2(4, 0))
	var row := sim.entities[1] as Dictionary
	_check("typed_ai_contract_attaches", row.has("ai_update_interface"))
	_check("auto_acquire_flags_bind", bool(row.get("auto_acquire_enabled", false)) and bool(row.get("auto_acquire_attack_buildings", false)) and not bool(row.get("auto_acquire_while_stealthed", true)))
	_check("authored_mood_timer_binds_ticks", int(row.get("mood_attack_check_rate_ticks", 0)) == 2)
	sim.tick()
	_check("idle_cadence_acquires_nearest_enemy", int(row.get("target_id", 0)) == 90 and String(row.get("order_kind", "")) == "auto_attack")
	_check("acquisition_records_chase_origin", row.has("auto_attack_origin"))
	row["position"] = Vector2(4, 0)
	sim.tick()
	_check("stop_chase_distance_clears_auto_target", int(row.get("target_id", -1)) == 0 and String(row.get("state", "")) == "idle")
	var snapshot := sim.snapshot()
	var state_hash := sim.state_hash()
	var restored := _make_sim()
	_check("ai_timer_state_snapshot_restores", restored.restore(snapshot))
	_check("ai_timer_state_hash_round_trips", restored.state_hash() == state_hash)
	sim.tick()
	restored.tick()
	_check("restored_ai_cadence_continues_deterministically", restored.state_hash() == sim.state_hash())

	var stealth := _make_sim()
	stealth._unit_module_contracts[Sim.SOLDIER_HORDE_ID] = [_contract(true, [])]
	_spawn(stealth, 10, Sim.PLAYER_TEAM, Vector2.ZERO)
	_spawn(stealth, 91, Sim.ENEMY_TEAM, Vector2(2, 0))
	(stealth.entities[10] as Dictionary)["stealth_until_tick"] = 100
	stealth.tick()
	_check("stealthed_source_without_flag_does_not_acquire", int((stealth.entities[10] as Dictionary).get("target_id", 0)) == 0)

	var disabled := _make_sim()
	disabled._unit_module_contracts[Sim.SOLDIER_HORDE_ID] = [_contract(false, [])]
	_spawn(disabled, 20, Sim.PLAYER_TEAM, Vector2.ZERO)
	_spawn(disabled, 92, Sim.ENEMY_TEAM, Vector2(2, 0))
	disabled.tick()
	_check("disabled_auto_acquire_stays_idle", int((disabled.entities[20] as Dictionary).get("target_id", 0)) == 0)

	var receipt := ((row.get("ai_update_interface", {}) as Dictionary).get("unsupported_semantics", []) as Array)
	_check("lua_and_turret_are_explicitly_receipted", receipt.has("unsupported_ai_field:AILuaEventsList") and receipt.has("turret_weapon_slot_aim_requires_weapon_mount_runtime"))
	_check("bounded_hold_ground_distance_is_preserved", is_equal_approx(float((row.get("ai_update_interface", {}) as Dictionary).get("hold_ground_close_range_source", -1.0)), 40.0))
	_check("can_attack_while_contained_is_preserved", bool((row.get("ai_update_interface", {}) as Dictionary).get("can_attack_while_contained", false)))

	var ran := passed + failed
	if ran != EXPECTED_CHECKS:
		failed += 1
		printerr("AI_UPDATE_INTERFACE_RUNTIME_FAIL liveness ran=%d expected=%d" % [ran, EXPECTED_CHECKS])
	print("AI_UPDATE_INTERFACE_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _contract(enabled: bool, flags: Array) -> Dictionary:
	return {"module": "AIUpdateInterface", "extraction": "typed", "runtimeStatus": "deferred", "tag": "ModuleTag_AI", "line": 10, "fields": {
		"AutoAcquireEnemiesWhenIdle": {"enabled": enabled, "flags": flags},
		"CanAttackWhileContained": {"value": true}, "AILuaEventsList": {"value": "FixtureFunctions"},
		"MoodAttackCheckRate": {"milliseconds": 200.0}, "HoldGroundCloseRangeDistance": {"value": 40.0},
		"StopChaseDistance": {"value": 3.0},
		"Turrets": [{"TurretTurnRate": {"value": 90.0}, "ControlledWeaponSlots": {"value": ["PRIMARY"]}}],
	}}


func _make_sim() -> RetailSliceSim:
	var unit_rules := {}
	for object_id in [Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, Sim.ARCHER_OBJECT_ID, Sim.TOWER_GUARD_OBJECT_ID, Sim.KNIGHT_OBJECT_ID]:
		unit_rules[object_id] = _unit_rule().duplicate(true)
	var sim: RetailSliceSim = Sim.new()
	sim.setup({}, {"unit_rules": unit_rules, "source_map_transform_scale": 1.0, "logic_random_seed": 19})
	sim.ai_enabled = false
	sim.base_loop_enabled = false
	sim.entities.clear()
	sim.structures.clear()
	return sim


func _spawn(sim: RetailSliceSim, id: int, team: int, at: Vector2) -> void:
	sim._add_battalion(id, team, at, "Fixture", Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, 0, _unit_rule())


func _unit_rule() -> Dictionary:
	return {"horde_id": Sim.SOLDIER_HORDE_ID, "category": "infantry", "speed": 1.0, "speed_source": 10.0, "acceleration": 1.0, "acceleration_source": 10.0, "turn_rate_degrees_per_second": 180.0, "braking": 1.0, "braking_source": 10.0, "attack_range": 1.0, "attack_range_source": 1.0, "minimum_attack_range": 0.0, "minimum_attack_range_source": 0.0, "vision_range": 10.0, "vision_range_source": 10.0, "delay_between_shots_ms": 1000.0, "pre_attack_delay_ms": 0.0, "firing_duration_ms": 0.0, "attack_period_ticks": 10, "pre_attack_ticks": 0, "firing_duration_ticks": 0, "member_damage": 1, "member_health": 100, "member_count": 1, "formation_positions": [Vector3.ZERO], "provenance": {}}


func _check(label: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		push_error("AI_UPDATE_INTERFACE_RUNTIME_FAIL %s" % label)
