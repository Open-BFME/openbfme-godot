extends SceneTree

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const EXPECTED := 15
var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var sim := _sim()
	for id in [1, 2, 3]:
		sim._attach_fire_spread_contract(sim.entities[id] as Dictionary, _contract())
	_check("source_attaches", (sim.entities[1] as Dictionary).has("fire_spread_update"))
	_check("target_attaches", (sim.entities[2] as Dictionary).has("fire_spread_update"))
	var source_policy := (sim.entities[1] as Dictionary).get("fire_spread_update", {}) as Dictionary
	_check("ignition_owner_receipted", (source_policy.get("unsupported_semantics", []) as Array).has("ignition_source_owned_by_fire_damage_consumer"))
	var activation := sim.set_fire_spread_active(1, true)
	_check("explicit_ignition_activates", bool(activation.get("ok", false)))
	var due := int(activation.get("next_spread_tick", -1))
	_check("authored_delay_bounds", due >= 20 and due <= 40)
	sim.tick_index = due - 1
	sim._step_fire_spread_updates()
	_check("spread_waits_for_due_tick", not bool(((sim.entities[2] as Dictionary)["fire_spread_update"] as Dictionary).get("burning", false)))
	sim.tick_index = due
	sim._step_fire_spread_updates()
	_check("nearest_eligible_target_ignites", bool(((sim.entities[2] as Dictionary)["fire_spread_update"] as Dictionary).get("burning", false)))
	_check("spread_event_emitted", String((sim.events[-1] as Dictionary).get("kind", "")) == "module.fire_spread")
	_check("out_of_range_target_untouched", not bool(((sim.entities[3] as Dictionary)["fire_spread_update"] as Dictionary).get("burning", false)))
	var next_due := int(((sim.entities[1] as Dictionary)["fire_spread_update"] as Dictionary).get("next_spread_tick", -1))
	_check("source_reschedules_in_bounds", next_due >= due + 20 and next_due <= due + 40)
	var snapshot := sim.snapshot()
	var hash := sim.state_hash()
	var restored := _sim()
	_check("snapshot_restores", restored.restore(snapshot))
	_check("state_hash_round_trips", restored.state_hash() == hash)
	_check("burning_state_round_trips", bool(((restored.entities[2] as Dictionary)["fire_spread_update"] as Dictionary).get("burning", false)))
	_check("deactivation_clears_schedule", bool(restored.set_fire_spread_active(2, false).get("ok", false)) and int(((restored.entities[2] as Dictionary)["fire_spread_update"] as Dictionary).get("next_spread_tick", 0)) == -1)
	var opaque := _contract()
	opaque["extraction"] = "opaque"
	(restored.entities[3] as Dictionary).erase("fire_spread_update")
	restored._attach_fire_spread_contract(restored.entities[3] as Dictionary, opaque)
	_check("opaque_contract_fails_closed", not (restored.entities[3] as Dictionary).has("fire_spread_update"))
	print("FIRE_SPREAD_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 and passed == EXPECTED else 1)


func _contract() -> Dictionary:
	return {"module": "FireSpreadUpdate", "extraction": "typed", "fields": {"MinSpreadDelay": {"milliseconds": 2000}, "MaxSpreadDelay": {"milliseconds": 4000}, "SpreadTryRange": {"value": 50}}}


func _sim() -> RetailSliceSim:
	var sim: RetailSliceSim = Sim.new()
	var unit_rules := {}
	for object_id in [Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, Sim.ARCHER_OBJECT_ID, Sim.TOWER_GUARD_OBJECT_ID, Sim.KNIGHT_OBJECT_ID]:
		unit_rules[object_id] = _rule()
	sim.setup({}, {"source_map_transform_scale": 1.0, "unit_rules": unit_rules})
	sim.ai_enabled = false
	sim.base_loop_enabled = false
	sim.entities.clear()
	sim.structures.clear()
	for row in [
		{"id": 1, "team": 0, "health": 100, "position": Vector2.ZERO},
		{"id": 2, "team": 1, "health": 100, "position": Vector2(10, 0)},
		{"id": 3, "team": 1, "health": 100, "position": Vector2(100, 0)},
	]:
		sim.entities[int(row.id)] = row
	return sim


func _rule() -> Dictionary:
	return {"horde_id": Sim.SOLDIER_HORDE_ID, "category": "infantry", "speed": 1.0, "speed_source": 10.0, "acceleration": 1.0, "acceleration_source": 10.0, "turn_rate_degrees_per_second": 180.0, "braking": 1.0, "braking_source": 10.0, "attack_range": 1.0, "attack_range_source": 1.0, "minimum_attack_range": 0.0, "minimum_attack_range_source": 0.0, "vision_range": 10.0, "vision_range_source": 10.0, "delay_between_shots_ms": 1000.0, "pre_attack_delay_ms": 0.0, "firing_duration_ms": 0.0, "attack_period_ticks": 10, "pre_attack_ticks": 0, "firing_duration_ticks": 0, "member_damage": 1, "member_health": 100, "member_count": 1, "formation_positions": [Vector3.ZERO], "provenance": {}}


func _check(label: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		push_error("FIRE_SPREAD_RUNTIME_FAIL " + label)
