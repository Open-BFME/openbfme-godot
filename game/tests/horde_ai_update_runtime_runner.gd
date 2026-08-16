extends SceneTree

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const EXPECTED_CHECKS := 20
var passed := 0
var failed := 0

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var sim := _make_sim()
	sim._unit_module_contracts[Sim.SOLDIER_HORDE_ID] = [_contract(true)]
	_spawn(sim, 1, 0, Vector2.ZERO)
	_spawn(sim, 2, 1, Vector2(0.5, 0.0))
	var row := sim.entities[1] as Dictionary
	var policy := row.get("horde_ai_update", {}) as Dictionary
	_check("typed_contract_attaches", not policy.is_empty())
	_check("auto_acquire_enabled_consumed", bool(row.get("auto_acquire_enabled", false)))
	_check("building_flag_consumed", bool(row.get("auto_acquire_attack_buildings", false)))
	_check("stealth_flag_absence_consumed", not bool(row.get("auto_acquire_while_stealthed", true)))
	_check("mood_cadence_quantized", int(row.get("mood_attack_check_rate_ticks", 0)) == 3)
	_check("contained_attack_opt_in_consumed", bool(policy.get("can_attack_while_contained", false)))
	_check("cower_bounds_quantized", int(policy.get("minimum_cower_ticks", 0)) == 2 and int(policy.get("maximum_cower_ticks", 0)) == 4)
	_check("repeated_lua_rows_preserved", (policy.get("ai_lua_event_lists", []) as Array) == ["ListA", "ListB"])
	var receipts := policy.get("unsupported_semantics", []) as Array
	_check("lua_hook_receipted", receipts.has("script_hook_binding:AILuaEventsList"))
	_check("attack_priority_receipted", receipts.has("target_classification_binding:AttackPriority"))
	var cower := sim.trigger_horde_cower(1)
	_check("cower_duration_in_authored_bounds", bool(cower.get("ok", false)) and int(cower.get("duration_ticks", 0)) >= 2 and int(cower.get("duration_ticks", 0)) <= 4)
	sim._step_entity(1)
	_check("cower_holds_action", String(row.get("state", "")) == "cower" and is_zero_approx(float(row.get("current_speed", 1.0))))
	var snap := sim.snapshot()
	var hash_before := sim.state_hash()
	var restored := _make_sim()
	_check("snapshot_restores", restored.restore(snap))
	_check("state_hash_round_trips", restored.state_hash() == hash_before)
	_check("cower_deadline_round_trips", int((restored.entities[1] as Dictionary).get("cower_until_tick", -1)) == int(row.get("cower_until_tick", -2)))
	var blocked := _make_sim()
	blocked._unit_module_contracts[Sim.SOLDIER_HORDE_ID] = [_contract(false)]
	_spawn(blocked, 3, 0, Vector2.ZERO)
	_spawn(blocked, 4, 1, Vector2(0.5, 0.0))
	blocked.entity_container[3] = 99
	var blocked_row := blocked.entities[3] as Dictionary
	blocked_row["target_id"] = 4
	blocked._step_entity(3)
	_check("contained_attack_default_denied", String(blocked_row.get("state", "")) == "contained")
	sim.entity_container[1] = 99
	row.erase("cower_until_tick")
	row["target_id"] = 2
	row["target_kind"] = "battalion"
	var prior_health := int((sim.entities[2] as Dictionary).get("health", 0))
	for _tick in 8:
		sim.tick_index += 1
		sim._step_entity(1)
	_check("contained_attack_opt_in_fires", int((sim.entities[2] as Dictionary).get("health", 0)) < prior_health)
	_check("contained_attacker_does_not_move", Vector2(row.get("position", Vector2.ONE)).is_equal_approx(Vector2.ZERO))
	var opaque := _make_sim()
	var opaque_contract := _contract(true)
	opaque_contract["extraction"] = "opaque"
	opaque._unit_module_contracts[Sim.SOLDIER_HORDE_ID] = [opaque_contract]
	_spawn(opaque, 5, 0, Vector2.ZERO)
	_check("opaque_contract_fails_closed", String(opaque.trigger_horde_cower(5).get("reason", "")) == "typed-horde-ai-contract-missing")
	var disabled := _make_sim()
	disabled._unit_module_contracts[Sim.SOLDIER_HORDE_ID] = [_contract(true, false)]
	_spawn(disabled, 6, 0, Vector2.ZERO)
	_spawn(disabled, 7, 1, Vector2(0.5, 0.0))
	disabled._step_entity(6)
	_check("authored_no_disables_idle_acquisition", int((disabled.entities[6] as Dictionary).get("target_id", 0)) == 0)
	var ran := passed + failed
	if ran != EXPECTED_CHECKS:
		failed += 1
		push_error("HORDE_AI_UPDATE_RUNTIME_FAIL liveness ran=%d expected=%d" % [ran, EXPECTED_CHECKS])
	print("HORDE_AI_UPDATE_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)

func _contract(can_attack_contained: bool, acquire_enabled: bool = true) -> Dictionary:
	return {"module":"HordeAIUpdate", "extraction":"typed", "tag":"ModuleTag_AI", "line":20, "fields":{
		"AutoAcquireEnemiesWhenIdle":{"enabled":acquire_enabled,"flags":["ATTACK_BUILDINGS"]},
		"MoodAttackCheckRate":{"milliseconds":300.0}, "MinCowerTime":{"milliseconds":200.0},
		"MaxCowerTime":{"milliseconds":400.0}, "CanAttackWhileContained":{"value":can_attack_contained},
		"AttackPriority":{"value":"AttackPriority_Infantry"},
		"AILuaEventsList":[{"value":"ListA"},{"value":"ListB"}],
	}}

func _make_sim() -> RetailSliceSim:
	var rules := {}
	for object_id in [Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, Sim.ARCHER_OBJECT_ID, Sim.TOWER_GUARD_OBJECT_ID, Sim.KNIGHT_OBJECT_ID]:
		rules[object_id] = _unit_rule().duplicate(true)
	var sim: RetailSliceSim = Sim.new()
	sim.setup({}, {"unit_rules":rules, "source_map_transform_scale":1.0})
	sim.ai_enabled = false
	sim.base_loop_enabled = false
	sim.entities.clear()
	sim.structures.clear()
	return sim

func _spawn(sim: RetailSliceSim, id: int, team: int, position: Vector2) -> void:
	sim._add_battalion(id, team, position, "Fixture", Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, 0, _unit_rule())

func _unit_rule() -> Dictionary:
	return {"horde_id":Sim.SOLDIER_HORDE_ID,"category":"infantry","speed":1.0,"speed_source":10.0,"acceleration":1.0,"acceleration_source":10.0,"turn_rate_degrees_per_second":180.0,"braking":1.0,"braking_source":10.0,"attack_range":1.0,"attack_range_source":1.0,"minimum_attack_range":0.0,"minimum_attack_range_source":0.0,"vision_range":10.0,"vision_range_source":10.0,"delay_between_shots_ms":100.0,"pre_attack_delay_ms":0.0,"firing_duration_ms":0.0,"attack_period_ticks":1,"pre_attack_ticks":0,"firing_duration_ticks":0,"member_damage":5,"member_health":100,"member_count":1,"formation_positions":[Vector3.ZERO],"provenance":{}}

func _check(label: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		push_error("HORDE_AI_UPDATE_RUNTIME_FAIL %s" % label)
