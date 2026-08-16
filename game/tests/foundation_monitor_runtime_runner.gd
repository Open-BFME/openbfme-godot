extends SceneTree

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const EXPECTED := 29
var passed := 0
var failed := 0

func _initialize() -> void: call_deferred("_run")

func _run() -> void:
	var sim := _sim([_foundation(2), _monitor()])
	var structure := sim.structures[1000] as Dictionary
	var foundation := structure.get("foundation_ai_update", {}) as Dictionary
	_check("FoundationAIUpdate_typed_contract_attaches", not foundation.is_empty())
	_check("authored_build_variation_is_exact", int(foundation.get("build_variation", 0)) == 2)
	_check("variation_is_live_structure_state", int(structure.get("build_variation", 0)) == 2)
	_check("variation_query_is_command_safe", int(sim.foundation_build_variation(1000).get("value", 0)) == 2)
	_check("foundation_visual_dispatch_gap_is_explicit", (foundation.get("unsupported_semantics", []) as Array).has("foundation-construction-dispatch-unwired"))
	var unit := sim.entities[1] as Dictionary
	var monitor := unit.get("monitor_condition_update", {}) as Dictionary
	_check("MonitorConditionUpdate_typed_contract_attaches", not monitor.is_empty())
	_check("authored_default_command_set_preserved", String(monitor.get("default_command_set", "")) == "FixtureDefaultCommandSet")
	_check("presentation_seams_are_not_overclaimed", (monitor.get("unsupported_semantics", []) as Array).has("command-surface-consumer-unwired") and (monitor.get("unsupported_semantics", []) as Array).has("model-condition-producer-unwired"))
	unit["weapon_set_flags"] = ["WEAPONSET_TOGGLE_1"]
	unit["model_conditions"] = ["ATTACKING_POSITION"]
	sim._step_monitor_condition_updates()
	_check("model_route_has_authored_priority", String(unit.get("command_set_id", "")) == "FixtureStopCommandSet")
	_check("model_route_receipt", String(monitor.get("active_route", "")) == "model-condition")
	unit["model_conditions"] = []
	sim._step_monitor_condition_updates()
	_check("weapon_route_activates", String(unit.get("command_set_id", "")) == "FixtureWeaponCommandSet")
	unit["weapon_set_flags"] = []
	sim._step_monitor_condition_updates()
	_check("cleared_conditions_restore_default", String(unit.get("command_set_id", "")) == "FixtureDefaultCommandSet")
	var modes := unit.get("weapon_modes", {}) as Dictionary; modes["weaponset_toggle_1"] = (modes["default"] as Dictionary).duplicate(true); unit["weapon_modes"] = modes
	var toggle_on := sim._apply_ability_weapon_toggle(unit, {"toggleMode": "weaponset_toggle_1"})
	_check("weapon_toggle_core_publishes_authored_flag", bool(toggle_on.get("ok", false)) and (unit.get("weapon_set_flags", []) as Array).has("WEAPONSET_TOGGLE_1"))
	sim._step_monitor_condition_updates()
	_check("live_weapon_toggle_drives_monitor", String(unit.get("command_set_id", "")) == "FixtureWeaponCommandSet")
	var toggle_off := sim._apply_ability_weapon_toggle(unit, {"toggleMode": "weaponset_toggle_1"})
	_check("weapon_toggle_release_clears_flag", bool(toggle_off.get("ok", false)) and (unit.get("weapon_set_flags", []) as Array).is_empty())
	sim._step_monitor_condition_updates()
	_check("weapon_toggle_release_restores_command_set", String(unit.get("command_set_id", "")) == "FixtureDefaultCommandSet")
	_check("route_transitions_are_evented", _event_count(sim, "module.monitor_condition_command_set") == 5)
	var before := sim.state_hash(); var snapshot := sim.snapshot(); var restored := _sim([_foundation(2), _monitor()])
	_check("snapshot_restores", restored.restore(snapshot))
	_check("snapshot_hash_round_trips", restored.state_hash() == before)
	_check("restored_default_is_stable", String((restored.entities[1] as Dictionary).get("command_set_id", "")) == "FixtureDefaultCommandSet")
	var empty_sim := _sim([_foundation(0)])
	var empty_policy := (empty_sim.structures[1000] as Dictionary).get("foundation_ai_update", {}) as Dictionary
	_check("fieldless_foundation_marker_attaches", not empty_policy.is_empty())
	_check("fieldless_foundation_does_not_invent_variation", not (empty_sim.structures[1000] as Dictionary).has("build_variation"))
	_check("fieldless_foundation_receipts_engine_default", (empty_policy.get("unsupported_semantics", []) as Array).has("engine-default-build-variation-unresolved"))
	var opaque := _foundation(2); opaque["extraction"] = "opaque"
	_check("opaque_foundation_fails_closed", ((_sim([opaque]).structures[1000] as Dictionary).get("foundation_ai_update", {}) as Dictionary).is_empty())
	var no_default := _sim([_monitor()], false)
	var no_default_unit := no_default.entities[1] as Dictionary; no_default_unit["weapon_set_flags"] = ["WEAPONSET_TOGGLE_1"]; no_default._step_monitor_condition_updates()
	_check("missing_default_refuses_transition", not no_default_unit.has("command_set_id"))
	_check("missing_default_is_receipted", (((no_default_unit.get("monitor_condition_update", {}) as Dictionary).get("unsupported_semantics", []) as Array).has("default-command-set-unresolved")))
	var incomplete := _monitor(); (incomplete["fields"] as Dictionary).erase("ModelConditionRoute"); ((incomplete["fields"] as Dictionary)["WeaponSetRoute"] as Dictionary).erase("commandSet")
	_check("malformed_route_fails_closed", ((_sim([incomplete]).entities[1] as Dictionary).get("monitor_condition_update", {}) as Dictionary).is_empty())
	var opaque_monitor := _monitor(); opaque_monitor["extraction"] = "opaque"
	_check("opaque_monitor_fails_closed", ((_sim([opaque_monitor]).entities[1] as Dictionary).get("monitor_condition_update", {}) as Dictionary).is_empty())
	_check("typed_provenance_preserved", int(monitor.get("line", 0)) == 20 and String(monitor.get("tag", "")) == "ModuleTag_CommandSetSwapper")
	if passed + failed != EXPECTED: failed += 1; printerr("FOUNDATION_MONITOR_RUNTIME_FAIL liveness")
	print("FOUNDATION_MONITOR_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)

func _foundation(variation: int) -> Dictionary:
	var fields := {}
	if variation > 0: fields["BuildVariation"] = {"authored": str(variation), "value": variation, "sourceIni": "fixture/foundation.ini", "line": 11}
	return {"module": "FoundationAIUpdate", "runtimeStatus": "deferred", "extraction": "typed", "tag": "ModuleTag_foundationAI", "sourceIni": "fixture/foundation.ini", "line": 10, "fields": fields}

func _monitor() -> Dictionary:
	return {"module": "MonitorConditionUpdate", "runtimeStatus": "deferred", "extraction": "typed", "tag": "ModuleTag_CommandSetSwapper", "sourceIni": "fixture/unit.ini", "line": 20, "fields": {"ModelConditionRoute": {"flags": {"value": ["ATTACKING_POSITION"]}, "commandSet": {"value": "FixtureStopCommandSet"}}, "WeaponSetRoute": {"flags": {"value": ["WEAPONSET_TOGGLE_1"]}, "commandSet": {"value": "FixtureWeaponCommandSet"}}}}

func _sim(contracts: Array, with_default: bool = true) -> RetailSliceSim:
	var rule := {"horde_id": "FixtureUnit", "category": "monster", "speed": 1.0, "speed_source": 10.0, "acceleration": 1.0, "acceleration_source": 10.0, "turn_rate_degrees_per_second": 180.0, "braking": 1.0, "braking_source": 10.0, "attack_range": 0.1, "attack_range_source": 1.0, "minimum_attack_range": 0.0, "minimum_attack_range_source": 0.0, "vision_range": 1.0, "vision_range_source": 10.0, "delay_between_shots_ms": 1000.0, "pre_attack_delay_ms": 0.0, "firing_duration_ms": 0.0, "attack_period_ticks": 10, "pre_attack_ticks": 0, "firing_duration_ticks": 0, "member_damage": 1, "member_health": 100, "member_count": 1, "formation_positions": [Vector3.ZERO], "provenance": {}}
	if with_default: rule["default_command_set_id"] = "FixtureDefaultCommandSet"
	var rules := {}; for id in [Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, Sim.ARCHER_OBJECT_ID, Sim.TOWER_GUARD_OBJECT_ID, Sim.KNIGHT_OBJECT_ID, "FixtureUnit"]: rules[id] = rule
	var sim: RetailSliceSim = Sim.new(); sim.setup({}, {"unit_rules": rules}); sim.ai_enabled = false; sim.base_loop_enabled = false; sim.entities.clear(); sim.structures.clear()
	sim.register_structure_module_contracts("FixtureFoundation", contracts)
	sim.structures[1000] = {"id": 1000, "team": Sim.PLAYER_TEAM, "source_object_id": "FixtureFoundation", "structure_kind": "foundation", "position": Vector2.ZERO, "health": 100, "max_health": 100}
	sim._attach_structure_module_contracts(sim.structures[1000] as Dictionary)
	sim._unit_module_contracts["FixtureUnit"] = contracts
	sim._add_battalion(1, Sim.PLAYER_TEAM, Vector2.ZERO, "Fixture", "FixtureUnit", "FixtureUnit", 0, rule)
	return sim

func _event_count(sim: RetailSliceSim, kind: String) -> int:
	var count := 0; for value in sim.events: if String((value as Dictionary).get("kind", "")) == kind: count += 1
	return count

func _check(name: String, condition: bool) -> void:
	if condition: passed += 1
	else: failed += 1; push_error("FOUNDATION_MONITOR_RUNTIME_FAIL " + name)
